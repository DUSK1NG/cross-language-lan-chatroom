# 服务端加固计划（v2 · 技术明细）：心跳 · FTS5 搜索 · 拆分 Hub

> 状态：已修订（v2，2026-11-23）· **技术明细附录** —— 执行顺序、时间线与门禁以合并计划 [2026-11-23 v1.4 升级与加固合并计划](2026-11-23-v1.4-plus-hardening-plan.md) 为准
> 制定：2026-11-23（v1）；修订：2026-11-23（v2，依据实际构建与测试取证；修订清单见 §0.1）
> 执行方式：编码 agent 按 H1 → H2 → H3 → H4（A → B → C1 → C2）顺序实现，每阶段过门禁后再进下一阶段
> 关联：[v1.4 升级计划（已被合并计划取代）](2026-11-22-v1.4-upgrade-plan.md) · [工作日志](../../WORKLOG.md)
> 方法论：[engineering-methodology.md](../project/steering/engineering-methodology.md)（与本文冲突时以本文 §3 为准）

---

## 0. 给 agent 的一句话

在 `server-rust`、`protocol-rust`、`crates/client-core` 中完成三件事：**应用层心跳**（尽快清理断线会话）、**历史搜索改用 SQLite FTS5**（中文子串可搜、不再全表扫描）、**拆分局域网 Hub 并把附件 IO 移出全局锁**（大文件上传不再卡住聊天）。阶段 H1 的代码**已经写好且已验证可以编译**（见 §4），你的第一件事是补齐测试、修复基线里已失败的端到端测试、并完成文档；不要假设它从未编译过。

### 0.1 v2 相对 v1 的修订（全部为实测结论）

1. §0 / §5.1：删除“阶段 A 代码从未编译过”。实测可用 §4 的绕过命令编译通过。
2. §4 P0：原内容只写 ACL/LNK1104；补入实际的 MSVC `cl.exe` D8050 失败、可用的环境变量绕过方式与“禁止改 ACL”。
3. §3.6 Git：修正事实——仓库**有** Git（分支 `feat/01-stability-downloads`，HEAD `78552a7`，约 306 项未提交改动），不是“未被 Git 跟踪”；因此不是 `git init`，而是“由 S0 提交 Rust 迁移基线”。
4. §5 A1：范围缩为“测试 + clippy + 文档 + 修基线红测试”，不再包含“让它编译”。
5. §5 A2：`--heartbeat-timeout-secs` 上限收到 60（客户端读超时硬编码 60s），并注明仅供测试缩短。
6. §5 A3：取消 `lan-chat-transport` dev-dependency 例外——手写 TLS 测试客户端用已有的 `tokio-rustls` 即可；需要真实客户端路径时复用 `lan-chat-client-core`。
7. §5 A4：把 input/output 循环抽函数降级为“可选、只在 A3 测试不足以覆盖时做”。
8. §6 B：补“版本一致性自愈”“FTS MATCH 报错回退 instr”“迁移耗时/体积记录”“迁移中断不损坏数据”四项设计与测试。
9. §7 C1：新增 C1.0“先补 characterization 测试”，验收从“测试数量不变”升级为“基线测试集全部通过 + 行数 + 移动/可见性清单”。
10. §8 C2：新增 H4（WAL 决策）、H5（commit 后 rename 补偿）、权限检查对照表、并发写测试、`test-hooks` feature 替代无效的 `#[cfg(test)]` 钩子、用 `grep` 取代表述 `rg` 的结构验收。
11. §9 门禁：改为两级（本机可跑的 `-p` 范围门禁 + CI/干净环境 workspace 全量），并明确 D8050 环境下的记录方式。

## 1. 目标与完成判据

