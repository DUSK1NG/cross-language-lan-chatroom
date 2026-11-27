# P0 加固计划：未确认私信 / 预认证槽位 / 登录限流

> 状态：已实施代码与服务端验收测试（2026-11-27）；真实公网压测、跨 `/64` 多前缀网络测试与线上数据库备份恢复演练待运维窗口执行。
> 范围：在线版服务端（`server-rust` 在线模式，本次由数据库 `user_version=5` 迁移至 `6`，另附带 `lan/mod.rs` 的 `accept()` 瞬时错误容错）。构建与测试门禁参照 [测试指南](../testing.md) 与 [WORKLOG](../../WORKLOG.md)（[v1.4 合并计划](2026-11-23-v1.4-plus-hardening-plan.md) 中的 `S0`/`H1–H4`/`S1–S5` 已完成，仅作历史参考）。
> 三项可分开实施，但均涉及 `main.rs` 或共享状态，合并时需重新核对冲突；`main.rs` 中的 `LOGIN_TIMEOUT` 被 `lan/connection.rs` 复用，改造 P0-2 时须保持局域网模式语义与编译兼容（`lan/mod.rs` 同类的预认证限额与局域网离线私信暂不纳入本批，另行评估）。数据库删除不可通过回退程序撤销。

## 1. 当前事实与目标

以下依据当前代码静态检查，攻击速率和实际资源消耗尚未压测。

| 项目 | 当前行为 | 目标 |
| --- | --- | --- |
| P0-1 | 在线私信初始为 `sent`；收到单条回执（`dm_received`）或离线批次确认（`dm_ack_batch`）后变为 `delivered`。离线队列查询排除已撤回消息（`recalled=0`）；`persist_chat` 的 10000 条裁剪排除在线私信 `sent` 行，`commit_file` 则直接写库且不触发该裁剪；撤回只改 `recalled=1` 而不改 `delivery_state`。 | 限制未确认消息增长，堵住撤回与附件写入绕过，并明确对消息历史和附件的影响。 |
| P0-2 | `main.rs` 在 TLS 前占用 `max_clients` 槽位，TLS 与登录帧共用 10 秒超时（`LOGIN_TIMEOUT`）；槽位满时直接关闭连接。 | 限制预认证资源，避免其占用已认证会话槽位，并在协议与客户端允许时提示繁忙。 |
| P0-3 | `online.rs::admit_login` 在全局 `Online` 锁内按完整 IP 对登录和注册共用 20 次/60 秒计数，成功也计数；4096 项满时拒绝新 IP；`Store::authenticate` 在未限制并发的情况下执行 bcrypt。 | 限制撞库与计算资源消耗，减少 IPv6 地址轮换绕过，解耦聊天主锁，并明确共享 IP、账号针对性拒绝服务与内存上限的权衡。 |

`sent` 表示服务端尚未收到送达确认，不能证明接收方从未看到消息；删除该行也会删除服务端历史。文本、图片（`dm_image_chunk`）、文件（`dm_file_commit`）消息都须纳入设计。每目标上限不能限制目标数量，也不能单独保证磁盘不会写满。

## 2. P0-1 未确认私信存储

### 策略

