use std::{fs, path::Path, time::Duration};

use anyhow::{Context, Result, ensure};
use base64::{Engine, engine::general_purpose::STANDARD as BASE64};
use chrono::{DateTime, SecondsFormat, TimeDelta, Utc};
use image::{ImageFormat, ImageReader};
use lan_chat_protocol::{
    MAX_ONLINE_FILE_BYTES, MAX_ONLINE_IMAGE_BYTES, Message, ONLINE_CHUNK_BYTES,
    is_legacy_generated_message_id,
};
use rusqlite::{Connection, OpenFlags, OptionalExtension, TransactionBehavior, params};
use uuid::Uuid;

use super::{Online, files, private_target};
use crate::store::{Identity, Rejected, Store, conversation, now, user_code};

const FILE_ACCESS: Rejected = Rejected("附件不存在或你没有访问权限");
const FILE_CHUNK: Rejected = Rejected("文件分块顺序或长度无效");
const MAX_FILE_CHUNKS: i64 = (MAX_ONLINE_FILE_BYTES - 1) / ONLINE_CHUNK_BYTES + 1;
const IMAGE_REQUEST: Rejected = Rejected("图片分片的消息编号、收件人、大小或序号无效");
const IMAGE_READ: Rejected = Rejected("图片读取的会话、消息编号或序号无效");
const IMAGE_FORMAT: Rejected = Rejected("图片须为 JPEG 或 PNG，宽高不超过 4096 像素");
const UNDELIVERED_FULL: Rejected = Rejected("对方暂不可接收新的私信，请稍后再试");
const UNDELIVERED_BYTES_FULL: Rejected = Rejected("对方待接收的消息数据已达上限，请稍后再试");
const IMAGE_BUDGET_FULL: Rejected = Rejected("服务器图片存储空间已满，请稍后再试");
const TTL_CLEANUP_BATCH: usize = 100;
const FILE_CLEANUP_BATCH: usize = 200;

pub(super) struct ImageUpload {
    message_id: String,
    target: String,
    pub(super) group_id: String,
    size: i64,
    offset: i64,
    pub(super) reference: String,
}

pub(super) struct PublishedImage {
    reference: String,
    digest: String,
    pub size: i64,
}

pub(super) enum InsertOutcome {
    Inserted,
    Duplicate { delivery_state: String },
}

/// 每个目标待投递私信的配额：条数与字节数各自独立，0 表示该项不限制。
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) struct Quota {
    pub(super) limit: usize,
    pub(super) bytes: usize,
}

pub(super) struct OnlineMessage<'a> {
    pub(super) message: &'a Message,
    pub(super) kind: &'a str,
    pub(super) key: &'a str,
    pub(super) author: &'a str,
    pub(super) target: &'a str,
    pub(super) image: Option<&'a PublishedImage>,
    // Task 7 uses this declared payload size for transactional budget checks.
    #[allow(dead_code)]
    pub(super) file_size: i64,
    pub(super) undelivered: Quota,
}

#[derive(Default, Debug, Clone, Copy, PartialEq, Eq)]
pub struct CleanupStats {
    pub deleted_messages: usize,
    pub deleted_file_rows: usize,
    pub deleted_chunks: usize,
    pub deleted_orphan_files: usize,
    pub skipped_invalid_timestamps: usize,
}

fn remove_if_exists(path: &Path) {
    if let Err(error) = fs::remove_file(path)
        && error.kind() != std::io::ErrorKind::NotFound
    {
        eprintln!(
            "remove staging/orphan file {} failed: {error}",
            path.display()
        );
    }
}

fn inspect_image(staging: &Path) -> Result<()> {
    let image = ImageReader::open(staging)?
        .with_guessed_format()
        .map_err(|_| IMAGE_FORMAT)?;
    ensure!(
        matches!(image.format(), Some(ImageFormat::Jpeg | ImageFormat::Png)),
        IMAGE_FORMAT
    );
    let (width, height) = image.into_dimensions().map_err(|_| IMAGE_FORMAT)?;
    ensure!(
        (1..=4096).contains(&width) && (1..=4096).contains(&height),
        IMAGE_FORMAT
    );
    Ok(())
}

impl Online {
    pub(super) fn receive_image(
        &mut self,
        id: Uuid,
        identity: &Identity,
        request: &Message,
    ) -> Result<()> {
        let group = if request.group_id.is_empty() {
            None
        } else {
            ensure!(self.peers[&id].group_attachments, FILE_ACCESS);
            Some(self.store.group_for(&identity.code, &request.group_id)?)
        };
        let target = if group.is_none() {
            private_target(identity, request)?
        } else {
            String::new()
        };
        let index = request.chunk_index.unwrap_or(0);
        ensure!(
            (1..=64).contains(&request.message_id.len())
                && !is_legacy_generated_message_id(&request.message_id)
                && (1..=MAX_ONLINE_IMAGE_BYTES).contains(&request.image_size)
                && (0..=(request.image_size - 1) / ONLINE_CHUNK_BYTES).contains(&index),
            IMAGE_REQUEST
        );
        let offset = index * ONLINE_CHUNK_BYTES;
        let invalid_length = Rejected("图片分片长度与声明不符");
        let data = BASE64
            .decode(&request.image_data)
            .map_err(|_| invalid_length)?;
        ensure!(
            data.len() as i64 == (request.image_size - offset).min(ONLINE_CHUNK_BYTES),
            invalid_length
        );

        let root = files::root(&self.store.0)?;
        if index == 0 {
            ensure!(
                self.peers.get(&id).is_some_and(|peer| peer.image.is_none()),
                Rejected("当前连接已有进行中的图片上传")
            );
            if group.is_none() {
                self.store.account(&target)?;
                let key = conversation(&identity.code, &target);
                if Store::private_message_state(
                    &self.store.0,
                    &key,
                    &identity.code,
                    &request.message_id,
                )?
                .is_none()
                {
                    Store::check_undelivered_quota(
                        &self.store.0,
                        &target,
                        self.limits.undelivered_quota(),
                    )?;
                }
            }
            let upload = ImageUpload {
                message_id: request.message_id.clone(),
                target: target.clone(),
                group_id: request.group_id.clone(),
                size: request.image_size,
                offset: 0,
                reference: format!("image-{}", Uuid::new_v4()),
            };
            let path = files::image_path(&root, &upload.reference)?;
            let staging = path.with_extension("partial");
            fs::create_dir_all(&root)?;
            if let Err(error) = files::append(&staging, 0, &data) {
                remove_if_exists(&staging);
                return Err(error);
            }
            let peer = self.peers.get_mut(&id).expect("registered client");
            let mut upload = upload;
            upload.offset = data.len() as i64;
            if upload.offset < upload.size {
                peer.image = Some(upload);
                return Ok(());
            }
            return self.finish_image(id, identity, group.as_ref(), upload, &path, &staging);
        }

        let completed = {
            let peer = self.peers.get_mut(&id).expect("registered client");
            let upload = peer
                .image
                .as_mut()
                .filter(|upload| {
                    upload.message_id == request.message_id
                        && upload.target == target
                        && upload.group_id == request.group_id
                        && upload.size == request.image_size
                        && upload.offset == offset
                })
                .ok_or(Rejected("图片分片与当前上传不匹配"))?;
            let path = files::image_path(&root, &upload.reference)?;
            let staging = path.with_extension("partial");
            fs::create_dir_all(&root)?;
            files::append(&staging, offset, &data)?;
            upload.offset += data.len() as i64;
            if upload.offset < upload.size {
                None
            } else {
                Some((peer.image.take().expect("completed upload"), path, staging))
            }
        };
        if let Some((upload, path, staging)) = completed {
            return self.finish_image(id, identity, group.as_ref(), upload, &path, &staging);
        }
        Ok(())
    }

