use std::{fmt, path::Path, sync::OnceLock, time::Duration};

use anyhow::{Context, Result, ensure};
use chrono::{SecondsFormat, Utc};
use lan_chat_protocol::{Message, OnlineUser, RECALLED_MESSAGE};
use ring::rand::SecureRandom;
use rusqlite::{Connection, OpenFlags, OptionalExtension, Row, TransactionBehavior, params};

use crate::ONLINE_BACKLOG_LIMIT;

pub(crate) const DEVICE_SECRET_COLUMN: &str = "device_secret_hash";

/// 已送达私信的过期清理索引：仅在缺失且待清理规模可接受时创建，避免老库启动长时间阻塞。
fn ensure_cleanup_index(db: &Connection) -> Result<()> {
    const NAME: &str = "idx_chat_messages_delivered_created";
    let exists: bool = db.query_row(
        "SELECT EXISTS(SELECT 1 FROM sqlite_schema WHERE type='index' AND name=?1)",
        [NAME],
        |row| row.get(0),
    )?;
    if exists {
        return Ok(());
    }
    let rows: i64 = db.query_row(
        "SELECT COUNT(*) FROM chat_messages WHERE kind='private' AND delivery_state='delivered'",
        [],
        |row| row.get(0),
    )?;
    if rows > 2_000_000 {
        eprintln!(
            "跳过创建 {NAME}：待清理消息 {rows} 条，请择机手动执行 CREATE INDEX {NAME} \
             ON chat_messages(created_at,id) WHERE kind='private' AND delivery_state='delivered'"
        );
        return Ok(());
    }
    db.execute_batch(&format!(
        "CREATE INDEX {NAME} ON chat_messages(created_at,id)
         WHERE kind='private' AND delivery_state='delivered'"
    ))?;
    Ok(())
}

