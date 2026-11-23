# Rust 迁移执行 Prompt（直接交给 agent 使用）

> 用法：把本文件全文作为任务描述发给编码 agent（Claude Code / Codex / 其他），工作目录设为 `C:\Users\Q1573\Desktop\projects\lan-chat`。
> 决策与阶段依据：[ADR-0001](../../decisions/0001-rust-only.md)、[Rust 迁移完成报告](../../rust-migration.md)。本文件是"怎么干"的操作说明，路线图是"为什么/做到哪"。
> **已归档（2026-11-22）**：迁移已完成，本文件保留供回溯；当前状态见 [Rust 迁移完成报告](../../rust-migration.md)。

## 0. 你的角色

你是本仓库的迁移执行者。目标是把 Lan Chat 从 Go + C++ + Rust 混合栈迁移为纯 Rust 栈（桌面 Tauri 2、服务端、协议、Android JNI 传输），最终删除 `server-go/`、`desktop/`（含 `desktop/native/` C++ MLS）。迁移期间不得破坏"能发布、能双机使用"的能力。

## 1. 开工前（约 10 分钟，必须先做）

1. 仓库当前**没有** `.git`（`ls .git` 失败）。先 `git init`，写 `.gitignore`（至少：`.tools/ out/ release/ vendor/ frontend/node_modules/ frontend/dist/ *.log *.db *.key *.pem *.crt`），提交一次基线：`chore: baseline before rust migration`。没有版本控制不得开始删改。
2. 记录基线结果（原样保存输出）：
   - `cargo test --workspace`
   - `cargo clippy --workspace --all-targets -- -D warnings`
   - `pnpm --dir frontend exec vitest run`
   基线若有失败项，先报告清单，不要顺手修。
3. 每阶段一个提交；阶段之间不改无关文件。

## 2. 硬性约束（违反即回滚）

1. **协议字节不变**：帧 = 4 字节大端长度 + UTF-8 JSON，1 ≤ length ≤ 65536（`protocol-rust` 的 `MAX_FRAME_BYTES = 64 * 1024`）；capability 字符串、登录/审批语义不变。
2. **MLS**：换实现不改密文格式；`isLegacyGeneratedMessageID` 的"纯数字 ID"规则逐字复刻；撤回占位文案"消息已撤回"不变；附件密文 `cipherOverhead = 64`、16 字节 tag 不变。
3. **TLS**：最低 1.2；禁止明文、禁止关闭证书校验；自签名证书必须显式 `--ca-file`。
4. **数据目录/文件名不变**：`%LocalAppData%\DUSK1NG\LAN Chat\host\{certs\server-lan.crt, certs\server-lan.key, chat.db}`；要兼容读取旧 `server-go\certs` 与 `server-go\chat.db`。
5. **前端不重写**：只替换桥接实现（`frontend/src/bridge/chatBridge.ts`、`frontend/src/app/runtimeBridge.ts`）。`window.go.main.Desktop` 暴露的命令与事件名（`chat:state` / `chat:result` / `chat:attachment` / `chat:error` / `chat:presence`）必须逐条对齐语义。
6. **不随意加依赖**：新增 crate 要锁版本并说明理由；MLS 选型（如 `openmls`）先用"双向密文互读"证明兼容再替换默认实现。
7. **不入库/不入包**：`.key .pem .crt .db`、聊天记录、`node_modules`、`.tools`、`out`、`release`、`*.log`。仓库根遗留的 `replay_pid38808.log` 应删除。
8. **禁止一次性大删除**：`server-go/`、`desktop/` 只能在 P6 验收通过后删除。每个阶段结束时仓库必须能 `cargo build`，且现有发布脚本仍能出包。
9. **不得绕过测试**：不改断言凑绿、不删测试、不跳过失败项。

## 3. 阶段任务

### P1 协议单源（3–5 天）

