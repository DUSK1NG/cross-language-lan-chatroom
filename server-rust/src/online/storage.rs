use anyhow::{Context, Result, ensure};
use rusqlite::{Connection, OptionalExtension, params};

use super::OnlineLimits;
use crate::store::Rejected;

#[derive(Debug, Default, PartialEq, Eq)]
pub(crate) struct CalibrationStats {
    pub(crate) scanned: usize,
}

const CALIBRATION_SCAN_SQL: &str = "
    SELECT m.id, m.kind, m.sender_code, COALESCE(f.size,m.online_file_size,0),
            length(CAST(m.content AS BLOB)) + COALESCE(m.online_image_size, length(CAST(m.online_image AS BLOB)), 0) + COALESCE(f.size,m.online_file_size,0),
            m.storage_counted
     FROM chat_messages m
     LEFT JOIN online_files f ON f.chat_id=m.id
     WHERE m.id > ?1
     ORDER BY m.id LIMIT ?2";

pub(crate) fn check_budget(
    db: &Connection,
    sender: &str,
    kind: &str,
    incoming_bytes: i64,
    limits: &OnlineLimits,
) -> Result<()> {
    ensure!(incoming_bytes >= 0, Rejected("消息存储大小无效"));
    let global: i64 = db
        .query_row(
            "SELECT used_bytes FROM storage_usage WHERE scope='global' AND sender_code=''",
            [],
            |row| row.get(0),
        )
        .optional()?
        .unwrap_or(0);
    ensure!(
        global.saturating_add(incoming_bytes) <= limits.storage_budget_bytes as i64,
        Rejected("服务器消息存储空间已满")
    );
    if kind == "online_group" {
        let sender_used: i64 = db
            .query_row(
                "SELECT used_bytes FROM storage_usage WHERE scope='group_sender' AND sender_code=?",
                [sender],
                |row| row.get(0),
            )
            .optional()?
            .unwrap_or(0);
        ensure!(
            sender_used.saturating_add(incoming_bytes) <= limits.group_sender_budget_bytes as i64,
            Rejected("群消息存储空间已满")
        );
    }
    if limits.min_disk_free_bytes > 0 {
        let db_path = {
            let mut statement = db.prepare("PRAGMA database_list")?;
            let mut rows = statement.query([])?;
            let mut found = None;
            while let Some(row) = rows.next()? {
                if row.get::<_, String>(1)? == "main" {
                    let candidate: String = row.get(2)?;
                    if !candidate.is_empty() {
                        found = Some(candidate);
                    }
                    break;
                }
            }
            found.context("在线数据库没有文件路径")?
        };
        ensure!(
            fs2::available_space(db_path)? >= limits.min_disk_free_bytes,
            Rejected("服务器磁盘空间不足")
        );
    }
    Ok(())
}

fn add_usage(tx: &Connection, scope: &str, sender: &str, bytes: i64) -> Result<()> {
    tx.execute(
        "INSERT INTO storage_usage(scope, sender_code, used_bytes)
         VALUES (?1, ?2, ?3)
         ON CONFLICT(scope, sender_code) DO UPDATE SET used_bytes = used_bytes + excluded.used_bytes",
        params![scope, sender, bytes],
    )?;
    Ok(())
}

