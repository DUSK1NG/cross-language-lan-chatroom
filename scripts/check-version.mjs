// 校验版本号三处来源一致（规格 01 D4 / R4.5）。无第三方依赖，仅用 Node 内建模块。
//
// 三处唯一来源（见 docs/project/steering/engineering-methodology.md §6）：
//   installer/LANChat.iss                   -> #define AppVersion "x.y.z"
//   scripts/package-installer.ps1           -> [string]$Version = 'x.y.z'
//   frontend/src/changelog/releaseNotes.ts  -> version: 'x.y.z'
//
// 任意一处缺失或彼此不一致时，打印诊断并以退出码 1 结束。
// 可导入 `collectVersions` 做自测（传入自定义文件路径即可）。
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, '..');

/** 每个来源：标签、相对仓库根的默认路径、以及从文件内容提取版本号的正则。 */
export function defaultSources(root = repoRoot) {
  return [
    {
      label: 'installer/LANChat.iss',
      file: path.join(root, 'installer', 'LANChat.iss'),
      pattern: /#define\s+AppVersion\s+"(\d+\.\d+\.\d+(?:\.\d+)?)"/
    },
    {
      label: 'scripts/package-installer.ps1',
      file: path.join(root, 'scripts', 'package-installer.ps1'),
      pattern: /\[string\]\$Version\s*=\s*'(\d+\.\d+\.\d+(?:\.\d+)?)'/
    },
    {
      label: 'frontend/src/changelog/releaseNotes.ts',
      file: path.join(root, 'frontend', 'src', 'changelog', 'releaseNotes.ts'),
      pattern: /version:\s*'(\d+\.\d+\.\d+(?:\.\d+)?)'/
    }
  ];
}

/**
 * 读取每个来源的版本号。返回 { versions, errors }。
 * versions: [{ label, version }]（仅成功解析的）；errors: 读取/解析失败的说明。
 */
export function collectVersions(sources = defaultSources()) {
  const versions = [];
  const errors = [];
  for (const source of sources) {
    let text;
    try {
      text = fs.readFileSync(source.file, 'utf8');
    } catch {
      errors.push(`无法读取 ${source.label}（${source.file}）`);
      continue;
    }
    const match = source.pattern.exec(text);
    if (!match) {
      errors.push(`${source.label} 中找不到版本号`);
      continue;
    }
    versions.push({ label: source.label, version: match[1] });
  }
  return { versions, errors };
}

/**
 * 检查三处版本一致。返回 { ok, version, lines }，不直接退出，便于自测。
 */
export function checkVersions(sources = defaultSources()) {
  const { versions, errors } = collectVersions(sources);
  const lines = versions.map((entry) => `  ${entry.label}: ${entry.version}`);
  if (errors.length > 0) {
    return { ok: false, version: null, lines: [...lines, ...errors.map((error) => `  ${error}`)] };
  }
  const unique = [...new Set(versions.map((entry) => entry.version))];
  if (unique.length !== 1) {
    return { ok: false, version: null, lines: [...lines, `  版本不一致：${unique.join(' / ')}`] };
  }
  return { ok: true, version: unique[0], lines };
}

/** 作为脚本直接运行时：打印结果并设置退出码。
 * 可用 `--root <dir>` 或环境变量 `CHECK_VERSION_ROOT` 指定仓库根（供自测使用）。 */
function resolveRootFromArgs(argv) {
  const index = argv.indexOf('--root');
  if (index !== -1 && argv[index + 1]) return argv[index + 1];
  if (process.env.CHECK_VERSION_ROOT) return process.env.CHECK_VERSION_ROOT;
  return repoRoot;
}

function main() {
  const root = resolveRootFromArgs(process.argv.slice(2));
  const result = checkVersions(defaultSources(root));
  if (result.ok) {
    console.log(`版本一致：${result.version}`);
    for (const line of result.lines) console.log(line);
    return 0;
  }
  console.error('版本校验失败：三处来源必须一致。');
  for (const line of result.lines) console.error(line);
  return 1;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exit(main());
}