- 把 `server-go/wire/*.go`、`server-go/message.go`、`hub.go` 顶部常量、`desktop/mls.go` 中出现的全部 wire 消息（`login / login_ok / login_error / chat / private_chat / room_* / users_request / rooms_request / history_* / attachment_* / mls_* / system / command_id 幂等确认` 等）逐字段迁入 `protocol-rust/src/message.rs`。用 `#[serde(rename_all = "snake_case")]`，Go 的 `omitempty`/指针对应 `Option`。
- 生成 golden 样本：用 Go 侧真实序列化输出存到 `protocol-rust/tests/golden/*.json`，Rust 断言"反序列化 + 重新序列化"与样本 JSON 归一化后一致。
- 验收：`cargo test -p lan-chat-protocol` 全绿；`cargo clippy -p lan-chat-protocol --all-targets -- -D warnings` 通过；输出"仍缺字段/消息"的显式清单。

### P2 Rust 服务端补局域网（2–3 周，最高风险）

- 目标文件：`server-rust/src/{lib.rs, main.rs, hub.rs, auth_store.rs, attachment_store.rs, discovery.rs, auto_cert.rs, tls.rs, wire.rs, online*.rs}`。
- 必须复刻的行为（以 Go 为唯一对照）：
  - 连接审批：仅内存、60s 超时；批准后新连接**接管** ActiveCodes 中同代码旧会话并关闭旧连接（见 `hub.go` / `client.go`）。
  - 权限：`canJoinRoom / canViewRoom / canReadRoom / canManageRoom`、频道邀请名单、`room_action` 的 invite/remove_member/delete。
  - 历史 worker（异步 `enqueueHistory` / `handleHistoryResult`）、令牌桶 `allowInboundMessage`、最多 64 连接、慢客户端剔除。
  - UDP 38888 发现广播（房间名/地址/端口/公钥证书/SHA-256 指纹）。
  - 首次启动自签证书生成（`auto_cert.go`）、`tls.go` 最低 1.2 等价配置。
  - 附件 AES-256-GCM 分块、MLS 附件清单保护、`attachment_init` 等命令。
  - SQLite 12 张表 schema 与迁移逻辑（`auth_store.go`）。
- **关键门（必须实物验证）**：用现有 Go/Wails 客户端连接新 Rust 服务端，跑通：建房 → 审批 → 频道创建/邀请/踢人/删除 → 群聊/私信/引用/撤回 → 历史分页 → 附件上传下载 → 断线重连。任一项失败先修，禁止带病进入 P3。
- 验收：`cargo test -p lan-chat-server` 覆盖 Go 的 `hub_test.go / auth_store_test.go / message_test.go / attachment_store_test.go / mls_test.go` 同场景；`server-go` 暂时保留只读对照。

### P3 Tauri 2 桌面客户端（3–4 周）

- 新建 `apps/desktop`（Tauri 2 + 复用 `frontend/`），把 `desktop/*.go` 逐文件迁到 `crates/client-core`：`session / chat / attachment / mls / discovery / online_file / online_image`，`state.go` 变成命令层；`desktop/native/mls_client.cpp` 与 CMake 在 P4 就位后移除。
- 桥接替换：`window.go.main.Desktop.{State, Dispatch}` → Tauri `invoke`；`window.runtime.EventsOn` → `listen`；`OnFileDrop/OnFileDropOff` → Tauri 拖放；`BrowserOpenURL` → opener 插件。**JSON 形状不变**，前端只动 2 个桥接文件。
- 验收：`cargo tauri build` 出包；`pnpm --dir frontend exec vitest run` 全绿（`FakeChatBridge` 仍可用于测试）；手工跑通局域网版全流程。

### P4 Rust MLS 替换 C++（1–2 周）

- 先做双向互读：Rust 客户端读 C++ 产生的密文、C++ 客户端读 Rust 产生的密文；两者都通过后才切换默认实现。
- 验收：Go `mls_test.go` 的等价场景在 Rust 全绿；附件清单保护行为不变。

