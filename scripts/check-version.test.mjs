// check-version.mjs 的自测（`node --test scripts/check-version.test.mjs`）。
// 构造临时的三处来源文件：一致时通过、不一致时脚本退出码为 1。
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { checkVersions, defaultSources } from './check-version.mjs';

const scriptPath = fileURLToPath(new URL('./check-version.mjs', import.meta.url));

/** 在临时目录里写出三处来源文件，返回仓库根路径。 */
function writeFixtureRepo({ iss, ps1, notes }) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'check-version-'));
  fs.mkdirSync(path.join(root, 'installer'), { recursive: true });
  fs.mkdirSync(path.join(root, 'scripts'), { recursive: true });
  fs.mkdirSync(path.join(root, 'frontend', 'src', 'changelog'), { recursive: true });
  fs.writeFileSync(path.join(root, 'installer', 'LANChat.iss'),
    `#ifndef AppVersion\n  #define AppVersion "${iss}"\n#endif\n`);
  fs.writeFileSync(path.join(root, 'scripts', 'package-installer.ps1'),
    `param(\n    [string]$Version = '${ps1}'\n)\n`);
  fs.writeFileSync(path.join(root, 'frontend', 'src', 'changelog', 'releaseNotes.ts'),
    `export const currentRelease = {\n  version: '${notes}',\n};\n`);
  return root;
}

test('一致的三处版本：checkVersions 返回 ok', () => {
  const root = writeFixtureRepo({ iss: '1.3.0', ps1: '1.3.0', notes: '1.3.0' });
  const result = checkVersions(defaultSources(root));
  assert.equal(result.ok, true);
  assert.equal(result.version, '1.3.0');
});

test('不一致的三处版本：checkVersions 返回 not ok', () => {
  const root = writeFixtureRepo({ iss: '1.3.0', ps1: '1.3.1', notes: '1.3.0' });
  const result = checkVersions(defaultSources(root));
  assert.equal(result.ok, false);
  assert.equal(result.version, null);
});

test('脚本在版本不一致时退出码为 1', () => {
  const root = writeFixtureRepo({ iss: '1.3.0', ps1: '9.9.9', notes: '1.3.0' });
  const proc = spawnSync(process.execPath, [scriptPath, '--root', root], {
    env: { ...process.env },
    encoding: 'utf8'
  });
  assert.equal(proc.status, 1);
});

test('脚本在版本一致时退出码为 0', () => {
  // 对真实仓库运行：当前三处均为 1.3.0。
  const proc = spawnSync(process.execPath, [scriptPath], { encoding: 'utf8' });
  assert.equal(proc.status, 0);
});
