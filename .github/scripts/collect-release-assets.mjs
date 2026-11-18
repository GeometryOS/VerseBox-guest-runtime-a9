// 给发布要携带的文件算哈希，写成 JSON。
//
// 输入是发布工作流从标签构建里下载下来的那批文件，输出是
// [{ name, bytes, sha256 }]，相对路径原样保留，因为正文要按同一份清单印出来，
// 读者拿它对着 SHA256SUMS 核对自己下载到的那一份。
// 输出按文件去重：同一个名字出现两次就停下，带重复项的上传清单会让发布动作
// 自己跟自己抢同一个附件。
// 文件是流式读的，ROM 包经常超过一个 GB；空文件在这里就拒收，不留给装机的人发现。

import { createHash } from 'node:crypto';
import { createReadStream, statSync, writeFileSync } from 'node:fs';
import { parseArgs } from 'node:util';

async function sha256File(filePath) {
  const hash = createHash('sha256');
  await new Promise((resolve, reject) => {
    createReadStream(filePath)
      .on('data', chunk => hash.update(chunk))
      .on('error', reject)
      .on('end', resolve);
  });
  return hash.digest('hex');
}

const { values, positionals } = parseArgs({
  options: { out: { type: 'string' } },
  allowPositionals: true,
});

if (!values.out || positionals.length === 0) {
  console.error('用法：node collect-release-assets.mjs --out <assets.json> <文件...>');
  process.exit(1);
}

const assets = [];
const seen = new Map();
for (const filePath of positionals) {
  const stats = statSync(filePath);
  if (!stats.isFile()) throw new Error(`不是普通文件：${filePath}`);
  if (stats.size === 0) throw new Error(`零字节，拒绝发布：${filePath}`);
  const name = filePath.split('\\').join('/');
  // 两条通配符可以匹配到同一个文件，而名字列两次会让上传去替换它刚建好的附件，
  // GitHub 对这种请求只回一个光秃秃的 Not Found。所以在这里拒收整份清单。
  if (seen.has(name)) {
    throw new Error(`同一个文件被列了两次：${name}（另一次来自 ${seen.get(name)}）`);
  }
  seen.set(name, filePath);
  assets.push({ name, bytes: stats.size, sha256: await sha256File(filePath) });
}

assets.sort((left, right) => left.name.localeCompare(right.name));
writeFileSync(values.out, `${JSON.stringify(assets, null, 2)}\n`, 'utf8');
console.log(`已收集 ${assets.length} 个附件，写入 ${values.out}`);
