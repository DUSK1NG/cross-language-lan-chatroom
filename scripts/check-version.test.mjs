import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { checkVersions } from './check-version.mjs';

const scriptPath = fileURLToPath(new URL('./check-version.mjs', import.meta.url));

function fixture(t, installerVersion = '1.4.0') {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'check-version-'));
  t.after(() => fs.rmSync(root, { recursive: true }));
  const files = {
    'Cargo.toml': '[workspace.package]\nversion = "1.4.0"',
    'Cargo.lock': 'name = "lan-chat-desktop"\nversion = "1.4.0"\n\nname = "lan-chat-server"\nversion = "1.4.0"',
    'apps/desktop/src-tauri/Cargo.toml': 'version.workspace = true',
    'server-rust/Cargo.toml': 'version.workspace = true',
    'apps/desktop/src-tauri/tauri.conf.json': '{"version": "1.4.0"}',
    'frontend/package.json': '{"version": "1.4.0"}',
    'installer/LANChat.iss': `#define AppVersion "${installerVersion}"`,
    'scripts/package-installer.ps1': "[string]$Version = '1.4.0'",
    'frontend/src/changelog/releaseNotes.ts': "version: '1.4.0'"
  };
  for (const [file, contents] of Object.entries(files)) {
    const target = path.join(root, file);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, contents);
  }
  return root;
}

test('桌面、服务端和安装器版本与工作区发布版本一致', (t) => {
  // Use a fixture so the assertion does not need rewriting on every release bump.
  assert.equal(checkVersions(fixture(t)), '1.4.0');
});

test('拒绝安装器版本与工作区版本不符的构建', (t) => {
  const root = fixture(t, '1.3.0');
  const proc = spawnSync(process.execPath, [scriptPath, '--root', root], { encoding: 'utf8' });
  assert.equal(proc.status, 1);
  assert.match(proc.stderr, /LANChat\.iss 的版本应为 1\.4\.0，实际为 1\.3\.0/);
});

test('同步安装器、前端和锁文件中的版本字段', (t) => {
  const root = fixture(t, '1.3.0');
  fs.writeFileSync(path.join(root, 'frontend/package.json'), '{"version": "1.3.0"}');
  const lockfile = path.join(root, 'Cargo.lock');
  fs.writeFileSync(lockfile, fs.readFileSync(lockfile, 'utf8').replaceAll('1.4.0', '1.3.0'));
  assert.equal(checkVersions(root, true), '1.4.0');
  assert.equal(checkVersions(root), '1.4.0');
});

test('同步要求先更新版本说明的正文和版本号', (t) => {
  const root = fixture(t);
  fs.writeFileSync(path.join(root, 'frontend/src/changelog/releaseNotes.ts'), "version: '1.3.0'");
  assert.throws(() => checkVersions(root, true), /releaseNotes.ts 的当前版本说明应为 1.4.0/);
});