const SCHEMA: &str = "
CREATE TABLE IF NOT EXISTS accounts (
    username TEXT NOT NULL,
    normalized_username TEXT NOT NULL UNIQUE,
    user_code TEXT NOT NULL,
    normalized_code TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS server_mode (id INTEGER PRIMARY KEY CHECK(id=1), mode TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS chat_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    message_id TEXT NOT NULL,
    kind TEXT NOT NULL,
    conversation_key TEXT NOT NULL,
    room TEXT,
    sender_username TEXT NOT NULL,
    sender_code TEXT NOT NULL,
    target_code TEXT,
    content TEXT NOT NULL,
    crypto_json TEXT,
    delivery_state TEXT NOT NULL DEFAULT 'sent',
    created_at TEXT NOT NULL,
    recalled INTEGER NOT NULL DEFAULT 0,
    online_image BLOB,
    online_image_ref TEXT,
    online_image_size INTEGER,
    online_image_sha256 TEXT,
    revision INTEGER NOT NULL DEFAULT 0,
    UNIQUE(sender_code, conversation_key, message_id)
);
CREATE INDEX IF NOT EXISTS idx_chat_messages_conversation
    ON chat_messages(kind, conversation_key, id);
CREATE INDEX IF NOT EXISTS idx_chat_messages_search_scope
    ON chat_messages(kind, conversation_key, content, id);
CREATE TABLE IF NOT EXISTS online_files (
    id TEXT PRIMARY KEY, owner TEXT NOT NULL, recipient TEXT NOT NULL, name TEXT NOT NULL,
    size INTEGER NOT NULL, received INTEGER NOT NULL DEFAULT 0, expires INTEGER NOT NULL,
    chat_id INTEGER UNIQUE
);
CREATE INDEX IF NOT EXISTS idx_online_files_pending_owner ON online_files(owner) WHERE chat_id IS NULL;
CREATE TABLE IF NOT EXISTS online_file_chunks (
    file_id TEXT NOT NULL, chunk_index INTEGER NOT NULL, data BLOB NOT NULL,
    PRIMARY KEY(file_id, chunk_index)
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS online_profiles (
    user_code TEXT PRIMARY KEY, display_name TEXT NOT NULL, bio TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS online_friendships (
    first_code TEXT NOT NULL, second_code TEXT NOT NULL,
    requested_by TEXT NOT NULL, accepted INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(first_code, second_code)
);
CREATE INDEX IF NOT EXISTS idx_online_friendships_second ON online_friendships(second_code);
CREATE TABLE IF NOT EXISTS online_groups (
    id TEXT PRIMARY KEY, name TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS online_group_members (
    group_id TEXT NOT NULL, user_code TEXT NOT NULL,
    PRIMARY KEY(group_id, user_code)
);
CREATE INDEX IF NOT EXISTS idx_online_group_members_user ON online_group_members(user_code);
";

const DROP_CHAT_FTS: &str = "
DROP TRIGGER IF EXISTS chat_messages_fts_insert;
DROP TRIGGER IF EXISTS chat_messages_fts_delete;
DROP TRIGGER IF EXISTS chat_messages_fts_update;
DROP TABLE IF EXISTS chat_messages_fts;
";

fn migrate_search(tx: &Connection) -> Result<()> {
    let version: i64 = tx.pragma_query_value(None, "user_version", |row| row.get(0))?;
    let objects: usize = tx.query_row(
        "SELECT count(*) FROM sqlite_schema
         WHERE (type='table' AND name='chat_messages_fts')
            OR (type='trigger' AND tbl_name='chat_messages' AND name IN
                ('chat_messages_fts_insert','chat_messages_fts_delete','chat_messages_fts_update'))",
        [],
        |row| row.get(0),
    )?;
    if version == 0 || objects != 4 {
        tx.execute_batch(DROP_CHAT_FTS)?;
        tx.execute_batch(
            "CREATE VIRTUAL TABLE chat_messages_fts USING fts5(
                 content, content='chat_messages', content_rowid='id', tokenize='trigram case_sensitive 1'
             );
             CREATE TRIGGER chat_messages_fts_insert AFTER INSERT ON chat_messages BEGIN
                 INSERT INTO chat_messages_fts(rowid,content) VALUES (new.id,new.content);
             END;
             CREATE TRIGGER chat_messages_fts_delete AFTER DELETE ON chat_messages BEGIN
                 INSERT INTO chat_messages_fts(chat_messages_fts,rowid,content) VALUES ('delete',old.id,old.content);
             END;
             CREATE TRIGGER chat_messages_fts_update AFTER UPDATE OF content ON chat_messages BEGIN
                 INSERT INTO chat_messages_fts(chat_messages_fts,rowid,content) VALUES ('delete',old.id,old.content);
                 INSERT INTO chat_messages_fts(rowid,content) VALUES (new.id,new.content);
             END;
             INSERT INTO chat_messages_fts(chat_messages_fts) VALUES ('rebuild');",
        )?;
        if version == 0 {
            tx.pragma_update(None, "user_version", 1)?;
        }
    }
    Ok(())
}

#[derive(Clone, Copy, Debug)]
pub struct Rejected(pub &'static str);

impl fmt::Display for Rejected {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.0)
    }
}

impl std::error::Error for Rejected {}

#[derive(Clone)]
pub struct Identity {
    pub username: String,
    pub display_code: String,
    pub code: String,
}

/// 手动回收磁盘：删掉历史消息后 SQLite 文件不会自己缩小，需要一次 VACUUM。
pub fn vacuum(path: &Path) -> Result<(i64, i64, i64, i64)> {
    let db = Connection::open(path).context("打开数据库")?;
    db.busy_timeout(Duration::from_secs(5))?;
    let pages = |db: &Connection| -> Result<(i64, i64)> {
        let page_count: i64 = db.query_row("PRAGMA page_count", [], |row| row.get(0))?;
        let free: i64 = db.query_row("PRAGMA freelist_count", [], |row| row.get(0))?;
        Ok((page_count, free))
    };
    let (before_pages, before_free) = pages(&db)?;
    db.execute_batch("VACUUM").context("执行 VACUUM")?;
    let (after_pages, after_free) = pages(&db)?;
    let _ = db.execute_batch("PRAGMA wal_checkpoint(TRUNCATE)");
    Ok((before_pages, before_free, after_pages, after_free))
}

pub struct Store(pub Connection);

fn dummy_bcrypt_hash() -> Result<&'static str> {
    static HASH: OnceLock<String> = OnceLock::new();
    if let Some(hash) = HASH.get() {
        return Ok(hash.as_str());
    }
    let mut entropy = [0u8; 32];
    ring::rand::SystemRandom::new()
        .fill(&mut entropy)
        .map_err(|_| anyhow::anyhow!("无法初始化认证保护随机数"))?;
    let generated =
        bcrypt::hash_with_result(entropy, 10)?.format_for_version(bcrypt::Version::TwoA);
    let _ = HASH.set(generated);
    Ok(HASH.get().expect("dummy bcrypt hash initialized").as_str())
}

pub fn user_code(code: &str) -> Result<String> {
    ensure!(
        (3..=16).contains(&code.len()) && code.bytes().all(|byte| byte.is_ascii_alphanumeric()),
        Rejected("用户代码须为 3–16 位字母或数字")
    );
    Ok(code.to_ascii_lowercase())
}

pub fn now() -> String {
    Utc::now().to_rfc3339_opts(SecondsFormat::AutoSi, true)
}

pub fn pair<'a>(first: &'a str, second: &'a str) -> (&'a str, &'a str) {
    if first < second {
        (first, second)
    } else {
        (second, first)
    }
}

