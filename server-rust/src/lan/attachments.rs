use std::{
    fs::{self, OpenOptions},
    io::{ErrorKind, Read, Write},
    path::{Path, PathBuf},
};

use anyhow::{Context, Result, ensure};
use base64::{Engine, engine::general_purpose::STANDARD};
use chrono::{DateTime, SecondsFormat, Utc};
use lan_chat_protocol::{
    ATTACHMENT_CHUNK_BYTES, ATTACHMENT_CIPHER_OVERHEAD, ATTACHMENT_TAG_BYTES, MAX_ATTACHMENT_BYTES,
    MAX_ROOM_ATTACHMENT_BYTES, Message,
};
use ring::digest::{Context as Digest, SHA256, digest};
use rusqlite::{OptionalExtension, TransactionBehavior, params};
use uuid::Uuid;

use super::Hub;
use crate::store::{Rejected, now};

const MAX_CIPHER_BYTES: i64 = ATTACHMENT_CHUNK_BYTES + ATTACHMENT_CIPHER_OVERHEAD;
const INVALID_CHUNK: Rejected = Rejected("Invalid attachment chunk");
const INCOMPLETE: Rejected = Rejected("Attachment chunks are incomplete");

struct Upload {
    attachment: String,
    room: String,
    author: String,
    size: i64,
    expiry: String,
}

impl Hub {
    pub(super) fn attachment(&mut self, id: Uuid, request: &Message) -> Result<()> {
        let peer = &self.clients[&id];
        let mut response = Message {
            kind: request.kind.clone(),
            command_id: request.command_id.clone(),
            ..Message::default()
        };
        if request.kind == "attachment.init" {
            ensure!(
                peer.room == request.room,
                Rejected("Attachment upload access denied")
            );
            self.attachment_init(request, &peer.identity.code.clone(), &mut response)?;
        } else if request.kind == "attachment.download" {
            self.attachment_download(request, &peer.room, &mut response)?;
        } else {
            let upload = self.attachment_upload(&request.upload_id)?;
            ensure!(
                peer.room == upload.room && peer.identity.code == upload.author,
                Rejected("Attachment upload access denied")
            );
            ensure!(
                DateTime::parse_from_rfc3339(&upload.expiry)? > Utc::now(),
                Rejected("Attachment upload expired")
            );
            match request.kind.as_str() {
                "attachment.chunk" => self.attachment_chunk(request, &upload, &mut response)?,
                "attachment.resume" => self.attachment_resume(request, &upload, &mut response)?,
                "attachment.commit" => self.attachment_commit(request, &upload, &mut response)?,
                _ => unreachable!("attachment dispatch"),
            }
        }
        self.deliver(id, response);
        Ok(())
    }

    fn attachment_init(
        &mut self,
        request: &Message,
        author: &str,
        response: &mut Message,
    ) -> Result<()> {
        let size = request.logical_size;
        ensure!(size > 0, Rejected("Invalid attachment size"));
        ensure!(
            size <= MAX_ATTACHMENT_BYTES,
            Rejected("attachment exceeds the 5 GiB limit")
        );
        let timestamp = Utc::now();
        let expires =
            (timestamp + chrono::Duration::hours(24)).to_rfc3339_opts(SecondsFormat::AutoSi, true);
        let created = timestamp.to_rfc3339_opts(SecondsFormat::AutoSi, true);
        let attachment = Uuid::new_v4().to_string();
        let upload = Uuid::new_v4().to_string();
        let tx = self
            .store
            .0
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        let changed = tx.execute(
            "INSERT INTO room_quotas (room,reserved_bytes,used_bytes) VALUES (?,?,0)
             ON CONFLICT(room) DO UPDATE SET reserved_bytes=room_quotas.reserved_bytes+excluded.reserved_bytes
             WHERE room_quotas.reserved_bytes+room_quotas.used_bytes+excluded.reserved_bytes<=?",
            params![request.room, size, MAX_ROOM_ATTACHMENT_BYTES],
        )?;
        ensure!(changed == 1, Rejected("Room attachment quota exceeded"));
        tx.execute(
            "INSERT INTO attachments (attachment_id,room,logical_size,created_at) VALUES (?,?,?,?)",
            params![attachment, request.room, size, created],
        )?;
        tx.execute(
            "INSERT INTO attachment_uploads
             (upload_id,attachment_id,room,author_code,logical_size,reserved_bytes,chunk_size,expires_at,created_at)
             VALUES (?,?,?,?,?,?,?,?,?)",
            params![upload, attachment, request.room, author, size, size, ATTACHMENT_CHUNK_BYTES, expires, created],
        )?;
        tx.commit()?;
        response.room = request.room.clone();
        response.upload_id = upload;
        response.attachment_id = attachment;
        response.chunk_size = ATTACHMENT_CHUNK_BYTES;
        response.expires_at = expires;
        Ok(())
    }