| 编号 | 目标 | 完成判据 |
| --- | --- | --- |
| H1（A） | 应用层心跳 `heartbeat-v1` | 已协商连接静默超时即被关闭并走正常移除；未协商客户端行为逐字不变；两者都有自动化测试 |
| H2（B） | FTS5 历史搜索 | ≥3 个 Unicode 字符走 trigram；<3 字符回退 `instr`；旧库自动迁移且幂等、可自愈；撤回消息搜不到；分页与权限范围不变；迁移耗时/体积有记录 |
| H3（C1） | Hub 纯重构 | `lan/mod.rs` ≤ 300 行；改动前后 characterization 测试集与端到端测试全部通过；无 wire 行为变化 |
| H4（C2） | 附件 IO 移出 Hub 锁 | init/chunk/resume/commit/download/cleanup 不再持 `Server.hub` 锁做文件 IO 或长事务；有测试证明附件阻塞时聊天仍能完成，且附件提交与聊天写入并发不失败 |

## 2. 必读

- `WORKLOG.md`（接手摘要与最新日志）、`docs/CODEX_HANDOFF.md`、`docs/architecture.md`、`docs/decisions/0001-rust-only.md`
- `protocol-rust/src/lib.rs`、`protocol-rust/src/message.rs`
- `server-rust/src/main.rs`（在线 `serve`、`Budget`、`IDLE_TIMEOUT`、参数解析）
- `server-rust/src/store.rs`（`open_mode` 迁移、`persist_chat`、`history`）
- `server-rust/src/lan/` 全部：`mod.rs`、`messages.rs`、`attachments.rs`、`store.rs`、`rooms.rs`、`mls.rs`、`host.rs`
- `server-rust/src/online.rs`（第 287 行附近 `UPDATE chat_messages SET content=...` 是在线编辑）
- `crates/client-core/src/session.rs`（`connection` / 维护循环）、`crates/client-core/src/transfers.rs`（附件响应如何关联）
- `server-rust/tests/lan_desktop.rs`（现有端到端测试写法；基线当前失败，见 §4）
- 合并计划全文（时间线、门禁、决策编号）

## 3. 硬约束（不可违反）

1. **协议帧不变**：4 字节大端长度 + UTF-8 JSON，1 ≤ length ≤ 65536。新行为只能由 capability 协商启用，未声明的客户端逐字保持原行为。
2. **安全不变量**：不得关闭或放宽 TLS 证书/主机名校验；不得新增绕过房主审批的路径；附件权限（房间、作者、过期）不得削弱；服务端不得获得附件明文；私钥、`chat.db`、日志不得进入输出或提交。
3. **数据兼容**：`%LocalAppData%\DUSK1NG\LAN Chat\host\chat.db` 与在线库能被新版本直接打开；表结构变更走 `PRAGMA user_version`，迁移幂等、可回滚；测试只用临时目录。
4. **依赖**：不新增第三方依赖，也不新增 dev-dependency。心跳测试用 `server-rust` 已有的 `tokio-rustls` / `rustls-pemfile` 写最小 TLS 客户端；需要整套客户端行为时用 `lan-chat-client-core`（已是 dev-dependency）。`socket2`（TCP keepalive）先过决策门 H1。
5. **测试先行**：先写会失败的聚焦测试并确认失败原因正确，再写最小实现，再跑全量。跑不通的测试不得注释或 `#[ignore]`，要报告。
6. **Git**：仓库当前在分支 `feat/01-stability-downloads`，HEAD `78552a7`，约 306 项未提交改动（Rust 迁移整体未提交）。开工前由 S0 在**用户批准下**提交基线并建 `stage/h-server-hardening` 分支；在此之前不得 `git reset` / `git clean` / `git stash` / `git checkout -- .` / 切分支 / 提交。
7. **环境**：本机默认 target 目录构建会因 MSVC `cl.exe` 报 D8050 失败（见 §4.1）。允许用 §4.2 的环境变量绕过，但必须在报告中同时贴出“原始失败命令”和“绕过命令+结果”。不得修改 ACL 或系统配置。
8. **范围**：不做本文未列出的重构。额外发现写进 §11「待评估」，不顺手修。
9. **不做破坏性清理**：不得删除既有数据、既有测试、既有文档；不得改动发布脚本行为（C2 若需改打包排除规则，单独列出）。

