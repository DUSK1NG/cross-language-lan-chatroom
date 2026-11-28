# LAN Chat Quota and Client Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复文件配额、LAN 附件预留、房间目录超大帧、UDP 发现洪泛和 Android 深链/FCM 问题，同时保持旧客户端兼容。

**Architecture:** 在线文件大小纳入现有事务配额；LAN 预留通过持久释放标记和附件 worker 幂等回收；房间目录使用可选能力协商和 offset 分页；UDP 发现在客户端内部限流、去重和限容；Android 使用不导出 trampoline 和切换前清理。

**Tech Stack:** Rust、Tokio、rusqlite/SQLite、React/TypeScript、Java/Android、Robolectric、JUnit。

**Spec:** `docs/superpowers/specs/2026-11-28-lan-chat-security-remediation-design.md`

## Global Constraints

- 发布版本保持 `1.4.1`。
- 仅在房间目录新增可选 `rooms_offset` 字段和能力 `lan-directory-page-v1`；旧客户端忽略新字段。
- 在线文件全局预算默认 `2 GiB`，文件总数默认 `20000`，保持现有行为。
- LAN 每用户未完成上传默认 `4` 个、预留默认 `1 GiB`；全局房间默认 `200`、每用户创建默认 `50`。
- UDP 发现默认最多 `128` 个主机、主机名最长 `64` 字符、每来源 `20` 条/秒、状态发布间隔 `100ms`。
- 数据库迁移只增不删、幂等可重入；已有超量房间不自动删除。
- 所有配额拒绝使用中性错误，不破坏现有重连和凭据保存语义。
- 前端任务实施前必须调用 `modern-web-guidance`，检索与分页/列表更新相关的指南。
- 本计划依赖服务端 P0 计划已完成：`chat_messages.online_file_size`、存储账本和 Hub 安全接口已存在。

---

### Task 1: 协议、配置与迁移骨架

**Files:**
- Modify: `protocol-rust/src/lib.rs`
- Modify: `protocol-rust/src/message.rs`
- Modify: `server-rust/src/main.rs`
- Modify: `server-rust/src/online.rs`
- Modify: `server-rust/src/lan/mod.rs`
- Modify: `server-rust/src/lan/store.rs`
- Test: `protocol-rust/tests/golden`（现有 JSON 样本）
- Test: `protocol-rust/src/message.rs`

**Interfaces:**
- Produces: `LAN_DIRECTORY_PAGE: &str = "lan-directory-page-v1"`。
- Produces: `Message.rooms_offset: i64`，缺省 `0`，序列化时省略零值。
- Produces: `OnlineLimits.file_budget_bytes: u64`、`file_max_count: usize`。
- Produces: `lan::Options` 的 pending/reserved/room 限制字段。
- Produces: `attachment_uploads.release_requested_at TEXT`。

- [ ] **Step 1: 添加协议向后兼容测试**

```rust
#[test]
fn rooms_offset_defaults_to_zero_and_round_trips() {
    let legacy: Message = serde_json::from_str(r#"{"type":"rooms_request"}"#).unwrap();
    assert_eq!(legacy.rooms_offset, 0);
    assert!(!serde_json::to_string(&legacy).unwrap().contains("rooms_offset"));

    let paged = Message { rooms_offset: 100, ..Message::default() };
    let json = serde_json::to_string(&paged).unwrap();
    assert!(json.contains("\"rooms_offset\":100"));
    let decoded: Message = serde_json::from_str(&json).unwrap();
    assert_eq!(decoded.rooms_offset, 100);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-protocol rooms_offset_defaults_to_zero_and_round_trips`
Expected: FAIL，字段尚不存在。

- [ ] **Step 3: 实现协议字段和能力**

```rust
pub const LAN_DIRECTORY_PAGE: &str = "lan-directory-page-v1";
```

```rust
#[serde(default, skip_serializing_if = "is_zero")]
pub rooms_offset: i64,
```

- [ ] **Step 4: 添加配置**

