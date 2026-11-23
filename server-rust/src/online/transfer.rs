use std::io::Cursor;

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

use super::{Online, private_target};
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
    size: i64,
    data: Vec<u8>,
}

impl Online {
    pub(super) fn receive_image(
        &mut self,
        id: Uuid,
        identity: &Identity,
        request: &Message,
    ) -> Result<()> {
        let target = private_target(identity, request).map_err(|_| IMAGE_REQUEST)?;
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

        let peer = self.peers.get_mut(&id).expect("registered client");
        if index == 0 {
            ensure!(
                peer.image.is_none(),
                Rejected("当前连接已有进行中的图片上传")
            );
            peer.image = Some(ImageUpload {
                message_id: request.message_id.clone(),
                target: target.clone(),
                size: request.image_size,
                data: Vec::with_capacity(request.image_size as usize),
            });
        }
        let upload = peer
            .image
            .as_mut()
            .filter(|upload| {
                upload.message_id == request.message_id
                    && upload.target == target
                    && upload.size == request.image_size
                    && upload.data.len() as i64 == offset
            })
            .ok_or(Rejected("图片分片与当前上传不匹配"))?;
        upload.data.extend(data);
        if (upload.data.len() as i64) < upload.size {
            return Ok(());
        }
        let upload = peer.image.take().expect("completed upload");
        let image = ImageReader::new(Cursor::new(&upload.data))
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
            content: "[图片]".into(),
            ..Message::default()
        };
        self.private_chat(id, identity, &message, Some(&upload.data))
    }

    pub(super) fn read_image(
        &mut self,
        id: Uuid,
        identity: &Identity,
        request: &Message,
    ) -> Result<()> {
        let peer = private_target(identity, request).map_err(|_| IMAGE_READ)?;
        let author = user_code(&request.user_code).map_err(|_| IMAGE_READ)?;
        let index = request.chunk_index.unwrap_or(0);
        ensure!(
            (author == identity.code || author == peer)
                && (1..=64).contains(&request.message_id.len())
                && (0..=(MAX_ONLINE_IMAGE_BYTES - 1) / ONLINE_CHUNK_BYTES).contains(&index),
            IMAGE_READ
        );
        let offset = index * ONLINE_CHUNK_BYTES;
        let (data, size): (Vec<u8>, i64) = self.store.0.query_row(
            "SELECT substr(online_image, ?, ?), length(online_image) FROM chat_messages
             WHERE kind='private' AND conversation_key=? AND sender_code=? AND message_id=? AND recalled=0 AND length(online_image)>?",
            params![offset + 1, ONLINE_CHUNK_BYTES, conversation(&identity.code, &peer), author, request.message_id, offset],
            |row| Ok((row.get(0)?, row.get(1)?)),
        ).optional()?.ok_or(Rejected("当前会话中没有该图片分片"))?;
        self.deliver(
            id,
            Message {
                kind: request.kind.clone(),
                message_id: request.message_id.clone(),
                user_code: request.user_code.clone(),
                target_user_code: request.target_user_code.clone(),
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
            let target = private_target(identity, request)?;
            self.store
                .begin_file(identity, &target, request, &mut reply)?;
        } else {
            let invalid_id = Rejected("无效的附件编号");
            let uuid = Uuid::parse_str(&request.attachment_id).map_err(|_| invalid_id)?;
            ensure!(uuid.to_string() == request.attachment_id, invalid_id);
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
                        self.publish_private(&identity.code, &message);
                    }
                }
                "dm_file_get" => {
                    let index = file_index(request)?;
                    self.store.read_file(identity, index, &mut reply)?;
                }
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
        Ok(())
    }
}