    fn finish_image(
        &mut self,
        id: Uuid,
        identity: &Identity,
        group: Option<&lan_chat_protocol::OnlineGroup>,
        upload: ImageUpload,
        path: &Path,
        staging: &Path,
    ) -> Result<()> {
        if let Err(error) = inspect_image(staging) {
            remove_if_exists(staging);
            return Err(error);
        }
        let budget = self.limits.image_budget_bytes;
        if budget > 0 {
            let used: i64 = self.store.0.query_row(
                "SELECT COALESCE(SUM(COALESCE(online_image_size, length(CAST(online_image AS BLOB)), 0)),0) FROM chat_messages",
                [],
                |row| row.get(0),
            )?;
            if used.saturating_add(upload.size) > budget as i64 {
                remove_if_exists(staging);
                return Err(IMAGE_BUDGET_FULL.into());
            }
        }
        let digest = match files::finalize(path, upload.size) {
            Ok(digest) => digest,
            Err(error) => {
                remove_if_exists(staging);
                return Err(error);
            }
        };
        let message = Message {
            message_id: upload.message_id,
            target_user_code: upload.target,
            group_id: upload.group_id,
            content: "[图片]".into(),
            ..Message::default()
        };
        let image = PublishedImage {
            reference: upload.reference,
            digest,
            size: upload.size,
        };
        let outcome = if let Some(group) = group {
            self.send_group(id, identity, group, &message, Some(&image))
        } else {
            self.private_chat(id, identity, &message, Some(&image))
        };
        match outcome {
            Ok(true) => Ok(()),
            Ok(false) => {
                remove_if_exists(path);
                Ok(())
            }
            Err(error) => {
                remove_if_exists(path);
                Err(error)
            }
        }
    }

    pub(super) fn read_image(
        &mut self,
        id: Uuid,
        identity: &Identity,
        request: &Message,
    ) -> Result<()> {
        let author = user_code(&request.user_code).map_err(|_| IMAGE_READ)?;
        let index = request.chunk_index.unwrap_or(0);
        ensure!(
            (1..=64).contains(&request.message_id.len())
                && (0..=(MAX_ONLINE_IMAGE_BYTES - 1) / ONLINE_CHUNK_BYTES).contains(&index),
            IMAGE_READ
        );
        let offset = index * ONLINE_CHUNK_BYTES;
        let (kind, key, after) = if request.group_id.is_empty() {
            let peer = private_target(identity, request).map_err(|_| IMAGE_READ)?;
            ensure!(author == identity.code || author == peer, IMAGE_READ);
            ("private", conversation(&identity.code, &peer), 0)
        } else {
            ensure!(self.peers[&id].group_attachments, FILE_ACCESS);
            (
                "online_group",
                request.group_id.clone(),
                self.store
                    .group_history_start(&identity.code, &request.group_id)?,
            )
        };
        let (legacy, size, reference, digest): (Option<Vec<u8>>, i64, Option<String>, Option<String>) = self.store.0.query_row(
            "SELECT CASE WHEN online_image_ref IS NULL THEN substr(online_image, ?1+1, ?2) END,
                    COALESCE(online_image_size,length(online_image)),online_image_ref,online_image_sha256 FROM chat_messages
             WHERE kind=?3 AND conversation_key=?4 AND sender_code=?5 AND message_id=?6
               AND recalled=0 AND COALESCE(online_image_size,length(online_image))>?1 AND id>?7",
            params![offset, ONLINE_CHUNK_BYTES, kind, key, author, request.message_id, after],
            |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?)),
        ).optional()?.ok_or(Rejected("当前会话中没有该图片分片"))?;
        ensure!(
            (1..=MAX_ONLINE_IMAGE_BYTES).contains(&size),
            "在线图片记录大小无效"
        );
        let data = match reference {
            Some(reference) => files::read_segment(
                &files::image_path(&files::root(&self.store.0)?, &reference)?,
                size,
                offset,
                digest.as_deref().ok_or(IMAGE_READ)?,
            )?,
            None => legacy.ok_or(IMAGE_READ)?,
        };
        self.deliver(
            id,
            Message {
                kind: request.kind.clone(),
                message_id: request.message_id.clone(),
                user_code: request.user_code.clone(),
                target_user_code: request.target_user_code.clone(),
                group_id: request.group_id.clone(),
                command_id: request.command_id.clone(),
                image_size: size,
                chunk_index: Some(index),
                image_data: BASE64.encode(data),
                ..Message::default()
            },
        );
        Ok(())
    }

    pub(super) fn transfer_file(
        &mut self,
        id: Uuid,
        identity: &Identity,
        request: &Message,
    ) -> Result<()> {
        let mut reply = Message {
            kind: request.kind.clone(),
            command_id: request.command_id.clone(),
            attachment_id: request.attachment_id.clone(),
            ..Message::default()
        };
        if request.kind == "dm_file_init" {
            let target = if request.group_id.is_empty() {
                private_target(identity, request)?
            } else {
                ensure!(self.peers[&id].group_attachments, FILE_ACCESS);
                self.store.group_for(&identity.code, &request.group_id)?;
                String::new()
            };
            self.store.begin_file(
                identity,
                &target,
                request,
                &mut reply,
                self.limits.undelivered_quota(),
            )?;
        } else {
            let invalid_id = Rejected("无效的附件编号");
            let uuid = Uuid::parse_str(&request.attachment_id).map_err(|_| invalid_id)?;
            ensure!(uuid.to_string() == request.attachment_id, invalid_id);
            if !self.peers[&id].group_attachments {
                let group: Option<String> = self
                    .store
                    .0
                    .query_row(
                        "SELECT group_id FROM online_files WHERE id=?",
                        [&request.attachment_id],
                        |row| row.get(0),
                    )
                    .optional()?
                    .flatten();
                ensure!(group.is_none(), FILE_ACCESS);
            }
            match request.kind.as_str() {
                "dm_file_chunk" => {
                    let index = file_index(request)?;
                    self.store.append_file(identity, request, index)?;
                    reply.chunk_index = Some(index);
                }
                "dm_file_commit" => {
                    if let Some(message) = self.store.commit_file(
                        identity,
                        &request.attachment_id,
                        self.limits.undelivered_quota(),
                    )? {
                        if message.group_id.is_empty() {
                            self.publish_private(&identity.code, &message);
                        } else {
                            self.publish_group(&identity.code, &message)?;
                        }
                    }
                }
                "dm_file_get" => {
                    let index = file_index(request)?;
                    self.store.read_file(identity, index, &mut reply)?;
                }
                "dm_file_cancel" => self.store.cancel_file(identity, &request.attachment_id)?,
                _ => unreachable!(),
            }
        }
        self.deliver(id, reply);
        Ok(())
    }

    #[cfg(test)]
    pub fn cleanup_files_at(
        &mut self,
        now_utc: DateTime<Utc>,
        min_orphan_age: Duration,
    ) -> Result<CleanupStats> {
        let mut stats = cleanup_database_batches(
            &mut self.store.0,
            now_utc,
            self.limits.undelivered_ttl_days,
            self.limits.delivered_ttl_days,
        )?;
        let active: Vec<_> = self
            .peers
            .values()
            .filter_map(|peer| peer.image.as_ref().map(|image| image.reference.as_str()))
            .collect();
        stats.deleted_orphan_files =
            files::cleanup_orphans(&self.store.0, &active, min_orphan_age)?;
        Ok(stats)
    }
}

