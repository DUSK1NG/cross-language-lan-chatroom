use std::{
    fs::{self, File, OpenOptions},
    io::{Read, Seek, SeekFrom, Write},
    path::{Path, PathBuf},
};

use anyhow::{Context, Result, ensure};
use lan_chat_protocol::ONLINE_CHUNK_BYTES;
use ring::digest::{Context as Digest, SHA256};
use rusqlite::{Connection, OpenFlags, OptionalExtension, TransactionBehavior, params};
use uuid::Uuid;

use crate::store::Store;

pub(crate) fn migrate_schema(db: &Connection) -> Result<()> {
    let version: i64 = db.pragma_query_value(None, "user_version", |row| row.get(0))?;
    if version < 2 {
        db.execute_batch(
            "ALTER TABLE online_files ADD COLUMN disk_backed INTEGER NOT NULL DEFAULT 0;
             ALTER TABLE online_files ADD COLUMN disk_sha256 TEXT;",
        )?;
        db.pragma_update(None, "user_version", 2)?;
    }
    if version < 3 {
        db.execute_batch(
            "ALTER TABLE online_files ADD COLUMN group_id TEXT;
             ALTER TABLE online_groups ADD COLUMN owner_code TEXT;
             ALTER TABLE online_groups ADD COLUMN announcement TEXT NOT NULL DEFAULT '';
             ALTER TABLE online_groups ADD COLUMN created_at TEXT NOT NULL DEFAULT '';
             ALTER TABLE online_group_members ADD COLUMN role TEXT NOT NULL DEFAULT 'member';
             ALTER TABLE online_group_members ADD COLUMN invited_by TEXT NOT NULL DEFAULT '';
             ALTER TABLE online_group_members ADD COLUMN joined_at TEXT NOT NULL DEFAULT '';
             ALTER TABLE online_group_members ADD COLUMN joined_after INTEGER NOT NULL DEFAULT 0;",
        )?;
        db.pragma_update(None, "user_version", 3)?;
    }
    if version < 4 {
        db.execute_batch(
            "CREATE TABLE online_bot_settings (
                 user_code TEXT PRIMARY KEY, revision INTEGER NOT NULL, settings TEXT NOT NULL
             );",
        )?;
        db.pragma_update(None, "user_version", 4)?;
    }
    if version < 5 {
        db.execute_batch("ALTER TABLE online_bot_settings ADD COLUMN openapi_key BLOB")?;
        db.pragma_update(None, "user_version", 5)?;
    }
    if version < 6 {
        db.execute_batch(
            "CREATE INDEX IF NOT EXISTS idx_chat_messages_pending_target
                 ON chat_messages(target_code, id)
                 WHERE kind='private' AND delivery_state='sent' AND recalled=0;
             CREATE INDEX IF NOT EXISTS idx_chat_messages_pending_created
                 ON chat_messages(created_at, id)
                 WHERE kind='private' AND delivery_state='sent';",
        )?;
        db.pragma_update(None, "user_version", 6)?;
    }
    Ok(())
}

pub(super) fn root(db: &Connection) -> Result<PathBuf> {
    let path = Path::new(db.path().context("在线数据库没有文件路径")?);
    let name = path.file_name().context("在线数据库缺少文件名")?;
    let mut directory = name.to_os_string();
    directory.push(".files");
    Ok(path.with_file_name(directory))
}

pub(super) fn file_path(root: &Path, id: &str) -> Result<PathBuf> {
    ensure!(
        Uuid::parse_str(id).is_ok_and(|uuid| uuid.to_string() == id),
        "数据库中的在线附件编号无效"
    );
    Ok(root.join(format!("file-{id}")))
}

pub(super) fn image_path(root: &Path, reference: &str) -> Result<PathBuf> {
    let valid = reference.strip_prefix("image-").is_some_and(|id| {
        Uuid::parse_str(id).is_ok_and(|uuid| uuid.to_string() == id)
            || id
                .parse::<i64>()
                .is_ok_and(|value| value > 0 && value.to_string() == id)
    });
    ensure!(valid, "数据库中的在线图片文件名无效");
    Ok(root.join(reference))
}

pub(super) fn append(path: &Path, received: i64, bytes: &[u8]) -> Result<()> {
    let mut output = OpenOptions::new()
        .create(true)
        .read(true)
        .write(true)
        .truncate(false)
        .open(path)?;
    ensure!(
        output.metadata()?.len() >= received as u64,
        "暂存文件短于已确认大小"
    );
    output.set_len(received as u64)?;
    output.seek(SeekFrom::Start(received as u64))?;
    output.write_all(bytes)?;
    output.sync_all()?;
    Ok(())
}