- 对 `kind='private' AND delivery_state='sent' AND recalled=0` 的消息按目标计数；撤回行不占目标待送达名额以防反向消耗对方收件箱。但因 `recall` 只置 `recalled=1` 而不改 `delivery_state`，且 `confirm_batch` 仅确认 `recalled=0`、`persist_chat` 裁剪排除 `delivery_state='sent'`，若不处理已撤回 `sent` 行，攻击者可通过「发送→撤回」循环绕过配额并滞留磁盘附件/图片（尽管 `read_image`/`read_file` 已拒绝读取 `recalled=1`）。因此必须由 §6 明确已撤回 `sent` 行的 TTL 清理、10000 条裁剪纳入条件或撤回时附件清理策略。
- 推荐达到每目标上限时拒绝新消息，不挤掉旧消息；到达 TTL 的未确认消息定期清理。上限、TTL 的默认值和是否允许关闭由 §6 决定。拒绝和到期都会改变用户体验，不能称为对用户不可见。
- 在同一数据库事务内检查配额并写入，覆盖普通私信（`private_chat`）、图片发布（`dm_image_chunk`）和文件提交（`dm_file_commit`）的实际入口；须先判定 `message_id` / `chat_id` 幂等重发，再检查配额，避免收件箱满后误拒客户端断线重连时的正常重发。
- **上传入口与失败清理约束**：`dm_image_chunk` 须在 `index == 0` 提前校验目标账号存在性与待送达配额，并在最后一片发布失败或幂等命中时立即删除本次新建的暂存/已 `finalize` 图片文件；`dm_file_init` 同样应尽早校验配额，`dm_file_commit` 须在 `files::finalize` 重命名前完成配额校验。**特别注意**：当前桌面端与 Android 端均未实现 `dm_file_cancel` 或对同一 `attachment_id` 的独立 `commit` 重试，且 `begin_file` 限制单账号 24 小时内最多 4 个未提交上传；若 `commit` 被拒后长期保留 `chat_id IS NULL` 行，发送方被拒 4 次即无法向任何人传文件并占用 2 GiB 全局配额，因此需在 §6 拍板「拒绝即清理暂存」或「补齐客户端取消/重试」。
- 为待处理行添加按目标与时间查询所需的部分索引，纳入 `online/files.rs::migrate_schema`（从 `user_version=5` 升级至 `6`）幂等创建，避免污染局域网库；用实际数据与 `EXPLAIN QUERY PLAN` 验证索引命中，并核查回退旧程序后的读库兼容性。
- TTL 删除采用有限批次；当前在线库未启用 WAL 且 `cleanup_files` 持全局 `Mutex<Online>` 锁，而认证与 FCM 推送使用独立 SQLite 连接。大量删除会触发 `chat_messages_fts_delete` 并锁表，须控制单批大小且避免长期持有 `Mutex<Online>` 阻塞全服聊天；维护任务先分批清理消息（及关联未引用附件行），再获取在传图片快照并清理磁盘孤儿文件，分别测试数据库行与 `<db>.files` 磁盘文件。
- `created_at` 使用 RFC3339 `AutoSi`，秒精度（`...Z`）与小数秒（`...123Z`）混用时因 ASCII `'.' < 'Z'` 并非总能按字典序排序；直接对列套用 `unixepoch(created_at)` 会导致普通 `(created_at)` 部分索引失效，而空值或异常格式在纯字符串 `<` 比较下可能被误删。实施时须选用兼顾部分索引的方案（如先以整秒边界粗筛命中索引，再精确校验时间并保护空/异常时间戳，或采用规范化时间/表达式索引），并覆盖同秒混合精度、边界秒与异常值测试。
- SQLite 删除通常只释放数据库内部可复用页（`freelist`），并不保证数据库文件缩小；数据库文件（若启用 WAL 则含 `-wal`/`-shm`）、图片和附件目录分别统计。上线前备份数据库及 `<db>.files` 目录、统计候选数量和大小，明确维护窗口与恢复步骤；不把回退二进制作为数据恢复方案。

待确认范围：仅每目标条数加 TTL 不能形成全局容量上界；需设计覆盖消息正文、历史 BLOB、磁盘图片、附件和数据库空间的总体预算，或明确依赖磁盘监控与运维容量限制。独立图片配额和发送者配额须先确定产品策略与计量方式，不能用未实现的计数表宣称已有保证。

### 验收

1. 达到上限后，下一条新文本、图片或文件消息明确失败，原消息保留；重复提交已成功消息保持幂等，不触发错误成功提示、不锁死发送方上传额度、不残留孤儿图片或暂存文件。
2. TTL 边界、混合精度时间与空/异常时间戳处理正确，`EXPLAIN QUERY PLAN` 命中预期索引；`delivered`、群消息、LAN 数据不被该规则误删，撤回消息及其附件行为符合已选策略。
3. 文件和图片关联清理后无误删或孤儿；分批清理期间新消息、认证、推送与回执行为正确，不长期阻塞主锁，失败可观察。
4. 在临时数据库验证迁移（`user_version` 5 → 6）、回滚旧程序后的读取以及备份恢复；记录删除前后 `freelist` 可复用空间、数据库文件大小与 `<db>.files` 占用，避免将两者混同。