pub fn cleanup_maintenance(
    database: &Path,
    online: &std::sync::Mutex<Online>,
) -> Result<CleanupStats> {
    let (ttl_days, delivered_ttl_days) = {
        let state = online
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        (
            state.limits.undelivered_ttl_days,
            state.limits.delivered_ttl_days,
        )
    };
    let mut db = Connection::open_with_flags(database, OpenFlags::SQLITE_OPEN_READ_WRITE)?;
    db.busy_timeout(Duration::from_secs(5))?;
    let mut stats = cleanup_database_batches(&mut db, Utc::now(), ttl_days, delivered_ttl_days)?;
    let active_owned = online
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
        .active_image_references();
    let active: Vec<&str> = active_owned.iter().map(String::as_str).collect();
    stats.deleted_orphan_files = files::cleanup_orphans(&db, &active, Duration::from_secs(120))?;
    Ok(stats)
}

pub(super) fn cleanup_database_batches(
    db: &mut Connection,
    now_utc: DateTime<Utc>,
    ttl_days: u32,
    delivered_ttl_days: u32,
) -> Result<CleanupStats> {
    let mut stats = CleanupStats::default();
    if ttl_days > 0 {
        let delta = TimeDelta::try_days(i64::from(ttl_days)).context("无效的未确认消息过期天数")?;
        let cutoff = now_utc - delta;
        let cutoff_upper =
            (cutoff + TimeDelta::seconds(2)).to_rfc3339_opts(SecondsFormat::Secs, true);
        let mut cursor: Option<(String, i64)> = None;
        loop {
            let batch: Vec<(i64, String)> = match &cursor {
                None => {
                    let mut stmt = db.prepare(
                        "SELECT id, created_at FROM chat_messages
                         WHERE kind='private' AND delivery_state='sent' AND created_at < ?1
                         ORDER BY created_at ASC, id ASC LIMIT ?2",
                    )?;
                    stmt.query_map(params![cutoff_upper, TTL_CLEANUP_BATCH as i64], |row| {
                        Ok((row.get(0)?, row.get(1)?))
                    })?
                    .collect::<rusqlite::Result<Vec<_>>>()?
                }
                Some((last_created, last_id)) => {
                    let mut stmt = db.prepare(
                        "SELECT id, created_at FROM chat_messages
                         WHERE kind='private' AND delivery_state='sent'
                           AND created_at < ?1
                           AND (created_at, id) > (?2, ?3)
                         ORDER BY created_at ASC, id ASC LIMIT ?4",
                    )?;
                    stmt.query_map(
                        params![
                            cutoff_upper,
                            last_created,
                            last_id,
                            TTL_CLEANUP_BATCH as i64
                        ],
                        |row| Ok((row.get(0)?, row.get(1)?)),
                    )?
                    .collect::<rusqlite::Result<Vec<_>>>()?
                }
            };
            if batch.is_empty() {
                break;
            }
            let count = batch.len();
            let last = batch.last().map(|(id, ts)| (ts.clone(), *id));
            let mut delete_ids = Vec::new();
            for (id, ts) in batch {
                match DateTime::parse_from_rfc3339(&ts) {
                    Ok(parsed) if parsed.with_timezone(&Utc) <= cutoff => delete_ids.push(id),
                    Ok(_) => {}
                    Err(_) => stats.skipped_invalid_timestamps += 1,
                }
            }
            if !delete_ids.is_empty() {
                let placeholders = vec!["?"; delete_ids.len()].join(",");
                let sql = format!(
                    "DELETE FROM chat_messages WHERE kind='private' AND delivery_state='sent' AND id IN ({placeholders})"
                );
                let tx = db.transaction_with_behavior(TransactionBehavior::Immediate)?;
                stats.deleted_messages +=
                    tx.execute(&sql, rusqlite::params_from_iter(delete_ids.iter()))?;
                tx.commit()?;
            }
            cursor = last;
            if count < TTL_CLEANUP_BATCH {
                break;
            }
        }
    }

    if delivered_ttl_days > 0 {
        let delta = TimeDelta::try_days(i64::from(delivered_ttl_days))
            .context("无效的已送达消息保留天数")?;
        let cutoff = now_utc - delta;
        let cutoff_upper =
            (cutoff + TimeDelta::seconds(2)).to_rfc3339_opts(SecondsFormat::Secs, true);
        let mut cursor: Option<(String, i64)> = None;
        loop {
            let tx = db.transaction_with_behavior(TransactionBehavior::Immediate)?;
            let batch: Vec<(i64, String)> = match &cursor {
                None => {
                    let mut stmt = tx.prepare(
                        "SELECT id, created_at FROM chat_messages
                         WHERE kind='private' AND delivery_state='delivered' AND created_at < ?1
                         ORDER BY created_at ASC, id ASC LIMIT ?2",
                    )?;
                    stmt.query_map(params![cutoff_upper, TTL_CLEANUP_BATCH as i64], |row| {
                        Ok((row.get(0)?, row.get(1)?))
                    })?
                    .collect::<rusqlite::Result<Vec<_>>>()?
                }
                Some((last_created, last_id)) => {
                    let mut stmt = tx.prepare(
                        "SELECT id, created_at FROM chat_messages
                         WHERE kind='private' AND delivery_state='delivered'
                           AND created_at < ?1
                           AND (created_at, id) > (?2, ?3)
                         ORDER BY created_at ASC, id ASC LIMIT ?4",
                    )?;
                    stmt.query_map(
                        params![
                            cutoff_upper,
                            last_created,
                            last_id,
                            TTL_CLEANUP_BATCH as i64
                        ],
                        |row| Ok((row.get(0)?, row.get(1)?)),
                    )?
                    .collect::<rusqlite::Result<Vec<_>>>()?
                }
            };
            if batch.is_empty() {
                tx.commit()?;
                break;
            }
            let count = batch.len();
            cursor = batch.last().map(|(id, ts)| (ts.clone(), *id));
            let mut delete_ids = Vec::new();
            for (id, ts) in batch {
                match DateTime::parse_from_rfc3339(&ts) {
                    Ok(parsed) if parsed.with_timezone(&Utc) <= cutoff => delete_ids.push(id),
                    Ok(_) => {}
                    Err(_) => stats.skipped_invalid_timestamps += 1,
                }
            }
            if !delete_ids.is_empty() {
                let placeholders = vec!["?"; delete_ids.len()].join(",");
                let sql = format!(
                    "DELETE FROM chat_messages WHERE kind='private' AND delivery_state='delivered' AND id IN ({placeholders})"
                );
                stats.deleted_messages +=
                    tx.execute(&sql, rusqlite::params_from_iter(delete_ids.iter()))?;
            }
            tx.commit()?;
            if count < TTL_CLEANUP_BATCH {
                break;
            }
        }
    }

    let now_ts = now_utc.timestamp();
    loop {
        let tx = db.transaction_with_behavior(TransactionBehavior::Immediate)?;
        let removed_ids: Vec<String> = {
            let mut stmt = tx.prepare(
                "DELETE FROM online_files
                 WHERE id IN (
                     SELECT id FROM online_files
                     WHERE (chat_id IS NULL AND expires<=?1)
                        OR (chat_id IS NOT NULL AND NOT EXISTS (SELECT 1 FROM chat_messages m WHERE m.id=online_files.chat_id))
                     LIMIT ?2
                 )
                 RETURNING id",
            )?;
            stmt.query_map(params![now_ts, FILE_CLEANUP_BATCH as i64], |row| row.get(0))?
                .collect::<rusqlite::Result<Vec<_>>>()?
        };
        let count = removed_ids.len();
        stats.deleted_file_rows += count;
        if count > 0 {
            let placeholders = vec!["?"; count].join(",");
            let sql = format!("DELETE FROM online_file_chunks WHERE file_id IN ({placeholders})");
            stats.deleted_chunks +=
                tx.execute(&sql, rusqlite::params_from_iter(removed_ids.iter()))?;
        }
        tx.commit()?;
        if count < FILE_CLEANUP_BATCH {
            break;
        }
    }

    loop {
        let tx = db.transaction_with_behavior(TransactionBehavior::Immediate)?;
        let stray_ids: Vec<String> = {
            let mut stmt = tx.prepare(
                "SELECT DISTINCT file_id FROM online_file_chunks c
                 WHERE NOT EXISTS (SELECT 1 FROM online_files f WHERE f.id=c.file_id)
                 LIMIT ?1",
            )?;
            stmt.query_map([FILE_CLEANUP_BATCH as i64], |row| row.get(0))?
                .collect::<rusqlite::Result<Vec<_>>>()?
        };
        if stray_ids.is_empty() {
            break;
        }
        let count = stray_ids.len();
        let placeholders = vec!["?"; count].join(",");
        let sql = format!("DELETE FROM online_file_chunks WHERE file_id IN ({placeholders})");
        stats.deleted_chunks += tx.execute(&sql, rusqlite::params_from_iter(stray_ids.iter()))?;
        tx.commit()?;
        if count < FILE_CLEANUP_BATCH {
            break;
        }
    }
    // 仅 incremental auto_vacuum 的库会真正归还页；其他库上是空操作。
    let _ = db.execute_batch("PRAGMA incremental_vacuum(4096);");
    Ok(stats)
}