```rust
#[arg(long, default_value_t = 2 * 1024 * 1024 * 1024)]
file_budget_bytes: u64,
#[arg(long, default_value_t = 20000)]
file_max_count: usize,
```

LAN 选项增加：

```rust
#[arg(long, default_value_t = 4)]
lan_max_pending_uploads: usize,
#[arg(long, default_value_t = 1024 * 1024 * 1024)]
lan_max_reserved_bytes_per_user: u64,
#[arg(long, default_value_t = 200)]
lan_max_rooms: usize,
#[arg(long, default_value_t = 50)]
lan_max_rooms_per_user: usize,
```

将这些值传入 `OnlineLimits` 和附件/房间服务，不散落在业务逻辑中。

- [ ] **Step 5: 添加 LAN 持久释放列**

```sql
ALTER TABLE attachment_uploads ADD COLUMN release_requested_at TEXT;
CREATE INDEX IF NOT EXISTS idx_attachment_uploads_author_release
ON attachment_uploads(author_code, release_requested_at);
```

迁移通过 `PRAGMA table_info` 和 `sqlite_master` 幂等执行。

- [ ] **Step 6: 运行协议测试和迁移测试**

```bash
cargo test --locked -p lan-chat-protocol
cargo test --locked -p lan-chat-server lan::store::tests
```

Expected: PASS。

- [ ] **Step 7: 提交**

```bash
git add protocol-rust server-rust/src/main.rs server-rust/src/online.rs server-rust/src/lan
git commit -m "feat: add quota and directory compatibility scaffolding"
```

---

### Task 2: 在线文件字节计入收件人配额

**Files:**
- Modify: `server-rust/src/online/transfer.rs`
- Modify: `server-rust/src/online.rs`
- Test: `server-rust/src/online/transfer.rs`

**Interfaces:**
- Consumes: `chat_messages.online_file_size`、`OnlineLimits.file_budget_bytes`、`OnlineLimits.file_max_count`。
- Produces: `check_undelivered_quota` 统计文本、图片和文件字节。

- [ ] **Step 1: 添加文件配额红灯测试**

```rust
#[tokio::test]
async fn online_file_bytes_count_toward_undelivered_quota() -> Result<()> {
    let online = setup_accounts(&temp_root("file-quota")?, OnlineLimits {
        undelivered_per_target: 10,
        undelivered_bytes_per_target: 64 * 1024 * 1024,
        file_budget_bytes: 2 * 1024 * 1024 * 1024,
        file_max_count: 20000,
        ..OnlineLimits::default()
    })?;
    online.store.0.execute(
        "INSERT INTO chat_messages(message_id,kind,conversation_key,sender_username,sender_code,target_code,content,delivery_state,created_at,online_file_size)
         VALUES ('file-1','private','fixture','Alice','alice','bob','[文件] a','sent','2026-11-28T00:00:00Z',?1)",
        [40 * 1024 * 1024],
    )?;
    let error = Online::check_undelivered_quota(
        &online.store.0,
        "bob",
        Quota { limit: 10, bytes: 64 * 1024 * 1024 },
    ).unwrap_err();
    assert!(format!("{error:#}").contains("对方暂不可接收新的私信"));
    Ok(())
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server online_file_bytes_count_toward_undelivered_quota`
Expected: FAIL，当前文件大小未计入配额。

- [ ] **Step 3: 修改配额 SQL**

```rust
let (count, bytes): (i64, i64) = db.query_row(
    "SELECT COUNT(*), COALESCE(SUM(length(content)+COALESCE(online_image_size,0)+COALESCE(online_file_size,0)),0)
     FROM chat_messages
     WHERE kind='private' AND target_code=?1 AND delivery_state='sent' AND recalled=0",
    params![target],
    |row| Ok((row.get(0)?, row.get(1)?)),
)?;
```

- [ ] **Step 4: 初始化时预检实际声明大小**