## 3. P0-2 预认证资源

- 使用有界的全局预认证配额与每来源并发配额，将 `LOGIN_TIMEOUT` 拆分为 TLS 握手与登录帧读取的独立超时；注意 `lan/connection.rs` 复用了 `crate::LOGIN_TIMEOUT`，改造时保持局域网模式语义与编译兼容。每 IP 上限对 NAT 用户有影响，具体阈值先压测后确定。
- 在异步侧先获取有界的认证计算并发名额（带短时有界等待）再进入 `spawn_blocking`，避免排队任务打满阻塞线程池。读到合法登录帧后，若已认证会话槽位（`max_clients`）已满，可在执行 `admit_login` 与 bcrypt 前即通过 `login_error` 快速拒绝（避免满载时浪费 bcrypt CPU 或消耗用户失败计数，仍受预认证与来源频控约束）；否则在认证成功后、**发送 `login_ok` 与注册会话之前**以非阻塞方式（`try_acquire_owned`）获取会话槽位。超时、断线、写失败、注册失败和任务取消均通过 RAII guard 释放全部资源；预认证计数的共享与释放须跨任务正确同步，不能让 accept 循环内的普通 `HashMap` 由任务直接修改。
- 预认证名额或每来源限制耗尽时，尚未完成 TLS，直接关闭连接；不能发送明文或伪称可返回 `login_error`。TLS 完成且读到合法登录帧后，若会话已满，可发送既有 `login_error`，写入受 `WRITE_TIMEOUT` 与预认证资源约束。**客户端兼容约束**：桌面端 `client-core/src/session.rs` 将 `login_error` 视作非瞬时错误并中止自动重连（会退出重连态并退回登录页）；Android 端手动登录展示 `login_error` 内容，而保存凭据自动重连时会展示「连接中断，正在重连…」并按退避重试。核验桌面及 Android 实际显示，并确认是否单列客户端跟进。
- 协议为裸 TLS over TCP（非 HTTP），默认不信任任何转发头；若部署在 L4 TCP 代理之后需明确代理导致全员共享来源 IP 对限额的影响。

### 验收

1. 多来源占满预认证配额时，已登录会话仍可通信；超时或断开后名额释放，新连接可再次登录。
2. 单来源触及并发上限时按预期关闭；测试多个共享 IP 的正常用户和 IPv4/IPv6。
3. 完成 TLS 和登录帧后，已认证会话满额返回可被现有客户端识别的繁忙错误且不浪费 bcrypt 计算；TLS 前超限直接断开。
4. 分别验证慢握手、慢登录帧、认证队列等待超时、认证任务失败与取消时资源释放；确认客户端首次登录与重连表现符合预期，正常登录、消息及附件路径与局域网模式回归通过。

## 4. P0-3 登录与注册限流