impl Store {
    pub(super) fn private_message_state(
        db: &Connection,
        key: &str,
        author: &str,
        message_id: &str,
    ) -> Result<Option<String>> {
        Ok(db
            .query_row(
                "SELECT delivery_state FROM chat_messages
                 WHERE kind='private' AND conversation_key=?1 AND sender_code=?2 AND message_id=?3",
                params![key, author, message_id],
                |row| row.get(0),
            )
            .optional()?)
    }

    pub(super) fn check_undelivered_quota(
        db: &Connection,
        target: &str,
        quota: Quota,
    ) -> Result<()> {
        if quota.limit == 0 && quota.bytes == 0 {
            return Ok(());
        }
        let (count, bytes): (i64, i64) = db.query_row(
            "SELECT COUNT(*), COALESCE(SUM(length(CAST(content AS BLOB))+COALESCE(online_image_size, length(CAST(online_image AS BLOB)), 0)),0)
             FROM chat_messages
             WHERE kind='private' AND target_code=?1 AND delivery_state='sent' AND recalled=0",
            params![target],
            |row| Ok((row.get(0)?, row.get(1)?)),
        )?;
        if quota.limit > 0 {
            ensure!(count < quota.limit as i64, UNDELIVERED_FULL);
        }
        if quota.bytes > 0 {
            ensure!(bytes < quota.bytes as i64, UNDELIVERED_BYTES_FULL);
        }
        Ok(())
    }

    pub(super) fn insert_online_message(
        &mut self,
        spec: OnlineMessage<'_>,
    ) -> Result<InsertOutcome> {
        let tx = self
            .0
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        if let Some(delivery_state) = tx
            .query_row(
                "SELECT delivery_state FROM chat_messages
                 WHERE kind=?1 AND conversation_key=?2 AND sender_code=?3 AND message_id=?4",
                params![spec.kind, spec.key, spec.author, spec.message.message_id],
                |row| row.get::<_, String>(0),
            )
            .optional()?
        {
            return Ok(InsertOutcome::Duplicate { delivery_state });
        }
        if spec.kind == "private" {
            Self::check_undelivered_quota(&tx, spec.target, spec.undelivered)?;
        }
        let changed = Self::persist_chat(
            &tx,
            spec.message,
            spec.kind,
            spec.key,
            spec.author,
            spec.target,
            None,
        )?;
        if let Some(image) = spec.image.filter(|_| changed) {
            tx.execute(
                "UPDATE chat_messages SET online_image_ref=?,online_image_size=?,online_image_sha256=? WHERE kind=? AND conversation_key=? AND sender_code=? AND message_id=?",
                params![
                    image.reference,
                    image.size,
                    image.digest,
                    spec.kind,
                    spec.key,
                    spec.author,
                    spec.message.message_id
                ],
            )?;
        }
        tx.commit()?;
        Ok(if changed {
            InsertOutcome::Inserted
        } else {
            InsertOutcome::Duplicate {
                delivery_state: spec.message.delivery_state.clone(),
            }
        })
    }

    fn begin_file(
        &self,
        identity: &Identity,
        target: &str,
        request: &Message,
        reply: &mut Message,
        quota: Quota,
    ) -> Result<()> {
        let name = &request.file_name;
        let size = request.logical_size;
        ensure!(
            (0..=MAX_ONLINE_FILE_BYTES).contains(&size)
                && (1..=255).contains(&name.len())
                && !name.ends_with(['.', ' '])
                && !name.contains(['<', '>', ':', '"', '/', '\\', '|', '?', '*'])
                && !name.chars().any(char::is_control),
            Rejected("文件名无效，或文件超过 32 MiB")
        );
        if request.group_id.is_empty() {
            self.account(target)?;
            Self::check_undelivered_quota(&self.0, target, quota)?;
        }
        let id = Uuid::new_v4().to_string();
        let now = Utc::now().timestamp();
        let expires = now + 24 * 60 * 60;
        let inserted = self.0.execute(
            "INSERT INTO online_files(id,owner,recipient,name,size,expires,disk_backed,group_id)
             SELECT ?1,?2,?3,?4,?5,?6,1,NULLIF(?8,'') WHERE (SELECT COALESCE(SUM(size),0) FROM online_files)+?5<=2147483648
               AND (SELECT COUNT(*) FROM online_files)<20000
               AND (SELECT COUNT(*) FROM online_files WHERE owner=?2 AND chat_id IS NULL AND expires>?7)<4",
            params![id, identity.code, target, name, size, expires, now, request.group_id],
        )?;
        ensure!(
            inserted == 1,
            Rejected("附件存储配额已满，或该账号有过多未完成的上传")
        );
        reply.attachment_id.clone_from(&id);
        reply.upload_id = id;
        reply.chunk_size = ONLINE_CHUNK_BYTES;
        reply.expires_at = DateTime::from_timestamp(expires, 0)
            .expect("valid expiry")
            .to_rfc3339_opts(SecondsFormat::Secs, true);
        Ok(())
    }