    fn attachment_upload(&self, id: &str) -> Result<Upload> {
        let missing = Rejected("Attachment upload not found");
        ensure!(valid_id(id), missing);
        let (upload, chunk_size) = self.store.0.query_row(
            "SELECT attachment_id,room,author_code,logical_size,expires_at,chunk_size FROM attachment_uploads WHERE upload_id=?",
            [id],
            |row| Ok((Upload {
                attachment: row.get(0)?,
                room: row.get(1)?,
                author: row.get(2)?,
                size: row.get(3)?,
                expiry: row.get(4)?,
            }, row.get::<_, i64>(5)?)),
        ).optional()?.ok_or(missing)?;
        ensure!(
            valid_id(&upload.attachment)
                && (1..=MAX_ATTACHMENT_BYTES).contains(&upload.size)
                && chunk_size == ATTACHMENT_CHUNK_BYTES,
            "数据库中的上传元数据无效"
        );
        Ok(upload)
    }

    fn attachment_chunk(
        &mut self,
        request: &Message,
        upload: &Upload,
        response: &mut Message,
    ) -> Result<()> {
        let index = request.chunk_index.unwrap_or(0);
        ensure!(
            (0..chunk_count(upload.size)).contains(&index),
            INVALID_CHUNK
        );
        let data = STANDARD
            .decode(&request.ciphertext)
            .map_err(|_| INVALID_CHUNK)?;
        ensure!(
            (1..=MAX_CIPHER_BYTES).contains(&(data.len() as i64)),
            INVALID_CHUNK
        );
        let sha = hex(digest(&SHA256, &data).as_ref());
        ensure!(
            sha == request.cipher_sha256,
            Rejected("Attachment chunk hash mismatch")
        );
        let path = chunk_path(&self.partial_dir(&request.upload_id), index);
        let tx = self
            .store
            .0
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        let existing: Option<(i64, String)> = tx.query_row(
            "SELECT cipher_size,cipher_sha256 FROM attachment_chunks WHERE upload_id=? AND chunk_index=?",
            params![request.upload_id, index],
            |row| Ok((row.get(0)?, row.get(1)?)),
        ).optional()?;
        let orphan = path.try_exists()?;
        if let Some((size, hash)) = &existing {
            ensure!(
                *size == data.len() as i64 && *hash == sha,
                Rejected("Attachment chunk rejected")
            );
            ensure!(
                read_chunk(&path)? == data,
                Rejected("Attachment chunk rejected")
            );
        } else {
            if orphan {
                ensure!(
                    read_chunk(&path)? == data,
                    Rejected("Attachment chunk rejected")
                );
            } else {
                write_chunk(&path, &data)?;
            }
            tx.execute(
                "INSERT INTO attachment_chunks (upload_id,chunk_index,cipher_size,cipher_sha256,created_at) VALUES (?,?,?,?,?)",
                params![request.upload_id, index, data.len() as i64, sha, now()],
            )?;
        }
        tx.commit()?;
        response.upload_id = request.upload_id.clone();
        response.chunk_index = request.chunk_index;
        response.content = if existing.is_some() || orphan {
            "duplicate"
        } else {
            "stored"
        }
        .into();
        Ok(())
    }

