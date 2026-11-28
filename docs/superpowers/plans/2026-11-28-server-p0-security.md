# LAN Chat Server P0 Security Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复服务端 P0 高危问题，消除 LAN 准入拒绝服务、Hub panic、设备绑定竞态、文件 IO 全局锁和群消息无界存储。

**Architecture:** 在现有裸 TLS + 长度前缀 JSON 协议上做内部硬化：LAN 使用独立预认证许可和批准后正式槽；Hub 统一锁恢复并避免访问已移除会话；设备绑定使用锁外 bcrypt 与短事务条件更新；Online 文件命令改为授权/IO/发布三阶段；存储通过增量 schema、事务字节账本和物理磁盘高水位控制。

**Tech Stack:** Rust 2024、Tokio、rustls、rusqlite/SQLite、bcrypt、ring、clap、fs2。

**Spec:** `docs/superpowers/specs/2026-11-28-lan-chat-security-remediation-design.md`

## Global Constraints

- 发布版本保持 `1.4.1`，不升级版本号。
- 现有消息类型、字段含义和命令分发保持不变；本计划不新增线协议字段。
- 数据库迁移只增不删，必须幂等、可重入，不在启动时对大库执行全表重建。
- LAN 全局预认证默认 `128`，每来源默认 `8`，TLS 超时默认 `5s`，登录帧超时默认 `8s`。
- 全局数据库逻辑预算默认 `8 GiB`，每发送者群消息预算默认 `1 GiB`，最低剩余磁盘默认 `2 GiB`。
- 群历史默认仍为每会话 `10000` 条，不新增默认 TTL。
- 限流、预算不足和并发冲突必须返回中性错误，不泄露账号、收件箱、路径或密钥信息。
- 不在 Hub/Online 全局锁内执行 bcrypt、文件哈希、`fsync` 或文件读写。
- 所有新增测试使用临时目录，不读取生产数据库或真实凭据。

---

### Task 1: LAN Hub 锁恢复与移除成员安全

**Files:**
- Modify: `server-rust/src/lan/mod.rs`
- Modify: `server-rust/src/lan/rooms.rs`
- Modify: `server-rust/src/lan/presence.rs`
- Modify: `server-rust/src/lan/messages.rs`
- Modify: `server-rust/src/lan/connection.rs`
- Modify: `server-rust/src/lan/history.rs`
- Modify: `server-rust/src/lan/attachments.rs`
- Test: `server-rust/src/lan/rooms.rs`（`#[cfg(test)] mod tests`）

**Interfaces:**
- Produces: `Server::lock_hub(&self) -> std::sync::MutexGuard<'_, Hub>`
- Produces: `Hub::rooms(&mut self, Uuid)` 在客户端不存在时安全返回。
- Consumes: 现有 `Hub::deliver`、`Hub::remove`、`Hub::room_action`。

- [ ] **Step 1: 添加锁中毒恢复测试**

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    #[test]
    fn poisoned_hub_lock_recovers() {
        let root = std::env::temp_dir().join(format!("lan-hub-lock-{}", Uuid::new_v4()));
        std::fs::create_dir_all(&root).unwrap();
        let hub = test_hub(&root).unwrap();
        let server = Arc::new(Server {
            hub: Mutex::new(hub),
            admin_code: String::new(),
            host_token: None,
        });

        let poisoned = std::panic::catch_unwind(|| {
            let _guard = server.hub.lock().unwrap();
            panic!("intentional test poison");
        });
        assert!(poisoned.is_err());
        assert!(server.lock_hub().clients.is_empty());
        let _ = std::fs::remove_dir_all(root);
    }
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server lan::rooms::tests::poisoned_hub_lock_recovers`
Expected: FAIL，`Server::lock_hub` 和 `test_hub` 尚不存在。

- [ ] **Step 3: 实现锁助手和测试夹具**

```rust
impl Server {
    pub(crate) fn lock_hub(&self) -> std::sync::MutexGuard<'_, Hub> {
        self.hub.lock().unwrap_or_else(|poisoned| {
            eprintln!("LAN hub mutex poisoned; recovering after panic");
            poisoned.into_inner()
        })
    }
}

#[cfg(test)]
pub(crate) fn test_hub(root: &Path) -> Result<Hub> {
    let store = Store::open_lan(&root.join("chat.db"))?;
    let (history, _jobs) = mpsc::channel(64);
    let (attachments, _attachment_jobs) = mpsc::channel(64);
    let mut rooms = store.lan_rooms()?;
    rooms.insert(LOBBY.into(), Room {
        owner: String::new(),
        private: false,
        allowed: HashMap::new(),
        admins: HashSet::new(),
        password_hash: None,
        generation: Uuid::new_v4(),
    });
    Ok(Hub {
        store,
        attachments,
        clients: HashMap::new(),
        codes: HashMap::new(),
        rooms,
        pending: HashMap::new(),
        last_message: 0,
        history,
    })
}

