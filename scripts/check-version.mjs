// Cargo.toml is the desktop release version source. Use --sync to update build metadata.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = fileURLToPath(new URL('..', import.meta.url));
const workspaceVersion = /\[workspace\.package\][^[]*?^version\s*=\s*"(\d+\.\d+\.\d+)"/m;

const derivedVersions = [
  ['Cargo.lock', /(name = "lan-chat-desktop"\r?\nversion = ")([\d.]+)"/],
  ['apps/desktop/src-tauri/tauri.conf.json', /("version"\s*:\s*")([\d.]+)"/],
  ['frontend/package.json', /("version"\s*:\s*")([\d.]+)"/],
  ['installer/LANChat.iss', /(#define\s+AppVersion\s+")([\d.]+)"/],
  ['scripts/package-installer.ps1', /(\[string\]\$Version\s*=\s*')([\d.]+)'/]
];

export function checkVersions(root = repoRoot, sync = false) {
  const read = (file) => fs.readFileSync(path.join(root, file), 'utf8');
  const version = workspaceVersion.exec(read('Cargo.toml'))?.[1];
  if (!version) throw new Error('Cargo.toml 缺少 [workspace.package].version');
  const desktopManifest = 'apps/desktop/src-tauri/Cargo.toml';
  if (!/^version\.workspace\s*=\s*true\s*$/m.test(read(desktopManifest))) {
    throw new Error(`${desktopManifest} 须设置 version.workspace = true`);
  }
  const notesFile = 'frontend/src/changelog/releaseNotes.ts';
  const notesVersion = /version:\s*'([\d.]+)'/.exec(read(notesFile))?.[1];
  if (notesVersion !== version) throw new Error(`${notesFile} 的当前版本说明应为 ${version}`);

  const pending = [];
  for (const [file, pattern] of derivedVersions) {
    const content = read(file);
    const match = pattern.exec(content);
    if (!match) throw new Error(`${file} 缺少版本项 ${pattern.source}`);
    const [, prefix, actual] = match;
    if (actual === version) continue;
    if (!sync) throw new Error(`${file} 的版本应为 ${version}，实际为 ${actual}`);
    const start = match.index + prefix.length;
    pending.push([file, content.slice(0, start) + version + content.slice(start + actual.length)]);
  }
  for (const [file, content] of pending) fs.writeFileSync(path.join(root, file), content);
  return version;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  const rootIndex = args.indexOf('--root');
  try {
    const root = rootIndex === -1 ? process.env.CHECK_VERSION_ROOT : args[rootIndex + 1];
    if (rootIndex !== -1 && (!root || root.startsWith('--'))) throw new Error('--root 需要仓库路径');
    const version = checkVersions(root, args.includes('--sync'));
    console.log(`发布版本一致：${version}`);
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
