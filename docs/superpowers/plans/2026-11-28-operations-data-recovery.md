# LAN Chat Operations and Data Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 10000 条历史裁剪可恢复，提供一致性备份/验证/恢复，增加服务化与健康指标，并清理公开运维信息和开发者转移包风险。

**Architecture:** 活跃消息物理裁剪前写入独立归档表和归档目录；归档容量不足时拒绝裁剪而不丢数据；新增独立 `lan-chat-backup` workspace crate 使用 SQLite 在线备份 API；服务端增加默认关闭的 loopback 健康/指标端点；systemd/Windows 服务配置自动恢复；公开信息通过内容扫描、模板化和经确认的历史重写处理。

**Tech Stack:** Rust、rusqlite/SQLite、ring、clap、fs2、Tokio、systemd、PowerShell、GitHub CLI、git-filter-repo。

**Spec:** `docs/superpowers/specs/2026-11-28-lan-chat-security-remediation-design.md`

## Global Constraints

- 发布版本保持 `1.4.1`。
- 活跃历史默认仍为每会话 `10000` 条；归档默认保留 `365` 天，归档预算默认 `8 GiB`，均可配置。
- 归档容量不足时保留活跃消息并返回可观察错误，绝不静默丢弃。
- 备份必须使用 SQLite 在线备份 API 或停写快照，不能运行时分别复制 `.db`、`-wal`、`-shm`。
- 恢复只允许目标不存在或已停止运行；拒绝路径穿越、符号链接、损坏清单和运行中实例。
- 健康端点默认关闭，只允许 loopback，不输出账号、正文、token、私钥或完整 IP。
- 公开文档和计划不得包含真实服务器地址、SSH 用户名、私钥路径、用户 IP 或密钥路径。
- Git 历史重写、强推、SSH 凭据轮换和生产部署必须在执行前展示具体命令和影响并再次确认。
- 本计划依赖服务端 P0 与配额计划完成；使用发布链计划升级后的 rusqlite/依赖锁。

---

### Task 1: 归档 Schema、预算与路径规则

**Files:**
- Create: `server-rust/src/archive.rs`
- Modify: `server-rust/src/main.rs`
- Modify: `server-rust/src/store.rs`
- Modify: `server-rust/src/online/files.rs`
- Test: `server-rust/src/archive.rs`

**Interfaces:**
- Produces: `archive::migrate(&Connection) -> Result<()>`
- Produces: `archive::root(db: &Connection) -> Result<PathBuf>`
- Produces: `archive::ensure_capacity(&Transaction, i64, i64) -> Result<()>`
- Produces: `ArchiveConfig { retention_days: u32, budget_bytes: u64, min_disk_free_bytes: u64 }`。
- Produces: `ArchiveStats { archived_messages, archived_files, skipped_bytes }`。

- [ ] **Step 1: 添加归档红灯测试**

```rust
#[test]
fn archive_capacity_rejection_preserves_active_messages() -> Result<()> {
    let db = Connection::open_in_memory()?;
    archive::migrate(&db)?;
    db.execute_batch("INSERT INTO storage_usage(scope,sender_code,used_bytes) VALUES('archive','',900)")?;
    let tx = db.transaction()?;
    assert!(archive::ensure_capacity(&tx, 200, 1024).is_err());
    drop(tx);
    let active: i64 = db.query_row("SELECT COUNT(*) FROM chat_messages", [], |r| r.get(0))?;
    assert_eq!(active, 0);
    Ok(())
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server archive::tests::archive_capacity_rejection_preserves_active_messages`
Expected: FAIL，模块尚不存在。

- [ ] **Step 3: 实现增量 Schema**