`begin_file` 在目标存在性检查后，将 `request.logical_size` 纳入待投递预算预检；提交时仍在同一 `IMMEDIATE` 事务内按实际 `size` 再检查，避免并发提交绕过。

- [ ] **Step 5: 配置化文件全局上限**

将 `begin_file` SQL 中硬编码的 `2147483648` 和 `20000` 替换为 `self.limits.file_budget_bytes` 与 `self.limits.file_max_count`。

- [ ] **Step 6: 提交时写文件字节列**

```rust
tx.execute(
    "UPDATE chat_messages SET online_file_size=? WHERE id=?",
    params![size, chat_id],
)?;
```

该更新与 `chat_id`、`disk_sha256` 和配额检查在同一事务内完成。

- [ ] **Step 7: 运行测试确认通过**

Run: `cargo test --locked -p lan-chat-server online_file_bytes_count_toward_undelivered_quota online_file_commit_idempotent_at_budget_boundary`
Expected: PASS。

- [ ] **Step 8: 提交**

```bash
git add server-rust/src/online/transfer.rs server-rust/src/online.rs
git commit -m "fix(server): count file bytes toward recipient quotas"
```

---

### Task 3: LAN 附件每用户预留与生命周期释放

**Files:**
- Modify: `server-rust/src/lan/attachments.rs`
- Modify: `server-rust/src/lan/presence.rs`
- Modify: `server-rust/src/lan/rooms.rs`
- Modify: `server-rust/src/lan/store.rs`
- Modify: `server-rust/src/lan/mod.rs`
- Test: `server-rust/src/lan/attachments.rs`

**Interfaces:**
- Produces: `Store::request_attachment_release(&str) -> Result<usize>`
- Produces: `Store::attachment_user_usage(&str) -> Result<(usize, i64)>`
- Produces: `LanAttachmentLimits { max_pending_uploads: usize, max_reserved_bytes_per_user: u64 }` 和测试夹具 `Fixture::open_with_limits`。
- Consumes: 附件 worker 现有 `Work::Cleanup` 和 `cleanup_attachments`。

- [ ] **Step 1: 添加预留上限测试**

```rust
#[tokio::test]
async fn lan_reservation_enforces_user_count_and_bytes() -> Result<()> {
    let root = temp_root("lan-reservation")?;
    let mut fixture = Fixture::open_with_limits(&root, LanAttachmentLimits {
        max_pending_uploads: 4,
        max_reserved_bytes_per_user: 20 * 1024 * 1024 * 1024,
    })?;
    for _ in 0..4 {
        let response = fixture.request(Message {
            kind: "attachment.init".into(),
            room: LOBBY.into(),
            logical_size: 5 * 1024 * 1024 * 1024,
            ..Message::default()
        })?;
        assert_ne!(response.kind, "error");
    }
    let rejected = fixture.request(Message {
        kind: "attachment.init".into(),
        room: LOBBY.into(),
        logical_size: 1,
        ..Message::default()
    })?;
    assert_eq!(rejected.kind, "error");
    fixture.close();

    let mut strict = Fixture::open_with_limits(&root.join("strict"), LanAttachmentLimits {
        max_pending_uploads: 4,
        max_reserved_bytes_per_user: 1024 * 1024 * 1024,
    })?;
    let rejected = strict.request(Message {
        kind: "attachment.init".into(),
        room: LOBBY.into(),
        logical_size: 5 * 1024 * 1024 * 1024,
        ..Message::default()
    })?;
    assert_eq!(rejected.kind, "error");
    Ok(())
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server lan_reservation_enforces_user_count_and_bytes`
Expected: FAIL，当前只检查房间总配额。

- [ ] **Step 3: 初始化事务增加用户检查**