pub fn conversation(first: &str, second: &str) -> String {
    let (first, second) = pair(first, second);
    format!("{first}:{second}")
}

impl Store {
    pub fn open(path: &Path) -> Result<Self> {
        Self::open_mode(path, "online")
    }

    pub(crate) fn open_mode(path: &Path, mode: &str) -> Result<Self> {
        let fresh = std::fs::metadata(path)
            .map(|meta| meta.len() == 0)
            .unwrap_or(true);
        let mut db = Connection::open(path)?;
        db.busy_timeout(Duration::from_secs(5))?;
        // WAL 让读写分离、提交只追加 WAL，避免每次写都 fsync 整库；新库用
        // INCREMENTAL auto_vacuum，删行后可用 incremental_vacuum 归还磁盘页。
        if fresh {
            if let Err(error) = db.pragma_update(None, "auto_vacuum", "INCREMENTAL") {
                eprintln!("无法为新数据库启用 incremental auto_vacuum: {error}");
            }
        }
        if let Err(error) =
            db.query_row("PRAGMA journal_mode=WAL", [], |row| row.get::<_, String>(0))
        {
            eprintln!("无法启用 WAL 日志模式，继续使用回滚日志: {error}");
        }
        if let Err(error) = db.pragma_update(None, "synchronous", "NORMAL") {
            eprintln!("无法设置 synchronous=NORMAL: {error}");
        }
        let tx = db.transaction_with_behavior(TransactionBehavior::Immediate)?;
        tx.execute_batch(SCHEMA)?;
        tx.execute(
            "INSERT INTO server_mode (id, mode)
             SELECT 1, ?1 WHERE ?1 = 'lan' OR NOT EXISTS (SELECT 1 FROM accounts WHERE password_hash = '')
             ON CONFLICT(id) DO NOTHING",
            [mode],
        )?;
        let stored_mode: Option<String> = tx
            .query_row("SELECT mode FROM server_mode WHERE id=1", [], |row| {
                row.get(0)
            })
            .optional()?;
        ensure!(
            stored_mode.as_deref() == Some(mode),
            "数据库模式为 {:?}，当前需要 {mode}",
            stored_mode
        );

        // Match the current Go schema before serving traffic.
        let columns = {
            let mut statement =
                tx.prepare("SELECT name FROM pragma_table_info('chat_messages')")?;
            statement
                .query_map([], |row| row.get::<_, String>(0))?
                .collect::<rusqlite::Result<Vec<_>>>()?
        };
        for (column, migration) in [
            (
                "crypto_json",
                "ALTER TABLE chat_messages ADD COLUMN crypto_json TEXT",
            ),
            (
                "delivery_state",
                "ALTER TABLE chat_messages ADD COLUMN delivery_state TEXT NOT NULL DEFAULT 'sent'",
            ),
            (
                "online_image",
                "ALTER TABLE chat_messages ADD COLUMN online_image BLOB",
            ),
            (
                "revision",
                "ALTER TABLE chat_messages ADD COLUMN revision INTEGER NOT NULL DEFAULT 0",
            ),
            (
                "online_image_ref",
                "ALTER TABLE chat_messages ADD COLUMN online_image_ref TEXT",
            ),
            (
                "online_image_size",
                "ALTER TABLE chat_messages ADD COLUMN online_image_size INTEGER",
            ),
            (
                "online_image_sha256",
                "ALTER TABLE chat_messages ADD COLUMN online_image_sha256 TEXT",
            ),
        ] {
            if !columns.iter().any(|name| name == column) {
                tx.execute(migration, [])?;
            }
        }
        let account_columns = {
            let mut statement = tx.prepare("SELECT name FROM pragma_table_info('accounts')")?;
            statement
                .query_map([], |row| row.get::<_, String>(0))?
                .collect::<rusqlite::Result<Vec<_>>>()?
        };
        if !account_columns
            .iter()
            .any(|name| name == DEVICE_SECRET_COLUMN)
        {
            tx.execute(
                &format!(
                    "ALTER TABLE accounts ADD COLUMN {DEVICE_SECRET_COLUMN} TEXT NOT NULL DEFAULT ''"
                ),
                [],
            )?;
        }
        ensure_cleanup_index(&tx)?;
        let definition: String = tx.query_row(
            "SELECT sql FROM sqlite_schema WHERE type='table' AND name='chat_messages'",
            [],
            |row| row.get(0),
        )?;
        if definition
            .to_ascii_lowercase()
            .contains("message_id text not null unique")
        {
            tx.execute_batch(DROP_CHAT_FTS)?;
            tx.execute_batch(
                "DROP INDEX IF EXISTS idx_chat_messages_conversation;
                 DROP INDEX IF EXISTS idx_chat_messages_search_scope;
                 ALTER TABLE chat_messages RENAME TO chat_messages_legacy;",
            )?;
            tx.execute_batch(SCHEMA)?;
            tx.execute_batch(
                "INSERT INTO chat_messages
                     (id,message_id,kind,conversation_key,room,sender_username,sender_code,target_code,
                      content,crypto_json,delivery_state,created_at,recalled,online_image,revision,
                      online_image_ref,online_image_size,online_image_sha256)
                 SELECT id,message_id,kind,conversation_key,room,sender_username,sender_code,target_code,
                        content,crypto_json,delivery_state,created_at,recalled,online_image,revision,
                        online_image_ref,online_image_size,online_image_sha256
                 FROM chat_messages_legacy;
                 DROP TABLE chat_messages_legacy;",
            )?;
        }
        migrate_search(&tx)?;
        if mode == "online" {
            crate::online::migrate_schema(&tx)?;
        }
        tx.commit()?;
        Ok(Self(db))
    }

