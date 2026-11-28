# LAN Chat

[![CI](https://github.com/DUSK1NG/cross-language-lan-chatroom/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/DUSK1NG/cross-language-lan-chatroom/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/DUSK1NG/cross-language-lan-chatroom?label=release)](https://github.com/DUSK1NG/cross-language-lan-chatroom/releases/latest)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Android-4a4a4a)

一个 Windows 聊天程序，有两种用法：

- **局域网版**：一个人（房主）在自己电脑上开一个聊天室，同一个局域网里的人就能加入聊天，不用注册账号。
- **在线版**：你自己架一台服务器，大家注册账号后可以跨网络聊天，对方不在线时消息会先存着，等他上线再收到。

另外还有 Android 手机版，配合在线版使用。整个程序不依赖任何第三方云服务。

| 版本 | 适合什么场景 | 你需要准备什么 |
| --- | --- | --- |
| 局域网版 `LANChat-Setup-x64.exe` | 同一个局域网（或虚拟局域网）里聊天 | 一台 Windows 电脑，不用注册 |
| 在线版 `LANChat-Online-Setup-x64.exe` | 不在同一个网络，想长期使用 | 自己架服务器，并准备域名和证书 |
| Android `LANChat-Android-arm64.apk` | 用手机登录在线版 | 和电脑端同一个账号 |

![局域网浅色界面](docs/assets/chat-light.png)

## 功能

**局域网版**

- 聊天室和私聊；聊天室可以设密码、白名单，也可以邀请别人
- 房主可以设管理员、禁言，每个人加入都要房主点头同意
- 查看历史消息、搜索聊天记录、撤回消息
- 发送的文件会加密保存
- 可以自动发现附近的聊天室，也可以手动填地址；有浅色和深色两种主题

**在线版**

- 注册登录、离线消息、私聊发文字和图片
- 群聊和群文件、已读提示、多台设备同时登录
- 文件保存在服务器数据库旁边的 `.files` 文件夹里
- 可选的手机推送，Android 能收到通知

**两个版本都有**

- 自动更新：只接受官方签名的安装包，验证不通过就不安装
- 在程序里可以直接看版本更新说明

## 下载与安装

到 [Releases](https://github.com/DUSK1NG/cross-language-lan-chatroom/releases/latest) 下载：

| 文件 | 说明 |
| --- | --- |
| [LANChat-Setup-x64.exe](https://github.com/DUSK1NG/cross-language-lan-chatroom/releases/latest/download/LANChat-Setup-x64.exe) | 局域网版安装包 |
| [LANChat-Online-Setup-x64.exe](https://github.com/DUSK1NG/cross-language-lan-chatroom/releases/latest/download/LANChat-Online-Setup-x64.exe) | 在线版安装包 |
| [LANChat-Android-arm64.apk](https://github.com/DUSK1NG/cross-language-lan-chatroom/releases/latest/download/LANChat-Android-arm64.apk) | Android 手机版（在线版） |
| [SHA256SUMS](https://github.com/DUSK1NG/cross-language-lan-chatroom/releases/latest/download/SHA256SUMS) | 所有文件的校验码，用来确认下载没被改过 |

系统要求：Windows 10 或 11（64 位），并安装 [Microsoft Edge WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/)（Windows 11 自带）。两个桌面版本可以同时装。

安装包带有用于自动更新的签名，但没有购买 Windows 的代码签名证书，所以 Windows 可能弹出“未知发布者”的提示。请对照 Releases 页面里的 `SHA256SUMS` 确认文件没有问题再安装。

### 第一次使用

1. **局域网版**：房主点“创建本地聊天室”，填用户名、用户代码和房主密码，然后启动。其他人点“加入局域网聊天室”，在“附近聊天室”里选中房主，**先通过微信、电话等可信的方式，和房主核对屏幕上显示的证书指纹**，再申请加入，房主同意后就能进去。更详细的步骤见 [docs/release-setup.md](docs/release-setup.md)。
2. **在线版**：填服务器的域名和端口，第一次使用时注册用户名、密码和用户代码。点会话列表里的加号，输入对方的用户代码就能私聊；对方不在线时，消息会在他下次登录后送到。
3. **Android**：安装 APK，用同一个账号登录。想要通知，到“设置 → 私信系统通知”里打开。

## 安全说明

这是自己架设的聊天工具，安全情况和大型公共聊天软件不一样，使用前请先读完这一节。

- **传输加密**：客户端和服务端之间的通信是加密的。局域网版由房主自己生成证书，第一次连接某位房主时，需要你人工核对证书指纹，确认没连错人。自动发现附近聊天室时，系统并不能证明对方就是房主本人，所以核对指纹这一步不能省。
- **谁能看到聊天内容**：房主（局域网版）或服务器管理员（在线版）**能看到聊天内容**，这是设计如此。发送的文件虽然加密保存，但这只是防止文件被明文放在硬盘上、被同一网络里的旁观者直接看到，**防不住房主或服务器管理员**。
- **身份**：局域网版没有密码，身份就是“用户名 + 用户代码”，真正的把关人是房主的审批，所以请当面或打电话确认对方是谁。某人第一次成功加入后，这个账号会和他当前这台电脑绑定，换一台设备登录同一个账号会被拒绝；房主把这个成员“踢出”后，绑定才会解除。老版本客户端不带这种绑定也能进房间，但收不到离线私信。在线版的密码在服务器上是加密后保存的，不存明文。
- **更新安全**：只接受 HTTPS 下载，并用程序里内置的公钥验证签名，同时检查文件大小和文件名。没有内置公钥的版本会直接拒绝更新。
- **防止被占满**：在线服务器对每个人待收的离线消息有上限（默认 200 条或 64 MiB，14 天后过期），已经送达的私信默认保留 30 天。图片总量、整体存储空间、单个人发的群消息量、磁盘最少剩余空间也都有上限（默认依次为 2 GiB、8 GiB、1 GiB、2 GiB）。超过上限时，发消息的人会看到“稍后再试”，旧消息不会被挤掉。
- **防止恶意占位**：局域网版会单独限制“还没登录成功的连接”的数量（默认总共 128 个，同一来源最多 8 个），每个连接握手和登录各自有时间限制（默认 5 秒、8 秒）。只有房主同意后，才算正式占用房间名额，所以有人故意只连不登录，也占不满聊天室。
- **数据存放**：证书私钥、数据库、聊天记录和文件只保存在本机的用户数据目录里（在线版的文件放在数据库旁的 `.files`）。数据库运行时旁边还会有 `-wal`、`-shm` 两个临时文件，备份时请一起复制。请不要把这些文件传到 Git、同步网盘或 Release 里。

发现安全问题，请按 [SECURITY.md](SECURITY.md) 私下报告，不要直接在公开的 issue 里写。

## 技术栈

以下内容面向开发者。

| 部分 | 使用的技术 |
| --- | --- |
| 桌面端 | Tauri 2.12、WebView2、Rust 1.85 |
| 界面 | React 19.2、TypeScript 6、Vite 8、Tailwind CSS 4、Radix UI、framer-motion |
| 界面测试 | Vitest 4、Testing Library、jsdom |
| 服务端 | Rust、tokio、rustls、rusqlite（内置 SQLite，支持全文搜索） |
| 通信格式 | 自定义 JSON 消息：先发 4 字节表示长度，再发 JSON 内容，单条 1 到 65536 字节 |
| 加密 | 传输用 TLS 1.2 及以上；文件用 AES-256-GCM 分块加密，文件清单用 `mls-rs` 0.58 分发；更新包用 minisign 签名 |
| Android | Java（最低 Android 9，目标 Android 15）+ Rust 通信层 + FCM 推送 |
| 构建 | Cargo workspace、pnpm 11.19、GitHub Actions（5 个任务） |

## 仓库结构

```
protocol-rust/        通信协议与消息类型（唯一来源）
server-rust/          服务端：局域网房主模式 + 在线服务器
desktop-rust/         桌面端的业务逻辑与会话状态
crates/client-core/   客户端核心：加密连接、局域网发现、文件加密、更新
apps/desktop/         Tauri 桌面外壳（src-tauri）
frontend/             React + Vite 界面
android/              Android 客户端与 Rust 通信层
tools/bootstrap/      统一运行包的启动器
scripts/              构建、打包、发布与验收脚本
docs/                 架构、协议、发布与验收文档
```

## 从源码构建

需要安装 Rust（含 MSVC 工具链）、Node.js、pnpm 和 WebView2。第一次构建：

```powershell
pnpm.cmd --dir frontend install --frozen-lockfile
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-desktop.ps1
```

成品在 `out/desktop`，构建成功时最后一行会以 `Desktop build:` 开头。要构建在线版，在命令后面加 `-Edition online`。

想让构建出的程序支持自动更新，需要在构建时提供更新公钥，否则程序会拒绝更新：

```powershell
$env:LANCHAT_UPDATER_PUBLIC_KEY = "<minisign 公钥>"
```

### 测试

```powershell
pnpm.cmd --dir frontend run lint
pnpm.cmd --dir frontend exec vitest run
cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
node scripts/check-version.mjs
```

详见 [docs/testing.md](docs/testing.md)。

## 部署在线服务端

```bash
cargo build --locked --release -p lan-chat-server
./target/release/lan-chat-server --mode online \
  --listen 0.0.0.0:8443 \
  --db /var/lib/lan-chat/chat.db \
  --cert /etc/letsencrypt/live/example.com/fullchain.pem \
  --key /etc/letsencrypt/live/example.com/privkey.pem \
  --max-clients 512
```

下面是各种上限参数，括号里是默认值，都可以自己改：

- `--undelivered-per-target`（200）、`--undelivered-bytes-per-target`（67108864）、`--undelivered-ttl-days`（14）：每个人最多能暂存多少条离线消息、多少字节，以及消息保留几天。填 `0` 表示不限制。
- `--delivered-ttl-days`（30）：已送达的私信保留几天，填 `0` 表示一直保留。
- `--image-budget-bytes`（2147483648）：所有图片加起来最多占多少空间。
- `--storage-budget-bytes`（8589934592）：服务器存储的总上限。
- `--group-sender-budget-bytes`（1073741824）：单个人发的群消息最多占多少空间。
- `--min-disk-free-bytes`（2147483648）：数据库所在磁盘至少要留多少空闲空间，低于这个值就不再接收新文件，填 `0` 表示不检查。
- `--lan-max-preauth`（128）、`--lan-max-preauth-per-ip`（8）、`--lan-tls-handshake-timeout-secs`（5）、`--lan-login-frame-timeout-secs`（8）：局域网版里，还没登录成功的连接最多允许多少个（总共、每个来源），以及握手、登录各等多少秒。
- `--preauth-fail-limit`（8）、`--preauth-fail-window-secs`（60）、`--preauth-ban-secs`（120）：某个来源在 60 秒内连续失败 8 次，就暂时封禁 120 秒。
- `--vacuum`：单独运行一次“磁盘整理”，把已删除消息占用的空间还给磁盘（运行时不会启动服务，需要同时指定 `--mode online` 和 `--db`，并预留和数据库文件差不多大的空闲空间）。
- 其他参数（`--max-preauth`、`--max-preauth-per-ip`、`--max-auth-concurrency`、`--account-fail-threshold` 等）见 [计划文档](docs/plans/2026-11-27-p0-hardening-plan.md) 第 6 节。

另外几点：

- `--cert` 和 `--key` 必须是和客户端所填域名（或 IP）匹配的证书和私钥。
- 文件默认保存在数据库同目录的 `.files` 里；备份、迁移和清理旧文件见 [docs/architecture.md](docs/architecture.md#在线存储与迁移)。
- 要开启手机推送，需要 `--fcm-service-account`（Firebase 的服务账号文件，只放在服务器上）、`--fcm-relay-url`、`--fcm-relay-secret-file`，并且手机 App 要使用同一个 Firebase 项目。
- 客户端必须能访问服务器的这个 TCP 端口；如果服务器在路由器后面，需要做端口映射，或者使用虚拟局域网，参考 [docs/cross-network-radmin.md](docs/cross-network-radmin.md)。

发布新版本的流程（版本号、安装包、更新清单、签名文件）见 [docs/release-runbook.md](docs/release-runbook.md) 和 [docs/github-publishing.md](docs/github-publishing.md)。

## 项目现状

- 最新正式版是 `v1.4.0`。`master` 分支现在是 1.4.1 的开发版本，已经加入一批安全加固（防占满局域网聊天室、修复踢人和设备绑定的问题、给在线服务器加上存储上限、文件读写不再卡住其他人），还没有正式发布。设计和计划见 [docs/superpowers/](docs/superpowers/)。
- 分支做法：只保留 `master`；要发新版本时临时开分支，合并后就删除。推送 `v` 开头的标签会触发 [自动发布流程](.github/workflows/release.yml)。
- 自动检查（CI）会跑 Rust 测试与代码检查、界面检查与测试、Windows 桌面构建、Android 构建和源码启动器；`master` 上的检查必须保持全绿。
- 依赖版本锁在 `Cargo.lock` 和 `frontend/pnpm-lock.yaml` 里，升级请单独提交，并跑完整测试。
- 接手开发请先读 [WORKLOG.md](WORKLOG.md) 和 [docs/CODEX_HANDOFF.md](docs/CODEX_HANDOFF.md)；收工前在 WORKLOG 最上面补一条记录。

## 文档

| 文档 | 内容 |
| --- | --- |
| [docs/README.md](docs/README.md) | 文档总目录 |
| [docs/architecture.md](docs/architecture.md) | 程序怎么运行、通信、存储和安全边界 |
| [docs/protocol.md](docs/protocol.md) | 消息格式和兼容规则 |
| [docs/known-issues.md](docs/known-issues.md) | 已知问题和绕开办法 |
| [docs/release-setup.md](docs/release-setup.md) | 局域网版使用说明 |
| [WORKLOG.md](WORKLOG.md) | 开发日志和当前进度 |

## 参与

欢迎提 issue 和 pull request。提交前请：

1. 在本地跑完上面“测试”里的命令；
2. 不要提交证书、私钥、数据库、日志，也不要在截图里露出真实账号；
3. 如果是安全漏洞，请按 [SECURITY.md](SECURITY.md) 私下报告，不要开公开 issue。

## 许可

本仓库目前**没有附加开源许可证**，也就是保留所有权利。如果想转发、修改后发布或商用，请先联系作者，取得书面许可。
