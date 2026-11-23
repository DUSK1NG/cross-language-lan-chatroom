# 构建与测试说明

在 Windows 开发机上按 README 准备 Rust、MSVC、WebView2、Node.js 和 pnpm。在项目根目录执行：

~~~powershell
pnpm.cmd --dir frontend run lint
pnpm.cmd --dir frontend exec vitest run
pnpm.cmd --dir frontend run build
cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo build --workspace --locked --release
~~~

协议测试校验帧边界与历史 JSON 样本。局域网桌面集成测试通过两个客户端创建聊天室、批准成员、加入密码频道、发送消息并上传和解密下载附件。测试使用临时的证书和数据库，房主端口为 TCP 8888。

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
