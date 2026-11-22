// Records one published release in the index.
//
// The file is a JSON array with one entry per version, and the version is the tag name, so a
// version can only be published as one entry. The newest entry sits at the end and only the last
// MAX_ENTRIES versions are kept.
//
// Replacing instead of appending is what keeps the file trustworthy. A publish run can be
// repeated after a failed step, and a tag can be deleted and recreated on a new commit; either
// would leave two rows for one version with two different hashes, and a reader looking a version
// up would have no way to say which row is current.
//
// An entry that does not match the schema stops the run, because a numeric version would make
// the replacement stop matching without any error.
//
// Usage:
//   node update-release-index.mjs --file release/release.json --version <version> \
//     --title <title> \
//     --rom-properties rom/rom.properties.yaml \
//     --pack-report <pack report> --commit <sha> \
//     --run-url <url> --assets release-assets.json

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
  if (!values[name]) fail(`--${name} is required`);
}

const version = values.version;
if (!VERSION_PATTERN.test(version)) fail(`--version ${version} is not vX.Y.Z`);

const maxEntries = values['max-entries'] ? Number(values['max-entries']) : MAX_ENTRIES;
if (!Number.isInteger(maxEntries) || maxEntries < 1) fail('--max-entries must be a positive integer');

const rom = readRomProperties(values['rom-properties']);
const report = readPackReport(values['pack-report']);
const romKey = ROM_KEY_PATTERN.exec(report.romKey);
if (!romKey) fail(`${values['pack-report']}: romKey ${report.romKey} is not <id>@<version>`);
if (romKey[1] !== rom.id || romKey[2] !== rom.defaultRomVersion) {
  fail(`package ${report.romKey} does not match ${values['rom-properties']}` +
    ` (${rom.id}@${rom.defaultRomVersion})`);
}

const allAssets = JSON.parse(readFileSync(values.assets, 'utf8'));
if (!Array.isArray(allAssets) || allAssets.length === 0) fail(`${values.assets}: expected a non-empty array`);
for (const asset of allAssets) {
  if (typeof asset.name !== 'string' || !asset.name) fail(`${values.assets}: an asset has no name`);
  if (!Number.isInteger(asset.bytes) || asset.bytes <= 0) fail(`${values.assets}: ${asset.name} has no byte count`);
  if (!SHA256_PATTERN.test(asset.sha256)) fail(`${values.assets}: ${asset.name} has no sha256`);
}

// The checksum file itself is left out. A checksum of the list of checksums describes only
// that file, and every reader compares the assets against it anyway.
const assets = allAssets.filter(asset => asset.name !== 'SHA256SUMS');

let entries = [];
try {
  entries = JSON.parse(readFileSync(values.file, 'utf8'));
} catch (error) {
  if (error.code !== 'ENOENT') throw error;
}
if (!Array.isArray(entries)) fail(`${values.file}: expected a JSON array`);
entries.forEach((item, index) => {
  if (!item || typeof item !== 'object' || typeof item.version !== 'string'
    || !VERSION_PATTERN.test(item.version)) {
    fail(`${values.file}: entry ${index} has no version in vX.Y.Z form; fix the file by hand`);
  }
});

const entry = {
  version,
  title: values.title,
  romVersion: romKey[2],
  romKey: report.romKey,
  versionPointer: report.versionPointer,
  minAppVersionCode: report.minAppVersionCode,
  packSeconds: report.packSeconds,
  files: report.files,
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
console.log(`${values.file}: ${replaced} entry for ${version} replaced, ${updated.length} entries kept`);