```sql
CREATE TABLE IF NOT EXISTS message_archive (
    source_id INTEGER PRIMARY KEY,
    message_id TEXT NOT NULL,
    kind TEXT NOT NULL,
    conversation_key TEXT NOT NULL,
    room TEXT,
    sender_username TEXT NOT NULL,
    sender_code TEXT NOT NULL,
    target_code TEXT,
    content TEXT NOT NULL,
    crypto_json TEXT,
    delivery_state TEXT NOT NULL,
    created_at TEXT NOT NULL,
    recalled INTEGER NOT NULL,
    online_image BLOB,
    online_image_ref TEXT,
    online_image_size INTEGER,
    online_image_sha256 TEXT,
    revision INTEGER NOT NULL,
    archived_at TEXT NOT NULL,
    archive_reason TEXT NOT NULL,
    payload_archive_path TEXT,
    archive_state TEXT NOT NULL DEFAULT 'pending'
);

CREATE TABLE IF NOT EXISTS archive_usage (
    id INTEGER PRIMARY KEY CHECK(id=1),
    used_bytes INTEGER NOT NULL DEFAULT 0
);

ALTER TABLE online_files ADD COLUMN archive_path TEXT;
ALTER TABLE online_files ADD COLUMN archived_at TEXT;

CREATE INDEX IF NOT EXISTS idx_message_archive_created
ON message_archive(archived_at, source_id);
CREATE INDEX IF NOT EXISTS idx_online_files_archived
ON online_files(archived_at) WHERE archived_at IS NOT NULL;
```

所有 `ALTER TABLE` 先通过 `PRAGMA table_info` 检查；`user_version` 增加到 7，旧版本只读不兼容时通过备份恢复。

- [ ] **Step 4: 实现容量账本**

`message_archive` 的 INSERT/DELETE 触发器维护 `archive_usage.used_bytes`，字节数为正文、图片 BLOB、图片大小和文件大小之和。`ensure_capacity` 在归档事务内检查 `archive_budget_bytes` 和 `min_disk_free_bytes`，不足时返回 `Rejected("归档空间不足，已保留活跃消息")`。

- [ ] **Step 5: 添加配置**

```rust
#[arg(long, default_value_t = 365)]
archive_retention_days: u32,
#[arg(long, default_value_t = 8 * 1024 * 1024 * 1024)]
archive_budget_bytes: u64,
```

- [ ] **Step 6: 运行测试确认通过**

Run: `cargo test --locked -p lan-chat-server archive::tests`
Expected: PASS。

- [ ] **Step 7: 提交**

```bash
git add server-rust/src/archive.rs server-rust/src/main.rs server-rust/src/store.rs server-rust/src/online/files.rs
git commit -m "feat(server): add recoverable message archive schema"
```

---

### Task 2: 接入 10000 条裁剪与附件归档

**Files:**
- Modify: `server-rust/src/store.rs`
- Modify: `server-rust/src/archive.rs`
- Modify: `server-rust/src/online/files.rs`
- Modify: `server-rust/src/online/transfer.rs`
- Test: `server-rust/tests/archive.rs`

**Interfaces:**
- Produces: `archive::prune_conversation(&Connection, &str, &str, i64, &ArchiveConfig) -> Result<ArchiveStats>`
- Consumes: `store::prune_conversation` 当前调用点。

- [ ] **Step 1: 添加 10001 条归档测试**

```rust
#[tokio::test]
async fn pruning_10001_messages_archives_oldest_and_keeps_history_compatible() -> Result<()> {
    let root = temp_root("archive-prune")?;
    let store = Store::open(&root.join("chat.db"))?;
    archive::migrate(&store.0)?;
    let tx = store.0.transaction()?;
    for index in 0..10001 {
        tx.execute(
            "INSERT INTO chat_messages(message_id,kind,conversation_key,sender_username,sender_code,content,created_at)
             VALUES (?1,'online_group','group-fixture','Fixture','alice','message','2026-11-28T00:00:00Z')",
            [format!("m-{index}")],
        )?;
    }
    tx.commit()?;
    let stats = archive::prune_conversation(&store.0, "online_group", "group-fixture", 10000, &ArchiveConfig::default())?;
    assert_eq!(stats.archived_messages, 1);
    let active: i64 = store.0.query_row("SELECT COUNT(*) FROM chat_messages", [], |r| r.get(0))?;
    let archived: i64 = store.0.query_row("SELECT COUNT(*) FROM message_archive", [], |r| r.get(0))?;
    assert_eq!((active, archived), (10000, 1));
    let archived_id: String = store.0.query_row("SELECT message_id FROM message_archive", [], |r| r.get(0))?;
    assert_eq!(archived_id, "m-0");
    Ok(())
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server --test archive pruning_10001_messages_archives_oldest_and_keeps_history_compatible`
Expected: FAIL，当前直接 DELETE。

