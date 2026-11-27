# P0 加固计划：未投递私信 / 预认证槽位 / 登录限流

> 状态：**待评审**（2026-11-27）· 本文只是方案，代码尚未改动
> 依据：2026-11-27 全仓审计（server-rust、crates/client-core、frontend、android）的 P0 结论
> 执行方式：三个独立 PR，按 P0-1 → P0-2 → P0-3 顺序或并行；每个 PR 自带单测与压测脚本，可单独回滚
> 关联：[工作日志](../../WORKLOG.md) · [架构说明](../architecture.md) · [已知问题](../known-issues.md) · [v1.4 合并计划](2026-11-23-v1.4-plus-hardening-plan.md)

---

## 0. 给 agent 的一句话

在 `server-rust` 内做三件事：**给未投递私信加上限与 TTL**（否则磁盘会被写满）、**把预认证连接与会话槽位拆成两层**（否则约 7 连接/秒就能让服务器对新连接装死）、**把登录限流改成 IP + 账号双维度**（否则一台机器轮换 IPv6 前缀即可无限撞库）。三处都不改协议、不改数据库既有语义，只加索引、加参数、加拒绝路径。

优先级：**P0-1 先做**——它是唯一会真的把磁盘写满、导致全站宕机的一条；P0-2/P0-3 是可用性与凭证安全问题，可紧随其后。

---

## 1. 问题与完成判据

| 编号 | 问题 | 当前后果 | 完成判据 |
| --- | --- | --- | --- |
| P0-1 | 未投递私信没有 TTL、没有每目标上限，唯一的大删除显式排除 `sent` | 单个已注册用户即可持续灌库至磁盘写满；消息与内联图片永不回收 | 超出 TTL 或每目标上限的行被删除；删除后附件由既有 cleanup 回收；有测试证明 201 条时最旧一条被挤掉 |
| P0-2 | 连接槽位在 TLS 握手之前用 `try_acquire_owned` 抢占，满了直接 `continue` | 慢速连接可占满全部槽位，新用户连接被静默丢弃（无任何错误提示） | 未认证连接与本机无关地受限；槽位耗尽时返回 `login_error`；TLS 握手与登录帧各自独立超时 |
| P0-3 | 登录限流只按 IP、成功也计数、4096 条满了拒绝全体、IPv6 不归一 | 轮换 IPv6 /64 即可绕过限流；注册与登录共用桶；表满时对所有人拒绝服务 | IP 与账号双维度；IPv6 归一到 /64；成功不计失败；LRU 淘汰而非整体拒绝；注册独立桶 |

---

## 2. 现状取证（行号为 2026-11-27 master `e1f97f9`）

### 2.1 未投递私信

- `server-rust/src/store.rs:497` —— `backlog()` 只读 `delivery_state='sent'`，即"已投递"的消息；未投递的留在库里等对方上线。
- 全库删除只有三处：
  - `server-rust/src/store.rs:369` —— 每会话保留最新 10000 条，条件是 `kind='online_group' OR delivery_state<>'sent' OR EXISTS(server_mode='lan')`，**显式排除在线模式的已投递私信**（也就是说：连"已读的私信"都永不刪）。
  - `server-rust/src/online/social.rs:575` —— 解散群时删该群消息。
  - `server-rust/src/lan/store.rs:397` —— 删房间时删消息。
- **没有 TTL、没有 GC、没有每目标上限**；维护 ticker（`server-rust/src/main.rs:207-216`，每小时）只跑附件清理与索引维护。

### 2.2 预认证槽位

- `server-rust/src/main.rs:224-234` —— 在 `accept()` 之后立刻 `slots.clone().try_acquire_owned()`，失败 `continue`，连接被直接关闭，客户端看到的是"连接被重置"，没有任何提示。
- `server-rust/src/main.rs:38` `LOGIN_TIMEOUT = 10s`；`main.rs:255-261` 把 TLS 握手与登录帧读取塞进同一个 timeout —— 慢速握手可以合法占用槽位 10 秒。
- `max_clients` 默认 64（`main.rs:90-91`）。

### 2.3 登录限流

- `server-rust/src/online.rs:72-90` —— `admit_login`：只按 IP；`*count += 1` 成功也计数；`count < 20` / 60 秒；`login_attempts.len() < 4096` 满了**拒绝全体**；注册与登录共用同一个表。
- IPv6 地址按完整 128 位做 key → 一个 /64 前缀就能提供 2^64 个不同 key。