pub(crate) fn calibrate(db: &Connection, batch_size: i64) -> Result<CalibrationStats> {
    ensure!(batch_size > 0, Rejected("校准批次大小须大于 0"));
    db.execute_batch("BEGIN IMMEDIATE")?;
    let result = (|| -> Result<CalibrationStats> {
        let last_id: i64 = db.query_row(
            "SELECT last_id FROM storage_calibration_state WHERE id=1",
            [],
            |row| row.get(0),
        )?;
        let rows: Vec<(i64, String, String, i64, i64, i64)> = {
            let mut statement = db.prepare(CALIBRATION_SCAN_SQL)?;
            statement
                .query_map([last_id, batch_size], |row| {
                    Ok((
                        row.get(0)?,
                        row.get(1)?,
                        row.get(2)?,
                        row.get(3)?,
                        row.get(4)?,
                        row.get(5)?,
                    ))
                })?
                .collect::<rusqlite::Result<_>>()?
        };
        let scanned = rows.len();
        let last_examined = rows.last().map(|row| row.0);
        for (id, kind, sender, file_size, bytes, storage_counted) in rows {
            if storage_counted == 0 {
                db.execute(
                    "UPDATE chat_messages SET online_file_size=? WHERE id=? AND storage_counted=0",
                    params![file_size, id],
                )?;
                add_usage(db, "global", "", bytes)?;
                if kind == "online_group" {
                    add_usage(db, "group_sender", &sender, bytes)?;
                }
                db.execute(
                    "UPDATE chat_messages SET storage_counted=1 WHERE id=? AND storage_counted=0",
                    [id],
                )?;
            }
        }
        if let Some(last_id) = last_examined {
            db.execute(
                "UPDATE storage_calibration_state SET last_id=? WHERE id=1",
                [last_id],
            )?;
        }
        Ok(CalibrationStats { scanned })
    })();
    match result {
        Ok(stats) => {
            if let Err(error) = db.execute_batch("COMMIT") {
                let _ = db.execute_batch("ROLLBACK");
                return Err(error.into());
            }
            Ok(stats)
        }
        Err(error) => {
            let _ = db.execute_batch("ROLLBACK");
            Err(error)
        }
    }
}

#[cfg(test)]
mod tests {
    use std::{
        fs,
        path::{Path, PathBuf},
    };

    use anyhow::Result;
    use base64::{Engine, engine::general_purpose::STANDARD};
    use lan_chat_protocol::Message;
    use rusqlite::{Connection, OptionalExtension, params};
    use tokio::sync::mpsc;
    use uuid::Uuid;

    use crate::{
        online::{Online, OnlineLimits, storage},
        store::{Identity, Store},
    };

    fn temp_root(name: &str) -> Result<PathBuf> {
        let root = std::env::temp_dir().join(format!("lan-chat-{name}-{}", Uuid::new_v4()));
        fs::create_dir_all(&root)?;
        Ok(root)
    }

    fn setup_accounts(root: &Path, limits: OnlineLimits) -> Result<Online> {
        let store = Store::open(&root.join("chat.db"))?;
        for code in ["alice", "bobby", "carol"] {
            store.0.execute(
            "INSERT INTO accounts (username,normalized_username,user_code,normalized_code,password_hash,created_at) VALUES (?1,?1,?1,?1,'fixture','')",
            [code],
        )?;
        }
        Ok(Online::with_limits(store, None, None, limits))
    }

    fn insert_raw_group_message(db: &Connection, sender: &str, content: &str) -> Result<()> {
        db.execute(
        "INSERT INTO chat_messages(message_id,kind,conversation_key,sender_username,sender_code,content,created_at)
         VALUES (?1,'online_group','group-fixture','Fixture',?2,?3,'2026-11-28T00:00:00Z')",
        params![Uuid::new_v4().to_string(), sender, content],
    )?;
        Ok(())
    }

    fn usage(db: &Connection, scope: &str, sender: &str) -> Result<i64> {
        Ok(db
            .query_row(
                "SELECT used_bytes FROM storage_usage WHERE scope=? AND sender_code=?",
                [scope, sender],
                |row| row.get(0),
            )
            .optional()?
            .unwrap_or(0))
    }