- [ ] **Step 3: 实现先归档后删除**

```rust
pub(crate) fn prune_conversation(
    db: &Connection,
    kind: &str,
    key: &str,
    keep: i64,
    config: &ArchiveConfig,
) -> Result<ArchiveStats> {
    let tx = db.transaction_with_behavior(TransactionBehavior::Immediate)?;
    let candidates: Vec<i64> = {
        let mut stmt = tx.prepare(
            "SELECT id FROM chat_messages WHERE kind=?1 AND conversation_key=?2
             ORDER BY id DESC LIMIT -1 OFFSET ?3"
        )?;
        stmt.query_map(params![kind, key, keep], |r| r.get(0))?.collect::<rusqlite::Result<_>>()?
    };
    let bytes = archive::candidate_bytes(&tx, &candidates)?;
    archive::ensure_capacity(&tx, bytes, config.budget_bytes)?;
    for id in &candidates {
        archive::insert_from_active(&tx, *id, "conversation-limit")?;
    }
    tx.commit()?;
    archive::publish_payloads(db, &candidates)?;
    archive::delete_active(db, &candidates)?;
    Ok(ArchiveStats { archived_messages: candidates.len(), ..Default::default() })
}
```

`store::prune_conversation` 改为调用该函数，默认 `keep=10000`。

- [ ] **Step 4: 实现附件安全归档**

1. 对磁盘附件先复制到 `<db>.archive/file-<id>.partial`，`sync_all` 后校验 SHA-256，再重命名为 `file-<id>`。
2. 更新 `online_files.archive_path/archived_at` 和 `message_archive.payload_archive_path`。
3. 活跃消息删除提交后才删除原 `.files/file-<id>`；崩溃时留下可恢复的重复文件。
4. 图片 BLOB 直接写入 `message_archive.online_image`，不复制文件。
5. `publish_payloads` 可重入，`archive_state='pending'` 的记录优先恢复。

- [ ] **Step 5: 排除活跃清理误删**

`cleanup_maintenance` 中删除 `online_files` 的 SQL 增加 `AND archived_at IS NULL`；孤儿文件扫描不得删除 `.archive` 目录。

- [ ] **Step 6: 运行归档测试**

```bash
cargo test --locked -p lan-chat-server --test archive
cargo test --locked -p lan-chat-server online::transfer::tests
```

Expected: PASS。

- [ ] **Step 7: 提交**

```bash
git add server-rust/src/store.rs server-rust/src/archive.rs server-rust/src/online/files.rs server-rust/src/online/transfer.rs server-rust/tests/archive.rs
git commit -m "fix(server): archive messages before history pruning"
```

---

### Task 3: 归档 TTL 清理与崩溃恢复

**Files:**
- Modify: `server-rust/src/archive.rs`
- Modify: `server-rust/src/online/transfer.rs`
- Test: `server-rust/tests/archive.rs`

**Interfaces:**
- Produces: `archive::cleanup(&Connection, &ArchiveConfig, i64) -> Result<ArchiveCleanupStats>`。

- [ ] **Step 1: 添加 TTL 边界测试**