---

## 3. P0-1 未投递私信无限驻留

### 3.1 方案 A —— TTL + 每目标上限 + 小时级 GC（推荐）

参数：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--undelivered-ttl-days` | 14 | 未投递消息最长保留天数；`0` 表示不按时间清理 |
| `--undelivered-per-target` | 200 | 每个目标代码最多滞留条数；`0` 表示不限 |

索引（幂等，走现有"列不存在就 ALTER"的迁移模式，见 `store.rs:198-231`）：

~~~sql
CREATE INDEX IF NOT EXISTS idx_chat_messages_pending_target
  ON chat_messages(target_code, id)
  WHERE kind='private' AND delivery_state='sent';

CREATE INDEX IF NOT EXISTS idx_chat_messages_pending_age
  ON chat_messages(created_at)
  WHERE kind='private' AND delivery_state='sent';
~~~

裁剪（在写消息的同一事务内，插入之后执行）：

~~~sql
DELETE FROM chat_messages
 WHERE kind='private' AND delivery_state='sent' AND target_code=?1
   AND id NOT IN (
     SELECT id FROM chat_messages
      WHERE kind='private' AND delivery_state='sent' AND target_code=?1
      ORDER BY id DESC LIMIT ?2);
~~~

GC（挂在每小时维护 ticker 上，与 `cleanup_files()` 相邻）:

~~~sql
DELETE FROM chat_messages
 WHERE kind='private' AND delivery_state='sent' AND created_at < ?1;
~~~

`?1` 由 Rust 计算：`now() - ttl`，仍用 `store.rs:146-148` 的 `to_rfc3339_opts(SecondsFormat::AutoSi, true)` 格式。**不要用 SQLite 的 `datetime()` 去解析带 `T`/`Z` 的字符串**——本项目所有时间戳都是同一种 RFC3339 UTC 格式，字典序即时间序，直接做字符串比较即可。

配套代码骨架：

~~~rust
// store.rs
pub fn purge_undelivered(&self, cutoff_rfc3339: &str) -> rusqlite::Result<usize> {
    self.conn.execute(
        "DELETE FROM chat_messages
          WHERE kind='private' AND delivery_state='sent' AND created_at < ?1",
        [cutoff_rfc3339],
    )
}
~~~

~~~rust
// main.rs 维护 ticker，每小时
if let Some(days) = args.undelivered_ttl_days.filter(|d| *d > 0) {
    let cutoff = store::now_minus_days(days);
    match store.purge_undelivered(&cutoff) {
        Ok(0) => {}
        Ok(n) => eprintln!("undelivered_purged={n} older_than={cutoff}"),
        Err(e) => eprintln!("undelivered_purge_failed={e}"),
    }
}
~~~

**顺带好处**：消息行删除后，`server-rust/src/online/transfer.rs:279-284` 的孤儿清理会在下一轮把对应附件行与磁盘文件一并回收，全球 2 GiB 配额（`transfer.rs:343`）随之释放。

**副作用（可接受）**：发送方只会看到"一直未送达"，到期后静默消失——与现有"每会话只留 1 万条"的静默截断语义一致，不引入新协议字段。

### 3.2 方案 B —— 发送前软拒（建议与 A 叠加）

插入前先数一次：

~~~sql
SELECT COUNT(*) FROM chat_messages
 WHERE kind='private' AND delivery_state='sent' AND target_code=?1;
~~~

≥ 上限时直接回 `Rejected("对方收件箱已满，请稍后再发")`。相比"先写再裁"少一次写放大，且给发送方明确反馈。代价是多一次索引查询（走 `idx_chat_messages_pending_target`，O(1) 量级）。

### 3.3 方案 C —— pending 独立存储 + 固定配额

把未投递消息搬到独立表/独立文件，按目标分配固定配额，超了写失败。**不推荐**：与 `chat_messages` 的撤回、历史、附件外键、`revision` 全部耦合，改造成本远高于收益。

### 3.4 方案 D —— 只存指针，正文由发送方保留并重发

服务器只存摘要，离线投递时向发送方拉取。**不推荐**：离线消息的核心价值就是服务器暂存；而且要改协议并在三端同步，等于砍功能换架构。

### 3.5 推荐组合与容量验算

**A + B**：TTL 14 天 + 每目标 200 条 + 发送前软拒。

- 单条 ≤ 64KB → 每目标 ≤ 12.8MB；内联图片 4MiB 时第 200 条自然封顶。
- 攻击者要持续占盘必须不断换目标代码，而 `private_chat` 目前只校验目标存在（`online.rs:260-269`）——这是 **P1 要堵的下一层**，见 §7。
- 建议同时给 `kind='private'` 的 `online_image` 加一条全局预算（如 512MiB），插入时拒绝超额——实现方式是单独维护一张 `online_image_usage` 计数表（`SUM` 全表扫描太贵，且图片是 BLOB）。

### 3.6 验收测试

1. 同一目标连发 201 条未投递消息 → 第 201 条写入后总数为 200，且被删的是 `id` 最小的一条。
2. cutoff 前 1 秒的消息被 GC 删除、cutoff 后 1 秒的一条不删。
3. GC 后调用 `cleanup_files()`：对应 `online_files` 行与磁盘文件都被回收。
4. 已是 `sent` 的历史私信不受影响（避免误删正常历史）。
5. 参数为 `0` 时行为退回现状（可回滚开关）。

### 3.7 风险与回滚

- 只加索引与删除，不改表结构 → 回滚只需回退二进制，索引留在库里无害。
- 若担心误删，先以 `--undelivered-ttl-days 90` 上线观察一周，再收到 14。

---

## 4. P0-2 预认证槽位耗尽

### 4.1 方案 A —— 两层准入 + 每 IP 并发上限 + 超时拆分（推荐）

核心思路：**"连上来"的配额必须远大于"登录成功"的配额**，并且未认证连接要按来源受限。

~~~rust
const PREAUTH_MAX: usize = 256;              // 4 × max_clients
const PREAUTH_PER_IP: usize = 3;
const HANDSHAKE_TIMEOUT: Duration = Duration::from_secs(4);
const LOGIN_FRAME_TIMEOUT: Duration = Duration::from_secs(4);
~~~

accept 循环改造（单任务，`per_ip` 用普通 `HashMap` 即可，不需要锁）：

~~~rust
let mut per_ip: HashMap<IpAddr, usize> = HashMap::new();
loop {
    let (socket, address) = listener.accept().await?;
    let ip = address.ip();
    if per_ip.get(&ip).copied().unwrap_or(0) >= PREAUTH_PER_IP { continue; }
    let Ok(pre) = preauth.clone().try_acquire_owned() else { continue };
    *per_ip.entry(ip).or_default() += 1;
    // spawn 任务；任务结束时 decrement(ip) 并 drop(pre)
}
~~~

任务内部顺序调整为：

1. `TlsAcceptor::accept(socket)` 套 `HANDSHAKE_TIMEOUT`；
2. 握手成功后 `slots.try_acquire()`；失败则**写一个正常的 `login_error` 帧**再关（复用 `main.rs:292-301` 的编码路径），文案"服务器繁忙，请稍后重试"；
3. 读登录帧套 `LOGIN_FRAME_TIMEOUT`；
4. 登录结束（成功或失败）后释放 preauth guard。

效果：已认证会话仍然 ≤ `max_clients`；未认证连接的总量与每 IP 并发都被限住；合法用户在高峰期拿到的是可读的错误，而不是"连接被重置"。

### 4.2 方案 B —— 只调参数（治标）

`LOGIN_TIMEOUT` 10s → 3s，`--max-clients` 调大。只能降低触发概率：攻击者的成本仍低于服务器扩容成本，且槽位满了照样静默丢弃。**不建议单独使用**。

### 4.3 方案 C —— 前置 nginx / haproxy 限连接

运维层最省代码，但项目定位是"单二进制可自部署"，要求用户装反代违背定位。可作为文档里的**可选加固**（给出 `limit_conn` 示例），不作为默认路径。

### 4.4 方案 D —— 预认证失败黑名单（建议与 A 叠加）

按 IP 前缀记录预认证失败（TLS 错、超时、非法帧）：60 秒内 ≥ 20 次 → 封 10 分钟；LRU 65536 条，淘汰最旧。直接掐掉"不停重连"这一类，且比方案 A 更早生效（A 只能限并发，D 能限频率）。

### 4.5 方案 E —— 连接挑战（SYN-cookie 式）

TLS 之前无法安全地发应用层挑战，收益不抵复杂度。**不推荐**。

### 4.6 验收测试

1. 打开 64 个裸 TCP 连接但从不握手 → 5 秒后新连接仍能拿到 `login_error`（而不是被 RST）。
2. 同一 IP 的第 4 个并发连接被立即关闭。
3. 慢速握手（每秒 1 字节）在 4 秒时被断开，槽位释放。
4. 正常连接不受影响：登录、发消息、附件上传全部通过。

---

## 5. P0-3 登录限流

### 5.1 方案 A —— IP/账号双维度 + /64 归一 + 指数退避 + LRU（推荐）

新模块 `server-rust/src/online/limits.rs`：

~~~rust
pub struct LoginLimiter {
    attempts:  Lru<Key, TokenBucket>,   // 30 次/60s，含成功（防 disconnect-churn）
    failures:  Lru<Key, TokenBucket>,   // 10 次/60s，仅失败
    accounts:  Lru<String, Backoff>,    // 连续失败 5 次后逐次翻倍，上限 15 分钟
    registers: Lru<Key, TokenBucket>,   // 3 次/小时
}

fn key(ip: IpAddr) -> IpAddr {
    match ip {
        IpAddr::V4(v4) => IpAddr::V4(v4),
        IpAddr::V6(v6) => {
            let mut s = v6.segments();
            s[4..].fill(0);
            IpAddr::V6(Ipv6Addr::from(s))
        }
    }
}
~~~

规则：

1. **IP 维度硬拒**：`attempts` 或 `failures` 桶空 → 回统一文案"登录尝试过于频繁，请稍后再试"（不区分哪个维度空，避免信息泄露）。
2. **账号维度只延迟、不硬锁**：连续失败 5 次后，下一次校验前 `sleep(min(2^n × 5s, 15min))`。**这条很关键**——若用账号硬锁，攻击者拿受害者用户名故意输错就能把人锁死（DoS 反噬）。
3. **成功清零**：`settle(ok=true)` 时清空该账号的失败计数；`attempts` 仍计数（防"登录成功→断开→重连"绕过）。
4. **注册独立桶**：3 次/小时/IP 前缀，避免注册接口成为绕过登录限流的旁路。
5. **LRU 而非硬上限**：65536 条，满了淘汰过期再淘汰最旧，**永不"满了拒绝全体"**。

调用点改造：现在是"先 `admit_login` 再 `Store::authenticate`"，拿不到成败。改成三段式：

~~~rust
let gate = { let mut state = state.lock(); state.admit(ip, &request.kind, &request.username)? };

let result = tokio::task::spawn_blocking({
    let database = database.clone();
    let request = request.clone();
    move || Store::authenticate(&database, request)
}).await?;

state.lock().settle(gate, result.is_ok());
~~~

**注意**：`authenticate` 必须在全局 `state` 锁**之外**执行（当前实现是在锁内跑 bcrypt，见 `main.rs:401-414` 的同类问题），否则登录风暴会把所有聊天命令串行阻塞。

### 5.2 方案 B —— 最小改动（治标）

三件事：成功不计数、4096 改成 LRU、注册单独桶。半小时能上线，但 IPv6 轮换仍可绕过、账号维度仍缺失。

### 5.3 方案 C —— fail2ban / 云 WAF

读日志封 IP。可行但脆弱：日志格式一变就失效，IPv6 前缀同样要自己处理，且单二进制自部署场景下用户未必有 fail2ban。

### 5.4 方案 D —— 登录前 PoW 挑战

客户端算 hashcash 再提交，可根治"换 IP 就重来"。但要改协议并同步三个客户端，**留作 1.5 的可选项**。

### 5.5 附带：bcrypt 并发闸

bcrypt cost 10 是 CPU 密集操作，并发登录会打满所有核。加一个 `Semaphore::new(num_cpus::get().max(1))` 限制并发哈希，超出的排队——这同时把"登录风暴"从 CPU 维度关住。

### 5.6 验收测试（`tokio::time::pause` 注入时钟）

1. 同一 IP 20 次失败后第 21 次被拒。
2. 成功登录一次后，该账号失败计数清零。
3. `2001:db8:1:2::1` 与 `2001:db8:1:2::ffff` 落在同一个桶。
4. 4097 个不同 IP 时，最早的一条被淘汰，而不是全体被拒。
5. 账号连续失败后响应**变慢**但返回文案始终相同（不出现"账号已锁定"）。
6. 注册桶与登录桶互不影响。

---

## 6. 共同落地方式

### 6.1 PR 拆分

| PR | 内容 | 改动量 | 依赖 |
| --- | --- | --- | --- |
| #1 | P0-1：两条索引 + `purge_undelivered` + ticker 挂钩 + 两个 CLI 参数 + 测试 | ~120 行 | 无 |
| #2 | P0-2：accept 两层准入 + 超时拆分 + 忙碌错误帧 + 失败黑名单 + 测试 | ~90 行 | 无 |
| #3 | P0-3：`limits.rs` + 三段式登录改造 + bcrypt 并发闸 + 测试 | ~150 行 | 建议在 #2 之后（复用 IP 归一函数） |

三个 PR 互不触碰同一函数，可并行开发、独立回滚。

### 6.2 迁移

- 只建索引，不改列 → 走现有 `pragma_table_info` 检查模式，幂等。
- 上线首轮 GC 可能删掉大量历史 pending 行，**先打日志再删**：第一版可以只统计不删（`--undelivered-ttl-days 0` + 日志输出"将删除 N 条"），观察一周再开。

### 6.3 观测

每次拒绝/清理都打一行结构化日志，这是磁盘写满前唯一的信号：

~~~text
undelivered_purged=1234 older_than=2026-11-13T00:00:00Z
preauth_rejected_total=... preauth_rejected_per_ip=...
login_throttled ip=... account=... reason=...
~~~

建议同时加 `/healthz`（或服务端启动日志），输出：数据库文件大小、pending 行数、当前连接数、附件占用。没有这个，运维只能等磁盘报警。

### 6.4 压测脚本

放 `tools/`（Rust 小 bin 或现成的 tokio 测试），三个场景各跑一次基线，修完再跑对比：

1. 64 个连接不握手，持续 60 秒，统计新连接成功率。
2. 同一 /64 前缀轮换 1000 个地址做登录，统计被限流比例。
3. 单账号向一个目标灌 10 万条 1KB 私信，统计数据库增长与 GC 后回收量。

### 6.5 发布说明

三处都是"加固"，对正常用户不可见，但要在 release notes 里写明：

- 未投递私信现在有保留期（默认 14 天）与每目标上限（默认 200 条）。
- 服务器繁忙时登录会看到明确提示，而不是连接被重置。
- 登录失败过快会被限流（IP 与账号双维度）。

---

## 7. 仍需 P1 配合（否则 P0-1 只堵了一半）

- **私信不要求好友关系**：`server-rust/src/online.rs:253-293` 只校验目标账号存在。攻击者仍可换着目标代码灌库。建议同时加：
  - 每账号每日发送上限（如 5000 条）；
  - 非好友私信更严的上限（如 20 条/天），或直接要求先加好友。
- **附件配额没有按人份额**：`transfer.rs:343` 是全局 2 GiB。建议加 `owner` 维度 256 MiB。
- **全局 Mutex 串行**（`main.rs:401-414`）：history、附件提交、fsync 全在锁内，一个慢查询卡住所有人。这是 P1，但与 P0-3 的 `settle` 改造可以一起做（把 bcrypt 移出锁内顺手解决一部分）。

---

## 8. 待拍板

1. TTL 默认值：**14 天**还是 7 天/30 天？（影响"离线久的人回来还能不能收到"）
2. 每目标上限：**200** 条是否够用？（企业内网用户可能一天就发满）
3. IPv6 归一：**/64**（多数运营商按客户分配 /64）还是 /56（部分云厂商）？
4. 账号维度：接受"只延迟不硬锁"，还是要求硬锁 + 手动解锁接口？
5. 是否本轮就加"私信需好友"？这会改变产品语义，需要产品确认。
6. 是否接受首轮 GC 只统计不删（更安全，但要多等一周）。

---

## 9. 参考

- `server-rust/src/store.rs`（`backlog`、`prune`、迁移模式、`now()`）
- `server-rust/src/main.rs`（accept 循环、`LOGIN_TIMEOUT`、`Budget`、维护 ticker）
- `server-rust/src/online.rs`（`admit_login`、`private_chat`）
- `server-rust/src/online/transfer.rs`（全局配额、孤儿清理）
- `docs/architecture.md`（数据流与存储布局）