#[cfg(test)]
pub(crate) fn test_peer(
    username: &str,
    display_code: &str,
    code: &str,
    room: &str,
    send: mpsc::Sender<Message>,
    close: oneshot::Sender<()>,
    admin: bool,
) -> Peer {
    Peer {
        identity: Identity { username: username.into(), display_code: display_code.into(), code: code.into() },
        room: room.into(),
        admin,
        room_roles: true,
        muted: false,
        attachment_session: Uuid::new_v4(),
        send,
        close,
        capabilities: HashSet::new(),
    }
}
```

- [ ] **Step 4: 添加队列满移除成员测试**

```rust
#[test]
fn queue_full_remove_member_does_not_panic_or_poison_hub() {
    let root = std::env::temp_dir().join(format!("lan-hub-remove-{}", Uuid::new_v4()));
    std::fs::create_dir_all(&root).unwrap();
    let mut hub = test_hub(&root).unwrap();
    let owner_id = Uuid::new_v4();
    let target_id = Uuid::new_v4();
    let (target_tx, _target_rx) = mpsc::channel(256);
    for _ in 0..256 {
        target_tx.try_send(Message::default()).unwrap();
    }
    let (owner_tx, _owner_rx) = mpsc::channel(256);
    let (close_tx, _close_rx) = oneshot::channel();
    let (target_close_tx, _target_close_rx) = oneshot::channel();
    hub.clients.insert(owner_id, test_peer("Owner", "OWNER1", "owner", owner_tx, close_tx, true));
    hub.clients.insert(target_id, test_peer("Target", "TARGET1", "target", target_tx, target_close_tx, false));
    hub.codes.insert("owner".into(), owner_id);
    hub.codes.insert("target".into(), target_id);
    hub.rooms.insert("room".into(), Room {
        owner: "owner".into(),
        private: false,
        allowed: HashMap::from([("owner".into(), true), ("target".into(), true)]),
        admins: HashSet::new(),
        password_hash: None,
        generation: Uuid::new_v4(),
    });
    hub.clients.get_mut(&target_id).unwrap().room = "room".into();

    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        hub.room_action(owner_id, &Message {
            kind: "room_action".into(),
            room: "room".into(),
            target_user_code: "TARGET1".into(),
            content: "remove_member".into(),
            ..Message::default()
        })
    }));
    assert!(result.is_ok());
    assert!(!hub.clients.contains_key(&target_id));
    assert!(hub.clients.contains_key(&owner_id));
    let _ = std::fs::remove_dir_all(root);
}
```

`test_peer` 按 `Peer` 当前字段构造，`attachment_session` 使用新 UUID，`muted=false`、`room_roles=false`。

- [ ] **Step 5: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server lan::rooms::tests::queue_full_remove_member_does_not_panic_or_poison_hub`
Expected: FAIL，`rooms(target)` 访问已移除客户端并 panic。

- [ ] **Step 6: 实现安全刷新**

```rust
pub(super) fn rooms(&mut self, id: Uuid) {
    let Some(peer) = self.clients.get(&id) else { return; };
    // 现有目录构造逻辑保持不变
}

fn evict_room_member(&mut self, id: Uuid) {
    let Some(peer) = self.clients.get_mut(&id) else { return; };
    peer.room = LOBBY.into();
    peer.attachment_session = Uuid::new_v4();
    if peer.room_roles {
        self.deliver(id, Message {
            kind: "room_join".into(),
            room: LOBBY.into(),
            ..Message::default()
        });
    }
}
```

在 `room_action` 中把 `self.rooms(target)` 改为：

```rust
if self.clients.contains_key(&target) {
    self.rooms(target);
}
```

在 `messages.rs` 的 `mute` 分支中，把 `self.clients[&id].room_roles` 改为 `self.clients.get(&id).is_some_and(|peer| peer.room_roles)`。将所有 LAN `.hub.lock().expect("LAN hub poisoned")` 替换为 `server.lock_hub()` 或 `state.lock_hub()`。为后续目录能力协商，在 `Peer` 增加 `capabilities: HashSet<String>`，现有注册路径先插入空集合，不改变旧行为。

- [ ] **Step 7: 运行测试确认通过**

Run: `cargo test --locked -p lan-chat-server lan::rooms::tests`
Expected: PASS。

- [ ] **Step 8: 提交**

```bash
git add server-rust/src/lan
git commit -m "fix(lan): prevent hub poisoning during member removal"
```

---

### Task 2: 原子设备绑定，消除并发审批绕过

**Files:**
- Modify: `server-rust/src/lan/store.rs`
- Modify: `server-rust/src/lan/connection.rs`
- Test: `server-rust/src/lan/store.rs`
- Test: `server-rust/tests/lan_p0.rs`

**Interfaces:**
- Produces: `Store::ensure_lan_account(&Identity) -> Result<()>`
- Produces: `Store::lan_device_hash(&Identity) -> Result<String>`
- Produces: `Store::bind_lan_device_if_empty(&Identity, &str) -> Result<bool>`，参数为已计算的 bcrypt 哈希。
- Produces: `bind_approved_device(&Server, &Identity, &str) -> Result<bool>`。

- [ ] **Step 1: 添加并发绑定单元测试**

```rust
#[test]
fn concurrent_empty_hash_binding_allows_one_device() {
    let root = std::env::temp_dir().join(format!("lan-bind-race-{}", Uuid::new_v4()));
    std::fs::create_dir_all(&root).unwrap();
    let store = Store::open_lan(&root.join("chat.db")).unwrap();
    let identity = Identity {
        username: "Alice".into(),
        display_code: "ALICE1".into(),
        code: "alice".into(),
    };
    store.ensure_lan_account(&identity).unwrap();
    let db_path = root.join("chat.db");
    let barrier = Arc::new(std::sync::Barrier::new(2));
    let handles = ["device-a", "device-b"].map(|secret| {
        let db_path = db_path.clone();
        let identity = identity.clone();
        let barrier = barrier.clone();
        std::thread::spawn(move || {
            let store = Store::open_lan(&db_path).unwrap();
            let hash = password_hash(secret).unwrap();
            barrier.wait();
            store.bind_lan_device_if_empty(&identity, &hash).unwrap()
        })
    });
    let results = handles.map(|handle| handle.join().unwrap());
    assert_eq!(results.iter().filter(|bound| **bound).count(), 1);
    let _ = std::fs::remove_dir_all(root);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server lan::store::tests::concurrent_empty_hash_binding_allows_one_device`