- 登录按规范化来源和规范化账号分别计量；注册（`account_register`）使用独立来源额度，且同样纳入 bcrypt 并发限流（因注册在冲突检查前即执行 `bcrypt::hash_with_result`）。IPv6 默认建议按 `/64` 聚合，IPv4-mapped IPv6 应归一为 IPv4；确认地址来源及实际部署拓扑。`/64` 只限制同一前缀内轮换，不能阻止跨前缀或分布式攻击。
- 在写入账号限流表或消耗 bcrypt 名额前，先复用（或抽取共享）与 `Store::authenticate` 一致的 `username` 长度（1–32 UTF-8 字节）/ `password` 长度校验与用户名小写规范化逻辑；格式校验错误只计入来源尝试额度，不触发账号失败计数。来源登录表、来源注册表与账号失败表须拆分为各自独立的有界表，防止伪造随机用户名打满来源限流表。
- 区分认证尝试速率限制与认证失败计数：成功可清除账号连续失败计数，但仍计入来源尝试额度；只把凭据错误（`Rejected("用户名或密码不正确")`）计作失败，数据库故障、bcrypt 内部错误与任务取消须作为服务错误处理。并限制单账号并发在途认证或用代次校验解决并发请求对失败计数的乱序覆盖。
- 账号维度推荐采用**基于时间戳的非阻塞冷却拒绝策略（记录下次允许时间立即拒绝，不休眠占用预认证槽位和阻塞线程）**；任何情况下不得在持有状态锁或已认证会话槽位时休眠，且认证等待总时长不能撞上桌面/Android 15 秒登录超时。**错误信息约束**：限流、冷却与过载拒绝统一返回限流/繁忙提示（如既有「登录尝试过于频繁，请稍后再试」），**绝不能复用「用户名或密码不正确」**，否则会触发 Android `ChatConnection` 的 `AuthenticationException` 并清空合法用户本地已保存凭据。
- 限流表使用有界内存和过期清理；淘汰活跃项会让攻击者刷键绕过限制，单纯 LRU 不能保证安全。容量耗尽时采用明确的有界准入策略，并测试合法新来源和攻击者刷键的权衡；不得宣称既永不拒绝合法用户又无法绕过。
- 将限流器从 `Mutex<Online>` 拆出为独立轻量的 `Mutex`（仅做 O(1) 内存读写，绝不持有该锁执行 bcrypt、DB 或磁盘 IO），避免 P0-1 的 `cleanup_files` 与聊天命令阻塞登录准入，也保持 bcrypt 不持锁的边界。
- 不记录密码、消息内容或完整认证请求；限流记录避免输出账号及完整 IP，聚合计数足以观察拒绝和容量情况。

### 验收

1. 覆盖来源与账号阈值、格式校验隔离、成功后清除失败计数、注册与登录隔离，以及凭据错误和存储/任务故障的区别。
2. 覆盖同一 IPv6 `/64`、不同前缀、IPv4-mapped 地址与共享 IP 的行为。
3. 覆盖独立限流表满与刷键、并发失败及成功竞态、认证任务取消，以及登录/注册共同触发 bcrypt 过载时的有界行为；验证被限流时不会导致 Android 清空已保存凭据。
4. 测量正常登录延迟、并发登录和已有聊天响应；核实账号冷却拒绝不会占用预认证槽位或阻塞状态锁。

## 5. 实施与发布

按三个改动分别审查、测试和合并；共享入口及资源管理在集成时复核。测试以服务端相关单测和端到端测试为主，涉及协议显示或重连/上传边界时补充桌面及 Android 验证。压测仅在隔离环境和临时数据上进行，记录来源数量、连接状态、耗时、数据库（含若启用 WAL 时的 `-wal`/`-shm`）和 `<db>.files` 占用；IPv6 多地址测试需真实可用的测试地址，不能把同一地址模拟成网络验证。

发布前完成备份及恢复演练、候选删除量统计、迁移和兼容性验证；发布说明应说明未确认消息保留及拒绝策略、繁忙提示的适用阶段和限流行为。测试、压测或恢复演练未完成时分别标注，不视为上线验收。

## 6. 已确认决策与实施记录（2026-11-27）

