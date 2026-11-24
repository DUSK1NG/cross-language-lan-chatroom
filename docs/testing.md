# 构建与测试说明

在 Windows 开发机上按 README 准备 Rust、MSVC、WebView2、Node.js 和 pnpm。在项目根目录执行：

~~~powershell
pnpm.cmd --dir frontend run lint
pnpm.cmd --dir frontend exec vitest run
pnpm.cmd --dir frontend run build
cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo build --workspace --locked --release
node scripts/check-version.mjs
node --test scripts/check-version.test.mjs
~~~

桌面发布版本来自根 `Cargo.toml` 的 `[workspace.package].version`。修改当前版本说明后，运行 `node scripts/check-version.mjs --sync` 同步构建元数据。

协议测试校验帧边界与历史 JSON 样本。局域网桌面集成测试通过两个客户端创建聊天室、批准成员、加入密码频道、发送消息并上传和解密下载附件。测试使用临时的证书和数据库，房主端口为 TCP 8888。

2026-11-23 本机 S0 回归通过工作区离线测试和 Clippy、LAN 附件连续 12 次、前端 188 项测试、版本校验 4 项及 TLS 11 项。MSVC 报 D8050 时，在当前 PowerShell 设 `CARGO_TARGET_DIR=C:/lc-target` 和 `CARGO_PROFILE_DEV_DEBUG=0` 后复跑。TLS 测试临时目录写入失败时，可在当前进程将 `TEMP` 和 `TMP` 指向工作区下可写的测试目录。

在 Windows 验证局域网更新构建时，为工作区测试和 Clippy 命令加 `--features lan-chat-desktop/lan-updater`。房主 SQLite 迁移验收使用旧版库的隔离副本，保留迁移前数据库、证书与附件目录的同一份备份。[1.4.0 局域网验收记录](release-acceptance.md#140-局域网验收记录)记录迁移、自愈与兼容结果。

## Windows 发布包

~~~powershell
.\scripts\package-unified-release.ps1
.\scripts\test-member-package.ps1 -PackageDirectory .\release\LANChat-Desktop-Windows-x64 -Smoke
.\scripts\package-installer.ps1 -SmokeTest
node .\scripts\p10-tls-acceptance.mjs .\out\desktop\lan-chat-server.exe
~~~

使用 `-Edition online` 验证在线版打包。源码启动器通过 `scripts/package-source-launcher.ps1` 构建，并检查 ZIP 文件范围和启动器的 `--dry-run`。

## 双机与手机验收

- 参考[发布验收清单](release-checklist.md)，在两台电脑上验证发现、证书、房主审批、聊天、频道、附件和重连。
- 在 Android ARM64 设备安装 APK，用测试账号验证登录、消息和 FCM 通知。推送测试需要客户端与在线服务端使用同一 Firebase 项目。