Expected: FAIL，新接口尚不存在。

- [ ] **Step 3: 拆分 Store 绑定接口**

```rust
pub(super) fn ensure_lan_account(&self, identity: &Identity) -> Result<()> {
    self.0.execute(
        "INSERT INTO accounts (username,normalized_username,user_code,normalized_code,password_hash,created_at)
         VALUES (?,?,?,?,'',?) ON CONFLICT DO NOTHING",
        params![identity.username, normalize_username(&identity.username), identity.display_code, identity.code, now()],
    )?;
    Ok(())
}

pub(super) fn lan_device_hash(&self, identity: &Identity) -> Result<String> {
    self.0.query_row(
        "SELECT device_secret_hash FROM accounts WHERE normalized_code=?",
        [&identity.code],
        |row| row.get(0),
    )
}

pub(super) fn bind_lan_device_if_empty(&self, identity: &Identity, hash: &str) -> Result<bool> {
    let changed = self.0.execute(
        "UPDATE accounts SET device_secret_hash=? WHERE normalized_code=? AND device_secret_hash=''",
        params![hash, identity.code],
    )?;
    Ok(changed == 1)
}
```

- [ ] **Step 4: 实现锁外 bcrypt 的绑定循环**

```rust
async fn bind_approved_device(
    server: &Arc<Server>,
    identity: &Identity,
    device_secret: &str,
) -> Result<bool> {
    for _ in 0..4 {
        let current = {
            let hub = server.lock_hub();
            hub.store.ensure_lan_account(identity)?;
            hub.store.lan_device_hash(identity)?
        };
        if current.is_empty() {
            if device_secret.is_empty() {
                return Ok(false);
            }
            let secret = device_secret.to_owned();
            let hash = tokio::task::spawn_blocking(move || password_hash(&secret)).await??;
            let bound = {
                let hub = server.lock_hub();
                hub.store.bind_lan_device_if_empty(identity, &hash)?
            };
            if bound {
                return Ok(true);
            }
            continue;
        }
        if device_secret.is_empty() {
            anyhow::bail!("This account is bound to another device; ask the room owner to reset it");
        }
        let current_for_verify = current.clone();
        let secret = device_secret.to_owned();
        let matches = tokio::task::spawn_blocking(move || {
            bcrypt::verify(&secret, &current_for_verify).unwrap_or(false)
        }).await?;
        ensure!(matches, "This account is bound to another device; ask the room owner to reset it");
        let unchanged = {
            let hub = server.lock_hub();
            hub.store.lan_device_hash(identity)? == current
        };
        if unchanged {
            return Ok(true);
        }
    }
    anyhow::bail!("Device binding changed concurrently; retry the connection")
}
```

将 `connection.rs` 中审批后的 `approve_identity` 调用替换为 `bind_approved_device`。旧客户端空密钥在未绑定账号上仍返回 `false`，不取离线私信；已绑定账号必须提供匹配密钥。

- [ ] **Step 5: 添加双审批集成测试**

在 `server-rust/tests/lan_p0.rs` 添加 `concurrent_approvals_bind_only_one_device`：使用现有桌面客户端夹具或裸 TLS 客户端，对同一未绑定身份建立两个连接，两个均等待审批；依次批准后断言一个 `login_ok`、另一个 `login_error`，离线私信只被成功设备取走。

- [ ] **Step 6: 运行绑定测试**

Run: `cargo test --locked -p lan-chat-server --features lan-chat-server/test-hooks --test lan_p0 concurrent_approvals_bind_only_one_device`
Expected: PASS。

- [ ] **Step 7: 提交**

```bash
git add server-rust/src/lan server-rust/tests/lan_p0.rs
git commit -m "fix(lan): make device binding atomic across approvals"
```

---

### Task 3: LAN 预认证与正式会话槽分离

**Files:**
- Create: `server-rust/src/admission.rs`
- Modify: `server-rust/src/main.rs`
- Modify: `server-rust/src/lan/mod.rs`
- Modify: `server-rust/src/lan/connection.rs`
- Modify: `server-rust/src/online/auth.rs`
- Test: `server-rust/src/admission.rs`
- Test: `server-rust/tests/lan_p0.rs`

**Interfaces:**
- Produces: `admission::normalize_source(IpAddr, u8) -> IpAddr`
- Produces: `AdmissionLimits { global: usize, per_source: usize, ipv6_prefix: u8 }`
- Produces: `AdmissionControl::try_acquire(IpAddr) -> Option<AdmissionPermit>`
- Produces: `AdmissionPermit`，Drop 时释放全局和来源计数。
- Consumes: 现有在线 `normalize_source` 逻辑，迁移后删除重复实现。

- [ ] **Step 1: 添加准入单元测试**