    fn append_file(&mut self, identity: &Identity, request: &Message, index: i64) -> Result<()> {
        let data = BASE64.decode(&request.file_data).map_err(|_| FILE_CHUNK)?;
        let tx = self.0.transaction()?;
        let (size, received, disk): (i64, i64, bool) = tx.query_row(
            "SELECT size,received,disk_backed FROM online_files f WHERE id=? AND owner=? AND chat_id IS NULL AND expires>?
             AND (f.group_id IS NULL OR EXISTS (SELECT 1 FROM online_group_members g WHERE g.group_id=f.group_id AND g.user_code=f.owner))",
            params![request.attachment_id, identity.code, Utc::now().timestamp()],
            |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
        ).optional()?.ok_or(FILE_ACCESS)?;
        let expected = (size - received).min(ONLINE_CHUNK_BYTES);
        ensure!(
            expected > 0 && data.len() as i64 == expected && received == index * ONLINE_CHUNK_BYTES,
            FILE_CHUNK
        );
        if disk {
            let root = files::root(&tx)?;
            fs::create_dir_all(&root)?;
            files::append(
                &files::file_path(&root, &request.attachment_id)?.with_extension("partial"),
                received,
                &data,
            )?;
        } else {
            tx.execute(
                "INSERT INTO online_file_chunks(file_id,chunk_index,data) VALUES (?,?,?)",
                params![request.attachment_id, index, data],
            )?;
        }
        tx.execute(
            "UPDATE online_files SET received=received+? WHERE id=?",
            params![expected, request.attachment_id],
        )?;
        tx.commit()?;
        Ok(())
    }

    fn commit_file(
        &mut self,
        identity: &Identity,
        id: &str,
        quota: Quota,
    ) -> Result<Option<Message>> {
        let tx = self
            .0
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        let Some((recipient, name, size, received, published, disk, group)) = tx.query_row(
                "SELECT recipient,name,size,received,chat_id,disk_backed,group_id FROM online_files f
             WHERE id=? AND owner=? AND (chat_id IS NOT NULL OR expires>?)
             AND (f.group_id IS NULL OR EXISTS (SELECT 1 FROM online_group_members g WHERE g.group_id=f.group_id AND g.user_code=f.owner))",
                params![id, identity.code, Utc::now().timestamp()],
                |row| {
                    Ok((
                        row.get::<_, String>(0)?,
                        row.get::<_, String>(1)?,
                        row.get::<_, i64>(2)?,
                        row.get::<_, i64>(3)?,
                        row.get::<_, Option<i64>>(4)?,
                        row.get::<_, bool>(5)?,
                        row.get::<_, Option<String>>(6)?,
                    ))
                },
            )
            .optional()? else { return Err(FILE_ACCESS.into()) };
        if published.is_some() {
            return Ok(None);
        }
        ensure!(size == received, Rejected("文件尚未完整上传"));
        if group.is_none()
            && let Err(error) = Self::check_undelivered_quota(&tx, &recipient, quota)
        {
            tx.execute(
                "DELETE FROM online_files WHERE id=? AND owner=? AND chat_id IS NULL",
                params![id, identity.code],
            )?;
            tx.execute("DELETE FROM online_file_chunks WHERE file_id=?", [id])?;
            tx.commit()?;
            if disk {
                remove_if_exists(
                    &files::file_path(&files::root(&self.0)?, id)?.with_extension("partial"),
                );
            }
            return Err(error);
        }
        let digest = if disk {
            let root = files::root(&tx)?;
            fs::create_dir_all(&root)?;
            Some(files::finalize(&files::file_path(&root, id)?, size)?)
        } else {
            None
        };
        let (kind, key, room, wire_kind) = match &group {
            Some(group) => {
                let name: String = tx.query_row(
                    "SELECT name FROM online_groups WHERE id=?",
                    [group],
                    |row| row.get(0),
                )?;
                ("online_group", group.clone(), name, "group_message")
            }
            None => (
                "private",
                conversation(&identity.code, &recipient),
                String::new(),
                "private_chat",
            ),
        };
        let created_at = now();
        let content = format!("[文件] {name}");
        let chat_id = tx.query_row(
            "INSERT INTO chat_messages(message_id,kind,conversation_key,room,sender_username,sender_code,target_code,content,created_at,online_file_size)
             VALUES (?,?,?,?,?,?,?,?,?,?) RETURNING id",
            params![id, kind, key, room, identity.username, identity.code, recipient, content, created_at, size],
            |row| row.get::<_, i64>(0),
        )?;
        tx.execute(
            "UPDATE online_files SET chat_id=?,disk_sha256=? WHERE id=?",
            params![chat_id, digest, id],
        )?;
        Self::prune_conversation(&tx, kind, &key)?;
        tx.commit()?;
        Ok(Some(Message {
            kind: wire_kind.into(),
            message_id: id.into(),
            username: identity.username.clone(),
            user_code: identity.display_code.clone(),
            target_user_code: recipient,
            group_id: group.unwrap_or_default(),
            room,
            content,
            created_at,
            delivery_state: "sent".into(),
            attachment_id: id.into(),
            file_name: name,
            logical_size: size,
            ..Message::default()
        }))
    }

    fn read_file(&self, identity: &Identity, index: i64, reply: &mut Message) -> Result<()> {
        let (name, size, digest, legacy): (String, i64, Option<String>, Option<Vec<u8>>) = self
            .0
            .query_row(
                "SELECT f.name,f.size,f.disk_sha256,CASE WHEN f.disk_sha256 IS NULL THEN c.data END FROM online_files f
             JOIN chat_messages m ON m.id=f.chat_id AND m.recalled=0
             LEFT JOIN online_file_chunks c ON c.file_id=f.id AND c.chunk_index=?1
             WHERE f.id=?2 AND ((f.group_id IS NULL AND (f.owner=?3 OR f.recipient=?3))
               OR (f.group_id IS NOT NULL AND EXISTS (SELECT 1 FROM online_group_members g
                      WHERE g.group_id=f.group_id AND g.user_code=?3 AND m.id>g.joined_after)))
               AND (f.disk_sha256 IS NOT NULL OR c.chunk_index IS NOT NULL OR (f.size=0 AND ?1=0))",
                params![index, reply.attachment_id, identity.code],
                |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?)),
            )
            .optional()?
            .ok_or(FILE_ACCESS)?;
        reply.file_name = name;
        reply.logical_size = size;
        let offset = index * ONLINE_CHUNK_BYTES;
        ensure!(
            (0..=MAX_ONLINE_FILE_BYTES).contains(&size),
            "在线文件记录大小无效"
        );
        ensure!((size > offset) || (size == 0 && index == 0), FILE_CHUNK);
        let data = if let Some(digest) = digest {
            files::read_segment(
                &files::file_path(&files::root(&self.0)?, &reply.attachment_id)?,
                size,
                offset,
                &digest,
            )?
        } else {
            legacy.unwrap_or_default()
        };
        reply.file_data = BASE64.encode(data);
        reply.chunk_index = Some(index);
        reply.chunk_size = ONLINE_CHUNK_BYTES;
        Ok(())
    }

    fn cancel_file(&mut self, identity: &Identity, id: &str) -> Result<()> {
        let tx = self.0.transaction()?;
        let removed = tx.execute(
            "DELETE FROM online_files WHERE id=? AND owner=? AND chat_id IS NULL",
            params![id, identity.code],
        )?;
        ensure!(removed == 1, FILE_ACCESS);
        tx.execute("DELETE FROM online_file_chunks WHERE file_id=?", [id])?;
        tx.commit()?;
        if let Ok(root) = files::root(&self.0)
            && let Ok(path) = files::file_path(&root, id)
        {
            remove_if_exists(&path.with_extension("partial"));
        }
        Ok(())
    }
}