```rust
#[test]
fn archive_ttl_boundary_keeps_recent_and_deletes_expired() -> Result<()> {
    let store = Store::open(&temp_root("archive-ttl")?.join("chat.db"))?;
    archive::migrate(&store.0)?;
    let now = Utc::now();
    for (index, age) in [chrono::Duration::hours(364 * 24 + 1), chrono::Duration::hours(365 * 24 + 1), chrono::Duration::days(366)].into_iter().enumerate() {
        store.0.execute(
            "INSERT INTO message_archive(source_id,message_id,kind,conversation_key,sender_username,sender_code,content,delivery_state,created_at,recalled,revision,archived_at,archive_reason,archive_state)
             VALUES (?1,?2,'online_group','group-fixture','Fixture','alice','message','sent','2026-11-28T00:00:00Z',0,0,?3,'conversation-limit','ready')",
            params![index as i64 + 1, format!("m-{index}"), (now - age).to_rfc3339()],
        )?;
    }
    let stats = archive::cleanup(&store.0, &ArchiveConfig::default(), 100)?;
    assert_eq!(stats.deleted_messages, 2);
    let remaining: i64 = store.0.query_row("SELECT COUNT(*) FROM message_archive", [], |r| r.get(0))?;
    assert_eq!(remaining, 1);
    Ok(())
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server --test archive archive_ttl_boundary_keeps_recent_and_deletes_expired`
Expected: FAIL，清理函数尚不存在。

- [ ] **Step 3: 实现有限批次清理**

每次最多处理 100 条归档消息和 100 个附件：先删除归档文件和数据库行，再更新 `archive_usage`。RFC3339 解析失败跳过并计数，不误删。清理在独立 SQLite 连接执行，不持 `Online` 锁。

- [ ] **Step 4: 接入小时维护**

在 `cleanup_maintenance` 中调用 `archive::cleanup`，记录 deleted_messages、deleted_files、skipped_invalid_timestamps。

- [ ] **Step 5: 运行测试确认通过**