```rust
#[test]
fn permits_are_bounded_and_released() {
    let control = AdmissionControl::new(AdmissionLimits {
        global: 2,
        per_source: 1,
        ipv6_prefix: 64,
    });
    let ip = "198.51.100.10".parse().unwrap();
    let first = control.try_acquire(ip).unwrap();
    assert!(control.try_acquire(ip).is_none());
    let other = control.try_acquire("198.51.100.11".parse().unwrap()).unwrap();
    assert!(control.try_acquire("198.51.100.12".parse().unwrap()).is_none());
    drop(first);
    assert!(control.try_acquire(ip).is_some());
    drop(other);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server admission::tests::permits_are_bounded_and_released`
Expected: FAIL，模块尚不存在。

- [ ] **Step 3: 实现准入控制**

```rust
pub(crate) struct AdmissionPermit {
    _global: OwnedSemaphorePermit,
    source: IpAddr,
    counts: Arc<Mutex<HashMap<IpAddr, usize>>>,
}

impl Drop for AdmissionPermit {
    fn drop(&mut self) {
        let mut counts = self.counts.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(count) = counts.get_mut(&self.source) {
            *count -= 1;
            if *count == 0 { counts.remove(&self.source); }
        }
    }
}

pub(crate) struct AdmissionControl {
    global: Arc<Semaphore>,
    per_source: usize,
    ipv6_prefix: u8,
    counts: Arc<Mutex<HashMap<IpAddr, usize>>>,
}

impl AdmissionControl {
    pub(crate) fn try_acquire(&self, ip: IpAddr) -> Option<AdmissionPermit> {
        let source = normalize_source(ip, self.ipv6_prefix);
        let global = self.global.clone().try_acquire_owned().ok()?;
        let mut counts = self.counts.lock().unwrap_or_else(|e| e.into_inner());
        let count = counts.entry(source).or_insert(0);
        if *count >= self.per_source { return None; }
        *count += 1;
        drop(counts);
        Some(AdmissionPermit { _global: global, source, counts: self.counts.clone() })
    }
}
```

清理 `counts` 中过期为零的条目；每来源上限检查失败时 `global` 许可随函数返回自动释放。

- [ ] **Step 4: 迁移 Online 来源归一化**

将 `online/auth.rs` 的 `normalize_source` 改为 `use crate::admission::normalize_source;`，保留现有在线预认证逻辑和测试。

- [ ] **Step 5: 修改 LAN 接受与连接流程**

1. `Server` 新增 `admission: Arc<AdmissionControl>` 和 `sessions: Arc<Semaphore>`。
2. `lan::run` 不再在 accept 循环获取 `max_clients`，改为获取 `AdmissionPermit`；无许可时直接关闭 socket。
3. `connection::serve` 新增 `preauth: AdmissionPermit` 参数，TLS 与登录帧分别使用 5 秒/8 秒超时。
4. 房主批准且设备绑定成功后，使用 `sessions.clone().try_acquire_owned()` 获取正式槽；满时发送现有中性 `login_error`。
5. 正式槽获取成功后显式 `drop(preauth)`；函数任何提前返回都由 RAII 释放。

- [ ] **Step 6: 添加未认证占槽集成测试**

在 `lan_p0.rs` 添加 `lan_preauth_exhaustion_preserves_existing_session_and_approval`：启动 `--lan-max-preauth 2 --lan-max-preauth-per-ip 2 --max-clients 4` 的 LAN 服务，建立两个不发送登录帧的 TCP 连接，第三个连接应立即断开；已有已批准客户端仍能发送 `users_request` 并收到响应；关闭两个预认证连接后新认证可恢复。

- [ ] **Step 7: 运行准入测试**

Run: `cargo test --locked -p lan-chat-server --features lan-chat-server/test-hooks --test lan_p0 lan_preauth_exhaustion_preserves_existing_session_and_approval`
Expected: PASS。

- [ ] **Step 8: 提交**

```bash
git add server-rust/src/admission.rs server-rust/src/main.rs server-rust/src/lan server-rust/src/online/auth.rs server-rust/tests/lan_p0.rs
git commit -m "fix(lan): separate preauth permits from approved sessions"
```

---

### Task 4: 存储字节账本与数据库预算 Schema

**Files:**
- Modify: `server-rust/Cargo.toml`
- Modify: `server-rust/src/main.rs`
- Modify: `server-rust/src/online.rs`
- Modify: `server-rust/src/store.rs`
- Create: `server-rust/src/online/storage.rs`
- Test: `server-rust/src/online/storage.rs`

**Interfaces:**
- Produces: `OnlineLimits { storage_budget_bytes: u64, group_sender_budget_bytes: u64, min_disk_free_bytes: u64, .. }`
- Produces: `OnlineMessage.file_size: i64`，文本/图片构造为 0，文件提交构造为实际大小。
- Produces: `storage::check_budget(&Connection, &str, &str, i64, &OnlineLimits) -> Result<()>`
- Produces: `storage::calibrate(&Connection, i64) -> Result<CalibrationStats>`
- Consumes: `fs2::available_space`。

- [ ] **Step 1: 添加账本测试**