impl Store {
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
        self.account(target)?;
        let id = Uuid::new_v4().to_string();
        let now = Utc::now().timestamp();
        let expires = now + 24 * 60 * 60;
        let inserted = self.0.execute(
            "INSERT INTO online_files(id,owner,recipient,name,size,expires)
             SELECT ?1,?2,?3,?4,?5,?6 WHERE (SELECT COALESCE(SUM(size),0) FROM online_files)+?5<=2147483648
               AND (SELECT COUNT(*) FROM online_files)<20000
               AND (SELECT COUNT(*) FROM online_files WHERE owner=?2 AND chat_id IS NULL AND expires>?7)<4",
            params![id, identity.code, target, name, size, expires, now],
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
        let (size, received): (i64, i64) = tx.query_row(
            "SELECT size,received FROM online_files WHERE id=? AND owner=? AND chat_id IS NULL AND expires>?",
            params![request.attachment_id, identity.code, Utc::now().timestamp()],
            |row| Ok((row.get(0)?, row.get(1)?)),
        ).optional()?.ok_or(FILE_ACCESS)?;
        let expected = (size - received).min(ONLINE_CHUNK_BYTES);
        ensure!(
            expected > 0 && data.len() as i64 == expected && received == index * ONLINE_CHUNK_BYTES,
            FILE_CHUNK
        );
        tx.execute(
            "INSERT INTO online_file_chunks(file_id,chunk_index,data) VALUES (?,?,?)",
            params![request.attachment_id, index, data],
        )?;
        tx.execute(
            "UPDATE online_files SET received=received+? WHERE id=?",
            params![expected, request.attachment_id],
        )?;
        tx.commit()?;
        Ok(())
    }

    fn commit_file(&mut self, identity: &Identity, id: &str) -> Result<Option<Message>> {
        let tx = self.0.transaction()?;
        let (recipient, name, size, received, published): (String, String, i64, i64, Option<i64>) =
            tx.query_row(
                "SELECT recipient,name,size,received,chat_id FROM online_files
             WHERE id=? AND owner=? AND (chat_id IS NOT NULL OR expires>?)",
                params![id, identity.code, Utc::now().timestamp()],
                |row| {
                    Ok((
                        row.get(0)?,
                        row.get(1)?,
                        row.get(2)?,
                        row.get(3)?,
                        row.get(4)?,
                    ))
                },
            )
            .optional()?
            .ok_or(FILE_ACCESS)?;
        if published.is_some() {
            return Ok(None);
        }
        ensure!(size == received, Rejected("文件尚未完整上传"));
        let created_at = now();
        let content = format!("[文件] {name}");
        let chat_id = tx.query_row(
            "INSERT INTO chat_messages(message_id,kind,conversation_key,room,sender_username,sender_code,target_code,content,created_at)
             VALUES (?,'private',?,'',?,?,?,?,?) RETURNING id",
            params![id, conversation(&identity.code, &recipient), identity.username, identity.code, recipient, content, created_at],
            |row| row.get::<_, i64>(0),
        )?;
        tx.execute(
            "UPDATE online_files SET chat_id=? WHERE id=?",
            params![chat_id, id],
        )?;
        tx.commit()?;
        Ok(Some(Message {
            kind: "private_chat".into(),
            message_id: id.into(),
            username: identity.username.clone(),
            user_code: identity.display_code.clone(),
            target_user_code: recipient,
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
        let (name, size, data): (String, i64, Option<Vec<u8>>) = self
            .0
            .query_row(
                "SELECT f.name,f.size,c.data FROM online_files f
             JOIN chat_messages m ON m.id=f.chat_id AND m.recalled=0
             LEFT JOIN online_file_chunks c ON c.file_id=f.id AND c.chunk_index=?1
             WHERE f.id=?2 AND (f.owner=?3 OR f.recipient=?3)
               AND (c.chunk_index IS NOT NULL OR (f.size=0 AND ?1=0))",
                params![index, reply.attachment_id, identity.code],
                |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
            )
            .optional()?
            .ok_or(FILE_ACCESS)?;
        reply.file_name = name;
        reply.logical_size = size;
        reply.file_data = data.map_or_else(String::new, |data| BASE64.encode(data));
        reply.chunk_index = Some(index);
        reply.chunk_size = ONLINE_CHUNK_BYTES;
        Ok(())
    }
}

fn file_index(request: &Message) -> Result<i64> {
    let index = request.chunk_index.unwrap_or(0);
    ensure!((0..MAX_FILE_CHUNKS).contains(&index), FILE_CHUNK);
    Ok(index)
}
