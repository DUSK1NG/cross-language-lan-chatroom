# 发布操作手册（本地 Windows 机器）

使用装有 WebView2、Rust、MSVC、Node、pnpm 和 Inno Setup 的 Windows 开发机。把下面的 `X.Y.Z` 替换为目标版本。

> 与 `docs/release-checklist.md` 的区别：旧清单里的版本号是写死的历史值（如 `1.2.0`），本手册以 `X.Y.Z` 为准。

## 0. 硬性规则

- 全程在 `master` 的干净检出上进行，不要在有未提交改动的工作树里打包。
- **不得**把 `.key`、`.crt`、`.pem`、`.db`、源码、`node_modules`、`.tools` 放进发布包（第 4 步会自动检查）。
- **不得**在未征得用户明确同意时执行第 7 步（打标签 / 创建公开 Release）。
- 任何一步失败就停下并报告原文错误，不要绕过检查、不要删测试。

## 1. 同步并确认版本号

```powershell
git checkout master
git pull --ff-only
git status --short            # 必须为空
node .\scripts\check-version.mjs   # 必须输出“版本一致：X.Y.Z”
```

三处版本号（`installer/LANChat.iss`、`scripts/package-installer.ps1`、`frontend/src/changelog/releaseNotes.ts`）必须都是 `X.Y.Z`；不一致先修，再继续。发行说明 `docs/release-notes/vX.Y.Z.md` 必须已存在。

## 2. 确认 CI 绿灯

```powershell
git rev-parse HEAD
gh api repos/DUSK1NG/cross-language-lan-chatroom/commits/$(git rev-parse HEAD)/check-runs --jq '.check_runs[] | [.name,.conclusion] | @tsv'
```

`Rust LAN and online server`、`React frontend`、`Windows desktop host` 和 `Windows source launcher` 全部为 `success` 才继续。

## 3. 构建并测试

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1
pnpm --dir frontend exec vitest run
cargo test --workspace --locked
```

## 4. 打包并检查

```powershell
# 统一运行包（生成 release\LANChat-Desktop-Windows-x64\ 与 .zip）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1

# 统一包内容检查（含禁止文件检查与启动烟雾测试）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-member-package.ps1 -PackageDirectory .\release\LANChat-Desktop-Windows-x64 -Smoke

# 安装器（需要 Inno Setup 6）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1 -Version X.Y.Z -SmokeTest
```

按 [发布验收清单](release-checklist.md) 检查最终 ZIP 的内容。构建在线版时，给打包和安装器脚本传入 `-Edition online`。

## 5. 记录产物哈希

```powershell
Get-FileHash .\release\LANChat-Desktop-Windows-x64.zip -Algorithm SHA256
Get-FileHash .\release\LANChat-Setup-x64.exe   -Algorithm SHA256
Get-FileHash .\out\desktop\LANChat.exe -Algorithm SHA256
```

生成 `release\SHA256SUMS.txt`（每行 `<小写哈希>  <文件名>`，包含 zip 与 exe 两个文件），并把三个哈希与 `git rev-parse --short HEAD` 记入 `docs/release-checklist.md` 的对应小节（单独一个文档提交，走 PR）。

## 6. 干净机器验收（人工）

在一台没有开发环境的 Windows 电脑上：
1. 运行 `LANChat-Setup-x64.exe` 安装并启动，应弹出一次“新版本”卡片。
2. 房主创建聊天室；另一台电脑加入，房主批准。
3. 发送一个小附件并下载，校验内容一致。
4. 浅色/深色切换正常，设置 › 关于 › 版本说明可重新打开卡片。

跨机器与大文件项见 `docs/release-acceptance.md`。

## 7. 发布（需要用户明确同意后才执行）

先发预发布版演练，确认无误再发正式版：

```powershell
# 预发布演练
git tag vX.Y.Z-rc.1
git push origin vX.Y.Z-rc.1
gh release create vX.Y.Z-rc.1 --prerelease --title "LAN Chat X.Y.Z-rc.1" --notes-file .\docs\release-notes\vX.Y.Z.md `
  .\release\LANChat-Setup-x64.exe .\release\LANChat-Desktop-Windows-x64.zip .\release\SHA256SUMS.txt

# 正式发布（用户确认后）
git tag vX.Y.Z
git push origin vX.Y.Z
gh release create vX.Y.Z --title "LAN Chat X.Y.Z" --notes-file .\docs\release-notes\vX.Y.Z.md `
  .\release\LANChat-Setup-x64.exe .\release\LANChat-Desktop-Windows-x64.zip .\release\SHA256SUMS.txt
```

说明：
- 等待标签对应的 `Windows desktop host` 检查通过，再创建正式 Release。
- 发行说明里的卡片图片指向 `master` 上的 `assets/release-card-vX.Y.Z.svg`，该文件必须已合并。
- 目前没有代码签名证书：发行说明末尾须注明“未签名，请核对 SHA256”。
- 发布后：把 `docs/release-checklist.md` 记录与 `docs/known-issues.md` 中已验证的条目更新并走 PR。

## 回滚

- 未公开前发现问题：`gh release delete vX.Y.Z --yes` 并 `git push origin :refs/tags/vX.Y.Z`，修复后重新打包。
- 已公开：不要改动已发布的文件；发布补丁版本 `X.Y.(Z+1)`，并在发行说明中说明。