```rust
#[test]
fn group_budget_rejects_global_and_sender_overflow_without_pruning() -> Result<()> {
    let root = temp_root("storage-budget")?;
    let online = setup_accounts(&root, OnlineLimits {
        storage_budget_bytes: 1024,
        group_sender_budget_bytes: 512,
        min_disk_free_bytes: 0,
        ..OnlineLimits::default()
    })?;
    let db = &online.store.0;
    assert!(storage::check_budget(db, "alice", "online_group", 600, &online.limits).is_err());
    insert_raw_group_message(db, "alice", &"a".repeat(400))?;
    assert!(storage::check_budget(db, "alice", "online_group", 200, &online.limits).is_err());
    insert_raw_group_message(db, "alice", &"b".repeat(400))?;
    assert!(storage::check_budget(db, "alice", "online_group", 300, &online.limits).is_err());
    let count: i64 = db.query_row("SELECT COUNT(*) FROM chat_messages", [], |r| r.get(0))?;
    assert_eq!(count, 2);
    Ok(())
}

fn insert_raw_group_message(db: &Connection, sender: &str, content: &str) -> Result<()> {
    db.execute(
        "INSERT INTO chat_messages(message_id,kind,conversation_key,sender_username,sender_code,content,created_at)
         VALUES (?1,'online_group','group-fixture','Fixture',?2,?3,'2026-11-28T00:00:00Z')",
        params![Uuid::new_v4().to_string(), sender, content],
    )?;
    Ok(())
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server online::storage::tests::group_budget_rejects_global_and_sender_overflow_without_pruning`
Expected: FAIL，`online::storage` 和预算字段尚不存在。

- [ ] **Step 3: 添加依赖和配置**

在 `server-rust/Cargo.toml` 增加：

```toml
fs2 = "0.4"
```

在 `main.rs` 增加：

```rust
#[arg(long, default_value_t = 8 * 1024 * 1024 * 1024)]
storage_budget_bytes: u64,
#[arg(long, default_value_t = 1024 * 1024 * 1024)]
group_sender_budget_bytes: u64,
#[arg(long, default_value_t = 2 * 1024 * 1024 * 1024)]
min_disk_free_bytes: u64,
```

在 `OnlineLimits` 增加同名字段，并在 `Default` 中设置上述值。

- [ ] **Step 4: 添加增量 Schema**

```sql
ALTER TABLE chat_messages ADD COLUMN online_file_size INTEGER NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS storage_usage (
    scope TEXT NOT NULL,
    sender_code TEXT NOT NULL DEFAULT '',
    used_bytes INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (scope, sender_code)
);

CREATE TRIGGER IF NOT EXISTS chat_messages_storage_insert
AFTER INSERT ON chat_messages BEGIN
    INSERT INTO storage_usage(scope, sender_code, used_bytes)
    VALUES ('global', '', length(NEW.content) + COALESCE(NEW.online_image_size,0) + COALESCE(NEW.online_file_size,0))
    ON CONFLICT(scope, sender_code) DO UPDATE SET used_bytes = used_bytes + excluded.used_bytes;
    INSERT INTO storage_usage(scope, sender_code, used_bytes)
    SELECT 'group_sender', NEW.sender_code, length(NEW.content) + COALESCE(NEW.online_image_size,0) + COALESCE(NEW.online_file_size,0)
    WHERE NEW.kind='online_group'
    ON CONFLICT(scope, sender_code) DO UPDATE SET used_bytes = used_bytes + excluded.used_bytes;
END;

CREATE TRIGGER IF NOT EXISTS chat_messages_storage_delete
AFTER DELETE ON chat_messages BEGIN
    UPDATE storage_usage SET used_bytes = MAX(0, used_bytes - (length(OLD.content) + COALESCE(OLD.online_image_size,0) + COALESCE(OLD.online_file_size,0)))
    WHERE scope='global' AND sender_code='';
    UPDATE storage_usage SET used_bytes = MAX(0, used_bytes - (length(OLD.content) + COALESCE(OLD.online_image_size,0) + COALESCE(OLD.online_file_size,0)))
    WHERE scope='group_sender' AND sender_code=OLD.sender_code AND OLD.kind='online_group';
END;
```

增加 update 触发器处理 `content`、`online_image_size`、`online_file_size` 变化。迁移通过 `PRAGMA table_info` 和 `sqlite_master` 幂等执行。

- [ ] **Step 5: 实现有限批次回填**

```rust
pub(crate) fn calibrate(db: &Connection, batch_size: i64) -> Result<CalibrationStats> {
    let tx = db.transaction_with_behavior(TransactionBehavior::Immediate)?;
    let rows: Vec<(i64, i64)> = {
        let mut stmt = tx.prepare(
            "SELECT m.id, COALESCE(f.size,0) FROM chat_messages m
             LEFT JOIN online_files f ON f.chat_id=m.id
             WHERE m.online_file_size=0 AND f.id IS NOT NULL
             ORDER BY m.id LIMIT ?1"
        )?;
        stmt.query_map([batch_size], |row| Ok((row.get(0)?, row.get(1)?)))?.collect::<rusqlite::Result<_>>()?
    };
    for (id, size) in rows {
        tx.execute("UPDATE chat_messages SET online_file_size=? WHERE id=?", params![size, id])?;
    }
    tx.commit()?;
    Ok(CalibrationStats { scanned: rows.len() })
}
```

账本重建使用独立维护任务，每次最多 1000 行，不在启动路径同步执行。

- [ ] **Step 6: 实现预算检查**

