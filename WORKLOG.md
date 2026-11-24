# LAN Chat 工作日志

> 作用：让任何 agent 或人随时接手。**开工先读「接手摘要」，收工在你的会话结束前把新条目加到「日志」最上面。**
> 配套材料：[交接说明](docs/CODEX_HANDOFF.md)（操作手册）· [架构说明](docs/architecture.md) · [v1.4 升级与加固合并计划](docs/plans/2026-11-23-v1.4-plus-hardening-plan.md) · [服务端加固计划 v2](docs/plans/2026-11-23-server-hardening-plan.md)

## 记录规则

1. 只追加、倒序（最新在上）；不改写旧条目，有更正就在新条目里写。
2. 每条固定写清：时间 / 执行者 / 做了什么（含文件路径）/ 验证（命令与结果，没跑就写“未验证”）/ 下一步 / 阻塞与需要人拍板的事。
3. 不写聊天内容、私钥、token、证书私钥内容；只写路径与结论。
4. 本文件纳入 Git（不要加进 .gitignore）。收工前跑 `git status --short` 并把要点写进条目。
5. 超过约 300 行时，把最早的条目移到 `docs/archive/worklog-YYYY.md`，本文件保留链接。

## 接手摘要（更新：2026-11-23 19:18）

- **项目**：Windows 聊天程序。局域网版 = 房主本机开房（TLS/TCP 8888 + UDP 38888 发现 + SQLite）；在线版 = 独立 Rust 服务器（账号、私信、离线消息、Android APK、FCM 推送）。桌面 = Tauri 2 + WebView2 + React；服务端、协议、桌面核心、Android JNI 传输均为 Rust。
- **版本**：当前桌面源码版本为 `1.3.0`。v1.4.0 的实施与验收按 [最终执行版](docs/prompts/v1.4/README.md) 推进。
- **Git**：Rust 工作区基线为 `fcb7b19`，与远端默认分支 `origin/master` 的 `3ebc740` 合入本地 `master`。`Claude-BugHunter/` 按本地第三方目录忽略，原有 autostash 保留。
- **S0 当前状态**：Git 基线已建立。附件端到端用例同一源码下出现通过和超时，仍须定位并修复。全量 workspace 离线测试缺依赖缓存。H1 以 S0 验收为开工条件。

## 关键约束（改代码前必读）