fn file_index(request: &Message) -> Result<i64> {
    let index = request.chunk_index.unwrap_or(0);
    ensure!((0..MAX_FILE_CHUNKS).contains(&index), FILE_CHUNK);
    Ok(index)
}

#[cfg(test)]
mod tests {
    use std::sync::Mutex;

    use tokio::sync::mpsc;

    use super::*;
    use crate::online::OnlineLimits;

    const PNG_1X1_BASE64: &str = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jR1sAAAAASUVORK5CYII=";

    fn setup_accounts(db: &Path, limits: OnlineLimits) -> Result<Online> {
        let store = Store::open(db)?;
        for code in ["alice", "bobby", "carol"] {
            store.0.execute(
                "INSERT INTO accounts (username,normalized_username,user_code,normalized_code,password_hash,created_at) VALUES (?1,?1,?1,?1,'fixture','')",
                [code],
            )?;
        }
        Ok(Online::with_limits(store, None, None, limits))
    }

    fn connect_peer(online: &mut Online, code: &str) -> Result<(Uuid, mpsc::Receiver<Message>)> {
        let id = Uuid::new_v4();
        let (sender, receiver) = mpsc::channel(64);
        online.register(
            id,
            Identity {
                code: code.into(),
                username: code.into(),
                display_code: code.into(),
            },
            sender,
            true,
            true,
        )?;
        Ok((id, receiver))
    }

    #[test]
    fn migration_5_to_6_creates_partial_indexes_hits_plans_and_skips_lan() -> Result<()> {
        let dir = std::env::temp_dir().join(format!("lan-chat-mig6-{}", Uuid::new_v4()));
        fs::create_dir_all(&dir)?;
        let online_db = dir.join("online.db");
        let store = Store::open(&online_db)?;
        store.0.execute_batch(
            "DROP INDEX IF EXISTS idx_chat_messages_pending_target;
             DROP INDEX IF EXISTS idx_chat_messages_pending_created;
             PRAGMA user_version=5;",
        )?;
        drop(store);

        let mut reopened = Store::open(&online_db)?;
        let version: i64 = reopened
            .0
            .pragma_query_value(None, "user_version", |row| row.get(0))?;
        assert_eq!(version, 6);

        // Seed realistic delivered + pending rows and run EXPLAIN QUERY PLAN on exact production queries.
        {
            let tx = reopened.0.transaction()?;
            for i in 0..200 {
                tx.execute(
                    "INSERT INTO chat_messages(message_id,kind,conversation_key,sender_username,sender_code,target_code,content,delivery_state,created_at,recalled)
                     VALUES (?1,'private','alice:bobby','alice','alice','bobby','hist','delivered','2026-10-01T00:00:00Z',0)",
                    [format!("hist-{i}")],
                )?;
            }
            for i in 0..10 {
                tx.execute(
                    "INSERT INTO chat_messages(message_id,kind,conversation_key,sender_username,sender_code,target_code,content,delivery_state,created_at,recalled)
                     VALUES (?1,'private','alice:bobby','alice','alice','bobby','pend','sent','2026-10-01T00:00:00Z',0)",
                    [format!("pend-{i}")],
                )?;
            }
            tx.commit()?;
        }
        let explain = |sql: &str| -> Result<Vec<String>> {
            let mut stmt = reopened.0.prepare(&format!("EXPLAIN QUERY PLAN {sql}"))?;
            let plans = stmt
                .query_map([], |row| row.get::<_, String>(3))?
                .collect::<rusqlite::Result<Vec<_>>>()?;
            Ok(plans)
        };
        let quota_plan = explain(
            "SELECT 1 FROM chat_messages WHERE kind='private' AND target_code='bobby' AND delivery_state='sent' AND recalled=0 LIMIT 1 OFFSET 199",
        )?;
        assert!(
            quota_plan
                .iter()
                .any(|line| line.contains("idx_chat_messages_pending_target")),
            "unexpected quota plan: {quota_plan:?}"
        );
        let ttl_plan = explain(
            "SELECT id, created_at FROM chat_messages INDEXED BY idx_chat_messages_pending_created WHERE kind='private' AND delivery_state='sent' AND created_at < '2026-11-01T00:00:00Z' ORDER BY created_at ASC, id ASC LIMIT 100",
        )?;
        assert!(
            ttl_plan
                .iter()
                .any(|line| line.contains("idx_chat_messages_pending_created"))
                && !ttl_plan.iter().any(|line| line.contains("TEMP B-TREE")),
            "unexpected ttl plan: {ttl_plan:?}"
        );
        let ttl_cursor_plan = explain(
            "SELECT id, created_at FROM chat_messages INDEXED BY idx_chat_messages_pending_created WHERE kind='private' AND delivery_state='sent' AND created_at < '2026-11-01T00:00:00Z' AND (created_at, id) > ('2026-10-01T00:00:00Z', 10) ORDER BY created_at ASC, id ASC LIMIT 100",
        )?;
        assert!(
            ttl_cursor_plan
                .iter()
                .any(|line| line.contains("idx_chat_messages_pending_created"))
                && !ttl_cursor_plan
                    .iter()
                    .any(|line| line.contains("TEMP B-TREE")),
            "unexpected ttl cursor plan: {ttl_cursor_plan:?}"
        );
        drop(reopened);

        // Verify read-only compatibility & that LAN mode DB does not receive online partial indexes.
        let ro = Connection::open_with_flags(&online_db, OpenFlags::SQLITE_OPEN_READ_ONLY)?;
        let count: i64 =
            ro.query_row("SELECT COUNT(*) FROM chat_messages", [], |row| row.get(0))?;
        assert_eq!(count, 210);
        drop(ro);

        let lan_db = dir.join("lan.db");
        let lan = Store::open_mode(&lan_db, "lan")?;
        let lan_idx_count: i64 = lan.0.query_row(
            "SELECT COUNT(*) FROM sqlite_schema WHERE type='index' AND name LIKE 'idx_chat_messages_pending_%'",
            [],
            |row| row.get(0),
        )?;
        assert_eq!(lan_idx_count, 0);
        drop(lan);

        fs::remove_dir_all(dir)?;
        Ok(())
    }