```rust
pub(crate) fn check_budget(
    db: &Connection,
    sender: &str,
    kind: &str,
    incoming_bytes: i64,
    limits: &OnlineLimits,
) -> Result<()> {
    let global: i64 = db.query_row(
        "SELECT used_bytes FROM storage_usage WHERE scope='global' AND sender_code=''",
        [], |row| row.get(0)
    ).optional()?.unwrap_or(0);
    ensure!(global.saturating_add(incoming_bytes) <= limits.storage_budget_bytes as i64,
        Rejected("服务器消息存储空间已满"));
    if kind == "online_group" {
        let sender_used: i64 = db.query_row(
            "SELECT used_bytes FROM storage_usage WHERE scope='group_sender' AND sender_code=?",
            [sender], |row| row.get(0)
        ).optional()?.unwrap_or(0);
        ensure!(sender_used.saturating_add(incoming_bytes) <= limits.group_sender_budget_bytes as i64,
            Rejected("群消息存储空间已满"));
    }
    if limits.min_disk_free_bytes > 0 {
        let db_path = {
            let mut stmt = db.prepare("PRAGMA database_list")?;
            let mut rows = stmt.query([])?;
            let mut found = None;
            while let Some(row) = rows.next()? {
                let candidate: String = row.get(2)?;
                if !candidate.is_empty() {
                    found = Some(candidate);
                    break;
                }
            }
            found.context("在线数据库没有文件路径")?
        };
        ensure!(fs2::available_space(db_path)? >= limits.min_disk_free_bytes,
            Rejected("服务器磁盘空间不足"));
    }
    Ok(())
}
```

- [ ] **Step 7: 运行账本测试**

Run: `cargo test --locked -p lan-chat-server online::storage::tests`
Expected: PASS。

- [ ] **Step 8: 提交**

```bash
git add server-rust/Cargo.toml server-rust/src/main.rs server-rust/src/online.rs server-rust/src/store.rs server-rust/src/online/storage.rs
git commit -m "feat(server): add transactional storage budgets"
```

---

### Task 5: 有界文件 IO 池与摘要缓存

**Files:**
- Create: `server-rust/src/online/io.rs`
- Modify: `server-rust/src/online.rs`
- Modify: `server-rust/src/main.rs`
- Test: `server-rust/src/online/io.rs`

**Interfaces:**
- Produces: `BlockingPool::new(usize)`
- Produces: `BlockingPool::run<F, R>(&self, F) -> Result<R>`，容量满时返回中性 `Rejected`。
- Produces: `DigestCacheKey { id: String, size: u64, modified_ns: u64 }`
- Produces: `FileIoService { pool: BlockingPool, locks: Mutex<HashMap<String, Arc<tokio::sync::Mutex<()>>>>, digests: Mutex<HashMap<DigestCacheKey, String>> }`
- Produces: `FileIoService::read_segment(...)`、`FileIoService::finalize(...)`。

- [ ] **Step 1: 添加缓存与容量测试**

```rust
#[tokio::test]
async fn first_chunk_hash_is_cached_and_file_io_does_not_block_commands() -> Result<()> {
    let root = temp_root("io-cache")?;
    let path = root.join("file.bin");
    std::fs::write(&path, vec![7u8; 1024])?;
    let service = FileIoService::new(2);
    let first = service.read_segment(&path, 1024, 0, &checksum(&path)?).await?;
    let second = service.read_segment(&path, 1024, 0, &checksum(&path)?).await?;
    assert_eq!(first, second);
    assert_eq!(service.digests.lock().unwrap().len(), 1);
    Ok(())
}

#[tokio::test]
async fn blocking_pool_rejects_over_capacity() -> Result<()> {
    let pool = BlockingPool::new(1);
    let (entered_tx, entered_rx) = oneshot::channel();
    let (release_tx, release_rx) = oneshot::channel();
    let release_rx = Arc::new(std::sync::Mutex::new(Some(release_rx)));
    let first_pool = pool.clone();
    let first = tokio::spawn(async move {
        first_pool.run(move || {
            entered_tx.send(()).unwrap();
            release_rx.lock().unwrap().take().unwrap().blocking_recv().unwrap();
            1
        }).await
    });
    entered_rx.await?;
    assert!(pool.run(|| 2).is_err());
    release_tx.send(())?;
    assert_eq!(first.await??, 1);
    Ok(())
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server online::io::tests`
Expected: FAIL，模块尚不存在。

- [ ] **Step 3: 实现 IO 服务**

```rust
pub(crate) struct BlockingPool {
    slots: Arc<Semaphore>,
}

impl BlockingPool {
    pub(crate) async fn run<F, R>(&self, work: F) -> Result<R>
    where F: FnOnce() -> R + Send + 'static, R: Send + 'static {
        let permit = self.slots.clone().try_acquire_owned()
            .map_err(|_| Rejected("服务器文件处理繁忙"))?;
        tokio::task::spawn_blocking(move || {
            let _permit = permit;
            work()
        }).await.map_err(|error| anyhow::anyhow!("文件处理任务失败: {error}"))
    }
}
```

`FileIoService::read_segment` 先获取每文件锁，在阻塞池内检查大小/修改时间；缓存未命中时调用 `files::checksum`，命中且元数据一致时直接读取分块。`finalize` 同样在池内执行 rename/checksum，并把结果写入缓存。

- [ ] **Step 4: 运行 IO 测试**

Run: `cargo test --locked -p lan-chat-server online::io::tests`
Expected: PASS。

- [ ] **Step 5: 提交**

```bash
git add server-rust/src/online/io.rs server-rust/src/online.rs server-rust/src/main.rs
git commit -m "feat(server): isolate file io and cache verified digests"
```