Run: `cargo test --locked -p lan-chat-server --test archive`
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add server-rust/src/archive.rs server-rust/src/online/transfer.rs server-rust/tests/archive.rs
git commit -m "feat(server): expire archived data in bounded batches"
```

---

### Task 4: lan-chat-backup 备份、验证与恢复工具

**Files:**
- Modify: `Cargo.toml`
- Modify: `.github/workflows/release.yml`
- Create: `tools/backup/Cargo.toml`
- Create: `tools/backup/src/main.rs`
- Create: `tools/backup/src/layout.rs`
- Create: `tools/backup/src/manifest.rs`
- Create: `tools/backup/src/lock.rs`
- Test: `tools/backup/src/manifest.rs`
- Test: `server-rust/tests/backup.rs`

**Interfaces:**
- Produces: `lan-chat-backup backup --db PATH --out DIR [--bot-key-file PATH] [--cert PATH --key PATH]`
- Produces: `lan-chat-backup verify --backup DIR`
- Produces: `lan-chat-backup restore --backup DIR --target-root DIR [--replace-stopped]`

- [ ] **Step 1: 添加清单测试**

```rust
#[test]
fn manifest_rejects_path_traversal_and_hash_mismatch() -> Result<()> {
    let root = temp_root("backup-manifest")?;
    std::fs::write(root.join("chat.db"), b"db")?;
    let mut manifest = Manifest::create(&root)?;
    manifest.files[0].sha256 = "0".repeat(64);
    assert!(manifest.verify(&root).is_err());
    manifest.files[0].path = "../escape".into();
    assert!(manifest.verify(&root).is_err());
    Ok(())
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-backup manifest_rejects_path_traversal_and_hash_mismatch`
Expected: FAIL，crate 尚不存在。

- [ ] **Step 3: 创建 workspace crate**

```bash
cargo init --bin --name lan-chat-backup tools/backup
```

`Cargo.toml` members增加 `tools/backup`。`tools/backup/Cargo.toml` 使用 `clap`、`rusqlite` bundled、`ring`、`serde`、`serde_json`、`fs2`、`anyhow`，dev-dependency 使用 `tempfile`。

- [ ] **Step 4: 实现在线备份**

```rust
let source = Connection::open_with_flags(db, OpenFlags::SQLITE_OPEN_READ_ONLY)?;
let target = Connection::open(temp_db)?;
{
    let backup = rusqlite::backup::Backup::new(&source, &target)?;
    backup.run_to_completion(32, Duration::from_millis(10), None)?;
}
layout::copy_tree(files_root, temp_files)?;
manifest::write_atomic(temp_root, &manifest)?;
fs::rename(temp_root, out)?;
```

备份目录先写 `.partial-<uuid>`，全部文件校验后原子发布为最终目录。

- [ ] **Step 5: 实现验证与恢复**

1. `verify` 校验清单 schema、相对路径、符号链接、文件大小和 SHA-256，并执行 `PRAGMA integrity_check`、`foreign_key_check`。
2. `restore` 获取目标实例文件锁；目标非空时必须显式 `--replace-stopped`，且服务进程不能运行。
3. 恢复先写 `.restore-partial`，验证后原子替换数据库、`.files`、bot-key、证书和私钥。
4. 任何失败非零退出并保留原目标。

- [ ] **Step 6: 添加 WAL 恢复集成测试**

`server-rust/tests/backup.rs` 创建未 checkpoint 的 WAL、消息和附件，调用 backup→verify→restore，断言消息、附件和 bot-key 哈希一致。

- [ ] **Step 7: 增加 Linux 备份工具 Release artifact**

在 `release.yml` 的 Linux job 中同时构建：

```bash
cargo build --locked --release -p lan-chat-server -p lan-chat-backup
cp target/release/lan-chat-server lan-chat-server-linux-x86_64
cp target/release/lan-chat-backup lan-chat-backup-linux-x86_64
```

两个资产都纳入 `SHA256SUMS` 和桌面 Environment 签名；上传 artifact 后再由汇总 job 校验。

- [ ] **Step 8: 运行测试确认通过**

```bash
cargo test --locked -p lan-chat-backup
cargo test --locked -p lan-chat-server --test backup
cargo build --locked --release -p lan-chat-server -p lan-chat-backup
```

Expected: PASS。

- [ ] **Step 9: 提交**

```bash
git add Cargo.toml tools/backup server-rust/tests/backup.rs .github/workflows/release.yml
git commit -m "feat(ops): add consistent backup verify and restore tooling"
```

---

### Task 5: LAN 主机整目录备份脚本

**Files:**
- Create: `scripts/backup-lan-host.ps1`
- Create: `scripts/tests/backup-lan-host.ps1`
- Modify: `docs/release-runbook.md`

**Interfaces:**
- Produces: `backup-lan-host.ps1 -HostDirectory PATH -OutputDirectory PATH [-RetentionDays 30] [-WhatIf]`。

- [ ] **Step 1: 添加脚本测试**

在 `scripts/tests/backup-lan-host.ps1` 创建临时 host 目录，包含 `chat.db`、`chat.db-wal`、`chat.db-shm`、`certs/server-lan.key`、`attachments/item`、`device-secret`；运行脚本后用 `lan-chat-backup verify` 或 SHA-256 校验全部文件存在，并验证 `-WhatIf` 不写输出。

- [ ] **Step 2: 运行测试确认失败**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/backup-lan-host.ps1`
Expected: FAIL，脚本尚不存在。

- [ ] **Step 3: 实现脚本**

1. 默认路径为 `%LocalAppData%\DUSK1NG\LAN Chat\host`。
2. 输出目录必须在用户指定备份根下，拒绝把备份写进 host 自身。
3. 使用 `Copy-Item` 前提示停住房主；支持 `-WhatIf`。
4. 复制数据库及 sidecar、证书、附件、`device-secret` 和可选 bot-key。
5. 生成 SHA-256 清单并调用 `lan-chat-backup verify` 验证普通文件；按保留周期删除旧备份。
6. 文档明确备份含私钥，必须加密并限制 ACL。

- [ ] **Step 4: 运行测试确认通过**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/backup-lan-host.ps1`
Expected: PASS。

- [ ] **Step 5: 提交**

```bash
git add scripts/backup-lan-host.ps1 scripts/tests/backup-lan-host.ps1 docs/release-runbook.md
git commit -m "feat(ops): add LAN host backup script"
```

---

### Task 6: 健康/指标端点与系统服务

**Files:**
- Create: `server-rust/src/health.rs`
- Modify: `server-rust/src/main.rs`
- Create: `deploy/systemd/lan-chat-online.service`
- Create: `scripts/render-systemd-unit.sh`
- Create: `scripts/install-online-service.ps1`
- Test: `server-rust/tests/health.rs`

**Interfaces:**
- Produces: `--health-listen Option<SocketAddr>`，默认关闭，仅允许 loopback。
- Produces: `/healthz`、`/readyz`、`/metrics`。
- Produces: `render-systemd-unit.sh --service NAME --binary PATH --db PATH --tls-cert-source PATH --tls-key-source PATH --health-listen ADDR [--extra-args ARGS]`，输出不包含真实秘密的 unit。

- [ ] **Step 1: 添加健康端点测试**

```rust
#[tokio::test]
async fn health_endpoint_rejects_non_loopback_and_redacts_metrics() -> Result<()> {
    assert!(parse_health_listen("0.0.0.0:8889").is_err());
    let listener = TcpListener::bind("127.0.0.1:0").await?;
    let addr = listener.local_addr()?;
    let state = HealthState::default();
    let server = tokio::spawn(serve_health(listener, state));
    let body = http_get(&addr, "/metrics").await?;
    assert!(body.contains("lanchat_active_sessions"));
    assert!(!body.contains("alice@example.com"));
    server.abort();
    Ok(())
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-server --test health`
Expected: FAIL，模块尚不存在。

- [ ] **Step 3: 实现最小 HTTP 服务**

使用 Tokio `TcpListener` 实现仅支持 GET 的 HTTP/1.1 服务：请求头上限 8 KiB、超时 2 秒、只响应三个路径。`/readyz` 检查数据库 `SELECT 1`、附件目录可写和维护任务最近成功时间；`/metrics` 使用 `AtomicU64` 聚合活跃/预认证连接、拒绝计数、锁等待、清理滞后、DB/WAL 大小和磁盘余量。

- [ ] **Step 4: 添加 systemd unit**

`deploy/systemd/lan-chat-online.service` 包含：

```ini
[Service]
DynamicUser=yes
StateDirectory=lan-chat-online
LoadCredential=tls-cert:/etc/lan-chat/tls.crt
LoadCredential=tls-key:/etc/lan-chat/tls.key
Environment=CHAT_TLS_CERT=/run/credentials/%n/tls-cert
Environment=CHAT_TLS_KEY=/run/credentials/%n/tls-key
ExecStart=/usr/local/bin/lan-chat-online --mode online --listen 0.0.0.0:8888 --db /var/lib/lan-chat-online/chat.db --health-listen 127.0.0.1:8889
Restart=on-failure
RestartSec=2
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/lan-chat-online
```

FCM/bot-key 参数由部署环境通过 credential 文件追加，不把真实值写入单元。

- [ ] **Step 5: 添加 unit 渲染脚本**

`render-systemd-unit.sh` 从模板生成 `/etc/systemd/system/<service>.service` 内容，参数只包含路径、监听地址和非秘密 extra args；输出到 stdout，部署集成脚本再重定向和安装。脚本拒绝包含 token、私钥内容或换行注入。

- [ ] **Step 6: 添加 Windows 服务脚本**

`install-online-service.ps1` 使用 `sc.exe create` 创建服务，配置失败恢复、服务账户、数据库/附件 ACL 和 loopback 健康检查；支持 `-Uninstall` 和 `-WhatIf`。

- [ ] **Step 7: 运行测试确认通过**

```bash
cargo test --locked -p lan-chat-server --test health
bash scripts/render-systemd-unit.sh --service lan-chat-online --binary /usr/local/bin/lan-chat-online --db /var/lib/lan-chat-online/chat.db --tls-cert-source /etc/lan-chat/tls.crt --tls-key-source /etc/lan-chat/tls.key --health-listen 127.0.0.1:8889 > /tmp/lan-chat-online.service
systemd-analyze verify /tmp/lan-chat-online.service
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/install-online-service.ps1 -WhatIf
```

Expected: PASS。

- [ ] **Step 8: 提交**

```bash
git add server-rust/src/health.rs server-rust/src/main.rs server-rust/tests/health.rs deploy/systemd scripts/render-systemd-unit.sh scripts/install-online-service.ps1
git commit -m "feat(ops): add health metrics and managed services"
```

---

### Task 7: 公开信息清理与开发者转移内容扫描

**Files:**
- Modify: `WORKLOG.md`
- Create: `docs/operations.md`
- Create: `scripts/scan-developer-transfer.ps1`
- Create: `deploy/developer-transfer-allowlist.json`
- Modify: `scripts/package-developer-transfer.ps1`
- Modify: `scripts/test-developer-transfer-package.ps1`
- Create: `scripts/tests/developer-transfer-content.ps1`

**Interfaces:**
- Produces: `scan-developer-transfer.ps1 -RepositoryRoot PATH -PackageDirectory PATH [-ArchivePath PATH]`。
- Produces: allowlist，只允许明确占位示例，不允许真实凭据。

- [ ] **Step 1: 添加内容扫描测试**

在临时 Git 仓库的普通 `.md`、patch、bundle blob 和 ZIP 中放入合成私钥/token；运行扫描器，断言全部被规则命中；放入 allowlist 中的占位示例则通过。输出只包含规则、文件和行号，不打印匹配内容。

- [ ] **Step 2: 运行测试确认失败**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/developer-transfer-content.ps1`
Expected: FAIL，扫描器尚不存在。

- [ ] **Step 3: 实现内容扫描**

1. 规则覆盖 PEM 私钥、云 access key、GitHub token、SSH 私钥路径、JWT、数据库文件名和 bot-key JSON。
2. 对普通文件、patch、ZIP 文本条目直接扫描。
3. 对 Git bundle 使用 `git bundle list-heads`，在临时仓库 fetch 后遍历全部 refs 和 blob；跳过二进制图片，但扫描文本 blob。
4. allowlist 只匹配精确路径和占位符，不匹配真实值。
5. 命中时返回非零退出，禁止生成转移 ZIP。

- [ ] **Step 4: 清理当前公开文档**

1. 从 `WORKLOG.md` 删除真实服务器地址、SSH 用户名、私钥路径、systemd 参数、在线用户 IP 和本机签名密钥路径。
2. 新增 `docs/operations.md`，只使用 `<host>`、`<user>`、`<credential-path>` 等占位示例。
3. 搜索整个受跟踪树，确认没有真实敏感字符串残留。

- [ ] **Step 5: 接入打包脚本**

`package-developer-transfer.ps1` 在创建 bundle 前和 ZIP 后调用扫描器；`test-developer-transfer-package.ps1` 增加内容级验证，不再只检查文件名。

- [ ] **Step 6: 运行测试确认通过**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/developer-transfer-content.ps1`
Expected: PASS。

- [ ] **Step 7: 提交**

```bash
git add WORKLOG.md docs/operations.md scripts/scan-developer-transfer.ps1 deploy/developer-transfer-allowlist.json scripts/package-developer-transfer.ps1 scripts/test-developer-transfer-package.ps1 scripts/tests/developer-transfer-content.ps1
git commit -m "security: sanitize operations docs and scan transfer contents"
```

---

### Task 8: GitHub 安全设置、历史重写准备与凭据轮换

**Files:**
- Create: `scripts/enable-branch-protection.sh`
- Modify: GitHub 仓库安全设置
- Modify: 生产 SSH 凭据（经确认）
- Prepare: Git 历史重写备份和字面量替换表

**Interfaces:**
- Consumes: Task 7 的敏感替换表和干净工作树。
- Produces: secret scanning、分支保护脚本、历史重写备份和新 SSH 凭据。
- Hands off: 实际历史重写和强推由 1.4.1 集成计划在合并后、打 tag 前执行。

- [ ] **Step 1: 启用 secret scanning 与 push protection**

```bash
gh api -X PUT repos/DUSK1NG/cross-language-lan-chatroom \
  -f security_and_analysis.secret_scanning.status=enabled \
  -f security_and_analysis.secret_scanning_push_protection.status=enabled
```

Expected: API 返回 200。

- [ ] **Step 2: 创建分支保护脚本但暂不执行**

`scripts/enable-branch-protection.sh` 使用以下 API；`required_approving_review_count=0` 避免唯一维护者无法批准自己的 PR，同时仍要求 PR 和全部 CI：

```bash
gh api -X PUT repos/DUSK1NG/cross-language-lan-chatroom/branches/master/protection \
  -F required_status_checks.strict=true \
  -F required_status_checks.contexts[]='Rust LAN and online server' \
  -F required_status_checks.contexts[]='React frontend' \
  -F required_status_checks.contexts[]='Windows desktop host' \
  -F required_status_checks.contexts[]='Android debug and Rust JNI' \
  -F required_status_checks.contexts[]='Windows source launcher' \
  -F required_status_checks.contexts[]='Supply chain' \
  -F enforce_admins=true \
  -F required_pull_request_reviews.required_approving_review_count=0 \
  -F allow_force_pushes=false \
  -F allow_deletions=false
```

脚本只在 1.4.1 集成分支完成全部直接 master 提交后执行。

- [ ] **Step 3: 轮换生产 SSH 凭据**

```bash
ssh-keygen -t ed25519 -a 100 -f "$HOME/.ssh/lan-chat-production-new" -C "lan-chat-production-$(date +%Y%m%d)"
ssh-copy-id -i "$HOME/.ssh/lan-chat-production-new.pub" "$PRODUCTION_USER@$PRODUCTION_HOST"
ssh -i "$HOME/.ssh/lan-chat-production-new" "$PRODUCTION_USER@$PRODUCTION_HOST" 'sudo ufw allow from <ADMIN_CIDR> to any port 22 proto tcp'
```

确认新密钥可登录后，删除服务器上的旧公钥和本地旧私钥；真实 `<ADMIN_CIDR>` 在执行前由用户确认，不写入仓库。

- [ ] **Step 4: 准备历史重写材料，但不执行重写**

```bash
git filter-repo --version
git bundle create /tmp/lan-chat-before-history-cleanup.bundle --all
```

在 `/tmp/lan-chat-sensitive-replacements.txt` 写入字面量替换，只包含已确认敏感字符串，不使用 regex。展示替换表、受影响 refs 和后续强推命令；若 `git-filter-repo` 不可用，停止并征求安装同意。

- [ ] **Step 5: 验证准备结果并提交候选分支**

```bash
bash -n scripts/enable-branch-protection.sh
git bundle verify /tmp/lan-chat-before-history-cleanup.bundle
git status --short
git add scripts/enable-branch-protection.sh docs/operations.md docs/release-runbook.md
git commit -m "docs: prepare security governance and history cleanup"
git push origin HEAD
```

Expected: 候选分支包含脚本和文档；尚未改写历史、尚未强推、尚未启用分支保护。