    #[test]
    fn undelivered_quota_counts_utf8_and_legacy_inline_image_bytes() -> Result<()> {
        let dir = std::env::temp_dir().join(format!("lan-chat-quota-bytes-{}", Uuid::new_v4()));
        fs::create_dir_all(&dir)?;
        let online = setup_accounts(&dir.join("chat.db"), OnlineLimits::default())?;
        online.store.0.execute(
            "INSERT INTO chat_messages
                (message_id,kind,conversation_key,sender_username,sender_code,target_code,
                 content,online_image,online_image_size,delivery_state,created_at,recalled)
             VALUES (?1,'private','alice:bobby','Fixture','alice','bobby',
                     'é🙂',?2,NULL,'sent','2026-11-28T00:00:00Z',0)",
            params![Uuid::new_v4().to_string(), &b"12345"[..]],
        )?;
        assert!(
            Store::check_undelivered_quota(
                &online.store.0,
                "bobby",
                Quota {
                    limit: 0,
                    bytes: 11,
                },
            )
            .is_err()
        );
        Store::check_undelivered_quota(
            &online.store.0,
            "bobby",
            Quota {
                limit: 0,
                bytes: 12,
            },
        )?;
        drop(online);
        fs::remove_dir_all(dir)?;
        Ok(())
    }

    #[test]
    fn image_budget_counts_legacy_inline_image_bytes() -> Result<()> {
        let dir =
            std::env::temp_dir().join(format!("lan-chat-image-budget-bytes-{}", Uuid::new_v4()));
        fs::create_dir_all(&dir)?;
        let png = BASE64.decode(PNG_1X1_BASE64)?;
        let mut online = setup_accounts(
            &dir.join("chat.db"),
            OnlineLimits {
                image_budget_bytes: png.len() + 4,
                ..OnlineLimits::default()
            },
        )?;
        online.store.0.execute(
            "INSERT INTO chat_messages
                (message_id,kind,conversation_key,sender_username,sender_code,content,
                 online_image,online_image_size,created_at)
             VALUES (?1,'private','alice:bobby','Fixture','alice','',?2,NULL,'2026-11-28T00:00:00Z')",
            params![Uuid::new_v4().to_string(), &b"12345"[..]],
        )?;
        let (alice_id, _receiver) = connect_peer(&mut online, "alice")?;
        let alice = Identity {
            code: "alice".into(),
            username: "alice".into(),
            display_code: "alice".into(),
        };
        let error = online
            .handle_command(
                alice_id,
                &alice,
                &Message {
                    kind: "dm_image_chunk".into(),
                    target_user_code: "bobby".into(),
                    message_id: Uuid::new_v4().to_string(),
                    image_size: png.len() as i64,
                    chunk_index: Some(0),
                    image_data: PNG_1X1_BASE64.into(),
                    ..Message::default()
                },
            )
            .expect_err("legacy inline bytes must consume the image budget");
        assert!(error.to_string().contains("服务器图片存储空间已满"));
        let count: i64 =
            online
                .store
                .0
                .query_row("SELECT COUNT(*) FROM chat_messages", [], |row| row.get(0))?;
        assert_eq!(count, 1);
        drop(online);
        fs::remove_dir_all(dir)?;
        Ok(())
    }

    #[test]
    fn undelivered_quota_enforces_text_image_file_idempotency_and_commit_cleanup() -> Result<()> {
        let dir = std::env::temp_dir().join(format!("lan-chat-p01-quota-{}", Uuid::new_v4()));
        fs::create_dir_all(&dir)?;
        let db = dir.join("chat.db");
        let mut online = setup_accounts(
            &db,
            OnlineLimits {
                undelivered_per_target: 2,
                undelivered_bytes_per_target: 0,
                undelivered_ttl_days: 14,
                delivered_ttl_days: 0,
                image_budget_bytes: 0,
                storage_budget_bytes: i64::MAX as u64,
                group_sender_budget_bytes: i64::MAX as u64,
                min_disk_free_bytes: 0,
                max_sessions_per_account: 8,
            },
        )?;
        let (alice_id, mut alice_rx) = connect_peer(&mut online, "alice")?;
        let alice = Identity {
            code: "alice".into(),
            username: "alice".into(),
            display_code: "alice".into(),
        };
        let png = BASE64.decode(PNG_1X1_BASE64)?;

        let text_id = Uuid::new_v4().to_string();
        online.handle_command(
            alice_id,
            &alice,
            &Message {
                kind: "private_chat".into(),
                target_user_code: "bobby".into(),
                message_id: text_id.clone(),
                content: "dm-1".into(),
                ..Message::default()
            },
        )?;
        let img_id = Uuid::new_v4().to_string();
        online.handle_command(
            alice_id,
            &alice,
            &Message {
                kind: "dm_image_chunk".into(),
                target_user_code: "bobby".into(),
                message_id: img_id.clone(),
                image_size: png.len() as i64,
                chunk_index: Some(0),
                image_data: PNG_1X1_BASE64.into(),
                ..Message::default()
            },
        )?;

        // Target quota (2) reached: new text, image index==0, and file init must fail cleanly.
        assert!(
            online
                .handle_command(
                    alice_id,
                    &alice,
                    &Message {
                        kind: "private_chat".into(),
                        target_user_code: "bobby".into(),
                        message_id: Uuid::new_v4().to_string(),
                        content: "dm-overflow".into(),
                        ..Message::default()
                    },
                )
                .is_err()
        );
        assert!(
            online
                .handle_command(
                    alice_id,
                    &alice,
                    &Message {
                        kind: "dm_image_chunk".into(),
                        target_user_code: "bobby".into(),
                        message_id: Uuid::new_v4().to_string(),
                        image_size: png.len() as i64,
                        chunk_index: Some(0),
                        image_data: PNG_1X1_BASE64.into(),
                        ..Message::default()
                    },
                )
                .is_err()
        );
        assert!(
            online
                .handle_command(
                    alice_id,
                    &alice,
                    &Message {
                        kind: "dm_file_init".into(),
                        target_user_code: "bobby".into(),
                        file_name: "blocked.txt".into(),
                        logical_size: 4,
                        ..Message::default()
                    },
                )
                .is_err()
        );

        // Idempotent resend of existing text and image succeeds even while quota is full,
        // and duplicate image upload leaves only the original image file on disk.
        online.handle_command(
            alice_id,
            &alice,
            &Message {
                kind: "private_chat".into(),
                target_user_code: "bobby".into(),
                message_id: text_id.clone(),
                content: "dm-1".into(),
                ..Message::default()
            },
        )?;
        online.handle_command(
            alice_id,
            &alice,
            &Message {
                kind: "dm_image_chunk".into(),
                target_user_code: "bobby".into(),
                message_id: img_id.clone(),
                image_size: png.len() as i64,
                chunk_index: Some(0),
                image_data: PNG_1X1_BASE64.into(),
                ..Message::default()
            },
        )?;
        let files_root = files::root(&online.store.0)?;
        let disk_count = fs::read_dir(&files_root)?.count();
        assert_eq!(disk_count, 1, "duplicate image upload left extra disk file");

        // Race between dm_file_init and dm_file_commit fills carol's quota; commit rejection must
        // clean both DB staging row and .partial file immediately (no upload slot lockup).
        while alice_rx.try_recv().is_ok() {}
        online.handle_command(
            alice_id,
            &alice,
            &Message {
                kind: "dm_file_init".into(),
                target_user_code: "carol".into(),
                file_name: "race.txt".into(),
                logical_size: 5,
                ..Message::default()
            },
        )?;
        let file_id = alice_rx.try_recv()?.attachment_id;
        online.handle_command(
            alice_id,
            &alice,
            &Message {
                kind: "dm_file_chunk".into(),
                attachment_id: file_id.clone(),
                chunk_index: Some(0),
                file_data: BASE64.encode(b"hello"),
                ..Message::default()
            },
        )?;
        for content in ["c1", "c2"] {
            online.handle_command(
                alice_id,
                &alice,
                &Message {
                    kind: "private_chat".into(),
                    target_user_code: "carol".into(),
                    message_id: Uuid::new_v4().to_string(),
                    content: content.into(),
                    ..Message::default()
                },
            )?;
        }
        assert!(
            online
                .handle_command(
                    alice_id,
                    &alice,
                    &Message {
                        kind: "dm_file_commit".into(),
                        attachment_id: file_id.clone(),
                        ..Message::default()
                    },
                )
                .is_err()
        );
        let pending_uploads: i64 = online.store.0.query_row(
            "SELECT COUNT(*) FROM online_files WHERE owner='alice' AND chat_id IS NULL",
            [],
            |row| row.get(0),
        )?;
        assert_eq!(pending_uploads, 0);
        assert!(
            !files::file_path(&files_root, &file_id)?
                .with_extension("partial")
                .exists()
        );

        // Recalling one pending DM for bobby frees slot for a new message (no reverse inbox DoS).
        online.handle_command(
            alice_id,
            &alice,
            &Message {
                kind: "message_recall".into(),
                message_id: text_id,
                ..Message::default()
            },
        )?;
        online.handle_command(
            alice_id,
            &alice,
            &Message {
                kind: "private_chat".into(),
                target_user_code: "bobby".into(),
                message_id: Uuid::new_v4().to_string(),
                content: "after-recall".into(),
                ..Message::default()
            },
        )?;

        drop(online);
        fs::remove_dir_all(dir)?;
        Ok(())
    }

