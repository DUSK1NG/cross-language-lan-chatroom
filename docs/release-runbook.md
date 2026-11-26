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
node .\scripts\check-version.mjs   # 必须输出“发布版本一致：X.Y.Z”
```

先修改根 `Cargo.toml` 的工作区版本和桌面版本说明，再运行 `node scripts/check-version.mjs --sync`。发行说明位于 `docs/release-notes/vX.Y.Z.md`。

签名发布由 `.github/workflows/release.yml` 处理。在 GitHub 的 `release` 环境配置发布审批。LAN 使用 `LANCHAT_UPDATER_PRIVATE_KEY` 和 `LANCHAT_UPDATER_PRIVATE_KEY_PASSWORD`，Online 使用 `LANCHAT_ONLINE_UPDATER_PRIVATE_KEY` 和 `LANCHAT_ONLINE_UPDATER_PRIVATE_KEY_PASSWORD`，两版使用不同的签名密钥。仓库变量 `LANCHAT_UPDATER_PUBLIC_KEY` 和 `LANCHAT_ONLINE_UPDATER_PUBLIC_KEY` 配置各版随客户端编译的公钥。

Android 发布使用 Secrets `LANCHAT_ANDROID_KEYSTORE_BASE64`、`LANCHAT_ANDROID_KEY_ALIAS`、`LANCHAT_ANDROID_STORE_PASSWORD`、`LANCHAT_ANDROID_KEY_PASSWORD` 和 `LANCHAT_ANDROID_GOOGLE_SERVICES_JSON`。签名密钥须能覆盖已发行 APK。工作流解码 keystore 到 runner 临时目录，构建结束后删除临时签名文件。

工作流根据已存在的 `vX.Y.Z` 或 `vX.Y.Z-rc.N` 标签生成两版桌面 ZIP、Inno 安装器、签名、独立清单和已签名 Android APK。稳定版使用 `latest-lan.json` 与 `latest-online.json`，候选版清单名带 `-rc`，候选客户端绑定该候选标签的更新地址。清单生成时核对安装器实际产品名和版本。全部资产构建成功后创建草稿，公开发布按批准范围执行。

首次启用签名更新时，用安装器覆盖现有安装。之后在设置的「关于」主动检查、下载并确认安装。安装包校验 HTTPS、包大小、发布签名和签名中的版本。便携版从对应发布页下载 ZIP 后解压运行。更新异常时撤回发布清单并按配套的数据备份恢复合适版本。

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

# 正式发布（用户确认后）
git tag vX.Y.Z
git push origin vX.Y.Z
```

说明：
- 工作流创建草稿后，核对标签对应的 `Windows desktop host` 检查、签名、清单及批准的交付资产，再按用户授权公开。
- 发布后：把 `docs/release-checklist.md` 记录与 `docs/known-issues.md` 中已验证的条目更新并走 PR。

## 回滚

- 未公开前发现问题：`gh release delete vX.Y.Z --yes` 并 `git push origin :refs/tags/vX.Y.Z`，修复后重新打包。
- 已公开：不要改动已发布的文件；发布补丁版本 `X.Y.(Z+1)`，并在发行说明中说明。

## AstrBot 手动接入

在 AstrBot WebUI 的「设置 → OpenAPI」创建拥有 `provider` 和 `persona` 权限的密钥。Windows 在线版和 Android 登录 LAN Chat 后，打开「设置 → 机器人」，填写 AstrBot 的 HTTPS 根地址和此密钥，即可读取、新建和编辑模型源、模型、人格。新建模型使用当前选择的模型源。模型 API Key、提示词和自定义出错回复保存到 AstrBot。

点击「保存到账号」同步地址及配置编号。启用密钥同步时，OpenAPI Key 也会按账号加密保存，另一台设备登录同一账号后打开机器人设置即可使用。清空密钥后保存可以移除账号上的密钥。新建的模型和人格可在 AstrBot 的配置中选用。

在线服通过 `--bot-key-file` 或 `CHAT_BOT_KEY_FILE` 指定一个包含 32 字节随机数据的文件以启用密钥同步。文件仅允许服务账号读取，独立于 SQLite 和应用发行包保管。恢复备份时使用原加密密钥文件。

在 AstrBot 管理面板新增 LAN Chat 消息平台，填写服务器地址 `host`、端口 `port`、证书对应的主机名 `tls_name`、账号名 `username` 和用户代码 `user_code`。使用私有 CA 或局域网自签证书时，在 `certificate` 填写 AstrBot 所在机器可读的证书路径。在线模式需开启 `online`，填写已注册账号的密码和 `allowed_user_codes` 白名单。

保存后在管理面板手动启用适配器。线上服务器已有配置时，运行 `sudo systemctl start lan-chat-astrbot.service` 手动启动，使用 `systemctl is-active lan-chat-astrbot.service` 检查状态。