---

### Task 6: 文件命令改为授权/IO/发布三阶段

**Files:**
- Modify: `server-rust/src/online.rs`
- Modify: `server-rust/src/online/transfer.rs`
- Modify: `server-rust/src/main.rs`
- Test: `server-rust/src/online/transfer.rs`
- Test: `server-rust/tests/online_hardening.rs`

**Interfaces:**
- Produces: `Online::prepare_file_read(&mut self, Uuid, &Identity, &Message) -> Result<PreparedFileRead>`
- Produces: `Online::finish_file_read(&mut self, Uuid, &Message, FileReadOutcome) -> Result<()>`
- Produces: `Online::prepare_file_write(...)`、`Online::finish_file_write(...)`。
- Consumes: Task 5 的 `FileIoService`。

- [ ] **Step 1: 添加全局锁不阻塞测试**

```rust
#[tokio::test]
async fn repeated_first_chunk_download_does_not_serialize_all_commands() -> Result<()> {
    let root = temp_root("file-lock")?;
    let mut online = setup_accounts(&root, OnlineLimits::default())?;
    let (sender_id, _) = connect_peer(&mut online, "alice")?;
    let (receiver_id, _) = connect_peer(&mut online, "bob")?;
    let file_id = seed_file_for_download(&mut online, &root, "alice", "bob", 32 * 1024 * 1024)?;
    let (entered_tx, entered_rx) = oneshot::channel();
    let (resume_tx, resume_rx) = oneshot::channel();
    online.io.set_test_pause(entered_tx, resume_rx);
    let online = Arc::new(Mutex::new(online));
    let request = Message {
        kind: "dm_file_get".into(),
        attachment_id: file_id,
        chunk_index: Some(0),
        ..Message::default()
    };
    let read_state = online.clone();
    let read = tokio::spawn(async move {
        let prepared = read_state.lock().unwrap().prepare_file_read(sender_id, &test_identity("alice"), &request)?;
        let outcome = read_state.lock().unwrap().io.read_prepared(prepared).await?;
        read_state.lock().unwrap().finish_file_read(sender_id, &request, outcome)
    });
    entered_rx.await?;
    let users_result = {
        let mut state = online.lock().unwrap();
        state.command(receiver_id, &Message { kind: "users_request".into(), ..Message::default() })
    };
    assert!(users_result.is_ok());
    resume_tx.send(())?;
    read.await??;
    Ok(())
}

fn test_identity(code: &str) -> Identity {
    Identity { username: code.into(), display_code: code.to_uppercase(), code: code.into() }
}

fn seed_file_for_download(
    online: &mut Online,
    root: &Path,
    owner: &str,
    recipient: &str,
    size: i64,
) -> Result<String> {
    let id = Uuid::new_v4().to_string();
    let files = super::files::root(&online.store.0)?;
    std::fs::create_dir_all(&files)?;
    let path = super::files::file_path(&files, &id)?;
    std::fs::write(&path, vec![0u8; size as usize])?;
    let digest = super::files::checksum(&path, size)?;
    let chat_id: i64 = online.store.0.query_row(
        "INSERT INTO chat_messages(message_id,kind,conversation_key,sender_username,sender_code,target_code,content,created_at,online_file_size)
         VALUES (?1,'private','fixture','Fixture',?2,?3,'[文件] fixture','2026-11-28T00:00:00Z',?4) RETURNING id",
        params![id, owner, recipient, size], |row| row.get(0))?;
    online.store.0.execute(
        "INSERT INTO online_files(id,owner,recipient,name,size,received,expires,chat_id,disk_backed,disk_sha256)
         VALUES (?1,?2,?3,'fixture.bin',?4,?4,?5,?6,1,?7)",
        params![id, owner, recipient, size, Utc::now().timestamp() + 3600, chat_id, digest],
    )?;
    Ok(id)
}
```

在 `online/io.rs` 的 `#[cfg(test)]` 路径中通过 `test-hooks` feature 暴露 `set_test_pause(oneshot::Sender<()>, oneshot::Receiver<()>)`，测试通过 oneshot 控制，不使用固定长 sleep。测试模块导入 `chrono::Utc`；Task 5 将 `files::checksum` 可见性改为 `pub(super)` 供夹具复用。

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server --features lan-chat-server/test-hooks repeated_first_chunk_download_does_not_serialize_all_commands`
Expected: FAIL，当前 `dm_file_get` 在全局锁内等待文件读取。

- [ ] **Step 3: 实现读取三阶段**

```rust
pub(crate) struct PreparedFileRead {
    attachment_id: String,
    path: PathBuf,
    size: i64,
    offset: i64,
    expected: String,
    generation: (u64, u64),
}

pub(crate) struct FileReadOutcome {
    bytes: Vec<u8>,
    generation: (u64, u64),
}
```

`prepare_file_read` 在全局锁内验证会话、接收人/群成员、附件归属、`recalled=0`、分块索引和数据库元数据，返回路径和代次，不读文件。`main.rs` 在锁外调用 `FileIoService::read_segment`。`finish_file_read` 重新获取全局锁，复核文件代次和权限，通过现有 `deliver` 发送结果。

- [ ] **Step 4: 实现写入三阶段**

1. `dm_file_chunk`：锁内校验 upload/owner/offset 并预留数据库状态；锁外执行 append+`sync_all`；锁内按预期 received 值更新，冲突则拒绝。
2. `dm_file_commit`：锁内校验完整性、配额和所有权；锁外执行 finalize/checksum；锁内复核文件代次并写入 `chat_id`、`disk_sha256`、`online_file_size`。
3. 任何阶段失败都清理本次 `.partial` 或回滚数据库预留，保持现有幂等语义。

- [ ] **Step 5: 运行文件测试**

Run: `cargo test --locked -p lan-chat-server --features lan-chat-server/test-hooks online::transfer::tests`
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add server-rust/src/online.rs server-rust/src/online/transfer.rs server-rust/src/main.rs
git commit -m "fix(server): move file io outside the online state lock"
```