    #[test]
    fn group_budget_rejects_global_and_sender_overflow_without_pruning() -> Result<()> {
        let root = temp_root("storage-budget")?;
        let online = setup_accounts(
            &root,
            OnlineLimits {
                storage_budget_bytes: 1024,
                group_sender_budget_bytes: 512,
                min_disk_free_bytes: 0,
                ..OnlineLimits::default()
            },
        )?;
        let db = &online.store.0;
        assert!(storage::check_budget(db, "alice", "online_group", 600, &online.limits).is_err());
        insert_raw_group_message(db, "alice", &"a".repeat(400))?;
        assert!(storage::check_budget(db, "alice", "online_group", 200, &online.limits).is_err());
        insert_raw_group_message(db, "alice", &"b".repeat(400))?;
        assert!(storage::check_budget(db, "alice", "online_group", 300, &online.limits).is_err());
        let count: i64 = db.query_row("SELECT COUNT(*) FROM chat_messages", [], |r| r.get(0))?;
        assert_eq!(count, 2);
        drop(online);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn budget_rejects_negative_incoming_bytes() -> Result<()> {
        let root = temp_root("storage-negative")?;
        let online = setup_accounts(&root, OnlineLimits::default())?;
        assert!(
            storage::check_budget(&online.store.0, "alice", "online_group", -1, &online.limits)
                .is_err()
        );
        drop(online);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn budget_uses_the_main_database_for_disk_checks() -> Result<()> {
        let root = temp_root("storage-disk")?;
        let online = setup_accounts(
            &root,
            OnlineLimits {
                min_disk_free_bytes: u64::MAX,
                ..OnlineLimits::default()
            },
        )?;
        let error = storage::check_budget(&online.store.0, "alice", "private", 0, &online.limits)
            .expect_err("impossible free-space requirement must reject");
        assert!(
            error.to_string().contains("服务器磁盘空间不足"),
            "unexpected disk budget error: {error:#}"
        );
        drop(online);

        let memory = Connection::open_in_memory()?;
        memory.execute_batch(
        "CREATE TABLE storage_usage (
             scope TEXT NOT NULL, sender_code TEXT NOT NULL DEFAULT '', used_bytes INTEGER NOT NULL DEFAULT 0,
             PRIMARY KEY (scope, sender_code)
         );",
    )?;
        let error = storage::check_budget(
            &memory,
            "alice",
            "private",
            0,
            &OnlineLimits {
                min_disk_free_bytes: 1,
                ..OnlineLimits::default()
            },
        )
        .expect_err("in-memory main database has no filesystem path");
        assert!(error.to_string().contains("在线数据库没有文件路径"));
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn counted_message_insert_update_delete_adjust_usage() -> Result<()> {
        let root = temp_root("storage-triggers")?;
        let online = setup_accounts(&root, OnlineLimits::default())?;
        let db = &online.store.0;
        insert_raw_group_message(db, "alice", &"a".repeat(400))?;
        let counted: i64 =
            db.query_row("SELECT storage_counted FROM chat_messages", [], |row| {
                row.get(0)
            })?;
        assert_eq!(counted, 1);
        assert_eq!(usage(db, "global", "")?, 400);
        assert_eq!(usage(db, "group_sender", "alice")?, 400);

        db.execute("UPDATE chat_messages SET content='short'", [])?;
        assert_eq!(usage(db, "global", "")?, 5);
        assert_eq!(usage(db, "group_sender", "alice")?, 5);
        db.execute(
            "UPDATE chat_messages SET online_image_size=7,online_file_size=11",
            [],
        )?;
        assert_eq!(usage(db, "global", "")?, 23);
        assert_eq!(usage(db, "group_sender", "alice")?, 23);
        db.execute("DELETE FROM chat_messages", [])?;
        assert_eq!(usage(db, "global", "")?, 0);
        assert_eq!(usage(db, "group_sender", "alice")?, 0);
        drop(online);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn unicode_bytes_are_counted_by_triggers_and_calibration() -> Result<()> {
        let root = temp_root("storage-unicode")?;
        let online = setup_accounts(&root, OnlineLimits::default())?;
        let db = &online.store.0;
        insert_raw_group_message(db, "alice", "é🙂")?;
        assert_eq!(usage(db, "global", "")?, 6);
        assert_eq!(usage(db, "group_sender", "alice")?, 6);

        db.execute_batch("DROP TRIGGER chat_messages_storage_insert;")?;
        insert_raw_group_message(db, "alice", "界🙂")?;
        let stats = storage::calibrate(db, 1000)?;
        assert_eq!(stats.scanned, 2);
        assert_eq!(usage(db, "global", "")?, 13);
        assert_eq!(usage(db, "group_sender", "alice")?, 13);

        db.execute(
            "UPDATE chat_messages SET content='界' WHERE content='é🙂'",
            [],
        )?;
        assert_eq!(usage(db, "global", "")?, 10);
        assert_eq!(usage(db, "group_sender", "alice")?, 10);
        db.execute("DELETE FROM chat_messages WHERE content='界'", [])?;
        assert_eq!(usage(db, "global", "")?, 7);
        assert_eq!(usage(db, "group_sender", "alice")?, 7);
        db.execute("DELETE FROM chat_messages", [])?;
        assert_eq!(usage(db, "global", "")?, 0);
        assert_eq!(usage(db, "group_sender", "alice")?, 0);
        drop(online);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn legacy_inline_image_bytes_fall_back_to_blob_length() -> Result<()> {
        let root = temp_root("storage-legacy-image")?;
        let online = setup_accounts(&root, OnlineLimits::default())?;
        let db = &online.store.0;
        db.execute(
            "INSERT INTO chat_messages
                (message_id,kind,conversation_key,sender_username,sender_code,content,
                 online_image,online_image_size,online_file_size,created_at)
             VALUES (?1,'online_group','group-fixture','Fixture','alice','',
                     ?2,NULL,2,'2026-11-28T00:00:00Z')",
            params![Uuid::new_v4().to_string(), &b"12345"[..]],
        )?;
        assert_eq!(usage(db, "global", "")?, 7);
        assert_eq!(usage(db, "group_sender", "alice")?, 7);

        db.execute("UPDATE chat_messages SET online_image=?1", [&b"123"[..]])?;
        assert_eq!(usage(db, "global", "")?, 5);
        assert_eq!(usage(db, "group_sender", "alice")?, 5);

        db.execute_batch("DROP TRIGGER chat_messages_storage_insert;")?;
        db.execute(
            "INSERT INTO chat_messages
                (message_id,kind,conversation_key,sender_username,sender_code,content,
                 online_image,online_image_size,created_at)
             VALUES (?1,'online_group','group-fixture','Fixture','alice','',?2,NULL,'2026-11-28T00:00:00Z')",
            params![Uuid::new_v4().to_string(), &b"123456"[..]],
        )?;
        let stats = storage::calibrate(db, 1000)?;
        assert_eq!(stats.scanned, 2);
        assert_eq!(usage(db, "global", "")?, 11);
        assert_eq!(usage(db, "group_sender", "alice")?, 11);

        db.execute("DELETE FROM chat_messages", [])?;
        assert_eq!(usage(db, "global", "")?, 0);
        assert_eq!(usage(db, "group_sender", "alice")?, 0);
        drop(online);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn calibration_counts_an_existing_row_once_and_backfills_its_file_size() -> Result<()> {
        let root = temp_root("storage-calibrate")?;
        let path = root.join("chat.db");
        let online = setup_accounts(&root, OnlineLimits::default())?;
        let db = &online.store.0;
        db.execute_batch("DROP TRIGGER chat_messages_storage_insert;")?;
        let file_id = Uuid::new_v4().to_string();
        let chat_id: i64 = db.query_row(
        "INSERT INTO chat_messages(message_id,kind,conversation_key,sender_username,sender_code,content,created_at)
         VALUES (?1,'online_group','legacy-group','Fixture','alice','legacy','2026-11-28T00:00:00Z') RETURNING id",
        [&file_id],
        |row| row.get(0),
    )?;
        db.execute(
            "INSERT INTO online_files(id,owner,recipient,name,size,received,expires,chat_id)
         VALUES (?1,'alice','','legacy.bin',123,123,0,?2)",
            params![file_id, chat_id],
        )?;
        drop(online);

        // Reopening migrates the schema but must not synchronously rebuild the ledger.
        let store = Store::open(&path)?;
        let db = &store.0;
        let before: (i64, i64) = db.query_row(
            "SELECT online_file_size,storage_counted FROM chat_messages",
            [],
            |row| Ok((row.get(0)?, row.get(1)?)),
        )?;
        assert_eq!(before, (0, 0));
        assert_eq!(usage(db, "global", "")?, 0);
        assert_eq!(usage(db, "group_sender", "alice")?, 0);

        let first = storage::calibrate(db, 1000)?;
        assert_eq!(first.scanned, 1);
        let after: (i64, i64) = db.query_row(
            "SELECT online_file_size,storage_counted FROM chat_messages",
            [],
            |row| Ok((row.get(0)?, row.get(1)?)),
        )?;
        assert_eq!(after, (123, 1));
        assert_eq!(usage(db, "global", "")?, 129);
        assert_eq!(usage(db, "group_sender", "alice")?, 129);

        let second = storage::calibrate(db, 1000)?;
        assert_eq!(second.scanned, 0);
        assert_eq!(usage(db, "global", "")?, 129);
        assert_eq!(usage(db, "group_sender", "alice")?, 129);
        drop(store);

        let reopened = Store::open(&path)?;
        assert_eq!(usage(&reopened.0, "global", "")?, 129);
        assert_eq!(usage(&reopened.0, "group_sender", "alice")?, 129);
        drop(reopened);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn calibration_processes_only_the_requested_batch_size() -> Result<()> {
        let root = temp_root("storage-batch")?;
        let online = setup_accounts(&root, OnlineLimits::default())?;
        let db = &online.store.0;
        db.execute_batch("DROP TRIGGER chat_messages_storage_insert;")?;
        for _ in 0..3 {
            insert_raw_group_message(db, "alice", &"x".repeat(10))?;
        }
        let stats = storage::calibrate(db, 2)?;
        assert_eq!(stats.scanned, 2);
        let counts: (i64, i64) = db.query_row(
            "SELECT SUM(storage_counted),COUNT(*) FROM chat_messages",
            [],
            |row| Ok((row.get(0)?, row.get(1)?)),
        )?;
        assert_eq!(counts, (2, 3));
        assert_eq!(usage(db, "global", "")?, 20);
        assert_eq!(usage(db, "group_sender", "alice")?, 20);
        drop(online);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn calibration_keyset_cursor_counts_scans_and_repairs_second_pass_zero_bytes() -> Result<()> {
        let root = temp_root("storage-calibration-cursor")?;
        let path = root.join("chat.db");
        let online = setup_accounts(&root, OnlineLimits::default())?;
        let db = &online.store.0;
        insert_raw_group_message(db, "alice", &"a".repeat(10))?;
        insert_raw_group_message(db, "alice", &"b".repeat(10))?;
        db.execute_batch("DROP TRIGGER chat_messages_storage_insert;")?;
        insert_raw_group_message(db, "alice", "")?;

        let first = storage::calibrate(db, 2)?;
        assert_eq!(first.scanned, 2);
        let ids: Vec<i64> = db
            .prepare("SELECT id FROM chat_messages ORDER BY id")?
            .query_map([], |row| row.get(0))?
            .collect::<rusqlite::Result<_>>()?;
        let cursor: i64 = db.query_row(
            "SELECT last_id FROM storage_calibration_state WHERE id=1",
            [],
            |row| row.get(0),
        )?;
        assert_eq!(cursor, ids[1]);
        assert_eq!(usage(db, "global", "")?, 20);
        drop(online);

        let store = Store::open(&path)?;
        let db = &store.0;
        let second = storage::calibrate(db, 2)?;
        assert_eq!(second.scanned, 1);
        let counted: (i64, i64) = db.query_row(
            "SELECT COUNT(*),SUM(storage_counted) FROM chat_messages",
            [],
            |row| Ok((row.get(0)?, row.get(1)?)),
        )?;
        assert_eq!(counted, (3, 3));
        assert_eq!(usage(db, "global", "")?, 20);
        assert_eq!(usage(db, "group_sender", "alice")?, 20);
        let cursor: i64 = db.query_row(
            "SELECT last_id FROM storage_calibration_state WHERE id=1",
            [],
            |row| row.get(0),
        )?;
        assert_eq!(cursor, ids[2]);
        drop(store);

        let reopened = Store::open(&path)?;
        let third = storage::calibrate(&reopened.0, 2)?;
        assert_eq!(third.scanned, 0);
        let cursor: i64 = reopened.0.query_row(
            "SELECT last_id FROM storage_calibration_state WHERE id=1",
            [],
            |row| row.get(0),
        )?;
        assert_eq!(cursor, ids[2]);
        assert_eq!(usage(&reopened.0, "global", "")?, 20);
        drop(reopened);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn calibration_query_plan_uses_a_primary_key_keyset_search() -> Result<()> {
        let root = temp_root("storage-calibration-plan")?;
        let online = setup_accounts(&root, OnlineLimits::default())?;
        let db = &online.store.0;
        let sql = super::CALIBRATION_SCAN_SQL
            .replace("?1", "0")
            .replace("?2", "100");
        let plan: Vec<String> = db
            .prepare(&format!("EXPLAIN QUERY PLAN {sql}"))?
            .query_map([], |row| row.get(3))?
            .collect::<rusqlite::Result<_>>()?;
        assert!(
            plan.iter()
                .any(|detail| detail.contains("SEARCH m USING INTEGER PRIMARY KEY (rowid>?)")),
            "expected a primary-key keyset search, got: {plan:?}"
        );
        assert!(
            !plan.iter().any(|detail| detail == "SCAN m"),
            "calibration must not scan the counted prefix, got: {plan:?}"
        );
        drop(online);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn calibration_rejects_non_positive_batch_sizes() -> Result<()> {
        let root = temp_root("storage-batch-invalid")?;
        let online = setup_accounts(&root, OnlineLimits::default())?;
        assert!(storage::calibrate(&online.store.0, 0).is_err());
        assert!(storage::calibrate(&online.store.0, -1).is_err());
        drop(online);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn file_commit_stores_and_counts_the_actual_file_size() -> Result<()> {
        let root = temp_root("storage-file-commit")?;
        let mut online = setup_accounts(&root, OnlineLimits::default())?;
        let group_id = Uuid::new_v4().to_string();
        online.store.0.execute(
            "INSERT INTO online_groups(id,name) VALUES (?1,'fixture')",
            [&group_id],
        )?;
        online.store.0.execute(
            "INSERT INTO online_group_members(group_id,user_code) VALUES (?1,'alice')",
            [&group_id],
        )?;
        let peer_id = Uuid::new_v4();
        let (sender, mut receiver) = mpsc::channel(16);
        online.register(
            peer_id,
            Identity {
                username: "alice".into(),
                display_code: "alice".into(),
                code: "alice".into(),
            },
            sender,
            true,
            true,
        )?;
        while receiver.try_recv().is_ok() {}

        online.command(
            peer_id,
            Message {
                kind: "dm_file_init".into(),
                group_id: group_id.clone(),
                file_name: "a.txt".into(),
                logical_size: 5,
                command_id: "init".into(),
                ..Message::default()
            },
        );
        let init = receiver.try_recv()?;
        assert_eq!(init.kind, "dm_file_init");
        let file_id = init.attachment_id;
        online.command(
            peer_id,
            Message {
                kind: "dm_file_chunk".into(),
                group_id: group_id.clone(),
                attachment_id: file_id.clone(),
                chunk_index: Some(0),
                file_data: STANDARD.encode(b"hello"),
                command_id: "chunk".into(),
                ..Message::default()
            },
        );
        online.command(
            peer_id,
            Message {
                kind: "dm_file_commit".into(),
                group_id,
                attachment_id: file_id.clone(),
                command_id: "commit".into(),
                ..Message::default()
            },
        );
        let stored: (i64, i64) = online.store.0.query_row(
            "SELECT online_file_size,storage_counted FROM chat_messages WHERE message_id=?",
            [&file_id],
            |row| Ok((row.get(0)?, row.get(1)?)),
        )?;
        assert_eq!(stored, (5, 1));
        assert_eq!(usage(&online.store.0, "global", "")?, 19);
        assert_eq!(usage(&online.store.0, "group_sender", "alice")?, 19);
        drop(online);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn uncounted_message_updates_and_deletes_leave_usage_untouched() -> Result<()> {
        let root = temp_root("storage-uncounted-triggers")?;
        let online = setup_accounts(&root, OnlineLimits::default())?;
        let db = &online.store.0;
        db.execute_batch("DROP TRIGGER chat_messages_storage_insert;")?;
        insert_raw_group_message(db, "alice", &"x".repeat(10))?;
        db.execute(
            "INSERT INTO storage_usage(scope,sender_code,used_bytes)
         VALUES ('global','',5),('group_sender','alice',7)",
            [],
        )?;
        db.execute(
            "UPDATE chat_messages SET content='updated content',online_file_size=13",
            [],
        )?;
        db.execute("DELETE FROM chat_messages", [])?;
        assert_eq!(usage(db, "global", "")?, 5);
        assert_eq!(usage(db, "group_sender", "alice")?, 7);
        drop(online);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn reopening_storage_schema_replaces_legacy_triggers_and_restores_calibration_state()
    -> Result<()> {
        let root = temp_root("storage-schema-upgrade")?;
        let path = root.join("chat.db");
        let online = setup_accounts(&root, OnlineLimits::default())?;
        online.store.0.execute_batch(
            "DROP TRIGGER chat_messages_storage_insert;
             DROP TRIGGER chat_messages_storage_update;
             DROP TRIGGER chat_messages_storage_delete;
             DROP TABLE storage_calibration_state;
             CREATE TRIGGER chat_messages_storage_insert
             AFTER INSERT ON chat_messages BEGIN
                 INSERT INTO storage_usage(scope, sender_code, used_bytes)
                 VALUES ('global', '', length(NEW.content) + COALESCE(NEW.online_image_size,0) + COALESCE(NEW.online_file_size,0))
                 ON CONFLICT(scope, sender_code) DO UPDATE SET used_bytes = used_bytes + excluded.used_bytes;
                 INSERT INTO storage_usage(scope, sender_code, used_bytes)
                 SELECT 'group_sender', NEW.sender_code, length(NEW.content) + COALESCE(NEW.online_image_size,0) + COALESCE(NEW.online_file_size,0)
                 WHERE NEW.kind='online_group'
                 ON CONFLICT(scope, sender_code) DO UPDATE SET used_bytes = used_bytes + excluded.used_bytes;
                 UPDATE chat_messages SET storage_counted=1 WHERE id=NEW.id;
             END;
             CREATE TRIGGER chat_messages_storage_update
             AFTER UPDATE OF content, online_image_size, online_file_size ON chat_messages
             WHEN OLD.storage_counted=1 BEGIN
                 UPDATE storage_usage
                 SET used_bytes = MAX(0, used_bytes - (length(OLD.content) + COALESCE(OLD.online_image_size,0) + COALESCE(OLD.online_file_size,0))
                     + (length(NEW.content) + COALESCE(NEW.online_image_size,0) + COALESCE(NEW.online_file_size,0)))
                 WHERE scope='global' AND sender_code='';
                 UPDATE storage_usage
                 SET used_bytes = MAX(0, used_bytes - (length(OLD.content) + COALESCE(OLD.online_image_size,0) + COALESCE(OLD.online_file_size,0))
                     + (length(NEW.content) + COALESCE(NEW.online_image_size,0) + COALESCE(NEW.online_file_size,0)))
                 WHERE scope='group_sender' AND sender_code=OLD.sender_code AND OLD.kind='online_group';
             END;
             CREATE TRIGGER chat_messages_storage_delete
             AFTER DELETE ON chat_messages WHEN OLD.storage_counted=1 BEGIN
                 UPDATE storage_usage SET used_bytes = MAX(0, used_bytes - (length(OLD.content) + COALESCE(OLD.online_image_size,0) + COALESCE(OLD.online_file_size,0)))
                 WHERE scope='global' AND sender_code='';
                 UPDATE storage_usage SET used_bytes = MAX(0, used_bytes - (length(OLD.content) + COALESCE(OLD.online_image_size,0) + COALESCE(OLD.online_file_size,0)))
                 WHERE scope='group_sender' AND sender_code=OLD.sender_code AND OLD.kind='online_group';
             END;",
        )?;
        drop(online);

        let store = Store::open(&path)?;
        let db = &store.0;
        let cursor: i64 = db.query_row(
            "SELECT last_id FROM storage_calibration_state WHERE id=1",
            [],
            |row| row.get(0),
        )?;
        assert_eq!(cursor, 0);
        db.execute(
            "INSERT INTO chat_messages
                (message_id,kind,conversation_key,sender_username,sender_code,content,
                 online_image,online_image_size,online_file_size,created_at)
             VALUES (?1,'online_group','group-fixture','Fixture','alice','é🙂',
                     ?2,NULL,2,'2026-11-28T00:00:00Z')",
            params![Uuid::new_v4().to_string(), &b"12345"[..]],
        )?;
        assert_eq!(usage(db, "global", "")?, 13);
        assert_eq!(usage(db, "group_sender", "alice")?, 13);

        db.execute(
            "UPDATE chat_messages SET content='界',online_image=?1",
            [&b"123"[..]],
        )?;
        assert_eq!(usage(db, "global", "")?, 8);
        assert_eq!(usage(db, "group_sender", "alice")?, 8);
        db.execute("DELETE FROM chat_messages", [])?;
        assert_eq!(usage(db, "global", "")?, 0);
        assert_eq!(usage(db, "group_sender", "alice")?, 0);
        drop(store);
        fs::remove_dir_all(root)?;
        Ok(())
    }

    #[test]
    fn legacy_schema_migration_is_additive_idempotent_and_does_not_rebuild_usage() -> Result<()> {
        let root = temp_root("storage-migration")?;
        let path = root.join("chat.db");
        let legacy = Connection::open(&path)?;
        legacy.execute_batch(
        "CREATE TABLE accounts (
             username TEXT NOT NULL, normalized_username TEXT NOT NULL UNIQUE,
             user_code TEXT NOT NULL, normalized_code TEXT NOT NULL UNIQUE,
             password_hash TEXT NOT NULL, created_at TEXT NOT NULL
         );
         CREATE TABLE server_mode (id INTEGER PRIMARY KEY CHECK(id=1), mode TEXT NOT NULL);
         INSERT INTO server_mode VALUES (1,'online');
         CREATE TABLE chat_messages (
             id INTEGER PRIMARY KEY AUTOINCREMENT, message_id TEXT NOT NULL, kind TEXT NOT NULL,
             conversation_key TEXT NOT NULL, room TEXT, sender_username TEXT NOT NULL,
             sender_code TEXT NOT NULL, target_code TEXT, content TEXT NOT NULL, crypto_json TEXT,
             delivery_state TEXT NOT NULL DEFAULT 'sent', created_at TEXT NOT NULL,
             recalled INTEGER NOT NULL DEFAULT 0, online_image BLOB, online_image_ref TEXT,
             online_image_size INTEGER, online_image_sha256 TEXT,
             revision INTEGER NOT NULL DEFAULT 0,
             UNIQUE(sender_code, conversation_key, message_id)
         );
         INSERT INTO chat_messages(message_id,kind,conversation_key,sender_username,sender_code,content,created_at)
         VALUES ('legacy','online_group','legacy-group','Fixture','alice','legacy','2026-11-28T00:00:00Z');",
    )?;
        drop(legacy);

        let store = Store::open(&path)?;
        let db = &store.0;
        let columns: Vec<String> = db
            .prepare("SELECT name FROM pragma_table_info('chat_messages')")?
            .query_map([], |row| row.get(0))?
            .collect::<rusqlite::Result<_>>()?;
        assert!(columns.iter().any(|name| name == "online_file_size"));
        assert!(columns.iter().any(|name| name == "storage_counted"));
        let legacy_row: (i64, i64) = db.query_row(
            "SELECT online_file_size,storage_counted FROM chat_messages WHERE message_id='legacy'",
            [],
            |row| Ok((row.get(0)?, row.get(1)?)),
        )?;
        assert_eq!(legacy_row, (0, 0));
        assert_eq!(usage(db, "global", "")?, 0);
        assert_eq!(usage(db, "group_sender", "alice")?, 0);
        insert_raw_group_message(db, "alice", &"n".repeat(400))?;
        assert_eq!(usage(db, "global", "")?, 400);
        drop(store);

        let reopened = Store::open(&path)?;
        assert_eq!(usage(&reopened.0, "global", "")?, 400);
        assert_eq!(usage(&reopened.0, "group_sender", "alice")?, 400);
        let stats = storage::calibrate(&reopened.0, 1000)?;
        assert_eq!(stats.scanned, 2);
        assert_eq!(usage(&reopened.0, "global", "")?, 406);
        assert_eq!(usage(&reopened.0, "group_sender", "alice")?, 406);
        drop(reopened);
        fs::remove_dir_all(root)?;
        Ok(())
    }
}