    pub fn authenticate(path: &Path, request: Message) -> Result<Identity> {
        let validated = crate::online::validate_credentials(&request)?;
        let db = Connection::open_with_flags(path, OpenFlags::SQLITE_OPEN_READ_WRITE)?;
        db.busy_timeout(Duration::from_secs(5))?;
        if request.kind == "account_register" {
            let (display_code, code) = validated.user_code.context("缺少用户代码")?;
            let conflict: bool = db.query_row(
                "SELECT EXISTS(SELECT 1 FROM accounts WHERE normalized_username=?1 OR normalized_code=?2)",
                params![validated.normalized_username, code],
                |row| row.get(0),
            )?;
            ensure!(!conflict, Rejected("用户名或用户代码已被使用"));
            let hash = bcrypt::hash_with_result(request.account_password, 10)?
                .format_for_version(bcrypt::Version::TwoA);
            let changed = db.execute(
                "INSERT INTO accounts (username,normalized_username,user_code,normalized_code,password_hash,created_at)
                 VALUES (?,?,?,?,?,?) ON CONFLICT DO NOTHING",
                params![
                    validated.username,
                    validated.normalized_username,
                    display_code,
                    code,
                    hash,
                    now()
                ],
            )?;
            ensure!(changed != 0, Rejected("用户名或用户代码已被使用"));
            return Ok(Identity {
                username: validated.username,
                display_code,
                code,
            });
        }
        let found: Option<(String, String, String)> = db
            .query_row(
                "SELECT username,user_code,password_hash FROM accounts WHERE normalized_username=? AND password_hash<>''",
                [validated.normalized_username],
                |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
            )
            .optional()?;
        match found {
            Some((username, display, hash)) => {
                ensure!(
                    bcrypt::verify(request.account_password, &hash)?,
                    Rejected(crate::online::INVALID_CREDENTIALS_MSG)
                );
                Ok(Identity {
                    username,
                    code: user_code(&display)?,
                    display_code: display,
                })
            }
            None => {
                // Equalize verification timing with existing accounts to prevent username enumeration.
                let _ = bcrypt::verify(request.account_password, dummy_bcrypt_hash()?);
                Err(Rejected(crate::online::INVALID_CREDENTIALS_MSG).into())
            }
        }
    }