---

### Task 7: 接入群消息预算与物理磁盘防线

**Files:**
- Modify: `server-rust/src/online/transfer.rs`
- Modify: `server-rust/src/online/social.rs`
- Modify: `server-rust/src/online.rs`
- Modify: `server-rust/src/online/storage.rs`
- Test: `server-rust/src/online/storage.rs`
- Test: `server-rust/src/online/social.rs`

**Interfaces:**
- Consumes: Task 4 的 `storage::check_budget` 和触发器。
- Produces: `Online::check_storage_budget(&Connection, &Identity, &str, i64) -> Result<()>`。

- [ ] **Step 1: 添加群预算红灯测试**

```rust
#[tokio::test]
async fn group_send_rejects_at_sender_budget_and_preserves_history() -> Result<()> {
    let mut online = setup_accounts(&temp_root("group-budget")?, OnlineLimits {
        storage_budget_bytes: 4096,
        group_sender_budget_bytes: 1024,
        min_disk_free_bytes: 0,
        ..OnlineLimits::default()
    })?;
    let first = Message {
        kind: "group_message".into(),
        message_id: Uuid::new_v4().to_string(),
        group_id: "group-fixture".into(),
        content: "a".repeat(700),
        ..Message::default()
    };
    let outcome = online.store.insert_online_message(super::transfer::OnlineMessage {
        message: &first,
        kind: "online_group",
        key: "group-fixture",
        author: "alice",
        target: "",
        image: None,
        file_size: 0,
        undelivered: super::transfer::Quota { limit: 0, bytes: 0 },
    })?;
    assert!(matches!(outcome, super::transfer::InsertOutcome::Inserted));
    let second = Message { message_id: Uuid::new_v4().to_string(), content: "b".repeat(700), ..first.clone() };
    let error = online.store.insert_online_message(super::transfer::OnlineMessage {
        message: &second,
        kind: "online_group",
        key: "group-fixture",
        author: "alice",
        target: "",
        image: None,
        file_size: 0,
        undelivered: super::transfer::Quota { limit: 0, bytes: 0 },
    }).unwrap_err();
    assert!(format!("{error:#}").contains("群消息存储空间已满"));
    let count: i64 = online.store.0.query_row("SELECT COUNT(*) FROM chat_messages", [], |r| r.get(0))?;
    assert_eq!(count, 1);
    Ok(())
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server group_send_rejects_at_sender_budget_and_preserves_history`
Expected: FAIL，当前群消息不检查预算。

- [ ] **Step 3: 在插入事务内检查预算**

```rust
let incoming = message.content.len() as i64
    + message.image_size
    + spec.file_size;
storage::check_budget(&tx, author, kind, incoming, &self.limits)?;
```

`insert_online_message` 必须在幂等判断之后、INSERT 之前调用；群消息使用发送者预算，私信使用全局预算。文件提交在写 `online_file_size` 后按实际大小检查。

- [ ] **Step 4: 接入维护校准**

在现有 `cleanup_maintenance` 中每次最多调用一次 `storage::calibrate(db, 1000)`，校准与 TTL 删除使用独立事务；记录 scanned 和 repaired 计数，不持 `Online` 锁执行全表扫描。

- [ ] **Step 5: 运行存储与群测试**

Run: `cargo test --locked -p lan-chat-server online::storage::tests online::social::tests`
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add server-rust/src/online.rs server-rust/src/online/transfer.rs server-rust/src/online/social.rs server-rust/src/online/storage.rs
git commit -m "fix(server): enforce group and disk storage budgets"
```

---

### Task 8: 服务端 P0 文档与全量验证

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture.md`
- Modify: `docs/testing.md`
- Modify: `docs/protocol.md`

- [ ] **Step 1: 更新文档**

1. README 增加 LAN 预认证、存储预算和文件 IO 配置说明。
2. `docs/architecture.md` 增加两阶段文件 IO、字节账本和设备绑定事务说明。
3. `docs/testing.md` 增加本计划新增测试命令。
4. `docs/protocol.md` 明确本计划没有新增线协议字段。

- [ ] **Step 2: 运行服务端测试**

```bash
cargo test --locked -p lan-chat-server --features lan-chat-server/test-hooks
cargo clippy --locked -p lan-chat-server --all-targets --features lan-chat-server/test-hooks -- -D warnings
```

Expected: 全部 PASS，0 Clippy 警告。

- [ ] **Step 3: 运行工作区回归**

```bash
cargo test --workspace --locked
node scripts/check-version.mjs
```

Expected: 全部 PASS，版本保持 1.4.1。

- [ ] **Step 4: 提交**

```bash
git add README.md docs/architecture.md docs/testing.md docs/protocol.md
git commit -m "docs: document server p0 security hardening"
```