1. **P0-1 默认阈值与满额行为**：默认 `--undelivered-per-target 200`、`--undelivered-ttl-days 14`（两者均支持设为 `0` 关闭），满额时拒绝新消息而不挤掉旧消息；已在同一 `IMMEDIATE` 数据库事务内先判定 `message_id` / `chat_id` 幂等重发再校验配额。
2. **存量与撤回 `sent` 清理**：`persist_chat` / `prune_conversation` 万条裁剪纳入 `recalled=1` 行；后台维护 `cleanup_maintenance` 在独立 SQLite 连接上按 100 条分批清理 `kind='private' AND delivery_state='sent'` 超期行（含 `recalled=1`），使用部分索引 `idx_chat_messages_pending_created` 与 RFC3339 精确校验保护混合精度与异常时间戳，随后批删失效 `online_files` / `online_file_chunks`，并仅短时读内存拍摄在传图片快照后清理 `.files/` 孤儿文件（120 秒最近修改宽限期）。
3. **`dm_file_commit` 拒绝清理**：配额被拒时在提交事务内立即删除 `online_files` / `online_file_chunks` 暂存记录并删除 `.partial` 文件；`dm_image_chunk` 在 `index == 0` 提前校验证书账号与配额并在校验/发布失败或幂等命中时清理临时或重复图片。全局磁盘预算与群附件配额归入 P1。
4. **P0-2 / P0-3 默认配置与客户端兼容**：全局预认证 `--max-preauth 128`、每来源 `--max-preauth-per-ip 8`、单账号并发会话 `--max-sessions-per-account 8`、TLS `--tls-handshake-timeout-secs 5`、登录帧 `--login-frame-timeout-secs 8`、bcrypt 并发 `--max-auth-concurrency 4`（等待 `--auth-queue-wait-secs 3`）、IPv6 `--ipv6-prefix 64`、登录来源 `--login-source-limit 20` / `60s`、注册来源 `--register-source-limit 5` / `300s`、账号连续失败 `--account-fail-threshold 5` 起按 `--account-cooldown-secs 30` 指数退避至 `--account-cooldown-max-secs 900`。仅改服务端，接受桌面端重连收到 `login_error` 繁忙提示后停止重连的现有行为；限流/繁忙提示均不复用 `用户名或密码不正确`，保护 Android 已保存凭据。
5. **顺手纳入的加固修复**：`listener.accept()` 区分瞬时错误不再退出进程（在线与局域网模式复用 `accept_client`）；在线账号注册先查重再计算 bcrypt；登录未知用户名执行一次伪校验消除计时差异。
6. **未变更项**：仍允许向非好友发送私信，未更改产品语义。
7. **未完成项（不视为上线验收）**：线上真实环境多 `/64` 前缀公网测试、高并发压力基准测试、生产库 `user_version=5 → 6` 升级前备份与回滚恢复演练仍需在发布维护窗口完成。

## 7. 补齐记录（2026-11-27 18:30，第二轮）

第一轮实施后逐条复核剩余缺口，本轮补齐如下（仍只改服务端与客户端登录字段，未改产品语义）。

### 7.1 存储与配额

1. **已送达私信也有上界**：新增 `--delivered-ttl-days`（默认 30，0 关闭），`cleanup_database_batches` 在同一 `IMMEDIATE` 事务里按 `(created_at, id)` 游标分批（100 条/批）删除 `kind='private' AND delivery_state='delivered'` 且早于截止时间、且 `recalled=0` 的行；RFC3339 解析失败只计 skipped，不误删。删除行后执行 `PRAGMA incremental_vacuum(4096)`。
2. **收件箱按字节计量**：新增 `--undelivered-bytes-per-target`（默认 64 MiB），`check_undelivered_quota` 同时校验条数与 `SUM(length(content)+COALESCE(online_image_size,0))`；文本、图片、文件三条路径共用同一 `Quota`，群消息用 `Quota{0,0}` 绕开。
3. **图片全局预算**：新增 `--image-budget-bytes`（默认 2 GiB），`finish_image` 在 `inspect_image` 之后、`finalize` 之前统计 `SUM(COALESCE(online_image_size,0))`，超限即删暂存并返回中性提示。
4. **中性拒绝文案**：`UNDELIVERED_FULL` 等改为“对方暂不可接收新的私信”“服务器图片存储空间已满”，不再向发送方暴露“对方收件箱已满”。
5. **删除后能缩文件**：新库开启 `PRAGMA auto_vacuum=INCREMENTAL`（仅 `user_version==0` 且文件为空时设置，已有库不改），维护周期执行增量回收；老库仍需停机 `VACUUM`，文件不会自动变小。
6. **WAL 与写放大**：`open_mode` 设置 `journal_mode=WAL` 与 `synchronous=NORMAL`（失败只告警），降低每次命令的写放大；代价是数据目录多出 `-wal`/`-shm`，备份需连同两者一起做，不能只拷贝 `.db`。

### 7.2 索引与迁移

7. **索引自愈**：新增 `ensure_cleanup_index`，按 `sqlite_master` 检查 `idx_chat_messages_delivered_created`（`created_at,id`，部分索引 `WHERE kind='private' AND delivery_state='delivered'`），缺失时：过滤后行数 ≤ 200 万才当场建，否则只打印提示要求手动建，避免大库启动时长时间持锁。清理 SQL 不再硬绑 `INDEXED BY`，索引缺失也能跑通（全表扫，慢但正确）。
8. **user_version 仍归在线库**：本轮的列与索引都通过 `pragma_table_info` / `sqlite_master` 探测后 `ALTER` / `CREATE IF NOT EXISTS`，没有新增版本号；`user_version` 仍只由在线迁移推进，局域网库不参与。