    pub fn account(&self, code: &str) -> Result<OnlineUser> {
        self.0.query_row(
            "SELECT username,user_code FROM accounts WHERE normalized_code=? AND password_hash<>''",
            [code],
            online_user,
        ).optional()?.ok_or_else(|| Rejected("找不到该用户代码").into())
    }

    pub fn contacts(&self, code: &str) -> Result<Vec<OnlineUser>> {
        let mut statement = self.0.prepare(
            "SELECT a.username,a.user_code FROM accounts a JOIN (
               SELECT CASE WHEN sender_code=? THEN target_code ELSE sender_code END AS peer, MAX(id) AS latest
               FROM chat_messages WHERE kind='private' AND (sender_code=? OR target_code=?)
               GROUP BY peer ORDER BY latest DESC LIMIT 200
             ) c ON a.normalized_code=c.peer ORDER BY c.latest DESC",
        )?;
        Ok(statement
            .query_map([code, code, code], online_user)?
            .collect::<rusqlite::Result<Vec<_>>>()?)
    }

    pub(crate) fn prune_conversation(db: &Connection, kind: &str, key: &str) -> Result<()> {
        db.execute(
            "DELETE FROM chat_messages
             WHERE kind=?1 AND conversation_key=?2 AND (kind='online_group' OR delivery_state<>'sent'
               OR recalled=1 OR EXISTS (SELECT 1 FROM server_mode WHERE mode='lan'))
               AND id NOT IN (SELECT id FROM chat_messages WHERE kind=?1 AND conversation_key=?2 ORDER BY id DESC LIMIT 10000)",
            [kind, key],
        )?;
        Ok(())
    }

    pub(crate) fn persist_chat(
        db: &Connection,
        message: &Message,
        kind: &str,
        key: &str,
        author: &str,
        target: &str,
        image: Option<&[u8]>,
    ) -> Result<bool> {
        let crypto = message
            .crypto
            .as_ref()
            .map(serde_json::to_string)
            .transpose()?;
        let changed = db.execute(
            "INSERT INTO chat_messages
                (message_id,kind,conversation_key,room,sender_username,sender_code,target_code,content,
                 delivery_state,created_at,online_image,crypto_json)
             VALUES (?,?,?,?,?,?,?,?,?,?,?,?)
             ON CONFLICT(sender_code,conversation_key,message_id) DO NOTHING",
            params![message.message_id, kind, key, message.room, message.username, author, target,
                message.content, message.delivery_state, message.created_at, image, crypto],
        )?;
        if changed != 0 {
            Self::prune_conversation(db, kind, key)?;
        }
        Ok(changed != 0)
    }

    pub fn history(
        &self,
        kind: &str,
        key: &str,
        request: &Message,
        after_id: i64,
    ) -> Result<(Vec<Message>, bool)> {
        let query = request.search_query.trim();
        if query.chars().nth(2).is_some() && !query.contains('\0') {
            let phrase = format!("\"{}\"", query.replace('"', "\"\""));
            let result = self.history_page(
                kind,
                key,
                request,
                &phrase,
                "recalled=0 AND chat_messages.id IN (SELECT rowid FROM chat_messages_fts WHERE chat_messages_fts MATCH ?4)",
                after_id,
            );
            match result {
                Err(ref error)
                    if error.downcast_ref::<rusqlite::Error>().is_some_and(
                        |error| match error {
                            rusqlite::Error::SqliteFailure(code, reason) => {
                                code.extended_code == rusqlite::ffi::SQLITE_CORRUPT_VTAB
                                    || (code.extended_code == rusqlite::ffi::SQLITE_ERROR
                                        && reason.as_deref()
                                            == Some("no such table: chat_messages_fts"))
                            }
                            _ => false,
                        },
                    ) =>
                {
                    eprintln!("History FTS index unavailable: {error}")
                }
                result => return result,
            }
        }
        self.history_page(
            kind,
            key,
            request,
            query,
            "(?4='' OR (recalled=0 AND instr(content,?4)>0))",
            after_id,
        )
    }

    pub(crate) fn lan_history(
        &self,
        kind: &str,
        key: &str,
        request: &Message,
    ) -> Result<(Vec<Message>, bool)> {
        self.history(kind, key, request, 0)
    }

    fn history_page(
        &self,
        kind: &str,
        key: &str,
        request: &Message,
        query: &str,
        predicate: &str,
        after_id: i64,
    ) -> Result<(Vec<Message>, bool)> {
        ensure!(
            (0..=100).contains(&request.limit),
            Rejected("历史记录页大小须在 0–100 之间")
        );
        let limit = if request.limit == 0 {
            50
        } else {
            request.limit
        } as usize;
        let mut statement = self.0.prepare(&format!(
            "SELECT chat_messages.id,message_id,COALESCE(room,''),sender_username,sender_code,
                    COALESCE(target_code,''),content,delivery_state,created_at,recalled,
                    COALESCE(online_image_size,length(online_image),0),revision,COALESCE(f.id,''),COALESCE(f.name,''),COALESCE(f.size,0),crypto_json
             FROM chat_messages LEFT JOIN online_files f ON f.chat_id=chat_messages.id
             WHERE kind=?1 AND conversation_key=?2 AND chat_messages.id>?6
               AND (?3='' OR chat_messages.id < (SELECT id FROM chat_messages
                   WHERE kind=?1 AND conversation_key=?2 AND message_id=?3 ORDER BY id DESC LIMIT 1))
               AND ({predicate})
             ORDER BY chat_messages.id DESC LIMIT ?5",
        ))?;
        let mut items = statement
            .query_map(
                params![
                    kind,
                    key,
                    request.before_message_id,
                    query,
                    limit + 1,
                    after_id
                ],
                stored_message,
            )?
            .map(|row| {
                row.map(|(_, mut message)| {
                    if kind == "online_group" {
                        message.kind = "group_message".into();
                        message.group_id = key.into();
                    } else if kind == "private" {
                        message.kind = "private_chat".into();
                        message.private = true;
                    } else {
                        message.kind = "chat".into();
                    }
                    message
                })
            })
            .collect::<rusqlite::Result<Vec<_>>>()?;
        let more = items.len() > limit;
        items.truncate(limit);
        items.reverse();
        Ok((items, more))
    }

    pub fn backlog(&self, code: &str) -> Result<Vec<(i64, Message)>> {
        let mut statement = self.0.prepare(
            "SELECT chat_messages.id,message_id,COALESCE(room,''),sender_username,sender_code,
                    target_code,content,delivery_state,created_at,recalled,
                    COALESCE(online_image_size,length(online_image),0),revision,COALESCE(f.id,''),COALESCE(f.name,''),COALESCE(f.size,0),crypto_json
             FROM chat_messages LEFT JOIN online_files f ON f.chat_id=chat_messages.id
             WHERE kind='private' AND target_code=? AND delivery_state='sent' AND recalled=0
             ORDER BY chat_messages.id LIMIT ?",
        )?;
        Ok(statement
            .query_map(params![code, ONLINE_BACKLOG_LIMIT], stored_message)?
            .map(|row| {
                row.map(|(id, mut message)| {
                    message.kind = "offline_message".into();
                    (id, message)
                })
            })
            .collect::<rusqlite::Result<Vec<_>>>()?)
    }
}