```rust
let (pending, reserved): (i64, i64) = tx.query_row(
    "SELECT COUNT(*), COALESCE(SUM(reserved_bytes),0) FROM attachment_uploads
     WHERE author_code=?1 AND release_requested_at IS NULL",
    [author],
    |row| Ok((row.get(0)?, row.get(1)?)),
)?;
ensure!(pending < limits.max_pending_uploads as i64, Rejected("未完成附件上传过多"));
ensure!(reserved.saturating_add(size) <= limits.max_reserved_bytes_per_user as i64,
    Rejected("附件预留空间不足"));
```

用户检查和房间 `room_quotas` 更新必须在同一事务中。

- [ ] **Step 4: 持久化释放请求**

```rust
pub(super) fn request_attachment_release(&self, author: &str) -> Result<usize> {
    self.0.execute(
        "UPDATE attachment_uploads SET release_requested_at=?1
         WHERE author_code=?2 AND release_requested_at IS NULL",
        params![now(), author],
    )
}
```

在 `Hub::remove` 成功删除会话后调用；离房、移除成员、删除房间时对目标作者调用。调用必须短且幂等，不直接删除附件文件。

- [ ] **Step 5: Worker 回收释放标记**

`cleanup_attachments` 的选择条件改为：

```sql
WHERE release_requested_at IS NOT NULL OR julianday(expires_at)<=julianday('now')
```

回收逻辑继续先删磁盘、再在事务中扣减 `room_quotas.reserved_bytes` 并删除元数据；任何一步失败都保留记录供下次重试。

- [ ] **Step 6: 添加生命周期测试**

新增 `lan_reservation_releases_on_disconnect_leave_remove_delete` 和 `lan_reservation_release_survives_worker_restart`，分别验证四个生命周期入口和 worker 重启后的幂等回收。

- [ ] **Step 7: 运行测试确认通过**

Run: `cargo test --locked -p lan-chat-server lan_reservation`
Expected: PASS。

- [ ] **Step 8: 提交**

```bash
git add server-rust/src/lan
git commit -m "fix(lan): bound and release per-user attachment reservations"
```

---

### Task 4: 服务端房间目录分页与数量上限

**Files:**
- Modify: `server-rust/src/lan/rooms.rs`
- Modify: `server-rust/src/lan/connection.rs`
- Modify: `server-rust/src/lan/mod.rs`
- Test: `server-rust/src/lan/rooms.rs`
- Test: `server-rust/tests/lan_p0.rs`

**Interfaces:**
- Consumes: `LAN_DIRECTORY_PAGE`、`Message.rooms_offset`。
- Produces: `Hub::rooms(&mut self, Uuid, &Message)` 支持能力协商分页；现有内部调用传入 `&Message::default()`。
- Produces: `Hub::create_room` 执行全局和每用户上限。

- [ ] **Step 1: 添加分页红灯测试**

```rust
#[test]
fn lan_directory_paginates_and_legacy_frame_stays_bounded() -> Result<()> {
    let mut hub = super::test_hub(&temp_root("room-page")?)?;
    let id = Uuid::new_v4();
    let (tx, mut rx) = mpsc::channel(16);
    let (close_tx, _close_rx) = oneshot::channel();
    let mut peer = super::test_peer("Owner", "OWNER1", "owner", LOBBY, tx, close_tx, true);
    peer.capabilities.insert(LAN_DIRECTORY_PAGE.into());
    hub.clients.insert(id, peer);
    for index in 0..250 {
        let name = format!("room-{index:03}");
        hub.rooms.insert(name.clone(), Room {
            owner: "owner".into(),
            private: false,
            allowed: HashMap::from([("owner".into(), true)]),
            admins: HashSet::new(),
            password_hash: None,
            generation: Uuid::new_v4(),
        });
    }
    hub.rooms(id, &Message { limit: 100, rooms_offset: 0, ..Message::default() });
    let first = rx.try_recv()?;
    assert_eq!(first.room_details.len(), 100);
    assert!(first.has_more);
    hub.rooms(id, &Message { limit: 100, rooms_offset: 100, ..Message::default() });
    let second = rx.try_recv()?;
    assert_eq!(second.room_details.len(), 100);
    hub.clients.get_mut(&id).unwrap().capabilities.clear();
    hub.rooms(id, &Message::default());
    let legacy = rx.try_recv()?;
    assert!(serde_json::to_vec(&legacy)?.len() <= MAX_FRAME_BYTES);
    assert!(legacy.has_more);
    Ok(())
}
```