## 4. 环境前置检查（P0，必须先做）

### 4.1 已知环境事实（2026-11-23 实测）

- 本机 `cargo 1.100.0 / rustc 1.100.0`；`cargo check -p lan-chat-server --tests` 在默认 target 下编译 bundled sqlite 失败：`cl.exe` 报 `命令行 error D8050 :无法执行 ...\MSVC\14.44.35207\bin\HostX64\x64\c1.dll: 未能将命令行放入调试记录中`，随后 `cc-rs: command did not execute successfully (status code exit code: 2)`、`warning: build failed`。
- 可用绕过：`CARGO_TARGET_DIR=C:/lc-target CARGO_PROFILE_DEV_DEBUG=0 cargo check --locked -p lan-chat-server --tests` 通过（11.4s）。
- 基线测试：`lan-chat-protocol` 3 个、`lan-chat-client-core` 2 个通过；`server-rust/tests/lan_desktop.rs` **失败**（panic 于 `server-rust/tests/lan_desktop.rs:76:25`：房主 `attachment.chooseUpload` 后客户端 15s 内收不到附件消息）。该测试是后续所有阶段的门禁，必须在 S0 阶段定位并修绿；修不动就停下报告，不要用忽略测试的方式绕过。

### 4.2 P0 执行步骤

1. `git status --short` 与 `git log -1 --oneline` 记录当前状态；确认分支与脏文件数，写进报告。
2. 默认 target 跑 `cargo --version`、`cargo metadata --format-version 1 --no-deps`；然后跑一次默认 target 的 `cargo test -p lan-chat-server --locked`，把失败原样记录（预期看到 D8050）。
3. 用 §4.1 的绕过环境变量重跑同一命令，记录结果。**若绕过仍失败**：立即停止报告，不要改 ACL、不要反复换目录试探。
4. 列出门禁基线：通过/失败的测试名、数量；红色 e2e 的定位结论（见 S0 任务）。
5. 在 `docs/testing.md` 的“环境”一节补上 D8050 与绕过命令，注明“Windows MSVC 工具链相关，非项目缺陷”。

## 5. H1（原阶段 A）：应用层心跳

### 5.1 已有代码（已验证可编译）

| 文件 | 内容 |
| --- | --- |
| `protocol-rust/src/lib.rs` | `HEARTBEAT = "heartbeat-v1"`、`HEARTBEAT_INTERVAL_SECS = 20`、`HEARTBEAT_TIMEOUT_SECS = 60`、编译期断言 超时 > 2×间隔；单测 `heartbeat_frames_are_minimal_and_pass_lan_command_validation` |
| `server-rust/src/main.rs` | `wants_heartbeat`、`read_timeout`、`pong`；在线登录回显 capability；已协商连接读超时 60s，否则 `IDLE_TIMEOUT`（5 分钟）；收 `ping` 回 `pong` |
| `server-rust/src/lan/mod.rs` | 登录回显 capability；读超时区分心跳/空闲；`ping` 走弱引用 + `try_send(pong)` 后 `continue` |
| `crates/client-core/src/session.rs` | 登录声明 capability、只认服务端回显；20s 发 `ping`；读超时 60s 映射为“服务器心跳超时”；`pong` 不交上层 |

### 5.2 任务