fn online_user(row: &Row<'_>) -> rusqlite::Result<OnlineUser> {
    Ok(OnlineUser {
        username: row.get(0)?,
        user_code: row.get(1)?,
        ..OnlineUser::default()
    })
}

fn stored_message(row: &Row<'_>) -> rusqlite::Result<(i64, Message)> {
    let recalled = row.get(9)?;
    let crypto = row
        .get::<_, Option<String>>(15)?
        .map(|raw| {
            serde_json::from_str(&raw).map_err(|error| {
                rusqlite::Error::FromSqlConversionFailure(
                    15,
                    rusqlite::types::Type::Text,
                    Box::new(error),
                )
            })
        })
        .transpose()?;
    Ok((
        row.get(0)?,
        Message {
            message_id: row.get(1)?,
            room: row.get(2)?,
            username: row.get(3)?,
            user_code: row.get(4)?,
            target_user_code: row.get(5)?,
            content: if recalled {
                RECALLED_MESSAGE.into()
            } else {
                row.get(6)?
            },
            delivery_state: row.get(7)?,
            created_at: row.get(8)?,
            recalled,
            image_size: row.get(10)?,
            revision: row.get(11)?,
            attachment_id: row.get(12)?,
            file_name: row.get(13)?,
            logical_size: row.get(14)?,
            crypto,
            ..Message::default()
        },
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lan_search_migrates_legacy_rows_and_repairs_index_without_changing_version() -> Result<()> {
        let path = std::env::temp_dir().join(format!("lan-fts-{}.db", uuid::Uuid::new_v4()));
        let legacy = Connection::open(&path)?;
        legacy.execute_batch(&SCHEMA.replacen(
            "message_id TEXT NOT NULL,",
            "message_id TEXT NOT NULL UNIQUE,",
            1,
        ))?;
        for (id, kind, key, content, recalled) in [
            ("first", "room", "main", "沿江\"公路\"设计", false),
            ("newest", "room", "main", "沿江\"公路\"施工", false),
            ("recall", "room", "main", "沿江\"公路\"旧稿", true),
            ("other", "room", "other", "沿江\"公路\"设计", false),
            ("private", "private", "alice:bob", "沿江\"公路\"设计", false),
        ] {
            legacy.execute(
                "INSERT INTO chat_messages(message_id,kind,conversation_key,sender_username,
                                           sender_code,content,created_at,recalled)
                 VALUES (?,?,?,'Alice','alice',?,'2026-11-23T10:00:00Z',?)",
                params![id, kind, key, content, recalled],
            )?;
        }
        drop(legacy);

        let store = Store::open_mode(&path, "lan")?;
        let db = &store.0;
        assert_eq!(
            db.pragma_query_value(None, "user_version", |row| row.get::<_, i64>(0))?,
            1
        );
        assert_eq!(
            db.query_row("SELECT count(*) FROM chat_messages", [], |row| row
                .get::<_, i64>(0))?,
            5
        );
        let mut request = Message {
            search_query: " 沿江\"公路\" ".into(),
            limit: 1,
            ..Message::default()
        };
        let (items, more) = store.lan_history("room", "main", &request)?;
        assert_eq!(items[0].message_id, "newest");
        assert!(more);
        request.before_message_id = items[0].message_id.clone();
        let (items, more) = store.lan_history("room", "main", &request)?;
        assert_eq!(items[0].message_id, "first");
        assert!(!more);
        request.before_message_id.clear();
        for query in ["公", "公路"] {
            request.search_query = query.into();
            let (items, _) = store.lan_history("private", "alice:bob", &request)?;
            assert_eq!(items[0].message_id, "private");
        }
        request.search_query.clear();
        let (items, _) = store.lan_history("room", "main", &request)?;
        assert!(items[0].recalled);

        db.execute(
            "UPDATE chat_messages SET content='替换后的内容 ABC' WHERE message_id='first'",
            [],
        )?;
        request.search_query = "内容 ABC".into();
        assert_eq!(
            store.lan_history("room", "main", &request)?.0[0].message_id,
            "first"
        );
        request.search_query = "内容 abc".into();
        assert!(store.lan_history("room", "main", &request)?.0.is_empty());
        db.execute("DELETE FROM chat_messages WHERE message_id='newest'", [])?;
        request.search_query = "施工".into();
        assert!(store.lan_history("room", "main", &request)?.0.is_empty());

        db.pragma_update(None, "user_version", 2)?;
        db.execute_batch("DROP TABLE chat_messages_fts;")?;
        request.search_query = "替换后".into();
        assert_eq!(
            store.lan_history("room", "main", &request)?.0[0].message_id,
            "first"
        );
        drop(store);
        let reopened = Store::open_mode(&path, "lan")?;
        assert_eq!(
            reopened
                .0
                .pragma_query_value(None, "user_version", |row| row.get::<_, i64>(0))?,
            2
        );
        let reader = Store(Connection::open_with_flags(
            &path,
            OpenFlags::SQLITE_OPEN_READ_ONLY,
        )?);
        assert_eq!(
            reader.lan_history("room", "main", &request)?.0[0].message_id,
            "first"
        );
        assert!(reader.0.execute("DELETE FROM chat_messages", []).is_err());
        drop(reader);
        drop(reopened);
        std::fs::remove_file(path)?;
        Ok(())
    }

    #[test]
    fn online_auth_hardening_and_prune_treat_recalled_sent_cleanly() -> Result<()> {
        let dir = std::env::temp_dir().join(format!("lan-chat-store-p0-{}", uuid::Uuid::new_v4()));
        std::fs::create_dir_all(&dir)?;
        let path = dir.join("online.db");
        let mut store = Store::open(&path)?;

        // Register Alice and reject duplicate username/code cleanly.
        let reg = Message {
            kind: "account_register".into(),
            username: "Alice".into(),
            user_code: "ALC001".into(),
            account_password: "password123".into(),
            ..Message::default()
        };
        let id = Store::authenticate(&path, reg.clone())?;
        assert_eq!(id.code, "alc001");
        assert!(Store::authenticate(&path, reg).is_err());

        // Valid login succeeds; missing user runs dummy bcrypt timing equalization cleanly.
        assert!(
            Store::authenticate(
                &path,
                Message {
                    kind: "account_login".into(),
                    username: "alice".into(),
                    account_password: "password123".into(),
                    ..Message::default()
                },
            )
            .is_ok()
        );
        let missing_err = Store::authenticate(
            &path,
            Message {
                kind: "account_login".into(),
                username: "nobody".into(),
                account_password: "password123".into(),
                ..Message::default()
            },
        )
        .err()
        .expect("missing user must fail");
        assert_eq!(
            missing_err.to_string(),
            crate::online::INVALID_CREDENTIALS_MSG
        );

        // Verify persist_chat 10,000-row prune protects pending non-recalled DM,
        // while allowing recalled 'sent' DMs outside top 10,000 to be pruned.
        let tx = store.0.transaction()?;
        tx.execute(
            "INSERT INTO chat_messages
             (message_id,kind,conversation_key,sender_username,sender_code,target_code,content,delivery_state,created_at,recalled)
             VALUES ('recalled-old','private','alc001:bob001','Alice','alc001','bob001','r','sent','2026-11-01T00:00:00Z',1),
                    ('pending-old','private','alc001:bob001','Alice','alc001','bob001','p','sent','2026-11-01T00:00:01Z',0)",
            [],
        )?;
        for idx in 0..10_000 {
            tx.execute(
                "INSERT INTO chat_messages
                 (message_id,kind,conversation_key,sender_username,sender_code,target_code,content,delivery_state,created_at,recalled)
                 VALUES (?1,'private','alc001:bob001','Alice','alc001','bob001','d','delivered','2026-11-02T00:00:00Z',0)",
                [format!("d-{idx}")],
            )?;
        }
        tx.commit()?;

        let latest = Message {
            message_id: uuid::Uuid::new_v4().to_string(),
            username: "Alice".into(),
            content: "newest".into(),
            delivery_state: "sent".into(),
            created_at: now(),
            ..Message::default()
        };
        assert!(Store::persist_chat(
            &store.0,
            &latest,
            "private",
            "alc001:bob001",
            "alc001",
            "bob001",
            None,
        )?);
        let recalled_left: i64 = store.0.query_row(
            "SELECT COUNT(*) FROM chat_messages WHERE message_id='recalled-old'",
            [],
            |row| row.get(0),
        )?;
        let pending_left: i64 = store.0.query_row(
            "SELECT COUNT(*) FROM chat_messages WHERE message_id='pending-old'",
            [],
            |row| row.get(0),
        )?;
        assert_eq!(recalled_left, 0);
        assert_eq!(pending_left, 1);

        drop(store);
        std::fs::remove_dir_all(dir)?;
        Ok(())
    }
}