### P5 Android 收敛（1 周）

- 不要重写 `android/app/src/main/java/dev/dusk1ng/lanchat/MainActivity.java`（157KB 单文件）；只让 `rustTransport` 成为唯一路径，删除 `javaTransport` 分支与 `-PrustTrial` 构建开关，保留 JNI 方法签名。
- 验收：去掉 `-PrustTrial` 后 APK 可登录、收发消息、接收 FCM 推送；`javaTransport` 相关文件删除。

### P6 构建发布改 Rust（3–5 天）

- 需要同步修改的脚本与行号（当前都指向 Go 产物）：
  - `scripts/build-desktop.ps1:53-56`（`go -C server-go build -o ...\server-go\chat-server.exe .`）
  - `scripts/package-unified-release.ps1:28-29`（`server-go\chat-server.exe`、创建 `server-go` 目录）
  - `scripts/start-gui.ps1:17`、`scripts/test-member-package.ps1:28`（所需文件清单含 `server-go\chat-server.exe`）
  - `scripts/start-server.ps1:8`（`server-go` 目录）
  - `scripts/package-source-launcher.ps1:57,96`（`server-go\certs` 排除规则）
  - `scripts/p10-tls-acceptance.mjs:4-5`（`server-go/certs/...` CA 路径）
- 新包内容：`LANChat.exe`、`lan-chat-server.exe`、运行时 DLL、前端资源；不含 `server-go/`。
- 验收：`package-unified-release.ps1` → `test-member-package.ps1 -PackageDirectory .\release\LANChat-Desktop-Windows-x64 -Smoke` → `package-installer.ps1 -Version X.Y.Z -SmokeTest` 全过。

### P7 删除 Go/C++（1 天）

- 删除 `server-go/`、`desktop/`（含 `native/`）、空的 `client-cpp/` 目录；清理只为 Go/C++ 服务的缓存；更新 README / architecture / CODEX_HANDOFF 中"冻结"措辞；打 tag `rust-only-vX.Y.Z`。
- 验收：干净 clone 上 `cargo test --workspace`、`cargo build --locked --release --workspace`、`pnpm --dir frontend run build`、打包脚本全过；`grep -ri "server-go"` 与 `grep -ri "wails"` 仅剩历史文档（`docs/superpowers/`、`docs/stage9-lan-test.md` 等）。

## 4. 每阶段必须的报告格式

1. 变更摘要（文件清单，附关键行号）
2. 验证命令 + **真实输出**（逐条写通过/失败）
3. 与 Go 行为的差异清单（有意/无意）
4. 风险与下一步；未完成项
5. 没跑过的验证一律写"未验证"，不要写"应该没问题"

## 5. 绝对不要

- 不要"顺便"加功能、改 UI、改协议字段名、改数据目录。
- 不要为了变绿放宽断言或跳过测试。
- 不要在 P6 验收前删除 `server-go/` / `desktop/`。
- 不要对真实 `chat.db` 做破坏性迁移；测试一律用临时目录。

## 附录 A：如果坚持"现在就删 Go"

现在删除等于：局域网版与服务端功能整体消失——`server-rust` 只有在线版入口（`main.rs` 约 364 行，无 hub/审批/房间/MLS/附件/发现），`desktop-rust` 只是 stdin/stdout 的 TLS 传输 CLI 演示，没有 GUI、没有状态机。同时会直接打破：`build-desktop.ps1:56`、`package-unified-release.ps1:28-29`、`start-gui.ps1:17`、`test-member-package.ps1:28`、`start-server.ps1:8`、`p10-tls-acceptance.mjs:4-5`。

若仍要删，最小安全动作：先 `git init` 并提交基线 → `git rm -r server-go desktop client-cpp` → 同步改上述 6 处脚本 → 接受"局域网版暂时不可用、只剩 Rust 在线版"。建议先走 P1/P2，再删。