- **A1 测试与门禁（1 人日）**：只修测试、clippy 与文档；代码若在绕过环境下仍编译不过，才动实现。
- **A2 测试专用超时参数（0.5 人日）**：新增隐藏参数 `--heartbeat-timeout-secs`，取值 **2..=60**（默认 `HEARTBEAT_TIMEOUT_SECS`），同时作用于局域网与在线 `serve` 的 `read_timeout`；>60 一律拒绝（客户端读超时硬编码 60s，调大只会制造“客户端先断”）。帮助文本注明“生产使用默认 60，仅自动化测试缩短”。
- **A3 心跳集成测试（1.5 人日，核心）**：新增 `server-rust/tests/heartbeat.rs`：
  - 用 `tokio-rustls` + `rustls-pemfile` 手写最小客户端（`--auto-cert` 生成证书，CA = 生成的 `server.crt`，SNI `localhost`），从 stderr 的 `listening on <addr>` 解析端口（服务器打印的是 `local_addr()`，支持 `127.0.0.1:0`）。
  - 用例 1：已协商连接连续发 40 个 `ping` 不被限流、连接不被关。
  - 用例 2：已协商但静默 → 在 `timeout + 容差` 内被服务器断开，且从用户列表消失。
  - 用例 3：未声明 `heartbeat-v1` 的客户端静默 4 秒（超时设 2 秒）仍保持连接。
  - 用例 4：断开后同一用户代码可立刻重新登录（验证清理路径）。
  - 用例 5（可选）：`--online` 模式同断言；跑不了就写“未验证”。
  - 不得新增 dev-dependency；`pong` 是 `try_send`，队列满时丢 `pong` 是有意行为，可加注释说明。
- **A4（可选，≤1 人日）**：把输入/输出循环抽成可测泛型函数（`tokio::io::duplex` + `start_paused`）。只在 A3 无法覆盖关键分支时做，做了必须保证既有测试不变绿→绿。
- **A5 文档（0.5 人日）**：`protocol.md` 写 `heartbeat-v1`（帧字段、20s/60s 关系、未协商行为、`ping` 不交上层、不计入限流）；`architecture.md` 与 `docs/testing.md` 同步；旧客户端行为明确为“逐字不变”。
- **A6 门禁**：§9 两级门禁；`lan_desktop` 必须通过。

### 5.3 设计要点（不要改坏）

- `pong` 必须走弱引用 `Sender::downgrade`：同账号接管旧连接时，旧连接已注销，强引用会让旧连接不退出。
- `ping` 分支只能在已协商连接特判；未协商的连接仍按原逻辑返回 `Unsupported message type`。
- 不得把 `if heartbeat && kind=="ping"` 改成 let-chain：原写法在 upgrade 失败时仍执行 `continue`，行为不同。

## 6. H2（原阶段 B）：FTS5 历史搜索

### 6.1 现状（已核实）

- `Store::history`（`server-rust/src/store.rs:330`）是搜索与分页唯一入口：`AND (?4='' OR instr(content,?4)>0)`，因此中文子串只能全表扫描；且没有 `recalled=0` 过滤，**已撤回消息能按原文搜出**。
- 索引 `idx_chat_messages_search_scope(kind,conversation_key,content,id)` 对 `instr` 子串无效。
- `open_mode`（`server-rust/src/store.rs:123`）已有 `IMMEDIATE` 事务、`busy_timeout(5s)`、schema 与 legacy `message_id` 重建逻辑；全文件目前**没有** `user_version` / FTS / trigram。
- 写入 content 的路径：`persist_chat` 插入；`online.rs` 编辑 `UPDATE chat_messages SET content=...`；`persist_chat` 每会话裁剪到 10000 条；撤回只置 `recalled=1` 不改 `content`。FTS 触发器必须覆盖插入/编辑/删除。
- 能力已确认：`libsqlite3-sys 0.35.0` 打开 `SQLITE_ENABLE_FTS5`，SQLite 3.50.2 → trigram（≥3.34）与 `case_sensitive 1`（≥3.45）可用。决策门 H2 因此关闭。

### 6.2 设计（在 v1 基础上加固）

在 `open_mode` 内、legacy 重建分支之后、同一 `IMMEDIATE` 事务内完成迁移，并加入自愈：