pub(super) fn finalize(path: &Path, size: i64) -> Result<String> {
    let staging = path.with_extension("partial");
    if !path.try_exists()? {
        if size == 0 && !staging.try_exists()? {
            File::create_new(&staging)?.sync_all()?;
        }
        let digest = checksum(&staging, size)?;
        fs::rename(staging, path)?;
        return Ok(digest);
    }
    checksum(path, size)
}

pub(super) fn checksum(path: &Path, size: i64) -> Result<String> {
    let mut file = File::open(path).with_context(|| format!("读取在线附件 {}", path.display()))?;
    ensure!(
        size >= 0 && file.metadata()?.len() == size as u64,
        "在线附件大小与数据库记录不一致"
    );
    let mut hash = Digest::new(&SHA256);
    let mut buffer = [0; 32 * 1024];
    loop {
        let count = file.read(&mut buffer)?;
        if count == 0 {
            break;
        }
        hash.update(&buffer[..count]);
    }
    Ok(hash
        .finish()
        .as_ref()
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect())
}

pub(super) fn read_segment(path: &Path, size: i64, offset: i64, expected: &str) -> Result<Vec<u8>> {
    if offset == 0 {
        ensure!(checksum(path, size)? == expected, "在线附件校验不通过");
    }
    let mut file = File::open(path).with_context(|| format!("读取在线附件 {}", path.display()))?;
    ensure!(
        file.metadata()?.len() == size as u64,
        "在线附件大小与数据库记录不一致"
    );
    let mut bytes = vec![0; (size - offset).min(ONLINE_CHUNK_BYTES) as usize];
    file.seek(SeekFrom::Start(offset as u64))?;
    file.read_exact(&mut bytes)?;
    Ok(bytes)
}

pub(crate) fn migrate_storage(database: &Path, dry_run: bool, prune: bool) -> Result<()> {
    let report = Connection::open_with_flags(database, OpenFlags::SQLITE_OPEN_READ_ONLY)?;
    report.pragma_update(None, "query_only", true)?;
    let mode: String = report.query_row("SELECT mode FROM server_mode WHERE id=1", [], |row| {
        row.get(0)
    })?;
    ensure!(mode == "online", "附件迁移需要在线数据库");
    let version: i64 = report.pragma_query_value(None, "user_version", |row| row.get(0))?;
    let count_files = if version < 2 {
        "SELECT COUNT(*),COALESCE(SUM(size),0) FROM online_files WHERE chat_id IS NOT NULL"
    } else {
        "SELECT COUNT(*),COALESCE(SUM(size),0) FROM online_files f WHERE chat_id IS NOT NULL
         AND (disk_backed=0 OR EXISTS (SELECT 1 FROM online_file_chunks c WHERE c.file_id=f.id))"
    };
    let (files, file_bytes): (i64, i64) =
        report.query_row(count_files, [], |row| Ok((row.get(0)?, row.get(1)?)))?;
    let (images, image_bytes): (i64, i64) = report.query_row(
        "SELECT COUNT(*),COALESCE(SUM(length(online_image)),0) FROM chat_messages WHERE online_image IS NOT NULL",
        [], |row| Ok((row.get(0)?, row.get(1)?)),
    )?;
    println!(
        "旧附件文件 {files} 项、图片 {images} 项，原始负载共 {} 字节",
        file_bytes + image_bytes
    );
    drop(report);
    if dry_run {
        return Ok(());
    }
    let mut store = Store::open(database)?;
    let db = &mut store.0;
    let root = root(db)?;
    fs::create_dir_all(&root)?;
    let tx = db.transaction_with_behavior(TransactionBehavior::Immediate)?;
    let integrity: String = tx.pragma_query_value(None, "integrity_check", |row| row.get(0))?;
    ensure!(integrity == "ok", "在线数据库完整性检查失败: {integrity}");
    let file_ids = {
        let mut statement = tx.prepare(
            "SELECT id,size,disk_sha256 FROM online_files WHERE chat_id IS NOT NULL
             AND disk_backed=0 OR (chat_id IS NOT NULL AND EXISTS
                (SELECT 1 FROM online_file_chunks c WHERE c.file_id=online_files.id)) ORDER BY id",
        )?;
        statement
            .query_map([], |row| {
                Ok((
                    row.get::<_, String>(0)?,
                    row.get::<_, i64>(1)?,
                    row.get::<_, Option<String>>(2)?,
                ))
            })?
            .collect::<rusqlite::Result<Vec<_>>>()?
    };
    for (id, size, stored) in file_ids {
        let path = file_path(&root, &id)?;
        let staging = path.with_extension("partial");
        let mut source = Digest::new(&SHA256);
        let mut output = if stored.is_none() && !path.try_exists()? {
            Some(File::create(&staging)?)
        } else {
            None
        };
        let mut offset = 0;
        {
            let mut statement = tx.prepare("SELECT chunk_index,data FROM online_file_chunks WHERE file_id=? ORDER BY chunk_index")?;
            let mut rows = statement.query([&id])?;
            while let Some(row) = rows.next()? {
                let index: i64 = row.get(0)?;
                let bytes: Vec<u8> = row.get(1)?;
                ensure!(
                    index * ONLINE_CHUNK_BYTES == offset
                        && bytes.len() as i64 == (size - offset).min(ONLINE_CHUNK_BYTES),
                    "旧附件 {id} 分块不连续"
                );
                offset += bytes.len() as i64;
                source.update(&bytes);
                if let Some(output) = &mut output {
                    output.write_all(&bytes)?;
                }
            }
        }
        ensure!(offset == size, "旧附件 {id} 长度与数据库不一致");
        if let Some(output) = output {
            output.sync_all()?;
        }
        let hash = finalize(&path, size)?;
        let expected: String = source
            .finish()
            .as_ref()
            .iter()
            .map(|byte| format!("{byte:02x}"))
            .collect();
        ensure!(
            hash == expected && stored.as_ref().is_none_or(|stored| stored == &hash),
            "旧附件 {id} 文件校验失败"
        );
        tx.execute(
            "UPDATE online_files SET disk_backed=1,disk_sha256=? WHERE id=?",
            params![hash, id],
        )?;
        if prune {
            tx.execute("DELETE FROM online_file_chunks WHERE file_id=?", [&id])?;
        }
    }
    let image_ids = {
        let mut statement =
            tx.prepare("SELECT id FROM chat_messages WHERE online_image IS NOT NULL ORDER BY id")?;
        statement
            .query_map([], |row| row.get::<_, i64>(0))?
            .collect::<rusqlite::Result<Vec<_>>>()?
    };
    for id in image_ids {
        let (bytes, stored): (Vec<u8>, Option<String>) = tx.query_row(
            "SELECT online_image,online_image_sha256 FROM chat_messages WHERE id=?",
            [id],
            |row| Ok((row.get(0)?, row.get(1)?)),
        )?;
        let reference = format!("image-{id}");
        let path = image_path(&root, &reference)?;
        if !path.try_exists()? {
            append(&path.with_extension("partial"), 0, &bytes)?;
        }
        let hash = finalize(&path, bytes.len() as i64)?;
        let expected: String = ring::digest::digest(&SHA256, &bytes)
            .as_ref()
            .iter()
            .map(|byte| format!("{byte:02x}"))
            .collect();
        ensure!(
            hash == expected && stored.as_ref().is_none_or(|stored| stored == &hash),
            "旧图片 {id} 文件校验失败"
        );
        tx.execute("UPDATE chat_messages SET online_image_ref=?,online_image_size=?,online_image_sha256=? WHERE id=?", params![reference, bytes.len(), hash, id])?;
        if prune {
            tx.execute(
                "UPDATE chat_messages SET online_image=NULL WHERE id=?",
                [id],
            )?;
        }
    }
    tx.commit()?;
    println!("在线附件迁移完成");
    Ok(())
}

