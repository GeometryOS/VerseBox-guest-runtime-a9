// 发布正文与版本索引共用的事实读取，输入都来自工作区。
//
// 两个来源：rom/rom.properties.yaml 决定产物叫什么名字，
// dist/pack-<id>-<version>.json 是打包器对这次要发布的包给出的自报。
// 两处都从这一个模块读，正文与索引才不会对同一个版本、同一个哈希各说一套，
// 而下游正是拿这两个值核对下载到的文件。

import { readFileSync } from 'node:fs';

// 标签就是版本，不带日期。日期属于发布说明，写进标签等于把同一个事实放在两处，
// 重打一次标签两边就会漂移。
export const TAG_PATTERN = /^v\d+\.\d+\.\d+$/;

// rom.properties.yaml 是平铺的键值表。出现缩进或嵌套说明格式变了，
// 在这里猜它的结构会悄悄改掉产物带的版本，所以直接报错。
export function readRomProperties(filePath) {
  const values = {};
  readFileSync(filePath, 'utf8').split(/\r?\n/).forEach((line, index) => {
    if (line.trim() === '' || line.trimStart().startsWith('#')) return;
    if (/^[ \t]/.test(line)) {
      throw new Error(`${filePath}:${index + 1}: 这一行有缩进，本读取器只接受顶层标量`);
    }
    const match = /^([A-Za-z][A-Za-z0-9]*):[ \t]*(.*)$/.exec(line);
    if (!match) throw new Error(`${filePath}:${index + 1}: 不是 key: value 形式：${line}`);
    const key = match[1];
    if (Object.hasOwn(values, key)) throw new Error(`${filePath}:${index + 1}: 键 ${key} 重复`);
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
  if (!version) throw new Error(`${filePath}: 没有 defaultRomVersion，包名就是由它来的`);
  return version;
}

const REPORT_FIELDS = ['romKey', 'payloadSha256', 'payloadBytes', 'payloadFiles', 'zipBytes', 'libMode'];

export function readPackReport(filePath) {
  const report = JSON.parse(readFileSync(filePath, 'utf8'));
  for (const field of REPORT_FIELDS) {
    if (report[field] === undefined) throw new Error(`${filePath}: 打包报告没有 ${field}`);
  }
  if (!/^[0-9a-f]{64}$/.test(report.payloadSha256)) {
    throw new Error(`${filePath}: payloadSha256 不是 64 位小写十六进制`);
  }
  return report;
}