1. **一致性自愈（新增）**：若 `user_version >= 1` 但 `chat_messages_fts` 或任一触发器缺失，视为未迁移：删残留 FTS 对象并把 `user_version` 置 0，再走正常迁移。用于断电/半迁移/legacy 重建等脏状态。
2. **legacy 重建互斥（新增）**：进入 `message_id text not null unique` 重建分支时，先 `DROP TABLE IF EXISTS chat_messages_fts` 与三个触发器，并保证重建后重新迁移（不能留下“版本=1 但 FTS 没了”的状态）。
3. **建表**：`chat_messages_fts` 使用 external content（`content='chat_messages'`、`content_rowid='id'`、`tokenize='trigram case_sensitive 1'`）+ `ai/ad/au` 三个触发器 + `INSERT INTO chat_messages_fts(chat_messages_fts) VALUES('rebuild')`；最后 `PRAGMA user_version=1`。
4. **查询**：保持 `Store::history` 签名、`limit(0..=100, 默认 50)`、`before_message_id` 游标、`ORDER BY id DESC LIMIT limit+1`、返回 `(Vec<Message>, bool)` 与 has_more 语义不变；只替换过滤条件：
   - 空查询：原逻辑不变；
   - ≥3 个 Unicode 字符：`AND recalled=0 AND id IN (SELECT rowid FROM chat_messages_fts WHERE chat_messages_fts MATCH ?)`；
   - 1–2 字符：回退原 `instr` 并同样加 `recalled=0`。
5. **MATCH 安全（新增）**：查询词按 FTS5 短语字面量处理（内部双引号双写）；**执行报错时回退 `instr` 路径**并记录一次告警，不能让 `...`、`--` 这类无 token 的查询变成连接错误。禁止字符串拼接用户输入。
6. **迁移记录（新增）**：迁移前记录 `chat_messages` 行数与 DB 文件大小，迁移后记录耗时与 `user_version`；写入报告。大库（十万级消息）启动迁移可能耗时数秒到数十秒，属预期，需在 `docs/testing.md` 与发布说明中提示。
7. **回滚 SQL**（写进文档）：`DROP TRIGGER IF EXISTS ...` ×3、`DROP TABLE IF EXISTS chat_messages_fts`、`PRAGMA user_version=0`。

### 6.3 任务

- **B0 失败测试（先写，约 1 人日）**：新测试文件建议 `server-rust/tests/search.rs`（复用 `lan_desktop` 的服务器启动方式），或先加 `store.rs` 单测。至少覆盖：
  1) 中文子串“域网聊”命中；2) 2 字符查询走 `instr` 命中；3) 不同房间/会话不串；4) 120 条消息 3 页游标分页正确；5) 撤回（服务端撤回与在线编辑两条写入路径）后搜不到；6) 编辑后新内容可搜、旧内容搜不到；7) 删除后搜不到；8) 旧库（无 FTS）启动自动迁移且第二次启动幂等；9) `lan_reader` 只读连接可搜；10) 含 FTS 语法字符的查询（`"`、`*`、`AND`、`NEAR(`、`...`、`--`）按字面匹配且不报错；11) `user_version=1` 但 FTS 缺失时能自愈；12) 迁移事务中断（手工开启事务执行部分步骤后回滚）后 `user_version=0`、原表完好；13) 迁移前后行数一致。
- **B1 实现（1 人日）**：按 §6.2 实现，含回退与自愈。
- **B2 端到端（0.5 人日）**：扩展 `server-rust/tests/lan_desktop.rs`（或新增同思路用例）：中文消息→搜索→撤回→再搜索为空。
- **B3 文档（0.5 人日）**：`protocol.md`（搜索语义与字符数规则）、`architecture.md`（FTS 表与触发器）、`docs/testing.md`（迁移与备份提示）、`known-issues.md`（若确认并修复“撤回可被搜索”就更新条目）。
- **B4 门禁**：§9 两级门禁 + 迁移记录。

## 7. H3（原阶段 C1）：拆分 Hub（纯重构）