- 协议帧：4 字节大端长度 + UTF-8 JSON，单帧 1 ≤ length ≤ 65536；新行为通过 capability 协商，旧客户端行为逐字不变。
- 数据目录：`%LocalAppData%\DUSK1NG\LAN Chat\host\`（certs\server-lan.crt、certs\server-lan.key、chat.db）；在线版数据在 `%LocalAppData%\DUSK1NG\LAN Chat Online\`。表结构迁移走 `PRAGMA user_version`，幂等、可回滚，测试只用临时目录。
- 安全：不关闭或放宽 TLS 证书/主机名校验；不新增绕过房主实时审批的路径；附件权限校验不得削弱；私钥、chat.db、日志、聊天内容不进 Git / Release / ZIP / 安装器 / 日志输出。
- 前端边界：React 不碰 socket、TLS、证书、私钥或 SQLite；桥接集中在 `frontend/src/bridge/chatBridge.ts` 与 `frontend/src/app/runtimeBridge.ts`。
- 动画只允许短时 transform / opacity；性能面板与基准门禁只用于测试，不进发布 UI。
- 局域网版零服务器、离线可开房；不新增重量级依赖。

## 常用验证命令（在项目根执行）

~~~powershell
git status --short
git log -1 --oneline

cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo build --workspace --locked --release

pnpm.cmd --dir .\frontend exec vitest run
pnpm.cmd --dir .\frontend run build

powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1
~~~

打包产物：`release\LANChat-Desktop-Windows-x64.zip`（package-unified-release.ps1）、`release\LANChat-Setup-x64.exe`（package-installer.ps1 -SmokeTest）。

## 环境坑（本机 / agent 会话）

- DSH `run_code` 里 OPS 工具要先 `ops_load_sdk`，且加载只在**下一个程序**生效；`tools.glob` 的 `"*"` 是递归按 basename 匹配（会命中 .tools 里 vendored 源码），列顶层要用 `ops_glob`（`kind:"all", pattern:["*"]`）。
- 早前会话里 shell `cd` 到本项目曾报 `Permission denied`；当前 file policy 为 danger-full-access，`ops_bash` 在项目根可直接跑 git/cargo（本文件里的 Git 状态即由此取得）。
- 文档写的诊断日志目录 `%LocalAppData%\DUSK1NG\LAN Chat\logs\` 在本机**尚不存在**：2026-11-23 检查 DUSK1NG 下没有任何 *.log / *.jsonl，只有证书、附件与 known-hosts。
- 机器时区 Asia/Taipei，日志时间用本地时间。

## 日志

### 2026-11-24 — 1.4 完整候选的本机验证

- 执行者：Codex。
- 改动：`server-rust/src/lan` 按职责拆分，并将附件文件 I/O 移出 Hub 锁。`server-rust/src/online` 增加 FTS5、磁盘附件和旧库迁移、群管理及群附件。桌面和 Android 接入群功能。发布工作流配置两版独立签名和 Android 构建。
- 验证：Rust 工作区、Clippy、前端 188 项、版本校验 4 项及 LAN TLS 11 项通过。群权限、旧附件迁移和 LAN 附件并发用例通过。两版 ZIP 及安装器已构建。Android 0.4.0 ARM64 APK 构建和 v2 签名通过。
- 产物：`release/v1.4-candidate/`。双机按用户要求跳过，Android 以本机 APK 验收。候选干净 CI 使用独立测试分支，正式签名和标准安装权限待确认。

### 2026-11-23 — 1.4 局域网候选与本机验收

- 执行者：Codex。
- 改动：S0 提交 `aa04d78` 推送测试分支。`server-rust` 完成心跳回归、FTS5 历史搜索和房间权限持久化，`client-core` 与 `frontend` 接入搜索分页及房间权限。Tauri、Inno Setup 和 LAN 发布工作流接入签名更新。
- 验证：S0 干净 CI 四项通过。本机工作区测试和 Clippy、前端 lint/188 项测试/构建、版本检查 4 项、TLS 11 项通过。完成 1.3 与 1.4 双向互通、旧库迁移、隔离签名更新及卸载保留数据。详见[局域网验收记录](docs/release-acceptance.md#140-局域网验收记录)。
- 工作区与产物：`stage/0-baseline` 上保留后续本地源码改动。`release/v1.4-local-acceptance/LANChat-Desktop-Windows-x64.zip` 仅含客户端、房主服务程序、图标和说明。
- 待验收：候选干净 CI、正式签名演练、可写注册表和快捷方式的 Windows 安装环境、真实双机发现与重连。完成后交付无数据 LAN 便携 ZIP。

### 2026-11-23 19:43 — S0 本机基线与局域网便携包

- 执行者：Codex。
- 改动：`server-rust/src/lan/mod.rs` 让附件控制帧和加密附件消息使用原命令限额排队，修复附件落盘后消息被限流的问题。`lan_desktop.rs` 同时断言发送方和接收方都能看到附件。桌面 `1.4.0` 版本从工作区 Cargo 配置同步，CI 增加 LAN 安装器预检及 TLS 验收。
- 验证：工作区离线测试及 Clippy 通过，附件连续 12 次通过，前端 188 项测试与构建通过，版本校验 4 项通过。Windows LAN 便携包启动、安装器预检和 TLS 11 项通过，ZIP 检查仅含客户端、房主服务程序、图标和说明。
- 产物：`release/v1.4-s0/LANChat-Desktop-Windows-x64.zip`。
- 后续：干净 CI、后续阶段和完整 1.4 验收待完成。

### 2026-11-23 19:18 — 合并 Rust 工作区与 v1.4 执行文档

- 执行者：Codex。
- 改动：将 Rust 服务端、桌面、Android、前端及 v1.4 文档提交为 `fcb7b19`，先同步远端 `master` 的 PR #25 合并提交，再合入本地 `master`。
- 验证：所有已获取的项目分支均在合并历史内。合并前检查通过，无冲突。前端 188 项测试、lint、生产构建及版本检查 4 项测试通过。Rust 协议 5 项通过，`lan_desktop` 首次通过、再次运行在附件步骤超时。全量 workspace 离线测试缺少 `alloc-no-stdlib v3.0.0`。
- 后续：按 S0 验收条件处理附件间歇失败及干净环境构建。当前 Git 操作为本地合并。

### 2026-11-23 18:40 — 补齐 H1–H4 执行 Prompt
- 执行者：lead（DSH agent）
- 做了什么：新建 `docs/prompts/v1.4/H1-heartbeat.md`（91 行）、`H2-fts-search.md`（89 行）、`H3-hub-split.md`（75 行）、`H4-attachment-io.md`（84 行），均含前置条件、任务分解、设计红线、验收命令、报告与禁止项；`docs/prompts/v1.4/README.md` 表格与引言改为合并计划口径并加入 H1–H4 行；合并计划 §13 交付物改为 10 份 prompt 实际链接。
- 验证：文档级改动，未跑测试；已确认 4 个新文件存在且无占位符。
- 下一步：等 G0 拍板 → S0 开工。
- 阻塞/拍板：G0（基线提交、Claude-BugHunter/）、H4（WAL）、H5（rename）；产品 P1–P7。

### 2026-11-23 18:20 — 修订加固计划并合并 v1.4 计划
- 执行者：lead（DSH agent）
- 做了什么：把 [服务端加固计划](docs/plans/2026-11-23-server-hardening-plan.md) 修订为 v2（修正编译/Git/环境事实；补 FTS 自愈与 MATCH 回退、C2 WAL/测试钩子/权限对照/rename、门禁分级）；新建 [v1.4 升级与加固合并计划](docs/plans/2026-11-23-v1.4-plus-hardening-plan.md)（8 周主路径 + 6 周保底、S0→H1–H4→S1–S5、统一决策编号）；两份原计划加状态说明；docs/README.md 索引已更新。
- 验证：文档级改动，未跑测试。引用基线事实均来自 2026-11-23 实测：D8050 环境变量绕过可编译；lan_desktop e2e 失败于附件步骤；git 分支 feat/01-stability-downloads，HEAD 78552a7，306 项未提交。
- 下一步：等用户拍板 G0 后执行 S0。
- 阻塞/拍板：G0（基线提交、Claude-BugHunter 去留）、H1/H4/H5、P1–P7。

### 2026-11-23 13:20 — 建立工作日志机制（本文件）

- 执行者：lead（DeepSeek Harness）
- 做了什么：用户要求“记录工作日志以保证其他 agent 可随时接手，先检查本机有没有日志”。检查结论：**此前没有任何工作日志**——仓库根无 WORKLOG.md / AGENTS.md，无 worklog 目录，全仓库无 *.log / *.jsonl（.tools 依赖缓存除外）；`%LocalAppData%\DUSK1NG` 下也无日志文件；最接近的交接材料是 `docs/CODEX_HANDOFF.md`（2026-11-22 更新）与 `docs/rust-migration.md`。新建本文件，并在 `README.md` 与 `docs/README.md` 增加入口。
- 验证：写入后用 `ops_read_files` 回读；README / docs 索引中的入口指向本文件。
- 下一步：按服务端加固计划 P0 做环境检查（cargo / link.exe 可读写），再进阶段 A。
- 阻塞：G0（305 项改动是否提交）、Claude-BugHunter/ 归属。

### 2026-11-23 — 服务端加固计划制定（待执行）

- 执行者：用户 / 其他 agent（本会话只读取，未参与制定）
- 做了什么：新增 `docs/plans/2026-11-23-server-hardening-plan.md`：A 应用层心跳 heartbeat-v1（20s ping / 60s 超时）、B FTS5 历史搜索、C1 拆分局域网 Hub、C2 附件 IO 移出 Hub 锁；要求在该计划完成后再做 v1.4 的 S2。
- 验证：**未验证**——计划自述“待执行”，阶段 A 代码已写入但从未编译。
- 下一步：P0 环境检查 → 基线 `cargo test --workspace --locked`。
- 阻塞：cargo / link.exe 的历史 ACL 故障（CodexSandboxUsers；cargo 报 `failed to create directory` / `failed to open: Cargo.lock`，link.exe 报 `LNK1104`）是否已修复未知。

### 2026-11-22 — v1.4 升级计划与阶段执行 Prompt 交付

- 执行者：lead（DeepSeek Harness），依据用户 4 轮问答
- 做了什么：产出 `docs/plans/2026-11-22-v1.4-upgrade-plan.md` 与 `docs/prompts/v1.4/`（00-baseline、01-release、02-lan-persistence、03-online-storage、04-online-groups、05-android-acceptance、README）；`docs/README.md` 增加“计划与执行 Prompt”段；48 条相对链接校验 0 失效。
- 决策：6 周 S0–S5；LAN 1.4.0 + Online 1.4.0 同版；优先 G1 发布与自更新 / G2 局域网权限持久化 / G3 在线存储重构 / G4 在线群 / G5 底座；超期先砍局域网管理员/禁言持久化；桌面 1.4.0、Android 0.4.x；单一版本源。
- 验证：链接已校验；计划本身未评审、未开工。
- 下一步：S0（Git 基线、版本同步脚本、CI 补齐）——但按服务端加固计划，先做该计划。

### 2026-11-22 — 文档去 Go 化、迁移完成报告与历史归档

- 执行者：lead（DeepSeek Harness）
- 做了什么：`docs/rust-migration.md` 重写为“迁移完成报告”；`docs/decisions/0001-rust-only.md` 标记“已接受并已执行”；`docs/project/specs/01–04`、`docs/handover`、`docs/prompts` 加归档横幅；`engineering-methodology.md` 命令表改为 Rust；Go + Wails + C++ 时代文档随后归入 `docs/archive/`（现含 README.md、developer-transfer.md、handover/、project-specs/、prompts/、stage9-lan-test.md、superpowers/），只读保留。
- 验证：当时仓库没有 .git，无法用 diff 核对；文档链接在后续索引更新中检查。
- 下一步：无（历史工作，已归档）。
