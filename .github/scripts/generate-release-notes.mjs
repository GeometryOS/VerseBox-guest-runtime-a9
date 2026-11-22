// Builds the release title and body for one tag.
//
// The title is the product prefix followed by the tag, so the version appears once and nothing
// else is invented. The body is assembled from facts, not prose: the commits between the
// previous tag and this one, the packer's report for the ROM package, and the asset list
// collected from the downloaded build. Publishing the same tag twice therefore produces the
// same text, and a release can be audited from the repository alone.
//
// Usage:
//   node generate-release-notes.mjs --tag <tag> --repo <owner/repo> \
//     --assets release-assets.json --rom-properties rom/rom.properties.yaml \
//     --pack-report <pack report> --commit <sha> --run-url <url> \
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
  if (!TAG_PATTERN.test(tag)) fail(`tag ${tag} is not vX.Y.Z; the release title is built from it`);
  return tag;
}

// A tag may sit on a commit that no earlier tag reaches, which is the normal state of the very
// first release. That is not an error: the body says so and leaves out the compare link.
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
  if (!values[name]) fail(`--${name} is required`);
}

if (git('rev-parse', '--is-shallow-repository') === 'true') {
  fail('release notes need the complete history and tags; check out with fetch-depth 0');
}

const tag = values.tag;
const version = requireVersion(tag);
const repositoryUrl = `https://github.com/${values.repo}`;
const rom = readRomProperties(values['rom-properties']);
const report = readPackReport(values['pack-report']);
const assets = JSON.parse(readFileSync(values.assets, 'utf8'));

// The pack report is the only record of what is inside the package, and rom.properties.yaml is
// the only record of what the repository calls it. When the two disagree the released file
// would carry a version and an id that no other document claims, so publishing stops here.
const romKey = ROM_KEY_PATTERN.exec(report.romKey);
if (!romKey) fail(`${values['pack-report']}: romKey ${report.romKey} is not <id>@<version>`);
if (romKey[1] !== rom.id) {
  fail(`package id ${romKey[1]} does not match ${values['rom-properties']} id ${rom.id}`);
}
if (romKey[2] !== rom.defaultRomVersion) {
  fail(`package version ${romKey[2]} does not match ${values['rom-properties']}` +
    ` defaultRomVersion ${rom.defaultRomVersion}; rebuild this tag before publishing it`);
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
  `| 版本指针 | \`${report.versionPointer}\` |`,
  `| 最低 app versionCode | ${report.minAppVersionCode} |`,
  `| 发布文件清单 | ${report.files.length} 个文件 |`,
  `| 构建提交 | [\`${values.commit.slice(0, 8)}\`](${repositoryUrl}/commit/${values.commit}) |`,
];
if (values['run-url']) lines.push(`| 构建记录 | [actions run](${values['run-url']}) |`);

lines.push('', '## 更新', '');
if (changes === null) {
  lines.push('这是第一个 tag，没有可比较的上一个版本。');
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
console.log(`range: ${previous ?? '(first tag)'}..${tag}, ${changes === null ? 0 : changes.length} commits`);