### 7.3 认证与限流

9. **预认证失败封禁**：新增 `--preauth-fail-limit`（8）/ `--preauth-fail-window-secs`（60）/ `--preauth-ban-secs`（120）。TLS 握手失败或超时、登录帧读取失败或超时都会 `record_preauth_failure`；`try_acquire_preauth` 先查来源封禁再拿全局许可，封禁到期自动清空。表容量 4096，满时先清理过期项再放弃记账（不驱逐活跃封禁）。
10. **冷却期先验密码**：`begin_account_login` 在冷却期内不再直接拒绝，而是继续走 bcrypt；密码正确则放行并清零失败计数，错误才返回限流错误。失败后返回 `Duration`（上限 `MAX_LOGIN_PENALTY` 2 秒）用于兼容旧调用方的睡眠语义，但正式路径不再阻塞线程。
11. **成功登录退还来源额度**：`SourceWindowTable` 增加 `refund`，认证成功且会话注册成功后把该来源窗口的计数减一，避免 NAT 出口下“同事登录成功反而挤掉自己”。
12. **锁中毒兜底**：`auth.rs` 与 `main.rs` 中所有 `Mutex` 的 `.expect` 改为 `unwrap_or_else(PoisonError::into_inner)`，包括 `PreAuthPermit::drop` 与 `accept` 循环路径；单个 panic 不再让服务永久拒绝新连接。

### 7.4 局域网 P0-4：设备绑定

13. **协议字段**：`protocol-rust/src/message.rs` 的 `Message` 新增 `device_secret: String`（空值不序列化，旧客户端/旧服务端互通）。
14. **绑定与校验**：`accounts` 新增 `device_secret_hash TEXT NOT NULL DEFAULT ''`（bcrypt cost 10）。房主批准后首次登录时若为空则写入绑定；后续登录 `verify_device_binding` 校验失败返回 `This account is bound to another device; ask the room owner to reset it`。空 `device_secret` 的旧客户端仍可登录，但 `initial` 不投递 `take_lan_private` 的离线私信——这正是 P0-4 的入口：按可猜 `target_code` 取走别人的离线私信。
15. **重置路径**：房主“踢出”该成员时 `reset_lan_device` 清空哈希，换设备后重新申请即可；客户端（`crates/client-core/src/lib.rs`）首次启动生成 32 字符随机密钥写入用户数据目录 `device-secret`，登录帧携带。
16. **已知取舍**：设备密钥只存在本机、不加密；拿到该文件的人可冒充该设备（等价于拿到本机用户数据目录）。房主审批仍是唯一准入关口，设备绑定只是把“猜代码取信”变成“需要房主重置”，不替代核验指纹。

### 7.5 仍未解决（不在本轮范围）

- 全局 `Mutex<Online>` 仍把 `history`、附件提交、`fsync` 串行化；WAL 降低单次写成本，但没有做出锁重构。高并发下首条命令的延迟仍受最慢磁盘操作影响。
- 更新器仍无最低版本/防回滚，持有发布渠道的人可让客户端“降级”到旧版。
- MLS 组附件对房主不保密（房主可被加入组），`docs/architecture.md` 已按“不构成对房主保密”表述，未改变实现。
- 附件全局 2 GiB 配额没有按用户份额，单个账号可占用全部额度，需要产品决策后再加。

### 7.6 验证状态

- `cargo check --workspace --tests --locked` 通过；`cargo fmt --all` 无改动。
- `online::auth::tests` 5/5 通过（不触碰 SQLite）。
- 依赖临时数据库的 11 个用例在本机全部因 `拒绝访问 (os error 5)` / `Error code 14: Unable to open the database file` 失败：沙箱或杀软禁止测试子进程在 `%TEMP%` 建库（用 python/touch 做过可写对照），不是代码失败；这些用例须在 CI 的 ubuntu runner 上取得结论。
- 未做：生产库升级演练、压测、真实多 `/64` 网络测试。