### 7.1 目标布局

| 文件 | 内容 |
| --- | --- |
| `lan/mod.rs` | `mod` 声明、`Options`、`Room`、`Peer`、`Hub`、`Server` 定义、`run()`、`validate_room_name`；≤ 300 行 |
| `lan/connection.rs` | `serve`：TLS 接入、登录鉴权、审批等待、注册、收发循环（含心跳） |
| `lan/presence.rs` | `register` / `remove` / `deliver` / `error` / `users` / `system` / `room_system` / `room_clients` |
| `lan/approval.rs` | `Approval`、`request_approval`、`expire_approval`、`approval_result`、`admins`；从 `admin_action` 抽 `approve_connection` / `deny_connection` |
| `lan/history.rs` | `HistoryJob`、`history_worker` + 从 `messages.rs` 移入 `Hub::history` / `history_result` |
| `lan/messages.rs` | 保留 `command` / `chat` / `receipt` / `recall` / `admin_action`（踢人、禁言） |

（以实际代码为准；动手前先产出“符号 → 现文件:行 → 新文件”的移动清单。）

### 7.2 任务

- **C1.0 characterization 测试（1 人日，必须先做）**：新增 `server-rust/tests/lan_characterization.rs`，用 `lan-chat-client-core` 覆盖：登录（回环免审批）、远程审批、建房/邀请/进房/聊天、历史分页、私信、断线移除、`ping` 心跳。记录基线测试名与数量；测试必须全绿。
- **C1.1 移动（1 人日）**：只移动，不改函数体、不改错误文本、不改锁范围；可见性最小化（必要时 `pub(crate)` / `pub(super)`），把变化列进报告。
- **C1.2 验收**：`lan/mod.rs` ≤ 300 行；`cargo test -p lan-chat-server` 全绿且测试数 ≥ C1.0 基线；`lan_desktop` 与 characterization 都过；报告附移动清单与可见性变化。

## 8. H4（原阶段 C2）：附件 IO 移出 Hub 锁

### 8.1 现状（已核实，动机成立）

`server-rust/src/lan/attachments.rs` 中 init/chunk/resume/commit/download/cleanup **全部**在 `Hub` 锁内执行；其中：

- `chunk`：base64 解码 + SHA-256 + 写临时文件 + `sync_all` + rename + `IMMEDIATE` 事务；
- `resume`：`deferred` 事务内逐块读文件校验；
- `commit`：`IMMEDIATE` 事务内读取并哈希整个附件（上限 5 GiB），长时间占用全局 Hub 锁与 SQLite 写锁（`busy_timeout` 5s）；
- `download`：锁内读块 + 哈希 + base64；
- `cleanup`：逐条事务内 `remove_dir_all`。
- 已知既有隐患：`commit` 中 `fs::rename(partial, published)` 在事务提交前执行。
- 已核实 `lan/store.rs:303` 的 `delete_protected_room` 会删除 `attachment_chunks` / `attachment_uploads` / `attachments` 行，所以 worker 必须把“记录消失”视为正常拒绝。
- 已核实响应统一带 `command_id`（`attachments.rs:38`），客户端按命令关联，worker 顺序执行即可，不需要额外保序机制。

### 8.2 决策门（开始 C2 前必须关闭，或按默认执行并记录）

- **H4 SQLite WAL**：建议在 C2 前于 `Store::open_mode` 启用 `PRAGMA journal_mode=WAL`（局域网与在线库），否则附件短事务与聊天写入并发仍可能撞 `busy_timeout`。启用后需处理：数据目录出现 `-wal` / `-shm`；备份/迁移文档说明；发布包与诊断日志排除规则确认；启动/关闭时 checkpoint。若决定不做，C2 必须实现 busy 重试并附“并发不失败”的测试与说明。
- **H5 rename 补偿**：`commit` 的 `fs::rename` 移到事务提交之后；发布后若后续步骤失败，尽力删除已发布文件并返回错误；加测试覆盖孤儿文件清理。若决定不修，登记到 `known-issues.md` 并在报告里写明。