测试模块导入 `std::collections::{HashMap, HashSet}`、`std::sync::mpsc`、`tokio::sync::oneshot` 和 `lan_chat_protocol::MAX_FRAME_BYTES`；`Room`/`LOBBY`/`test_hub`/`test_peer` 来自服务端 P0 计划。

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server lan_directory_paginates_and_legacy_frame_stays_bounded`
Expected: FAIL，当前全量返回且可能超帧。

- [ ] **Step 3: 实现服务端分页**

```rust
let paged = peer.room_roles && self.client_supports_directory_page(id);
let request_limit = if request.limit > 0 { request.limit } else { 100 };
let limit = if paged { request_limit.clamp(1, 100) } else { details.len() };
let offset = if paged { request.rooms_offset.max(0) as usize } else { 0 };
let page: Vec<_> = details.into_iter().skip(offset).take(limit).collect();
let has_more = offset + page.len() < details.len();
```

`Peer` 新增 `capabilities: HashSet<String>`，在 `Hub::register` 时从登录协商结果保存。`Hub::client_supports_directory_page(&self, Uuid) -> bool` 返回 `peer.capabilities.contains(LAN_DIRECTORY_PAGE)`。`login_ok.capabilities` 增加 `LAN_DIRECTORY_PAGE`。`rooms_response` 设置 `has_more`，新客户端后续请求携带 `rooms_offset`。

- [ ] **Step 4: 旧客户端安全降级**

未声明能力时，如果完整 `Message` 序列化超过 `MAX_FRAME_BYTES - 1024`，返回第一页并设置 `has_more=true`；不得关闭连接。新客户端才按页追加。

- [ ] **Step 5: 创建房间上限**

```rust
ensure!(self.rooms.len() < limits.max_rooms, Rejected("频道数量已达上限"));
let owned = self.rooms.values().filter(|room| room.owner == peer.identity.code).count();
ensure!(owned < limits.max_rooms_per_user, Rejected("你创建的频道过多"));
```

上限只阻止新建，不删除已有超量房间。

- [ ] **Step 6: 运行测试确认通过**

Run: `cargo test --locked -p lan-chat-server lan_directory`
Expected: PASS。

- [ ] **Step 7: 提交**

```bash
git add server-rust/src/lan
git commit -m "feat(lan): paginate room directories without breaking legacy clients"
```

---

### Task 5: 客户端房间分页与前端加载更多

**Files:**
- Modify: `crates/client-core/src/session.rs`
- Modify: `crates/client-core/src/lib.rs`
- Modify: `crates/client-core/src/state.rs`
- Modify: `frontend/src/app/WorkspacePage.tsx`
- Modify: `frontend/src/components/sidebar/ConversationSidebar.tsx`
- Test: `crates/client-core/src/state.rs`
- Test: `frontend/src/app/WorkspacePage.test.tsx`

**Interfaces:**
- Produces: `State.rooms_offset: i64`、`State.rooms_has_more: bool`、`State.rooms_loading: bool`。
- Produces: `Client::request_room_page(offset: i64)`。

- [ ] **Step 1: 调用前端指南**

Run: `npx.cmd -y modern-web-guidance@latest search "paginated list load more" --skill-version 2026_11_09-5bcc021b`
Retrieve matching guide before editing TypeScript/TSX。

- [ ] **Step 2: 添加状态测试**

```rust
#[test]
fn room_pages_append_without_dropping_loaded_rooms() {
    let mut state = State::default();
    state.rooms = vec![room("a"), room("b")];
    state.rooms_offset = 2;
    state.rooms_has_more = true;
    state.apply_room_page(vec![room("c"), room("d")], false);
    assert_eq!(state.rooms.iter().map(|r| r.name.as_str()).collect::<Vec<_>>(), vec!["a","b","c","d"]);
    assert_eq!(state.rooms_offset, 4);
    assert!(!state.rooms_has_more);
}