    #[test]
    fn ttl_cleanup_handles_mixed_precision_recalled_attachments_invalid_ts_and_freelist()
    -> Result<()> {
        let dir = std::env::temp_dir().join(format!("lan-chat-p01-ttl-{}", Uuid::new_v4()));
        fs::create_dir_all(&dir)?;
        let db = dir.join("chat.db");
        let mut online = setup_accounts(
            &db,
            OnlineLimits {
                undelivered_per_target: 200,
                undelivered_bytes_per_target: 0,
                undelivered_ttl_days: 14,
                delivered_ttl_days: 30,
                image_budget_bytes: 0,
                storage_budget_bytes: i64::MAX as u64,
                group_sender_budget_bytes: i64::MAX as u64,
                min_disk_free_bytes: 0,
                max_sessions_per_account: 8,
            },
        )?;
        let root = files::root(&online.store.0)?;
        fs::create_dir_all(&root)?;

        // Boundary reference: 2026-11-27T12:00:00.500Z => cutoff = 2026-11-13T12:00:00.500Z
        let now_utc = DateTime::parse_from_rfc3339("2026-11-27T12:00:00.500Z")?.with_timezone(&Utc);
        let insert = |kind: &str, state: &str, recalled: i64, ts: &str, pad: &str| -> Result<i64> {
            let msg_id = Uuid::new_v4().to_string();
            let content = if pad.is_empty() {
                msg_id.clone()
            } else {
                pad.to_owned()
            };
            Ok(online.store.0.query_row(
                "INSERT INTO chat_messages
                 (message_id,kind,conversation_key,sender_username,sender_code,target_code,content,delivery_state,created_at,recalled)
                 VALUES (?1,?2,'alice:bobby','alice','alice','bobby',?3,?4,?5,?6) RETURNING id",
                params![msg_id, kind, content, state, ts, recalled],
                |row| row.get(0),
            )?)
        };

        let expired_whole = insert(
            "private",
            "sent",
            0,
            "2026-11-13T12:00:00Z",
            &"x".repeat(4000),
        )?;
        let unexpired_frac = insert("private", "sent", 0, "2026-11-13T12:00:00.900Z", "")?;
        let recalled_expired = insert(
            "private",
            "sent",
            1,
            "2026-11-10T00:00:00Z",
            &"r".repeat(2000),
        )?;
        let delivered_old = insert("private", "delivered", 0, "2026-11-01T00:00:00Z", "")?;
        let group_old = insert("online_group", "sent", 0, "2026-11-01T00:00:00Z", "")?;

        // Attach a disk file + disk image to the recalled expired row.
        let file_id = Uuid::new_v4().to_string();
        let file_disk = files::file_path(&root, &file_id)?;
        fs::write(&file_disk, b"recalled-file")?;
        online.store.0.execute(
            "INSERT INTO online_files(id,owner,recipient,name,size,received,expires,disk_backed,disk_sha256,chat_id)
             VALUES (?1,'alice','bobby','a.bin',13,13,9999999999,1,'deadbeef',?2)",
            params![file_id, recalled_expired],
        )?;
        let image_ref = format!("image-{}", Uuid::new_v4());
        let image_disk = files::image_path(&root, &image_ref)?;
        fs::write(&image_disk, b"recalled-image")?;
        online.store.0.execute(
            "UPDATE chat_messages SET online_image_ref=?1, online_image_size=14 WHERE id=?2",
            params![image_ref, recalled_expired],
        )?;

        // More than TTL_CLEANUP_BATCH invalid timestamps at the start of the index must be skipped
        // without halting cursor progress or deleting invalid rows.
        for index in 0..105 {
            let ts = if index % 2 == 0 {
                ""
            } else {
                "0000-bad-timestamp"
            };
            insert("private", "sent", 0, ts, "")?;
        }

        // With ttl_days = 0, nothing is removed.
        online.limits.undelivered_ttl_days = 0;
        let disabled_stats = online.cleanup_files_at(now_utc, Duration::ZERO)?;
        assert_eq!(disabled_stats.deleted_messages, 0);

        online.limits.undelivered_ttl_days = 14;
        let stats = online.cleanup_files_at(now_utc, Duration::ZERO)?;
        assert_eq!(stats.deleted_messages, 2);
        assert_eq!(stats.skipped_invalid_timestamps, 105);
        assert_eq!(stats.deleted_file_rows, 1);
        assert_eq!(stats.deleted_orphan_files, 2);

        let exists = |id: i64| -> Result<bool> {
            Ok(online
                .store
                .0
                .query_row("SELECT 1 FROM chat_messages WHERE id=?", [id], |_| Ok(()))
                .optional()?
                .is_some())
        };
        assert!(!exists(expired_whole)?);
        assert!(!exists(recalled_expired)?);
        assert!(exists(unexpired_frac)?);
        assert!(exists(delivered_old)?);
        assert!(exists(group_old)?);
        assert!(!file_disk.exists());
        assert!(!image_disk.exists());

        let freelist: i64 = online
            .store
            .0
            .pragma_query_value(None, "freelist_count", |row| row.get(0))?;
        assert!(freelist > 0, "expected SQLite freelist pages after deletes");

        // Verify background cleanup_maintenance helper works out-of-lock while preserving active grace.
        let orphan_old = files::image_path(&root, &format!("image-{}", Uuid::new_v4()))?;
        fs::write(&orphan_old, b"recent")?;
        drop(online);
        let online_arc = Mutex::new(Online::new(Store::open(&db)?, None, None));
        let bg_stats = cleanup_maintenance(&db, &online_arc)?;
        assert_eq!(
            bg_stats.deleted_orphan_files, 0,
            "recent orphan should wait grace window"
        );
        assert!(orphan_old.exists());

        drop(online_arc);
        fs::remove_dir_all(dir)?;
        Ok(())
    }
}
