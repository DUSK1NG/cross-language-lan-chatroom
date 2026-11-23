# LAN Chat

Windows 聊天程序，提供局域网版和在线版。局域网版由房主在本机开房，在线版通过独立服务器提供账号登录和私信。

## 快速开始

两个版本都需要 Microsoft Edge WebView2 Runtime。局域网版使用 `LANChat-Setup-x64.exe`，在线版使用 `LANChat-Online-Setup-x64.exe`。两个版本可以同时安装。

房主选择“创建本地聊天室”，填写用户名、用户代码和房主密码并启动。成员选择“加入局域网聊天室”，在“附近聊天室”中选择房主，核对公开证书指纹后申请加入；房主批准后进入聊天室。

在线版填写同一台服务器的域名和端口，首次使用时注册用户名、密码和用户代码。登录后点会话列表上的加号，输入对方的用户代码发起私信。接收方离线时，服务器会保存消息并在其下次登录后投递。

Android 在线版从 APK 安装，使用同一套账号。手机和电脑可以同时登录，私信支持文字和图片。Android 的「设置 → 私信系统通知」用于开启或关闭消息提醒。

## 使用

局域网版支持频道、频道邀请、私信、历史记录、撤回、管理员操作和加密附件。

在「设置 → 外观」切换浅色和深色，消息区会随主题使用日间或夜间云层。

局域网自动发现、手动连接和房主审批见[局域网使用说明](docs/release-setup.md)。

## 文档

开发、测试、发布、架构与决策文档从[docs/README.md](docs/README.md)进入；Go + Wails + C++ 时代的历史计划与规格归档在[docs/archive/](docs/archive/README.md)，只读保留。

接手或恢复工作前先读[工作日志](WORKLOG.md)：最新进度、当前阻塞与下一步都在顶部；每次会话结束前在日志顶部追加一条。

## 配置

首次创建聊天室会在当前用户的本地应用数据目录生成服务端证书、私钥和数据库。只向成员共享公开证书；私钥、数据库和聊天记录不得打包或公开。首次加入应通过可信渠道核对证书指纹。

在线服务器使用 `lan-chat-server --mode online` 启动。`--listen` 设置 TCP 地址和端口，`--db` 指定在线数据库，`--cert` 和 `--key` 指定与客户端所填域名或 IP 匹配的 TLS 证书链和私钥，`--max-clients` 设置连接上限。FCM 参数为 `--fcm-service-account`、`--fcm-relay-url`、`--fcm-relay-secret-file`。跨网络使用时，客户端必须能访问服务器的 TCP 端口。

Android 系统通知使用 Firebase Cloud Messaging。构建时将 Android 应用配置 `google-services.json` 放到 `android/app/`，在线服务器通过 `--fcm-service-account` 读取同一 Firebase 项目的服务账号 JSON。服务账号文件保留在服务端。

## 开发

桌面端使用 Tauri 2、WebView2 和 React。服务端、协议、桌面业务与 Android JNI 传输均由 Rust 实现。Windows 构建需要 Rust、MSVC、Node.js 和 pnpm。先在 `frontend/` 安装锁定的依赖，再在项目根目录运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-desktop.ps1
```

成功时输出以 `Desktop build:` 开头的产物路径。运行 `cargo test --workspace --locked` 可执行 Rust 测试，成功时各测试组显示 `test result: ok.`。

`scripts/package-unified-release.ps1` 和 `scripts/package-installer.ps1` 接受 `-Edition lan` 或 `-Edition online`。在 Windows 和 Linux 上运行 `cargo build --locked --release -p lan-chat-server` 构建 Rust 在线服务端。成功时 Cargo 输出包含 `Finished`。

Android 构建需要 Cargo、`aarch64-linux-android` 编译目标和 `android/app/build.gradle` 指定的 NDK。在已配置的 Android SDK 环境中运行 `gradle -p android :app:assembleDebug`。成功时显示 `BUILD SUCCESSFUL`，ARM64 APK 位于 `android/app/build/outputs/apk/debug/app-debug.apk`。