fn room(name: &str) -> lan_chat_protocol::RoomInfo {
    lan_chat_protocol::RoomInfo { name: name.into(), ..Default::default() }
}
```

- [ ] **Step 3: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-client-core room_pages_append_without_dropping_loaded_rooms`
Expected: FAIL，接口尚不存在。

- [ ] **Step 4: 实现客户端分页**

1. LAN 登录能力列表增加 `LAN_DIRECTORY_PAGE`。
2. 首次 `rooms_request` 使用 `limit=100`、`rooms_offset=0`。
3. `rooms_response` 在 `rooms_offset==0` 时替换，否则按房间名去重追加。
4. `has_more=true` 时由 UI 触发 `request_room_page(state.rooms_offset)`。
5. 旧服务器未声明能力时保持一次全量请求和原替换语义。

- [ ] **Step 5: 前端添加加载更多**

在房间列表底部添加按钮，仅当 `roomsHasMore && !roomsLoading` 时显示；点击调用桥接 `directory.loadMoreRooms`，加载失败保留已有列表并显示可重试错误。不得引入第三方分页库。

- [ ] **Step 6: 添加前端测试**

```tsx
test('paginates room directory without dropping loaded pages', async () => {
  const bridge = createFakeBridge({ ...workspaceState, rooms: [room('a'), room('b')], roomsHasMore: true });
  render(<WorkspacePage bridge={bridge} />);
  await user.click(screen.getByRole('button', { name: /加载更多频道/ }));
  expect(screen.getByText('channel-c')).toBeInTheDocument();
  expect(screen.getByText('channel-a')).toBeInTheDocument();
});
```

- [ ] **Step 7: 运行测试**

```bash
cargo test --locked -p lan-chat-client-core room_pages_append_without_dropping_loaded_rooms
pnpm.cmd --dir frontend exec vitest run src/app/WorkspacePage.test.tsx -t paginates
```

Expected: PASS。

- [ ] **Step 8: 提交**

```bash
git add crates/client-core frontend/src/app/WorkspacePage.tsx frontend/src/components/sidebar/ConversationSidebar.tsx frontend/src/app/WorkspacePage.test.tsx
git commit -m "feat(client): append paginated LAN room directories"
```

---

### Task 6: UDP 发现限流、DER 去重与状态发布节流

**Files:**
- Modify: `crates/client-core/Cargo.toml`
- Modify: `crates/client-core/src/discovery.rs`
- Modify: `crates/client-core/src/lib.rs`
- Test: `crates/client-core/src/discovery.rs`

**Interfaces:**
- Produces: `DiscoveryLimits { max_hosts: usize, max_name_bytes: usize, per_source_per_second: u32, publish_interval: Duration }`
- Produces: `DiscoveryState::accept(Announcement, IpAddr) -> bool`。

- [ ] **Step 1: 添加发现洪泛测试**

```rust
#[tokio::test]
async fn udp_discovery_bounds_dedupes_der_and_throttles() -> Result<()> {
    let limits = DiscoveryLimits { max_hosts: 2, max_name_bytes: 64, per_source_per_second: 1, publish_interval: Duration::from_millis(100) };
    let mut state = DiscoveryState::new(limits);
    let pem = certificate_pem();
    let first = announcement("host-a", pem_with_comment(&pem, "a"));
    let duplicate = announcement("host-a", pem_with_comment(&pem, "b"));
    assert!(state.accept(first.clone(), "198.51.100.1".parse()?));
    assert!(!state.accept(duplicate, "198.51.100.1".parse()?));
    assert!(state.accept(announcement("host-b", pem_with_comment(&pem, "c")), "198.51.100.1".parse()?));
    assert!(!state.accept(announcement("host-c", certificate_pem()), "198.51.100.1".parse()?));
    Ok(())
}

fn certificate_pem() -> String {
    rcgen::generate_simple_self_signed(vec!["localhost".into()]).unwrap().pem()
}

fn pem_with_comment(pem: &str, comment: &str) -> String {
    format!("{pem}\n# {comment}\n")
}

fn announcement(name: &str, certificate: String) -> Announcement {
    Announcement {
        service: "lan-chat".into(),
        version: 1,
        host_name: name.into(),
        port: 8888,
        fingerprint_sha256: crate::checksum(certificate.as_bytes()),
        certificate_base64: STANDARD.encode(certificate),
    }
}
```