### 8.3 设计要点

1. 新增 `AttachmentService`：持有自己的 SQLite **读写**连接与 `attachment_root`，不引用 `Hub`；错误文本、重复块/孤儿块判定、配额 SQL、24h 过期、`valid_id` 规则全部逐字保留。
2. 写事务缩短：校验、解码、哈希、文件写入尽量在事务外；事务内只做“复查记录/块数/大小一致 + 必要的 UPDATE/DELETE”，提交后再 rename（H5）。
3. `Server` 增加有界 `mpsc::Sender<AttachmentJob>`（容量约 32），专用 tokio 任务 + `spawn_blocking` 顺序执行；连接收 `attachment.*` 时短暂锁 Hub 取快照 `(用户代码, 当前房间, 房间 generation)`，锁外投递；worker 完成后短暂锁 Hub 重新校验在线/同房/generation 再投递结果，否则返回原有错误文本。
4. **权限不变量对照表（新增，必须产出）**：对每条命令列出“原检查 → worker 复刻检查 → 错误文本”，至少覆盖：`init` 的 room 相等；`chunk/resume/commit` 的 room + author + 未过期；`download` 的 room 限定；配额与 5 GiB 上限。任何一条不得放宽或收紧。
5. 清理任务改为投递普通附件任务；Hub 删除受保护房间时附件记录消失按正常拒绝处理。
6. 中断/重启：投递队列只在内存，服务重启后未处理任务丢弃；客户端会重试或报错，不保证跨重启幂等（写进文档）。
7. 测试钩子：不要用 `#[cfg(test)]`（对集成测试无效）。在 `server-rust/Cargo.toml` 加 `[features] test-hooks = []`，用 `#[cfg(feature = "test-hooks")]` 提供“阻塞 worker”的钩子；集成测试以 `--features test-hooks` 运行。

### 8.4 任务

- **C2.1 AttachmentService 单测（1 人日）**：配额、过期、重复块、哈希不匹配、越权、记录消失。
- **C2.2 阻塞验证（1 人日）**：`--features test-hooks` 下阻塞 worker 的 commit，同时用另一连接完成聊天发送，断言聊天在 1s 内完成且附件随后成功。
- **C2.3 并发写验证（0.5 人日，新增）**：附件提交期间持续聊天写入，断言无 `database is locked` 失败（WAL 或重试方案自证）。
- **C2.4 实现（1.5 人日）**：按 §8.3 改造，含 H5 rename 补偿。
- **C2.5 权限对照测试（0.5 人日）**：每条命令的越权/正常路径至少一个断言。
- **C2.6 结构验收（0.5 人日）**：`grep -rn "fs::" server-rust/src/lan` 应只剩附件服务内的调用；`impl Hub` 不再有 `attachment_*`；附件命令不再经 `Hub::command` 分派；报告给出剩余调用清单。
- **C2.7 文档（0.5 人日）**：`architecture.md`（附件服务与队列）、`protocol.md`（若响应时机有变化）、`docs/testing.md`（`--features test-hooks` 用法）、`known-issues.md`（H5 若登记）。
- **C2.8 门禁**：§9 两级门禁 + 性能对照（同一 100 MiB 附件：改造前后 commit 期间聊天延迟）。

## 9. 门禁（两级，必须都记录）

### 9.1 本机门禁（每阶段必须跑）

```powershell
# 本文所有 cargo 命令在本机需加环境变量绕过 MSVC D8050（见 §4.1）
$env:CARGO_TARGET_DIR = "C:/lc-target"
$env:CARGO_PROFILE_DEV_DEBUG = "0"

cargo test --locked -p lan-chat-server -p lan-chat-protocol -p lan-chat-client-core
cargo clippy --locked -p lan-chat-server -p lan-chat-protocol -p lan-chat-client-core --all-targets -- -D warnings
```

