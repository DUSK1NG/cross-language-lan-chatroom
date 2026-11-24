use std::fs;

use anyhow::{Result, ensure};
use base64::{Engine, engine::general_purpose::STANDARD as BASE64};
use chrono::{DateTime, SecondsFormat, Utc};
use image::{ImageFormat, ImageReader};
use lan_chat_protocol::{
    MAX_ONLINE_FILE_BYTES, MAX_ONLINE_IMAGE_BYTES, Message, ONLINE_CHUNK_BYTES,
    is_legacy_generated_message_id,
};
use rusqlite::{OptionalExtension, params};
use uuid::Uuid;

use super::{Online, files, private_target};
use crate::store::{Identity, Rejected, Store, conversation, now, user_code};

const FILE_ACCESS: Rejected = Rejected("附件不存在或你没有访问权限");
const FILE_CHUNK: Rejected = Rejected("文件分块顺序或长度无效");
const MAX_FILE_CHUNKS: i64 = (MAX_ONLINE_FILE_BYTES - 1) / ONLINE_CHUNK_BYTES + 1;
const IMAGE_REQUEST: Rejected = Rejected("图片分片的消息编号、收件人、大小或序号无效");
const IMAGE_READ: Rejected = Rejected("图片读取的会话、消息编号或序号无效");
const IMAGE_FORMAT: Rejected = Rejected("图片须为 JPEG 或 PNG，宽高不超过 4096 像素");

pub(super) struct ImageUpload {
    message_id: String,
    target: String,
    pub(super) group_id: String,
    size: i64,
    offset: i64,
    reference: String,
}

pub(super) struct PublishedImage {
    reference: String,
    digest: String,
    pub size: i64,
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
        let peer = self.peers.get_mut(&id).expect("registered client");
        if index == 0 {
            ensure!(
                peer.image.is_none(),
                Rejected("当前连接已有进行中的图片上传")
            );
            peer.image = Some(ImageUpload {
                message_id: request.message_id.clone(),
                target: target.clone(),
                group_id: request.group_id.clone(),
                size: request.image_size,
                offset: 0,
                reference: format!("image-{}", Uuid::new_v4()),
            });
        }
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
            return Ok(());
        }
        let upload = peer.image.take().expect("completed upload");
        let image = ImageReader::open(&staging)?
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
        let message = Message {
            message_id: upload.message_id,
            target_user_code: upload.target,
            group_id: upload.group_id,
            content: "[图片]".into(),
            ..Message::default()
        };
        let image = PublishedImage {
            reference: upload.reference,
            digest: files::finalize(&path, upload.size)?,
            size: upload.size,
        };
        if let Some(group) = group {
            self.send_group(id, identity, &group, &message, Some(&image))
        } else {
            self.private_chat(id, identity, &message, Some(&image))
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
            self.store
                .begin_file(identity, &target, request, &mut reply)?;
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
                    if let Some(message) =
                        self.store.commit_file(identity, &request.attachment_id)?
                    {
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

    pub fn cleanup_files(&mut self) -> Result<()> {
        let tx = self.store.0.transaction()?;
        tx.execute(
            "DELETE FROM online_files WHERE (chat_id IS NULL AND expires<=?)
             OR (chat_id IS NOT NULL AND NOT EXISTS (SELECT 1 FROM chat_messages m WHERE m.id=online_files.chat_id))",
            [Utc::now().timestamp()],
        )?;
        tx.execute(
            "DELETE FROM online_file_chunks WHERE file_id NOT IN (SELECT id FROM online_files)",
            [],
        )?;
        tx.commit()?;
        let active: Vec<_> = self
            .peers
            .values()
            .filter_map(|peer| peer.image.as_ref().map(|image| image.reference.as_str()))
            .collect();
        files::cleanup_orphans(&self.store.0, &active)?;
        Ok(())
    }
}

impl Store {
    pub(super) fn insert_online_message(
        &mut self,
        message: &Message,
        kind: &str,
        key: &str,
        author: &str,
        target: &str,
        image: Option<&PublishedImage>,
    ) -> Result<bool> {
        let tx = self.0.transaction()?;
        let changed = Self::persist_chat(&tx, message, kind, key, author, target, None)?;
        if let Some(image) = image.filter(|_| changed) {
            tx.execute("UPDATE chat_messages SET online_image_ref=?,online_image_size=?,online_image_sha256=? WHERE kind=? AND conversation_key=? AND sender_code=? AND message_id=?",
                params![image.reference, image.size, image.digest, kind, key, author, message.message_id])?;
        }
        tx.commit()?;
        Ok(changed)
    }

    fn begin_file(
        &self,
        identity: &Identity,
        target: &str,
        request: &Message,
        reply: &mut Message,
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

    fn commit_file(&mut self, identity: &Identity, id: &str) -> Result<Option<Message>> {
        let tx = self.0.transaction()?;
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
            "INSERT INTO chat_messages(message_id,kind,conversation_key,room,sender_username,sender_code,target_code,content,created_at)
             VALUES (?,?,?,?,?,?,?,?,?) RETURNING id",
            params![id, kind, key, room, identity.username, identity.code, recipient, content, created_at],
            |row| row.get::<_, i64>(0),
        )?;
        tx.execute(
            "UPDATE online_files SET chat_id=?,disk_sha256=? WHERE id=?",
            params![chat_id, digest, id],
        )?;
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
        Ok(())
    }
}

fn file_index(request: &Message) -> Result<i64> {
    let index = request.chunk_index.unwrap_or(0);
    ensure!((0..MAX_FILE_CHUNKS).contains(&index), FILE_CHUNK);
    Ok(index)
}