pub(super) fn cleanup_orphans(
    db: &Connection,
    active_images: &[&str],
    min_age: std::time::Duration,
) -> Result<usize> {
    let root = root(db)?;
    if !root.try_exists()? {
        return Ok(0);
    }
    let cutoff = (!min_age.is_zero()).then(|| std::time::SystemTime::now() - min_age);
    let mut removed = 0;
    for entry in fs::read_dir(&root)? {
        let entry = entry?;
        if !entry.file_type()?.is_file() {
            continue;
        }
        let name = entry.file_name();
        let name = name.to_str().context("在线附件目录含非 UTF-8 文件名")?;
        let basename = name.strip_suffix(".partial").unwrap_or(name);
        let used = if let Some(id) = basename.strip_prefix("file-") {
            file_path(&root, id)?;
            db.query_row("SELECT 1 FROM online_files WHERE id=?", [id], |_| Ok(()))
                .optional()?
                .is_some()
        } else if basename.starts_with("image-") {
            image_path(&root, basename)?;
            active_images.contains(&basename)
                || db
                    .query_row(
                        "SELECT 1 FROM chat_messages WHERE online_image_ref=?",
                        [basename],
                        |_| Ok(()),
                    )
                    .optional()?
                    .is_some()
        } else {
            continue;
        };
        if !used {
            // Async maintenance runs outside the Online mutex; preserve recently modified files
            // so in-flight finalized images between memory take and DB insert are never reaped.
            if let Some(cutoff) = cutoff {
                let Ok(metadata) = entry.metadata() else {
                    continue;
                };
                if metadata.modified().is_ok_and(|modified| modified > cutoff) {
                    continue;
                }
            }
            match fs::remove_file(entry.path()) {
                Ok(()) => removed += 1,
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
                Err(error) => return Err(error.into()),
            }
        }
    }
    Ok(removed)
}