- 若用了 `test-hooks`：测试命令加 `--features test-hooks`。
- 前端有改动时：`pnpm.cmd --dir .\frontend exec vitest run` 与 `pnpm.cmd --dir .\frontend run build`。

### 9.2 全量门禁（CI 或干净环境）

```powershell
cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
```

本机因 `apps/desktop/src-tauri` 与 D8050 可能跑不了；跑不了必须在报告写“未验证：workspace 全量 / 桌面构建”，不得假装全绿。

## 10. 决策门

| 编号 | 触发条件 | 选项与建议 | 状态 |
| --- | --- | --- | --- |
| **G0** | 开工前 | 是否由 S0 提交当前 Rust 迁移为基线、建 `stage/h-server-hardening` 分支；建 `.gitignore` 并处理未跟踪的 `Claude-BugHunter/`（建议移出仓库或忽略）。**建议：先提交基线再动代码** | 待用户拍板 |
| **H1** | 心跳需要 TCP keepalive 时 | 引入 `socket2` 或继续只用应用层心跳。**建议：不做**，应用层心跳已覆盖 | 待定 |
| **H2** | ``FTS5/trigram 是否可用`` | 已实测：SQLite 3.50.2 + FTS5 + trigram + `case_sensitive` 可用 | **已关闭** |
| **H3** | C2 是否依赖附件响应顺序 | 已核实响应带 `command_id`、客户端按命令关联；worker 顺序执行即可。若实测仍有顺序依赖，改为按连接保序 | **已关闭**（如实测例外再开） |
| **H4** | C2 开始前 | 是否启用 SQLite WAL。**建议：启用**，并同步备份/打包/诊断排除规则 | 待用户拍板 |
| **H5** | C2 实现前 | `commit` 的 rename 是否改为提交后补偿。**建议：修**，并登记测试 | 待用户拍板 |

与合并计划的关系：本文件的 G0 → 合并计划 G0；H1–H5 在合并计划中编号为 H1–H5，产品决策为 P1–P7。

## 11. 待评估（不在本次范围，发现后在此追加）

- 删除已失效的 `idx_chat_messages_search_scope`（含整列 content，FTS 上线后只占空间）。
- 搜索改为不区分大小写（FTS 与回退路径要一起改）。
- 在线模式 `Arc<Mutex<Online>>` 中 `dm_file_chunk` / `dm_image_chunk` 同样在全局锁内读写 BLOB，结合 S3 在线存储重构处理。
- 附件 worker 由单线程改为小线程池（按房间或按上传分片）。
- TCP keepalive（见 H1）。
- `lan/mod.rs` 拆分后 `Hub` 字段是否可按职责再分组（不改变行为）。

## 12. 报告格式（每阶段结束、以及遇到决策门时）

1. 阶段/任务编号；2. 改动摘要（为什么）；3. 文件与行数变化；4. 协议/数据变更（capability、消息类型、表/触发器、`user_version`）；5. 门禁命令与结果（本机 + 全量；失败贴关键输出）；6. 新增测试名；7. 环境阻塞（D8050、绕过命令、未验证项）；8. 基线对照（`lan_desktop` 改造前后状态）；9. 迁移成本（B 阶段：行数、耗时、DB 体积）；10. `Cargo.lock` 是否变化及原因；11. 需要拍板的问题。

完成每个 `- [ ]` 后改成 `- [x]`，下一行追加 `> 证据：<命令 + 结果 + 测试名>`。

## 13. 完成标记

- H1：`heartbeat-v1` 文档 + 5 个测试通过 + `lan_desktop` 绿。
- H2：`user_version=1` 迁移 + 13 项搜索测试通过 + 迁移记录。
- H3：`lan/mod.rs` ≤ 300 行 + characterization 全绿 + 移动清单。
- H4：附件服务 + 并发测试 + 权限对照表 + 结构验收 + 性能对照。
