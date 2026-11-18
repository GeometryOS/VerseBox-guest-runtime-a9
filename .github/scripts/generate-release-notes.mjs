// 为一个标签生成发布标题与正文。
//
// 标题是产品前缀接上标签，版本只出现一次，不再发明别的东西。正文由事实拼成，
// 不写散文：上一个标签到这一个标签之间的提交、打包器给这个 ROM 包的自报、
// 以及从下载下来的构建产物里收集到的附件清单。因此同一个标签发两次得到逐字相同的文字，
// 一次发布可以只靠仓库复核。
//
// 用法：
//   node generate-release-notes.mjs --tag <标签> --repo <owner/repo> \
//     --assets release-assets.json --rom-properties rom/rom.properties.yaml \
//     --pack-report <打包报告> --commit <提交> --run-url <地址> \
//     --notes CHANGELOG.md --title release-title.txt --version-out release-version.txt

import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { parseArgs } from 'node:util';
import { TAG_PATTERN, readPackReport, readRomProperties } from './lib/release-facts.mjs';

const ROM_KEY_PATTERN = /^([A-Za-z0-9-]+)@(.+)$/;
const RELEASE_TITLE_PREFIX = 'VerseBox A9 ';

function fail (message) {
  console.error(message);
  process.exit(1);
}

function git (...args) {
  return execFileSync('git', args, { encoding: 'utf8' }).trim();
}

function requireVersion (tag) {
  if (!TAG_PATTERN.test(tag)) fail(`标签 ${tag} 不是 vX.Y.Z，发布标题就是由它拼的`);
  return tag;
}

// 标签可能落在任何更早的标签都到不了的提交上，第一次发布就是这种状态。
// 这不是错误：正文说明这一点，并且不写完整变更链接。
function previousTag (tag) {
  try {
    return git('describe', '--tags', '--abbrev=0', `${tag}^`) || null;
  } catch {
    return null;
  }
}

function escapeSubject (subject) {
  return subject.replace(/[\\`*_[\]<>]/g, '\\$&');
}

function commitsBetween (previous, tag, repositoryUrl) {
  if (!previous) return null;
  const raw = git('log', '--reverse', '--format=%H%x09%s', `${previous}..${tag}`);
  return raw.split('\n').filter(Boolean).map(line => {
    const separator = line.indexOf('\t');
    const hash = line.slice(0, separator);
    const subject = escapeSubject(line.slice(separator + 1));
    return `- ${subject} ([${hash.slice(0, 8)}](${repositoryUrl}/commit/${hash}))`;
  });
}

function formatSize (bytes) {
  return `${Number(bytes).toLocaleString('en-US')} 字节`;
}

const { values } = parseArgs({
  options: {
    tag: { type: 'string' },
    repo: { type: 'string' },
    assets: { type: 'string' },
    notes: { type: 'string' },
    title: { type: 'string' },
    'version-out': { type: 'string' },
    'rom-properties': { type: 'string' },
    'pack-report': { type: 'string' },
    commit: { type: 'string' },
    'run-url': { type: 'string' },
  },
});

const REQUIRED = ['tag', 'repo', 'assets', 'notes', 'title', 'version-out', 'rom-properties',
  'pack-report', 'commit'];
for (const name of REQUIRED) {
  if (!values[name]) fail(`--${name} 是必填`);
}

if (git('rev-parse', '--is-shallow-repository') === 'true') {
  fail('发布正文需要完整的历史与标签，检出时把 fetch-depth 设为 0');
}

const tag = values.tag;
const version = requireVersion(tag);
const repositoryUrl = `https://github.com/${values.repo}`;
const rom = readRomProperties(values['rom-properties']);
const report = readPackReport(values['pack-report']);
const assets = JSON.parse(readFileSync(values.assets, 'utf8'));

// 打包报告是包内结构唯一的记录，rom.properties.yaml 是仓库对它唯一的说法。
// 两者不一致时，发出去的文件会带一个别的文档都不承认的版本和 id，所以在这里停下。
const romKey = ROM_KEY_PATTERN.exec(report.romKey);
if (!romKey) fail(`${values['pack-report']}: romKey ${report.romKey} 不是 <id>@<版本>`);
if (romKey[1] !== rom.id) {
  fail(`包内 id ${romKey[1]} 与 ${values['rom-properties']} 的 id ${rom.id} 不一致`);
}
if (romKey[2] !== rom.defaultRomVersion) {
  fail(`包内版本 ${romKey[2]} 与 ${values['rom-properties']} 的 defaultRomVersion` +
    ` ${rom.defaultRomVersion} 不一致；发布前先重建这个标签`);
}

const previous = previousTag(tag);
const changes = commitsBetween(previous, tag, repositoryUrl);
const title = `${RELEASE_TITLE_PREFIX}${version}`;
const lines = [
  '## 本次发布',
  '',
  '| 项 | 值 |',
  '| --- | --- |',
  `| 发布版本 | \`${version}\` |`,
  `| 标签 | [\`${tag}\`](${repositoryUrl}/tree/${tag}) |`,
  `| ROM 版本 | \`${romKey[2]}\` |`,
  `| ROM id | \`${romKey[1]}\` |`,
  `| 客体 | ${rom.name ?? rom.id} |`,
  `| Android 版本 | ${rom.androidVersion ?? '?'} |`,
  `| payloadSha256 | \`${report.payloadSha256}\` |`,
  `| payloadBytes | ${formatSize(report.payloadBytes)} |`,
  `| payload 文件数 | ${report.payloadFiles} |`,
  `| 构建提交 | [\`${values.commit.slice(0, 8)}\`](${repositoryUrl}/commit/${values.commit}) |`,
];
if (values['run-url']) lines.push(`| 构建记录 | [actions run](${values['run-url']}) |`);

lines.push('', '## 更新', '');
if (changes === null) {
  lines.push('这是第一个标签，没有可比较的上一个版本。');
} else if (changes.length === 0) {
  lines.push('此标签范围内没有新增提交。');
} else {
  lines.push(...changes);
}

lines.push('', '## 产物', '');
for (const asset of assets) {
  lines.push(`- \`${asset.name}\`，${formatSize(asset.bytes)}，sha256 \`${asset.sha256}\``);
}

lines.push('', '## 校验', '', '下载后先在仓库根逐项核对，再交给宿主导入：', '', '```bash',
  'sha256sum -c SHA256SUMS', '```');

if (previous) lines.push('', `[完整变更](${repositoryUrl}/compare/${previous}...${tag})`);
lines.push('');

writeFileSync(values.notes, lines.join('\n'), 'utf8');
writeFileSync(values.title, `${title}\n`, 'utf8');
writeFileSync(values['version-out'], `${version}\n`, 'utf8');
console.log(`version: ${version}`);
console.log(`title: ${title}`);
console.log(`range: ${previous ?? '(首个标签)'}..${tag}, ${changes === null ? 0 : changes.length} commits`);
