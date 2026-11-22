// Facts the release text and the release index both need, read from the workspace.
//
// The generated pack-audit-intent.json is the release input manifest for one ROM version.
// This reader validates the version pointer and the bytes shipped to downstream app builds.

import { readFileSync } from 'node:fs';

// A tag is the version and nothing else. The date belongs to the release title, so a tag
// carrying a date would put the same fact in two places and drift on a re-cut.
export const TAG_PATTERN = /^v\d+\.\d+\.\d+$/;

// rom.properties.yaml is a flat map of scalars. A nested or indented key means the schema
// grew, and guessing at it here would silently change which version the artifacts carry.
export function readRomProperties(filePath) {
  const values = {};
  readFileSync(filePath, 'utf8').split(/\r?\n/).forEach((line, index) => {
    if (line.trim() === '' || line.trimStart().startsWith('#')) return;
    if (/^[ \t]/.test(line)) {
      throw new Error(`${filePath}:${index + 1}: indented key, this reader only accepts top-level scalars`);
    }
    const match = /^([A-Za-z][A-Za-z0-9]*):[ \t]*(.*)$/.exec(line);
    if (!match) throw new Error(`${filePath}:${index + 1}: not a key: value line: ${line}`);
    const key = match[1];
    if (Object.hasOwn(values, key)) throw new Error(`${filePath}:${index + 1}: duplicate key ${key}`);
    let value = match[2].trim();
    if (value.length >= 2 && (value[0] === '"' || value[0] === "'") && value.at(-1) === value[0]) {
      value = value.slice(1, -1);
    }
    values[key] = value;
  });
  return values;
}

export function readRomVersion(filePath) {
  const values = readRomProperties(filePath);
  const version = values.defaultRomVersion;
  if (!version) throw new Error(`${filePath}: defaultRomVersion is missing, the artifact name comes from it`);
  return version;
}

const REPORT_FIELDS = ['romKey', 'minAppVersionCode', 'packSeconds', 'files'];

export function readPackReport(filePath) {
  const report = JSON.parse(readFileSync(filePath, 'utf8'));
  for (const field of REPORT_FIELDS) {
    if (report[field] === undefined) throw new Error(`${filePath}: pack report has no ${field}`);
  }
  if (!Number.isInteger(report.minAppVersionCode) || report.minAppVersionCode < 1) {
    throw new Error(`${filePath}: minAppVersionCode must be a positive integer`);
  }
  if ('versionPointer' in report) {
    throw new Error(filePath + ": 审计清单不得声明 versionPointer（maxcv 属于 app 侧策略）");
  }
  if (!Array.isArray(report.files) || report.files.length < 2) {
    throw new Error(`${filePath}: files must list ROM zip and placeholder zip`);
  }
  for (const item of report.files) {
    if (typeof item.name !== 'string' || !item.name || !Number.isInteger(item.bytes) || item.bytes < 1
      || !/^[0-9a-f]{64}$/.test(item.sha256)) {
      throw new Error(`${filePath}: file entry must include name, positive bytes, and sha256`);
    }
  }
  return report;
}