    fn attachment_resume(
        &mut self,
        request: &Message,
        upload: &Upload,
        response: &mut Message,
    ) -> Result<()> {
        let directory = self.partial_dir(&request.upload_id);
        let published = self.attachment_root.join(&upload.attachment);
        let directory = if published.try_exists()? {
            published
        } else {
            directory
        };
        let tx = self.store.0.transaction()?;
        let mut indexes = Vec::new();
        for (index, size, sha) in stored_chunks(&tx, &request.upload_id)? {
            ensure!(
                (0..chunk_count(upload.size)).contains(&index),
                "数据库中的分块编号超出范围"
            );
            let path = chunk_path(&directory, index);
            let valid = match read_chunk(&path) {
                Ok(data) => {
                    data.len() as i64 == size && hex(digest(&SHA256, &data).as_ref()) == sha
                }
                Err(error) if error.kind() == ErrorKind::NotFound => false,
                Err(error) => return Err(error.into()),
            };
            if valid {
                indexes.push(index);
            } else {
                if path.try_exists()? {
                    fs::remove_file(path)?;
                }
                tx.execute(
                    "DELETE FROM attachment_chunks WHERE upload_id=? AND chunk_index=?",
                    params![request.upload_id, index],
                )?;
            }
        }
        tx.commit()?;
        response.upload_id = request.upload_id.clone();
        if indexes.len() > 8192 {
            let mut bitmap = vec![0; *indexes.last().expect("nonempty indexes") as usize / 8 + 1];
            for index in indexes {
                bitmap[index as usize / 8] |= 1 << (index % 8);
            }
            response.received_bitmap = hex(&bitmap);
        } else {
            response.received_indexes = indexes;
        }
        Ok(())
    }

    fn attachment_commit(
        &mut self,
        request: &Message,
        upload: &Upload,
        response: &mut Message,
    ) -> Result<()> {
        let partial = self.partial_dir(&request.upload_id);
        let published = self.attachment_root.join(&upload.attachment);
        let already_moved = published.try_exists()?;
        let directory = if already_moved { &published } else { &partial };
        let tx = self
            .store
            .0
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        let mut whole = Digest::new(&SHA256);
        let chunks = stored_chunks(&tx, &request.upload_id)?;
        ensure!(chunks.len() as i64 == chunk_count(upload.size), INCOMPLETE);
        for (expected, (index, size, sha)) in chunks.into_iter().enumerate() {
            ensure!(
                expected as i64 == index && size == expected_cipher_size(upload.size, index),
                INCOMPLETE
            );
            let data = read_chunk(&chunk_path(directory, index))
                .with_context(|| format!("读取附件分块 {index}"))?;
            ensure!(
                data.len() as i64 == size && hex(digest(&SHA256, &data).as_ref()) == sha,
                "附件分块 {index} 完整性检查失败"
            );
            whole.update(&data);
        }
        let whole = hex(whole.finish().as_ref());
        let rows = tx.execute(
            "UPDATE attachments SET whole_sha256=?,committed_at=? WHERE attachment_id=? AND committed_at IS NULL",
            params![whole, now(), upload.attachment],
        )?;
        ensure!(rows == 1, Rejected("Attachment already committed"));
        tx.execute(
            "UPDATE room_quotas SET reserved_bytes=reserved_bytes-?1,used_bytes=used_bytes+?1 WHERE room=?2",
            params![upload.size, upload.room],
        )?;
        tx.execute(
            "DELETE FROM attachment_chunks WHERE upload_id=?",
            [&request.upload_id],
        )?;
        tx.execute(
            "DELETE FROM attachment_uploads WHERE upload_id=?",
            [&request.upload_id],
        )?;
        if !already_moved {
            fs::rename(&partial, &published).context("发布已验证的附件")?;
        }
        tx.commit()?;
        response.attachment_id = upload.attachment.clone();
        response.logical_size = upload.size;
        response.whole_sha256 = whole;
        response.content = "committed".into();
        Ok(())
    }

    fn attachment_download(
        &self,
        request: &Message,
        peer_room: &str,
        response: &mut Message,
    ) -> Result<()> {
        let denied = Rejected("Attachment download access denied");
        ensure!(valid_id(&request.attachment_id), denied);
        let size: i64 = self.store.0.query_row(
            "SELECT logical_size FROM attachments WHERE attachment_id=? AND room=? AND committed_at IS NOT NULL",
            params![request.attachment_id, peer_room],
            |row| row.get(0),
        ).optional()?.ok_or(denied)?;
        ensure!(
            (1..=MAX_ATTACHMENT_BYTES).contains(&size),
            "数据库中的附件大小无效"
        );
        let index = request.chunk_index.unwrap_or(0);
        ensure!(
            (0..chunk_count(size)).contains(&index),
            Rejected("Attachment download failed")
        );
        let path = chunk_path(&self.attachment_root.join(&request.attachment_id), index);
        let data = read_chunk(&path)?;
        ensure!(
            data.len() as i64 == expected_cipher_size(size, index),
            "附件分块 {index} 长度无效"
        );
        response.attachment_id = request.attachment_id.clone();
        response.chunk_index = request.chunk_index;
        response.cipher_sha256 = hex(digest(&SHA256, &data).as_ref());
        response.ciphertext = STANDARD.encode(data);
        response.content = if index + 1 == chunk_count(size) {
            "last"
        } else {
            "chunk"
        }
        .into();
        Ok(())
    }

