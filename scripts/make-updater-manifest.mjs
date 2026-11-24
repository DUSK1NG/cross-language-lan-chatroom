import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

import { checkVersions } from './check-version.mjs';

const [tag, directory, notesFile, edition] = process.argv.slice(2);
const release = /^v(\d+\.\d+\.\d+)(-rc\.[1-9]\d*)?$/.exec(tag ?? '');
if (!release || !directory || !notesFile || !['lan', 'online'].includes(edition)) {
  throw new Error('用法: node scripts/make-updater-manifest.mjs vX.Y.Z[-rc.N] <发布目录> <说明文件> lan|online');
}
const [, version, candidate] = release;
if (checkVersions() !== version) throw new Error(`标签 ${tag} 与工作区版本不一致`);

const installer = edition === 'online' ? 'LANChat-Online-Setup-x64.exe' : 'LANChat-Setup-x64.exe';
const zip = edition === 'online' ? 'LANChat-Online-Windows-x64.zip' : 'LANChat-Desktop-Windows-x64.zip';
const product = edition === 'online' ? 'LAN Chat Online' : 'LAN Chat';
const metadata = JSON.parse(execFileSync('powershell.exe', ['-NoProfile', '-Command',
  '$v = (Get-Item -LiteralPath $env:LANCHAT_INSTALLER_PATH).VersionInfo; @{version=$v.ProductVersion.Trim();product=$v.ProductName.Trim()} | ConvertTo-Json -Compress'
], { encoding: 'utf8', env: { ...process.env, LANCHAT_INSTALLER_PATH: path.resolve(directory, installer) } }));
if (metadata.version !== version || metadata.product !== product) {
  throw new Error(`安装器版本或版别不符，期望 ${product} ${version}，实际 ${metadata.product} ${metadata.version}`);
}
const size = fs.statSync(path.join(directory, installer)).size;
if (size <= 0 || size > 96 * 1024 * 1024 || fs.statSync(path.join(directory, zip)).size <= 0) {
  throw new Error('候选产物大小无效');
}
const signature = fs.readFileSync(path.join(directory, `${installer}.sig`), 'utf8').trim();
if (!/^[A-Za-z0-9+/]+={0,2}$/.test(signature)) throw new Error('安装器签名格式无效');
const notes = fs.readFileSync(notesFile, 'utf8').trim();
if (!notes) throw new Error('发布说明为空');

const manifest = {
  version,
  pub_date: new Date().toISOString(),
  notes,
  platforms: {
    [`windows-x86_64-${edition}`]: {
      url: `https://github.com/DUSK1NG/cross-language-lan-chatroom/releases/download/${tag}/${installer}`,
      signature,
      size
    }
  }
};
const file = path.join(directory, `latest-${edition}${candidate ? '-rc' : ''}.json`);
fs.writeFileSync(file, `${JSON.stringify(manifest, null, 2)}\n`);
console.log(file);
