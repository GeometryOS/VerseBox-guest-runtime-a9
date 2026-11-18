// 把一次发布记进版本索引。
//
// 文件是一个 JSON 数组，一个版本一条记录，版本就是标签名，所以一个版本只能有一条。
// 最新的记录排在最末尾，只保留最近一百个版本。
//
// 覆盖而不是追加，是这个文件可信的原因。发布可能在某个步骤失败之后重跑，
// 标签也可能被删掉、在新的提交上重新打回来；两种情形都会让同一个版本再写一次记录，
// 只追加的话同一个版本就留下两条哈希不同的记录，按版本查的人无法说出哪一条是当前的。
//
// 一条记录不符合格式就停下，因为版本被写成数字会让替换静默失效，之后每次都追加一条。
//
// 用法：
//   node update-release-index.mjs --file release/release.json --version <版本> \
//     --title <标题> \
//     --rom-properties rom/rom.properties.yaml \
//     --pack-report <打包报告> --commit <提交> \
//     --run-url <地址> --assets release-assets.json

import { readFileSync, writeFileSync } from 'node:fs';
import { parseArgs } from 'node:util';
import { TAG_PATTERN, readPackReport, readRomProperties } from './lib/release-facts.mjs';

const MAX_ENTRIES = 100;
const VERSION_PATTERN = TAG_PATTERN;
const SHA256_PATTERN = /^[0-9a-f]{64}$/;
const ROM_KEY_PATTERN = /^([A-Za-z0-9-]+)@(.+)$/;

function fail (message) {
  console.error(message);
  process.exit(1);
}

const { values } = parseArgs({
  options: {
    file: { type: 'string' },
    version: { type: 'string' },
    title: { type: 'string' },
    'rom-properties': { type: 'string' },
    'pack-report': { type: 'string' },
    commit: { type: 'string' },
    'run-url': { type: 'string' },
    assets: { type: 'string' },
    'max-entries': { type: 'string' },
  },
});

const REQUIRED = ['file', 'version', 'title', 'rom-properties', 'pack-report', 'commit', 'assets'];
for (const name of REQUIRED) {
  if (!values[name]) fail(`--${name} 是必填`);
}

const version = values.version;
if (!VERSION_PATTERN.test(version)) fail(`--version ${version} 不是 vX.Y.Z`);

const maxEntries = values['max-entries'] ? Number(values['max-entries']) : MAX_ENTRIES;
if (!Number.isInteger(maxEntries) || maxEntries < 1) fail('--max-entries 必须是正整数');

const rom = readRomProperties(values['rom-properties']);
const report = readPackReport(values['pack-report']);
const romKey = ROM_KEY_PATTERN.exec(report.romKey);
if (!romKey) fail(`${values['pack-report']}: romKey ${report.romKey} 不是 <id>@<版本>`);
if (romKey[1] !== rom.id || romKey[2] !== rom.defaultRomVersion) {
  fail(`包 ${report.romKey} 与 ${values['rom-properties']}` +
    `（${rom.id}@${rom.defaultRomVersion}）不一致`);
}

const allAssets = JSON.parse(readFileSync(values.assets, 'utf8'));
if (!Array.isArray(allAssets) || allAssets.length === 0) fail(`${values.assets}: 期望一个非空数组`);
for (const asset of allAssets) {
  if (typeof asset.name !== 'string' || !asset.name) fail(`${values.assets}: 有附件没有名字`);
  if (!Number.isInteger(asset.bytes) || asset.bytes <= 0) fail(`${values.assets}: ${asset.name} 没有字节数`);
  if (!SHA256_PATTERN.test(asset.sha256)) fail(`${values.assets}: ${asset.name} 没有 sha256`);
}

// 校验清单本身不进附件列表。给一份"哈希的清单"再算一个哈希只描述它自己，
// 而每个读者本来就用它核对别的附件。
const assets = allAssets.filter(asset => asset.name !== 'SHA256SUMS');

let entries = [];
try {
  entries = JSON.parse(readFileSync(values.file, 'utf8'));
} catch (error) {
  if (error.code !== 'ENOENT') throw error;
}
if (!Array.isArray(entries)) fail(`${values.file}: 期望一个 JSON 数组`);
entries.forEach((item, index) => {
  if (!item || typeof item !== 'object' || typeof item.version !== 'string'
    || !VERSION_PATTERN.test(item.version)) {
    fail(`${values.file}: 第 ${index} 条没有 vX.Y.Z 形式的版本，请手改这个文件`);
  }
});

const entry = {
  version,
  title: values.title,
  romVersion: romKey[2],
  romKey: report.romKey,
  payloadSha256: report.payloadSha256,
  payloadBytes: report.payloadBytes,
  payloadFiles: report.payloadFiles,
  commit: values.commit,
  runUrl: values['run-url'] ?? null,
  publishedAt: new Date().toISOString(),
  assets,
};

const replaced = entries.filter(item => item.version === version).length;
const updated = [
  ...entries.filter(item => item.version !== version),
  entry,
].slice(-maxEntries);

writeFileSync(values.file, `${JSON.stringify(updated, null, 2)}\n`, 'utf8');
console.log(`${values.file}: 替换 ${replaced} 条 ${version}，保留 ${updated.length} 条`);