    pub(super) fn cleanup_attachments(&mut self) -> Result<()> {
        let expired = {
            let mut statement = self.store.0.prepare(
                "SELECT upload_id,attachment_id,room,reserved_bytes FROM attachment_uploads
                 WHERE julianday(expires_at)<=julianday('now')",
            )?;
            statement
                .query_map([], |row| {
                    Ok((
                        row.get::<_, String>(0)?,
                        row.get::<_, String>(1)?,
                        row.get::<_, String>(2)?,
                        row.get::<_, i64>(3)?,
                    ))
                })?
                .collect::<rusqlite::Result<Vec<_>>>()?
        };
        for (upload, attachment, room, reserved) in expired {
            ensure!(
                valid_id(&upload) && valid_id(&attachment),
                "数据库中的上传或附件编号无效"
            );
            let partial = self.partial_dir(&upload);
            let published = self.attachment_root.join(&attachment);
            let tx = self
                .store
                .0
                .transaction_with_behavior(TransactionBehavior::Immediate)?;
            tx.execute("DELETE FROM attachment_chunks WHERE upload_id=?", [&upload])?;
            tx.execute(
                "DELETE FROM attachment_uploads WHERE upload_id=?",
                [&upload],
            )?;
            let removed = tx.execute(
                "DELETE FROM attachments WHERE attachment_id=? AND committed_at IS NULL",
                [&attachment],
            )?;
            tx.execute(
                "UPDATE room_quotas SET reserved_bytes=reserved_bytes-? WHERE room=?",
                params![reserved, room],
            )?;
            if partial.try_exists()? {
                fs::remove_dir_all(partial)?;
            }
            if removed == 1 && published.try_exists()? {
                fs::remove_dir_all(published)?;
            }
            tx.commit()?;
        }
        Ok(())
    }

    fn partial_dir(&self, upload: &str) -> PathBuf {
        self.attachment_root.join(".partial").join(upload)
    }
}

fn valid_id(id: &str) -> bool {
    Uuid::parse_str(id).is_ok_and(|value| value.to_string() == id)
}

fn chunk_count(size: i64) -> i64 {
    (size - 1) / ATTACHMENT_CHUNK_BYTES + 1
}

fn expected_cipher_size(size: i64, index: i64) -> i64 {
    (size - index * ATTACHMENT_CHUNK_BYTES).min(ATTACHMENT_CHUNK_BYTES) + ATTACHMENT_TAG_BYTES
}

fn chunk_path(dir: &Path, index: i64) -> PathBuf {
    dir.join(format!("{index}.bin"))
}

fn read_chunk(path: &Path) -> std::io::Result<Vec<u8>> {
    let mut data = Vec::new();
    fs::File::open(path)?
        .take(MAX_CIPHER_BYTES as u64 + 1)
        .read_to_end(&mut data)?;
    Ok(data)
}

fn write_chunk(path: &Path, data: &[u8]) -> Result<()> {
    let directory = path.parent().expect("chunk path includes upload directory");
    fs::create_dir_all(directory)?;
    let temporary = directory.join(format!(".chunk-{}", Uuid::new_v4()));
    let mut file = OpenOptions::new()
        .create_new(true)
        .write(true)
        .open(&temporary)?;
    let result = file.write_all(data).and_then(|()| file.sync_all());
    drop(file);
    if let Err(error) = result.and_then(|()| fs::rename(&temporary, path)) {
        fs::remove_file(&temporary)?;
        return Err(error.into());
    }
    Ok(())
}

fn stored_chunks(db: &rusqlite::Connection, upload: &str) -> Result<Vec<(i64, i64, String)>> {
    let mut statement = db.prepare(
        "SELECT chunk_index,cipher_size,cipher_sha256 FROM attachment_chunks WHERE upload_id=? ORDER BY chunk_index",
    )?;
    Ok(statement
        .query_map([upload], |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)))?
        .collect::<rusqlite::Result<Vec<_>>>()?)
}

pub(super) fn hex(bytes: &[u8]) -> String {
    const DIGITS: &[u8; 16] = b"0123456789abcdef";
    let mut result = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        result.push(DIGITS[(byte >> 4) as usize] as char);
        result.push(DIGITS[(byte & 15) as usize] as char);
    }
    result
}