`pem_with_comment` 生成相同 DER、不同 PEM 注释的测试证书。

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-client-core udp_discovery_bounds_dedupes_der_and_throttles`
Expected: FAIL，当前按 PEM 摘要无限插入。

- [ ] **Step 3: 实现限制**

先在 `crates/client-core/Cargo.toml` 增加 dev-dependency `rcgen = "0.13.2"`，供测试生成相同 DER、不同 PEM 注释的证书。

1. 解析证书 DER，使用 `ring::digest::SHA256` 对 DER 计算内部稳定键；仍保留现有 PEM 指纹用于信任文件和 UI 展示。
2. 主机名 UTF-8 字节数超过 64 时丢弃。
3. 每来源使用令牌桶，默认 20 条/秒；超限丢弃。
4. `HashMap` 达到 128 时淘汰最旧 `last_seen`，仍优先更新已有主机。
5. `publish` 使用 `tokio::time::Instant` 节流，至少间隔 100ms；扫描结束时强制发布一次。

- [ ] **Step 4: 运行测试确认通过**

Run: `cargo test --locked -p lan-chat-client-core udp_discovery`
Expected: PASS。

- [ ] **Step 5: 提交**

```bash
git add crates/client-core/src/discovery.rs crates/client-core/src/lib.rs
git commit -m "fix(client): bound LAN discovery resource usage"
```

---

### Task 7: Android 深链隔离与 FCM 切换清理

**Files:**
- Modify: `android/app/src/main/AndroidManifest.xml`
- Create: `android/app/src/main/java/dev/dusk1ng/lanchat/NotificationRouteActivity.java`
- Create: `android/app/src/main/java/dev/dusk1ng/lanchat/AccountSwitchPolicy.java`
- Modify: `android/app/src/main/java/dev/dusk1ng/lanchat/MainActivity.java`
- Modify: `android/app/src/main/java/dev/dusk1ng/lanchat/MessagePushService.java`
- Modify: `android/app/build.gradle`
- Create: `android/app/src/test/java/dev/dusk1ng/lanchat/NotificationRouteActivityTest.java`
- Create: `android/app/src/test/java/dev/dusk1ng/lanchat/NotificationAccountSwitcherTest.java`

**Interfaces:**
- Produces: `NotificationRouteActivity`，`android:exported="false"`。
- Produces: `MainActivity.handleInternalNotificationRoute(Intent)`，只接受内部 extra。
- Produces: `AccountSwitchPolicy.shouldClean(String savedUsername, String newUsername)`。
- Produces: `MainActivity.prepareAccountSwitch(String newUsername)`。

- [ ] **Step 1: 添加 Android 测试依赖**

```groovy
testImplementation 'junit:junit:4.13.2'
testImplementation 'org.robolectric:robolectric:4.18.1'
```

- [ ] **Step 2: 添加深链红灯测试**

```java
@RunWith(RobolectricTestRunner.class)
public class NotificationRouteActivityTest {
    @Test public void externalExplicitDeepLinkDoesNotClearUnread() {
        Context context = ApplicationProvider.getApplicationContext();
        MessagePushService.markUnread(context, "alice", "bob", "m1");
        Intent external = new Intent(context, MainActivity.class)
            .setData(Uri.parse("lanchat://dm/alice/bob"));
        MainActivity activity = Robolectric.buildActivity(MainActivity.class, external).setup().get();
        activity.onNewIntent(external);
        assertEquals(1, MessagePushService.unreadCount(context, "alice", "bob"));
    }
}
```

- [ ] **Step 3: 运行测试确认失败**

Run: `gradle -p android --no-daemon :app:testDebugUnitTest --tests '*NotificationRouteActivityTest'`
Expected: FAIL，当前 `MainActivity` 处理外部 data 并清除未读。

- [ ] **Step 4: 实现不导出路由**

1. `NotificationRouteActivity` 不导出，只接受通知 PendingIntent 的显式启动。
2. 它校验 `ownCode`、会话格式和 group UUID，然后使用内部 extra 启动 `MainActivity`。
3. `MainActivity` 不再从 `getIntent().getData()` 读取路由；`onNewIntent` 只调用 `handleInternalNotificationRoute`。
4. `MessagePushService.show` 的 PendingIntent 改为 `NotificationRouteActivity.class`。

- [ ] **Step 5: 添加切换账号清理测试**

```java
public class AccountSwitchPolicy {
    public static boolean shouldClean(String savedUsername, String newUsername) {
        return savedUsername != null && newUsername != null
            && !savedUsername.equalsIgnoreCase(newUsername);
    }
}
```

`NotificationAccountSwitcherTest` 断言：

```java
Assert.assertTrue(AccountSwitchPolicy.shouldClean("alice", "bob"));
Assert.assertTrue(AccountSwitchPolicy.shouldClean("Alice", "bob"));
Assert.assertFalse(AccountSwitchPolicy.shouldClean("alice", "ALICE"));
Assert.assertFalse(AccountSwitchPolicy.shouldClean(null, "bob"));
```

- [ ] **Step 6: 修复切换分支**

```java
if (AccountSwitchPolicy.shouldClean(savedAccount == null ? null : savedAccount.username(), name)) {
    syncNotificationsBeforeLogout();
    clearSession();
    forgetAccount();
}
```

先停用并删除旧 token，再清除本地会话；新登录失败也不会继续收到旧账号推送。

- [ ] **Step 7: 运行 Android 测试**

```bash
gradle -p android --no-daemon :app:testDebugUnitTest --tests '*NotificationRouteActivityTest' --tests '*NotificationAccountSwitcherTest'
gradle -p android --no-daemon :app:assembleDebug
```

Expected: PASS。

- [ ] **Step 8: 提交**

```bash
git add android/app
git commit -m "fix(android): isolate notification routes and clean old push tokens"
```

---

### Task 8: 兼容性文档与全量验证

**Files:**
- Modify: `README.md`
- Modify: `docs/protocol.md`
- Modify: `docs/architecture.md`
- Modify: `docs/testing.md`

- [ ] **Step 1: 更新文档**

1. `docs/protocol.md` 记录 `rooms_offset`、`lan-directory-page-v1` 和旧客户端降级行为。
2. README 记录文件预算、LAN 预留、房间上限和发现限制配置。
3. `docs/testing.md` 增加本计划测试命令。

- [ ] **Step 2: 运行服务端与客户端测试**

```bash
cargo test --locked -p lan-chat-protocol -p lan-chat-server -p lan-chat-client-core --features lan-chat-server/test-hooks
cargo clippy --locked -p lan-chat-protocol -p lan-chat-server -p lan-chat-client-core --all-targets --features lan-chat-server/test-hooks -- -D warnings
pnpm.cmd --dir frontend run lint
pnpm.cmd --dir frontend exec vitest run
pnpm.cmd --dir frontend run build
gradle -p android --no-daemon :app:testDebugUnitTest :app:assembleDebug
node scripts/check-version.mjs
```

Expected: 全部 PASS，版本保持 1.4.1。

- [ ] **Step 3: 提交**

```bash
git add README.md docs/protocol.md docs/architecture.md docs/testing.md
git commit -m "docs: document quota and client compatibility hardening"
```
