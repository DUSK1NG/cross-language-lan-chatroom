# Rust 迁移完成报告

- 决策依据：[ADR-0001](decisions/0001-rust-only.md)
- 状态：**已完成**。Go（`server-go/`、`desktop/`）与 C++（`desktop/native/`、`client-cpp/`）实现已从仓库删除；桌面端为 Tauri 2，服务端、协议、桌面业务核心、Android 传输均为 Rust。
- 本文此前是迁移路线图；执行期的逐阶段操作说明见 [迁移执行 Prompt](archive/prompts/rust-migration-agent-prompt.md)（已归档）。文中的 P0–P7 指原路线图的阶段。

## 最终布局

```text
Cargo.toml                 workspace：protocol-rust / server-rust / desktop-rust /
                           crates/client-core / apps/desktop/src-tauri / tools/bootstrap
protocol-rust/             协议唯一权威 lan-chat-protocol：帧上限、LocalHostCredentials、capability 常量
server-rust/               服务端单进程，--mode lan/online
  src/lan/                 局域网：审批、房间与权限、消息、历史、私信、加密附件、MLS、自签证书、UDP 发现
  src/online/              在线：账号、私信、群组、文件、图片、FCM 推送
crates/client-core/        桌面业务核心：Edition、连接状态机、47 条命令分发、附件、MLS、发现
apps/desktop/src-tauri/    Tauri 2 外壳：chat_state / chat_dispatch / chat_open_url
desktop-rust/              客户端 TLS 传输（rustls）
tools/bootstrap/           源码启动器，包装 scripts/bootstrap-github.ps1
android/transport-rust/    Android 传输 JNI（workspace exclude，Gradle 单独构建）
frontend/                  React 19 + Vite 8，桥接改为 Tauri invoke / listen
```

与原计划的差异：计划的 `crates/protocol`、`crates/server`、`crates/transport` 没有新建，而是原地扩展 `protocol-rust`、`server-rust`，客户端 TLS 继续放在 `desktop-rust`；新增的只有 `crates/client-core` 与 `apps/desktop`。这样避免大范围移动文件，Git 历史保持连续。

## 阶段结果

| 阶段 | 计划 | 实际 | 证据 |
| --- | --- | --- | --- |
| P0 冻结与基线 | ADR / 路线图入库、CI 双轨 | Git 仓库已建立（origin GitHub，另有 backup bundle remote）；CI 提供 rust-server（协议与服务端测试）与 frontend（lint / vitest / build）两个作业 | `.git/config`、`.github/workflows/ci.yml` |
| P1 协议单源 | Go wire 类型迁入 protocol-rust | 帧与消息类型、能力常量、LocalHostCredentials 在 protocol-rust；Go 侧随删除消失 | protocol-rust/src/lib.rs、protocol-rust/src/message.rs |
| P2 局域网服务端 | 房间 / 审批 / 历史 / 私信 / 附件 / MLS / 自签证书 / 发现 | 全部在 server-rust/src/lan/（约 105 KB），--mode lan 并兼容旧单横线参数 | server-rust/src/lan/*.rs |
| P3 桌面客户端 | client-core + Tauri 2 + 桥接改造 | 完成；命令面 chat_state / chat_dispatch / chat_open_url，事件仍为 chat:state、chat:result 等 | crates/client-core/src/*.rs、apps/desktop/src-tauri/src/main.rs、frontend/src/app/runtimeBridge.ts |
| P4 MLS 替换 | Rust MLS 库替换 MLS++ | 采用 mls-rs 0.58（crates/client-core/Cargo.toml），服务端与客户端各有一套状态机移植 | crates/client-core/src/mls.rs、server-rust/src/lan/mls.rs |
| P5 Android 收敛 | 删除 javaTransport | android/app/build.gradle 指向 transport-rust；源码中已搜不到 javaTransport / -PrustTrial | android/transport-rust/src/lib.rs |
| P6 构建发布 | 构建改 cargo | scripts/build-desktop.ps1 走 cargo build -p lan-chat-desktop；新增 package-source-launcher.ps1 与 tools/bootstrap | scripts/*.ps1、tools/bootstrap/src/main.rs |
| P7 删除 Go | 删目录、改文档 | 已删除；全仓 **/*.go 计数为 0；README、architecture、testing、CODEX_HANDOFF、发布文档均已改写 | 全仓 glob / grep |

## 验证状态

已验证（文件证据）：

- 服务端与传输层可构建：out/rust/release/lan-chat-server.exe（6.6 MB）、out/rust/release/lan-chat-transport.exe（1.2 MB）存在。
- CI 覆盖协议与服务端测试、前端 lint / vitest / build；但不在 CI 中构建 Windows 桌面安装包（Tauri 打包仍需 Windows 本机脚本）。
- 数据兼容不变量保持：数据目录 %LocalAppData%\DUSK1NG\LAN Chat\host\ 未变；帧格式 4 字节大端长度 + UTF-8 JSON、1 <= length <= 65536 未变；TLS 最低 1.2 与「核对指纹 + 房主批准」两道准入检查未变。

尚未完成（截至 2026-11-22）：

- cargo test --workspace --locked 与前端测试尚未在本机跑通并留证；Go 时代约 65 KB 的 hub / auth_store / attachment / MLS 用例尚未移植，目前服务端只有 server-rust/tests/lan_desktop.rs 一个集成测试。
- 桌面安装包未产出：out/desktop、out/desktop-online 不存在，release/ 为空，installer/LANChat.iss 未验证。
- 版本仍是 1.3.0（apps/desktop/src-tauri/Cargo.toml），docs/release-notes/ 只有 v1.3.0.md；发布 Rust 版需要新版本号与发行说明。
- 双机手测清单（建房、指纹核对、审批、断线重连、加密附件、历史窗口化、深色主题、快捷键）的执行记录待补。

## 后续阅读

- 当前架构：[docs/architecture.md](architecture.md)
- 构建与测试命令：[docs/testing.md](testing.md)
- 历史规格：Go / Wails 时代文档已归档到 `docs/archive/project-specs/`（规格 01–04）与 `docs/archive/handover/`，顶部有归档横幅，仅作历史参考；索引见 [归档说明](archive/README.md)。
- 原执行说明：[迁移执行 Prompt](archive/prompts/rust-migration-agent-prompt.md)（已归档）。
