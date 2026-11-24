use std::{
    collections::{HashMap, HashSet},
    path::Path,
    time::Duration,
};

use anyhow::{Context, Result, ensure};
use lan_chat_protocol::{LOCAL_HOST_PASSWORD_MAX_BYTES, Message};
use rusqlite::{Connection, OpenFlags, OptionalExtension, TransactionBehavior, params};
use uuid::Uuid;

use super::{LOBBY, Room, validate_room_name};
use crate::store::{Identity, Rejected, Store, now, user_code};

const SCHEMA: &str = "
CREATE TABLE IF NOT EXISTS local_host_credential (
    id INTEGER PRIMARY KEY CHECK(id = 1),
    password_hash BLOB NOT NULL
);
CREATE TABLE IF NOT EXISTS offline_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    target_code TEXT NOT NULL,
    sender_username TEXT NOT NULL,
    sender_code TEXT NOT NULL,
    content TEXT NOT NULL,
    message_id TEXT,
    created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_offline_messages_target
    ON offline_messages(target_code, id);
CREATE TABLE IF NOT EXISTS mls_key_packages (
    user_code TEXT PRIMARY KEY,
    key_package TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS mls_groups (
    group_id TEXT PRIMARY KEY,
    room TEXT NOT NULL,
    current_epoch INTEGER NOT NULL DEFAULT -1
);
CREATE TABLE IF NOT EXISTS mls_group_epochs (
    group_id TEXT NOT NULL,
    epoch INTEGER NOT NULL,
    proposal_id TEXT NOT NULL DEFAULT '',
    commit_data TEXT NOT NULL,
    created_at TEXT NOT NULL,
    PRIMARY KEY(group_id, epoch)
);
CREATE TABLE IF NOT EXISTS mls_group_proposals (
    group_id TEXT NOT NULL,
    epoch INTEGER NOT NULL,
    proposal_id TEXT NOT NULL,
    proposal_data TEXT NOT NULL,
    proposal_digest TEXT NOT NULL,
    action TEXT NOT NULL DEFAULT 'add',
    target_code TEXT NOT NULL DEFAULT '',
    member_snapshot TEXT NOT NULL DEFAULT '[]',
    created_at TEXT NOT NULL,
    PRIMARY KEY(group_id, epoch, proposal_id),
    UNIQUE(group_id, epoch, proposal_digest)
);
CREATE TABLE IF NOT EXISTS mls_group_welcomes (
    group_id TEXT NOT NULL,
    epoch INTEGER NOT NULL,
    target_code TEXT NOT NULL,
    welcome TEXT NOT NULL,
    proposal_id TEXT NOT NULL DEFAULT '',
    welcome_digest TEXT NOT NULL DEFAULT '',
    accepted INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL,
    PRIMARY KEY(group_id, epoch, target_code)
);
CREATE TABLE IF NOT EXISTS mls_group_members (
    group_id TEXT NOT NULL,
    user_code TEXT NOT NULL,
    active INTEGER NOT NULL DEFAULT 1,
    joined_epoch INTEGER NOT NULL DEFAULT 0,
    removed_epoch INTEGER,
    PRIMARY KEY(group_id, user_code)
);
CREATE TABLE IF NOT EXISTS attachments (
    attachment_id TEXT PRIMARY KEY,
    room TEXT NOT NULL,
    logical_size INTEGER NOT NULL,
    whole_sha256 TEXT NOT NULL DEFAULT '',
    reference_count INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL,
    committed_at TEXT
);
CREATE TABLE IF NOT EXISTS attachment_uploads (
    upload_id TEXT PRIMARY KEY,
    attachment_id TEXT NOT NULL UNIQUE,
    room TEXT NOT NULL,
    author_code TEXT NOT NULL,
    logical_size INTEGER NOT NULL,
    reserved_bytes INTEGER NOT NULL,
    chunk_size INTEGER NOT NULL,
    expires_at TEXT NOT NULL,
    created_at TEXT NOT NULL,
    FOREIGN KEY (attachment_id) REFERENCES attachments(attachment_id)
);
CREATE TABLE IF NOT EXISTS attachment_chunks (
    upload_id TEXT NOT NULL,
    chunk_index INTEGER NOT NULL,
    cipher_size INTEGER NOT NULL,
    cipher_sha256 TEXT NOT NULL,
    created_at TEXT NOT NULL,
    PRIMARY KEY(upload_id, chunk_index),
    FOREIGN KEY (upload_id) REFERENCES attachment_uploads(upload_id)
);
CREATE TABLE IF NOT EXISTS room_quotas (
    room TEXT PRIMARY KEY,
    reserved_bytes INTEGER NOT NULL DEFAULT 0,
    used_bytes INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_attachment_uploads_expiry
    ON attachment_uploads(expires_at);
CREATE INDEX IF NOT EXISTS idx_attachments_room
    ON attachments(room);
";

fn migrate_lan_rooms(tx: &Connection) -> Result<()> {
    let version: i64 = tx.pragma_query_value(None, "user_version", |row| row.get(0))?;
    if version >= 2 {
        return Ok(());
    }
    tx.execute_batch(
        "CREATE TABLE lan_rooms (
            name TEXT PRIMARY KEY CHECK(name <> 'lobby'),
            owner_code TEXT NOT NULL,
            private INTEGER NOT NULL CHECK(private IN (0, 1)),
            password_hash TEXT CHECK(password_hash IS NULL OR private = 1),
            allowed_members TEXT NOT NULL,
            admins TEXT NOT NULL,
            generation TEXT NOT NULL UNIQUE
        );
        CREATE TABLE lan_mutes (user_code TEXT PRIMARY KEY);",
    )?;
    let legacy: bool = tx.query_row(
        "SELECT EXISTS(SELECT 1 FROM sqlite_schema WHERE type='table' AND name='protected_rooms')",
        [],
        |row| row.get(0),
    )?;
    if legacy {
        {
            let mut statement = tx.prepare(
                "SELECT name,owner_code,password_hash,allowed_members FROM protected_rooms",
            )?;
            let mut rows = statement.query([])?;
            while let Some(row) = rows.next()? {
                let name: String = row.get(0)?;
                validate_room_name(&name)?;
                let owner: String = row.get(1)?;
                ensure!(
                    user_code(&owner)? == owner,
                    "频道 {name} 的房主代码未规范化"
                );
                let hash = String::from_utf8(row.get(2)?)?;
                hash.parse::<bcrypt::HashParts>()
                    .with_context(|| format!("频道 {name} 的密码哈希无效"))?;
                let members: String = row.get(3)?;
                let allowed: HashMap<String, bool> = serde_json::from_str(&members)?;
                for code in allowed.keys() {
                    ensure!(user_code(code)? == *code, "频道 {name} 的成员代码未规范化");
                }
                tx.execute(
                    "INSERT INTO lan_rooms(name,owner_code,private,password_hash,allowed_members,admins,generation)
                     VALUES (?,?,1,?,?,'[]',?)",
                    params![name, owner, hash, members, Uuid::new_v4().to_string()],
                )?;
            }
        }
        tx.execute_batch("DROP TABLE protected_rooms;")?;
    }
    tx.pragma_update(None, "user_version", 2)?;
    Ok(())
}

impl Store {
    pub(super) fn open_lan(path: &Path) -> Result<Self> {
        let mut store = Self::open_mode(path, "lan")?;
        let tx = store
            .0
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        tx.execute_batch(SCHEMA)?;
        for (table, column, migration) in [
            (
                "offline_messages",
                "message_id",
                "ALTER TABLE offline_messages ADD COLUMN message_id TEXT",
            ),
            (
                "mls_group_epochs",
                "proposal_id",
                "ALTER TABLE mls_group_epochs ADD COLUMN proposal_id TEXT NOT NULL DEFAULT ''",
            ),
            (
                "mls_group_welcomes",
                "proposal_id",
                "ALTER TABLE mls_group_welcomes ADD COLUMN proposal_id TEXT NOT NULL DEFAULT ''",
            ),
            (
                "mls_group_welcomes",
                "welcome_digest",
                "ALTER TABLE mls_group_welcomes ADD COLUMN welcome_digest TEXT NOT NULL DEFAULT ''",
            ),
            (
                "mls_group_welcomes",
                "accepted",
                "ALTER TABLE mls_group_welcomes ADD COLUMN accepted INTEGER NOT NULL DEFAULT 1",
            ),
            (
                "mls_group_proposals",
                "action",
                "ALTER TABLE mls_group_proposals ADD COLUMN action TEXT NOT NULL DEFAULT 'add'",
            ),
            (
                "mls_group_proposals",
                "target_code",
                "ALTER TABLE mls_group_proposals ADD COLUMN target_code TEXT NOT NULL DEFAULT ''",
            ),
            (
                "mls_group_proposals",
                "member_snapshot",
                "ALTER TABLE mls_group_proposals ADD COLUMN member_snapshot TEXT NOT NULL DEFAULT '[]'",
            ),
        ] {
            let exists: bool = tx.query_row(
                "SELECT EXISTS(SELECT 1 FROM pragma_table_info(?) WHERE name=?)",
                params![table, column],
                |row| row.get(0),
            )?;
            if !exists {
                tx.execute(migration, [])?;
            }
        }
        tx.execute_batch("CREATE UNIQUE INDEX IF NOT EXISTS idx_offline_messages_message_id ON offline_messages(message_id);")?;
        migrate_lan_rooms(&tx)?;
        tx.commit()?;
        Ok(store)
    }

    pub(super) fn lan_reader(path: &Path) -> Result<Self> {
        let db = Connection::open_with_flags(path, OpenFlags::SQLITE_OPEN_READ_ONLY)?;
        db.busy_timeout(Duration::from_secs(5))?;
        Ok(Self(db))
    }

    pub(super) fn lan_identity(&self, username: &str, display_code: &str) -> Result<Identity> {
        ensure!(
            (1..=32).contains(&username.len()),
            Rejected("Invalid username")
        );
        let code = user_code(display_code)?;
        let normalized = normalize_username(username);
        let existing = self
            .0
            .query_row(
                "SELECT username,user_code,normalized_username,normalized_code FROM accounts
             WHERE normalized_username=? OR normalized_code=?",
                params![normalized, code],
                |row| {
                    Ok((
                        row.get::<_, String>(0)?,
                        row.get::<_, String>(1)?,
                        row.get::<_, String>(2)?,
                        row.get::<_, String>(3)?,
                    ))
                },
            )
            .optional()?;
        if let Some((username, display_code, old_username, old_code)) = existing {
            ensure!(
                normalized == old_username && code == old_code,
                Rejected("Connection identity failed")
            );
            return Ok(Identity {
                username,
                display_code,
                code,
            });
        }
        Ok(Identity {
            username: username.into(),
            display_code: display_code.into(),
            code,
        })
    }

    pub(super) fn approve_identity(&self, identity: &Identity) -> Result<Identity> {
        self.0.execute(
            "INSERT INTO accounts (username,normalized_username,user_code,normalized_code,password_hash,created_at)
             VALUES (?,?,?,?,'',?) ON CONFLICT DO NOTHING",
            params![identity.username, normalize_username(&identity.username), identity.display_code, identity.code, now()],
        )?;
        self.lan_identity(&identity.username, &identity.display_code)
    }

    pub(super) fn host_password(&self, password: &str) -> Result<()> {
        ensure!(
            !password.trim().is_empty() && password.len() <= LOCAL_HOST_PASSWORD_MAX_BYTES,
            "房主密码不能为空，最多 {LOCAL_HOST_PASSWORD_MAX_BYTES} 字节"
        );
        let query = "SELECT password_hash FROM local_host_credential WHERE id=1";
        let stored: Option<Vec<u8>> = self.0.query_row(query, [], |row| row.get(0)).optional()?;
        let stored = match stored {
            Some(hash) => hash,
            None => {
                let hash = password_hash(password)?;
                self.0.execute(
                    "INSERT INTO local_host_credential (id,password_hash) VALUES (1,?) ON CONFLICT DO NOTHING",
                    [hash.as_bytes()],
                )?;
                self.0.query_row(query, [], |row| row.get(0))?
            }
        };
        let hash = std::str::from_utf8(&stored).context("房主密码哈希格式错误")?;
        ensure!(bcrypt::verify(password, hash)?, "房主密码错误");
        Ok(())
    }

    pub(super) fn lan_rooms(&self) -> Result<HashMap<String, Room>> {
        let mut statement = self
            .0
            .prepare("SELECT name,owner_code,private,password_hash,allowed_members,admins,generation FROM lan_rooms")?;
        let mut rows = statement.query([])?;
        let mut rooms = HashMap::new();
        while let Some(row) = rows.next()? {
            let name: String = row.get(0)?;
            validate_room_name(&name)?;
            let owner: String = row.get(1)?;
            let private: bool = row.get(2)?;
            let hash = row.get::<_, Option<String>>(3)?;
            let allowed: HashMap<String, bool> = serde_json::from_str(&row.get::<_, String>(4)?)?;
            let admins: HashSet<String> = serde_json::from_str(&row.get::<_, String>(5)?)?;
            ensure!(
                name != LOBBY && user_code(&owner)? == owner && (private || hash.is_none()),
                "频道 {name} 的数据库记录无效"
            );
            if let Some(hash) = &hash {
                hash.parse::<bcrypt::HashParts>()
                    .with_context(|| format!("频道 {name} 的密码哈希无效"))?;
            }
            for code in allowed.keys().chain(&admins) {
                ensure!(user_code(code)? == *code, "频道 {name} 的成员代码未规范化");
            }
            rooms.insert(
                name,
                Room {
                    owner,
                    password_hash: hash,
                    allowed,
                    admins,
                    private,
                    generation: Uuid::parse_str(&row.get::<_, String>(6)?)?,
                },
            );
        }
        Ok(rooms)
    }

    pub(super) fn save_lan_room(&self, name: &str, room: &Room) -> Result<()> {
        self.0.execute(
            "INSERT INTO lan_rooms (name,owner_code,private,password_hash,allowed_members,admins,generation)
             VALUES (?,?,?,?,?,?,?) ON CONFLICT(name) DO UPDATE SET
                password_hash=excluded.password_hash, allowed_members=excluded.allowed_members, admins=excluded.admins",
            params![name, room.owner, room.private, room.password_hash,
                    serde_json::to_string(&room.allowed)?, serde_json::to_string(&room.admins)?, room.generation.to_string()],
        )?;
        Ok(())
    }

    pub(super) fn is_lan_muted(&self, code: &str) -> Result<bool> {
        Ok(self.0.query_row(
            "SELECT EXISTS(SELECT 1 FROM lan_mutes WHERE user_code=?)",
            [code],
            |row| row.get(0),
        )?)
    }

    pub(super) fn set_lan_muted(&self, code: &str, muted: bool) -> Result<()> {
        if muted {
            self.0.execute(
                "INSERT INTO lan_mutes (user_code) VALUES (?) ON CONFLICT DO NOTHING",
                [code],
            )?;
        } else {
            self.0
                .execute("DELETE FROM lan_mutes WHERE user_code=?", [code])?;
        }
        Ok(())
    }

    pub(super) fn delete_lan_room(&mut self, name: &str) -> Result<()> {
        let tx = self.0.transaction()?;
        for sql in [
            "DELETE FROM chat_messages WHERE kind='room' AND room=?",
            "DELETE FROM attachment_chunks WHERE upload_id IN (SELECT upload_id FROM attachment_uploads WHERE room=?)",
            "DELETE FROM attachment_uploads WHERE room=?",
            "DELETE FROM attachments WHERE room=?",
            "DELETE FROM room_quotas WHERE room=?",
            "DELETE FROM mls_group_epochs WHERE group_id IN (SELECT group_id FROM mls_groups WHERE room=?)",
            "DELETE FROM mls_group_proposals WHERE group_id IN (SELECT group_id FROM mls_groups WHERE room=?)",
            "DELETE FROM mls_group_welcomes WHERE group_id IN (SELECT group_id FROM mls_groups WHERE room=?)",
            "DELETE FROM mls_group_members WHERE group_id IN (SELECT group_id FROM mls_groups WHERE room=?)",
            "DELETE FROM mls_groups WHERE room=?",
            "DELETE FROM lan_rooms WHERE name=?",
        ] {
            tx.execute(sql, [name])?;
        }
        tx.commit()?;
        Ok(())
    }

    pub(super) fn insert_lan_chat(
        &mut self,
        message: &Message,
        kind: &str,
        key: &str,
        author: &str,
        target: &str,
        offline: bool,
    ) -> Result<bool> {
        let tx = self.0.transaction()?;
        let changed = Self::persist_chat(&tx, message, kind, key, author, target, None)?;
        if changed && offline {
            tx.execute(
                "INSERT INTO offline_messages (target_code,sender_username,sender_code,content,message_id,created_at)
                 VALUES (?,?,?,?,?,?)",
                params![target, message.username, message.user_code, message.content, message.message_id, message.created_at],
            )?;
            tx.execute(
                "DELETE FROM offline_messages WHERE target_code=?1 AND id NOT IN
                 (SELECT id FROM offline_messages WHERE target_code=?1 ORDER BY id DESC LIMIT 100)",
                [target],
            )?;
        }
        tx.commit()?;
        Ok(changed)
    }

    pub(super) fn take_lan_private(&mut self, identity: &Identity) -> Result<Vec<Message>> {
        let tx = self.0.transaction()?;
        let messages = {
            let mut statement = tx.prepare(
                "SELECT sender_username,sender_code,content,COALESCE(message_id,''),created_at
                 FROM offline_messages WHERE target_code=? ORDER BY id",
            )?;
            statement
                .query_map([&identity.code], |row| {
                    Ok(Message {
                        kind: "offline_message".into(),
                        username: row.get(0)?,
                        user_code: row.get(1)?,
                        content: row.get(2)?,
                        message_id: row.get(3)?,
                        created_at: row.get(4)?,
                        target_user_code: identity.display_code.clone(),
                        ..Message::default()
                    })
                })?
                .collect::<rusqlite::Result<Vec<_>>>()?
        };
        tx.execute(
            "DELETE FROM offline_messages WHERE target_code=?",
            [&identity.code],
        )?;
        tx.commit()?;
        Ok(messages)
    }
}

fn normalize_username(value: &str) -> String {
    value
        .chars()
        .map(|character| {
            character
                .to_lowercase()
                .next()
                .expect("lowercase character")
        })
        .collect()
}

pub(super) fn password_hash(password: &str) -> Result<String> {
    Ok(bcrypt::hash_with_result(password, 10)?.format_for_version(bcrypt::Version::TwoA))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lan_room_migration_and_writes_are_atomic() -> Result<()> {
        let path = std::env::temp_dir().join(format!("lan-rooms-{}.db", Uuid::new_v4()));
        let legacy = Connection::open(&path)?;
        legacy.execute_batch(
            "CREATE TABLE protected_rooms (
                name TEXT PRIMARY KEY, owner_code TEXT NOT NULL,
                password_hash BLOB NOT NULL, allowed_members TEXT NOT NULL
             );",
        )?;
        let hash = bcrypt::hash("legacy-room-password", 4)?;
        legacy.execute(
            "INSERT INTO protected_rooms VALUES('legacy','owner1',?,?)",
            params![hash.as_bytes(), r#"{"guest1":true}"#],
        )?;
        drop(legacy);

        let store = Store::open_lan(&path)?;
        assert_eq!(
            store
                .0
                .pragma_query_value(None, "user_version", |row| row.get::<_, i64>(0))?,
            2
        );
        let mut room = store
            .lan_rooms()?
            .remove("legacy")
            .context("legacy room missing")?;
        assert!(room.private && room.allowed["guest1"]);
        assert_eq!(room.password_hash.as_deref(), Some(hash.as_str()));
        let generation = room.generation;
        room.admins.insert("guest1".into());
        store.save_lan_room("legacy", &room)?;
        store.set_lan_muted("guest1", true)?;
        drop(store);

        let mut reopened = Store::open_lan(&path)?;
        let db_name = "legacy";
        let mut room = reopened
            .lan_rooms()?
            .remove(db_name)
            .context("persisted room missing")?;
        assert_eq!(room.generation, generation);
        assert!(room.admins.contains("guest1") && reopened.is_lan_muted("guest1")?);
        reopened.0.execute_batch(
            "CREATE TRIGGER fixture_reject_write BEFORE UPDATE ON lan_rooms
             BEGIN SELECT RAISE(ABORT,'fixture write rejected'); END;",
        )?;
        room.admins.clear();
        assert!(reopened.save_lan_room(db_name, &room).is_err());
        assert!(reopened.lan_rooms()?[db_name].admins.contains("guest1"));
        reopened
            .0
            .execute_batch("DROP TRIGGER fixture_reject_write")?;
        reopened.delete_lan_room(db_name)?;
        assert!(!reopened.lan_rooms()?.contains_key(db_name));
        drop(reopened);
        std::fs::remove_file(path)?;
        Ok(())
    }
}
