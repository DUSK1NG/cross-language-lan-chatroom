use std::{
    collections::HashMap,
    path::{Path, PathBuf},
};

use anyhow::{Context, Result, ensure};
use base64::{Engine, engine::general_purpose::STANDARD};
use lan_chat_protocol::{
    ATTACHMENT_CHUNK_BYTES, MAX_ATTACHMENT_BYTES, MAX_ONLINE_FILE_BYTES, MAX_ONLINE_IMAGE_BYTES,
    Message, ONLINE_CHUNK_BYTES,
};
use ring::{
    aead::{AES_256_GCM, Aad, LessSafeKey, Nonce, UnboundKey},
    digest::{SHA256, digest},
    rand::{SecureRandom, SystemRandom},
};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use tokio::{
    fs::{File, OpenOptions},
    io::{AsyncReadExt, AsyncWriteExt},
};
use uuid::Uuid;

use crate::{
    Client, Payload, checksum, filename,
    mls::Mls,
    session::{Network, TransportLost},
};

const MLS: &str = "mls-v1";

pub struct Cryptography {
    pub mls: Mls,
    identity: String,
    groups: HashMap<String, Group>,
    manifests: HashMap<String, Manifest>,
    message_attachments: HashMap<(String, String), String>,
}

struct Group {
    room: String,
    epoch: u64,
    welcome_digest: String,
}

impl Cryptography {
    pub fn new(identity: &str) -> Result<Self> {
        let identity = identity.to_ascii_lowercase();
        Ok(Self {
            mls: Mls::new(&identity)?,
            identity,
            groups: HashMap::new(),
            manifests: HashMap::new(),
            message_attachments: HashMap::new(),
        })
    }
}

#[derive(Clone, Serialize, Deserialize)]
struct Manifest {
    v: u8,
    attachment_id: String,
    file_name: String,
    room: String,
    group_id: String,
    logical_size: i64,
    chunk_size: i64,
    epoch: u64,
    key: String,
}

impl Manifest {
    fn check(&self) -> Result<()> {
        ensure!(
            self.v == 1
                && Uuid::parse_str(&self.attachment_id).is_ok()
                && valid_name(&self.file_name)
                && !self.room.is_empty()
                && !self.group_id.is_empty()
                && self.epoch > 0
                && (1..=MAX_ATTACHMENT_BYTES).contains(&self.logical_size)
                && self.chunk_size == ATTACHMENT_CHUNK_BYTES
                && STANDARD.decode(&self.key)?.len() == 32,
            "加密附件清单无效"
        );
        Ok(())
    }

    fn count(&self) -> i64 {
        (self.logical_size - 1) / self.chunk_size + 1
    }

    fn size(&self, index: i64) -> usize {
        (self.logical_size - index * self.chunk_size).min(self.chunk_size) as usize
    }

    fn key_nonce(&self, index: i64) -> Result<(LessSafeKey, Nonce, Vec<u8>)> {
        let key = UnboundKey::new(&AES_256_GCM, &STANDARD.decode(&self.key)?)
            .map_err(|_| anyhow::anyhow!("附件密钥无效"))?;
        let aad = format!("lan-chat/attachment-aead/v1\nattachment_id={}\nroom={}\ngroup_id={}\nlogical_size={}\nchunk_size={}\nchunk_index={}\n",
            self.attachment_id,self.room,self.group_id,self.logical_size,self.chunk_size,index).into_bytes();
        let nonce = digest(
            &SHA256,
            &[b"lan-chat/attachment-nonce/v1\n".as_slice(), &aad].concat(),
        );
        let nonce = Nonce::assume_unique_for_key(nonce.as_ref()[..12].try_into()?);
        Ok((LessSafeKey::new(key), nonce, aad))
    }

    fn summary(&self) -> Value {
        json!({"attachmentId":self.attachment_id,"fileName":self.file_name,"logicalSize":self.logical_size,
            "totalChunks":self.count(),"status":"available"})
    }
}

#[derive(Serialize, Deserialize)]
struct Envelope {
    v: u8,
    alg: String,
    group_id: String,
    epoch: u64,
    ciphertext: String,
}

impl Client {
    pub(crate) fn mls_event(&self, network: &Network, message: &Message) -> Result<()> {
        let Some(crypto) = &network.crypto else {
            return Ok(());
        };
        let mut crypto = crypto.lock().expect("MLS state");
        let id = &message.group_id;
        match message.kind.as_str() {
            "mls.group.welcome" if !message.welcome.is_empty() => {
                ensure!(
                    message
                        .target_user_code
                        .eq_ignore_ascii_case(&crypto.identity)
                        && message.epoch > 0
                        && message.welcome_digest == checksum(message.welcome.as_bytes()),
                    "MLS 邀请的目标、版本或校验值无效"
                );
                let new = if let Some(existing) = crypto.groups.get(id) {
                    ensure!(
                        existing.room == message.room
                            && existing.welcome_digest == message.welcome_digest,
                        "MLS 邀请与现有会话冲突"
                    );
                    false
                } else {
                    crypto.mls.join(id, &STANDARD.decode(&message.welcome)?)?;
                    crypto.groups.insert(
                        id.clone(),
                        Group {
                            room: message.room.clone(),
                            epoch: message.epoch,
                            welcome_digest: message.welcome_digest.clone(),
                        },
                    );
                    true
                };
                network.send(Message {
                    kind: "mls.group.welcome.accept".into(),
                    protocol_version: MLS.into(),
                    group_id: id.clone(),
                    room: message.room.clone(),
                    target_user_code: crypto.identity.clone(),
                    epoch: message.epoch,
                    proposal_id: message.proposal_id.clone(),
                    welcome_digest: message.welcome_digest.clone(),
                    ..Message::default()
                })?;
                if new {
                    network.send(Message {
                        kind: "mls.key_package.publish".into(),
                        protocol_version: MLS.into(),
                        key_package: opaque(crypto.mls.key_package()?)?,
                        ..Message::default()
                    })?;
                }
            }
            "mls.group.proposal" | "mls.group.commit" => {
                let value = if message.kind == "mls.group.commit" {
                    &message.commit
                } else {
                    &message.proposal
                };
                if value.is_empty() {
                    return Ok(());
                }
                let Some(group) = crypto.groups.get(id) else {
                    return Ok(());
                };
                ensure!(
                    group.room == message.room && group.epoch.checked_add(1) == Some(message.epoch),
                    "MLS 成员变更版本不连续"
                );
                let body = STANDARD.decode(value)?;
                if message.kind == "mls.group.commit" {
                    crypto.mls.handle_commit(id, &body)?;
                    crypto.groups.get_mut(id).expect("known MLS group").epoch = message.epoch;
                } else {
                    crypto.mls.handle_proposal(id, &body)?;
                }
            }
            _ => (),
        }
        Ok(())
    }

    pub(crate) fn attachment_summary(
        &self,
        network: &Network,
        message: &Message,
    ) -> Result<Option<Value>> {
        let (Some(crypto), Some(encrypted)) = (&network.crypto, &message.crypto) else {
            return Ok(None);
        };
        if message.recalled || message.kind != "chat" {
            return Ok(None);
        }
        let mut crypto = crypto.lock().expect("MLS state");
        let key = (
            message.user_code.to_ascii_lowercase(),
            message.message_id.clone(),
        );
        if let Some(id) = crypto.message_attachments.get(&key) {
            return Ok(Some(crypto.manifests[id].summary()));
        }
        if encrypted.get("alg").and_then(Value::as_str) != Some(MLS) {
            return Ok(None);
        }
        let envelope: Envelope = serde_json::from_value(encrypted.clone())?;
        let Some(group) = crypto.groups.get(&envelope.group_id) else {
            return Ok(None);
        };
        ensure!(
            envelope.v == 1 && group.room == message.room && envelope.epoch <= group.epoch,
            "加密附件群组信息不匹配"
        );
        let plain = crypto
            .mls
            .unprotect(&envelope.group_id, &STANDARD.decode(envelope.ciphertext)?)?;
        let manifest: Manifest = serde_json::from_slice(&plain)?;
        manifest.check()?;
        ensure!(
            manifest.group_id == envelope.group_id
                && manifest.room == message.room
                && manifest.epoch == envelope.epoch,
            "附件清单与消息不匹配"
        );
        let summary = manifest.summary();
        crypto
            .message_attachments
            .insert(key, manifest.attachment_id.clone());
        crypto
            .manifests
            .insert(manifest.attachment_id.clone(), manifest);
        Ok(Some(summary))
    }

    pub(crate) async fn upload(
        &self,
        network: &Network,
        id: &str,
        room: &str,
        path: &Path,
    ) -> Result<()> {
        let crypto = network.crypto.as_ref().context("加密组件未就绪")?;
        let members = {
            let state = self.state.lock().expect("client state");
            state
                .members
                .iter()
                .filter(|member| {
                    member.room == room
                        && !member
                            .user_code
                            .eq_ignore_ascii_case(&state.identity.user_code)
                })
                .map(|member| member.user_code.clone())
                .collect::<Vec<_>>()
        };
        ensure!(!members.is_empty(), "频道中没有可用的在线接收者");
        let mut file = File::open(path).await?;
        let info = file.metadata().await?;
        ensure!(
            info.is_file() && (1..=MAX_ATTACHMENT_BYTES as u64).contains(&info.len()),
            "请选择大于 0 字节且不超过 5 GiB 的文件"
        );
        let group = format!(
            "lan-chat-attachment/{}",
            checksum(format!("{room}\0{id}").as_bytes())
        );
        crypto.lock().expect("MLS state").mls.create(&group)?;
        for (index, member) in members.iter().enumerate() {
            let request = Message {
                room: room.into(),
                group_id: group.clone(),
                epoch: index as u64 + 1,
                proposal_id: format!("{id}-m{index}"),
                protocol_version: MLS.into(),
                ..Message::default()
            };
            let package = network
                .request(Message {
                    kind: "mls.key_package.fetch".into(),
                    target_user_code: member.clone(),
                    ..request.clone()
                })
                .await?;
            let add = crypto
                .lock()
                .expect("MLS state")
                .mls
                .add(&group, &STANDARD.decode(package.key_package)?)?;
            network
                .request(Message {
                    kind: "mls.group.proposal".into(),
                    action: "add".into(),
                    target_user_code: member.clone(),
                    proposal: opaque(add.proposal)?,
                    ..request.clone()
                })
                .await?;
            network
                .request(Message {
                    kind: "mls.group.commit".into(),
                    commit: opaque(add.commit)?,
                    ..request.clone()
                })
                .await?;
            let welcome = opaque(add.welcome)?;
            network
                .request(Message {
                    kind: "mls.group.welcome".into(),
                    target_user_code: member.clone(),
                    welcome_digest: checksum(welcome.as_bytes()),
                    welcome,
                    ..request
                })
                .await?;
        }
        let init = network
            .request(Message {
                kind: "attachment.init".into(),
                room: room.into(),
                logical_size: info.len() as i64,
                ..Message::default()
            })
            .await?;
        ensure!(
            Uuid::parse_str(&init.upload_id).is_ok(),
            "服务端返回了无效的上传编号"
        );
        let mut key = [0; 32];
        SystemRandom::new()
            .fill(&mut key)
            .map_err(|_| anyhow::anyhow!("附件密钥生成失败"))?;
        let manifest = Manifest {
            v: 1,
            attachment_id: init.attachment_id,
            file_name: filename(path)?.into(),
            room: room.into(),
            group_id: group.clone(),
            logical_size: info.len() as i64,
            chunk_size: init.chunk_size,
            epoch: members.len() as u64,
            key: STANDARD.encode(key),
        };
        manifest.check()?;
        self.attachment_event(id,json!({"type":"attachment.init","attachmentId":manifest.attachment_id,"uploadId":init.upload_id,
            "chunkSize":manifest.chunk_size,"chunkIndex":0,"receivedIndexes":[],"expiresAt":init.expires_at,"logicalSize":manifest.logical_size}));
        for index in 0..manifest.count() {
            let mut data = vec![0; manifest.size(index)];
            file.read_exact(&mut data).await?;
            let (key, nonce, aad) = manifest.key_nonce(index)?;
            key.seal_in_place_append_tag(nonce, Aad::from(aad), &mut data)
                .map_err(|_| anyhow::anyhow!("附件分块加密失败"))?;
            let part = Message {
                kind: "attachment.chunk".into(),
                upload_id: init.upload_id.clone(),
                chunk_index: Some(index),
                ciphertext: STANDARD.encode(&data),
                cipher_sha256: checksum(&data),
                ..Message::default()
            };
            let mut interrupted = false;
            loop {
                let epoch = network.epoch();
                if interrupted {
                    let receipt = network
                        .request(Message {
                            kind: "attachment.resume".into(),
                            upload_id: init.upload_id.clone(),
                            ..Message::default()
                        })
                        .await;
                    match receipt {
                        Ok(receipt) => {
                            ensure!(
                                receipt.upload_id == init.upload_id,
                                "续传确认与上传请求不符"
                            );
                            let indexes = received_indexes(&receipt, manifest.count())?;
                            self.attachment_event(id, json!({"type":"attachment.resume","attachmentId":manifest.attachment_id,"receivedIndexes":indexes}));
                            if indexes.contains(&index) {
                                break;
                            }
                        }
                        Err(error) if error.is::<TransportLost>() => {
                            network.reconnected(epoch).await?;
                            continue;
                        }
                        Err(error) => return Err(error),
                    }
                }
                match network.request(part.clone()).await {
                    Ok(ack) => {
                        ensure!(
                            ack.chunk_index.unwrap_or(0) == index
                                && ack.upload_id == init.upload_id,
                            "分块确认与上传请求不符"
                        );
                        self.attachment_event(id, json!({"type":"attachment.chunk","attachmentId":manifest.attachment_id,"chunkIndex":index}));
                        break;
                    }
                    Err(error) if error.is::<TransportLost>() => {
                        network.reconnected(epoch).await?;
                        interrupted = true;
                    }
                    Err(error) => return Err(error),
                }
            }
        }
        let commit = network
            .request(Message {
                kind: "attachment.commit".into(),
                upload_id: init.upload_id,
                ..Message::default()
            })
            .await?;
        ensure!(
            commit.attachment_id == manifest.attachment_id,
            "附件提交确认不匹配"
        );
        let message_id = format!("am-{}", &checksum(manifest.attachment_id.as_bytes())[..56]);
        let envelope = {
            let mut crypto = crypto.lock().expect("MLS state");
            let sealed = crypto
                .mls
                .protect(&group, &serde_json::to_vec(&manifest)?)?;
            let envelope = Envelope {
                v: 1,
                alg: MLS.into(),
                group_id: group.clone(),
                epoch: manifest.epoch,
                ciphertext: opaque(sealed)?,
            };
            crypto.groups.insert(
                group,
                Group {
                    room: room.into(),
                    epoch: manifest.epoch,
                    welcome_digest: String::new(),
                },
            );
            let identity = crypto.identity.clone();
            crypto.message_attachments.insert(
                (identity, message_id.clone()),
                manifest.attachment_id.clone(),
            );
            crypto
                .manifests
                .insert(manifest.attachment_id.clone(), manifest);
            envelope
        };
        network.send(Message {
            kind: "chat".into(),
            message_id,
            room: room.into(),
            crypto: Some(serde_json::to_value(envelope)?),
            ..Message::default()
        })?;
        self.attachment_event(id,json!({"type":"attachment.commit","attachmentId":commit.attachment_id,"content":commit.content}));
        Ok(())
    }

    pub(crate) async fn download(&self, network: &Network, id: &str) -> Result<()> {
        let manifest = network
            .crypto
            .as_ref()
            .context("加密组件未就绪")?
            .lock()
            .expect("MLS state")
            .manifests
            .get(id)
            .cloned()
            .context("找不到已解密的附件")?;
        let Some(path) = self.save(&manifest.file_name).await? else {
            return Ok(());
        };
        let (temp, mut file) = TemporaryFile::open(&path).await?;
        for index in 0..manifest.count() {
            let response = network
                .request(Message {
                    kind: "attachment.download".into(),
                    attachment_id: id.into(),
                    chunk_index: Some(index),
                    ..Message::default()
                })
                .await?;
            let mut data = STANDARD.decode(response.ciphertext)?;
            ensure!(
                response.attachment_id == id
                    && response.chunk_index.unwrap_or(0) == index
                    && checksum(&data) == response.cipher_sha256,
                "下载的附件分块校验失败"
            );
            let (key, nonce, aad) = manifest.key_nonce(index)?;
            let plain = key
                .open_in_place(nonce, Aad::from(aad), &mut data)
                .map_err(|_| anyhow::anyhow!("附件分块解密失败"))?;
            ensure!(
                plain.len() == manifest.size(index),
                "附件分块长度与清单不符"
            );
            file.write_all(plain).await?;
        }
        temp.commit(file, &path).await
    }

    pub(crate) async fn upload_online(
        &self,
        network: &Network,
        id: &str,
        target: &str,
        group: &str,
        path: &Path,
    ) -> Result<()> {
        let name = filename(path)?;
        ensure!(
            path.is_absolute() && valid_name(name),
            "请选择有效的本地文件"
        );
        let mut file = File::open(path).await?;
        let info = file.metadata().await?;
        ensure!(
            info.is_file() && info.len() <= MAX_ONLINE_FILE_BYTES as u64,
            "请选择不超过 32 MiB 的普通文件"
        );
        let size = info.len() as i64;
        let init = network
            .request(Message {
                kind: "dm_file_init".into(),
                target_user_code: target.into(),
                group_id: group.into(),
                file_name: name.into(),
                logical_size: size,
                ..Message::default()
            })
            .await?;
        ensure!(
            Uuid::parse_str(&init.attachment_id).is_ok() && init.chunk_size == ONLINE_CHUNK_BYTES,
            "服务端返回了无效的附件上传信息"
        );
        self.attachment_event(id,json!({"type":"attachment.init","attachmentId":init.attachment_id,"uploadId":init.upload_id,
            "chunkIndex":0,"chunkSize":init.chunk_size,"receivedIndexes":[],"expiresAt":init.expires_at,"logicalSize":size,"fileName":name}));
        let mut buffer = vec![0; ONLINE_CHUNK_BYTES as usize];
        for (index, offset) in (0..size).step_by(ONLINE_CHUNK_BYTES as usize).enumerate() {
            let part = &mut buffer[..(size - offset).min(ONLINE_CHUNK_BYTES) as usize];
            file.read_exact(part).await?;
            let ack = network
                .request(Message {
                    kind: "dm_file_chunk".into(),
                    attachment_id: init.attachment_id.clone(),
                    chunk_index: Some(index as i64),
                    file_data: STANDARD.encode(part),
                    ..Message::default()
                })
                .await?;
            ensure!(
                ack.attachment_id == init.attachment_id
                    && ack.chunk_index.unwrap_or(0) == index as i64,
                "附件上传确认与请求不符"
            );
            self.attachment_event(id,json!({"type":"attachment.chunk","attachmentId":init.attachment_id,"chunkIndex":index}));
        }
        let commit = network
            .request(Message {
                kind: "dm_file_commit".into(),
                attachment_id: init.attachment_id.clone(),
                ..Message::default()
            })
            .await?;
        ensure!(
            commit.attachment_id == init.attachment_id,
            "附件提交确认与请求不符"
        );
        Ok(())
    }

    pub(crate) async fn download_online(&self, network: &Network, id: &str) -> Result<()> {
        let request = |index| Message {
            kind: "dm_file_get".into(),
            attachment_id: id.into(),
            chunk_index: Some(index),
            ..Message::default()
        };
        let mut chunk = network.request(request(0)).await?;
        let (name, size) = (chunk.file_name.clone(), chunk.logical_size);
        ensure!(
            valid_name(&name) && (0..=MAX_ONLINE_FILE_BYTES).contains(&size),
            "服务端返回了无效的文件名或大小"
        );
        let Some(path) = self.save(&name).await? else {
            return Ok(());
        };
        let (temporary, mut file) = TemporaryFile::open(&path).await?;
        let mut index = 0;
        let mut left = size;
        loop {
            let data = STANDARD.decode(&chunk.file_data)?;
            ensure!(
                chunk.attachment_id == id
                    && chunk.chunk_index.unwrap_or(0) == index
                    && chunk.file_name == name
                    && chunk.logical_size == size
                    && chunk.chunk_size == ONLINE_CHUNK_BYTES
                    && data.len() as i64 == left.min(ONLINE_CHUNK_BYTES),
                "下载的文件分块与请求不符"
            );
            file.write_all(&data).await?;
            left -= data.len() as i64;
            if left == 0 {
                break;
            }
            index += 1;
            chunk = network.request(request(index)).await?;
        }
        temporary.commit(file, &path).await
    }

    pub(crate) async fn upload_image(
        &self,
        network: &Network,
        target: &str,
        group: &str,
        id: &str,
        path: &Path,
    ) -> Result<()> {
        let _transfer = network
            .transfer
            .try_lock()
            .context("请等待正在发送的图片")?;
        let mut data = Vec::new();
        File::open(path)
            .await?
            .take(MAX_ONLINE_IMAGE_BYTES as u64 + 1)
            .read_to_end(&mut data)
            .await?;
        ensure!(
            (1..=MAX_ONLINE_IMAGE_BYTES as usize).contains(&data.len()),
            "图片大小须在 1 字节至 4 MiB 之间"
        );
        image_mime(&data)?;
        for (index, chunk) in data.chunks(ONLINE_CHUNK_BYTES as usize).enumerate() {
            network.send(Message {
                kind: "dm_image_chunk".into(),
                message_id: id.into(),
                target_user_code: target.into(),
                group_id: group.into(),
                image_size: data.len() as i64,
                chunk_index: Some(index as i64),
                image_data: STANDARD.encode(chunk),
                ..Message::default()
            })?;
        }
        Ok(())
    }

    pub(crate) async fn read_image(&self, network: &Network, request: Payload) -> Result<String> {
        ensure!(
            (1..=MAX_ONLINE_IMAGE_BYTES).contains(&request.image_size),
            "图片大小无效"
        );
        let mut data = Vec::with_capacity(request.image_size as usize);
        for (index, offset) in (0..request.image_size)
            .step_by(ONLINE_CHUNK_BYTES as usize)
            .enumerate()
        {
            let response = network
                .request(Message {
                    kind: "dm_image_get".into(),
                    message_id: request.message_id.clone(),
                    user_code: request.form.user_code.clone(),
                    target_user_code: request.target_user_code.clone(),
                    group_id: request.group_id.clone(),
                    chunk_index: Some(index as i64),
                    ..Message::default()
                })
                .await?;
            let chunk = STANDARD.decode(response.image_data)?;
            ensure!(
                response.message_id == request.message_id
                    && response.group_id == request.group_id
                    && response.image_size == request.image_size
                    && response.chunk_index.unwrap_or(0) == index as i64
                    && chunk.len() as i64 == (request.image_size - offset).min(ONLINE_CHUNK_BYTES),
                "收到的图片分片与请求不符"
            );
            data.extend_from_slice(&chunk);
        }
        Ok(format!(
            "data:{};base64,{}",
            image_mime(&data)?,
            STANDARD.encode(data)
        ))
    }
}

fn received_indexes(receipt: &Message, count: i64) -> Result<Vec<i64>> {
    let mut indexes = receipt.received_indexes.clone();
    let bitmap = receipt.received_bitmap.as_bytes();
    ensure!(
        bitmap.len().is_multiple_of(2) && bitmap.len() / 2 <= ((count + 7) / 8) as usize,
        "服务端返回了无效的续传位图"
    );
    for (offset, pair) in bitmap.as_chunks::<2>().0.iter().enumerate() {
        let byte = u8::from_str_radix(std::str::from_utf8(pair)?, 16).context("读取续传位图")?;
        for bit in 0..8 {
            if byte & (1 << bit) != 0 {
                indexes.push(offset as i64 * 8 + bit);
            }
        }
    }
    ensure!(
        indexes.iter().all(|index| (0..count).contains(index)),
        "服务端返回了无效的续传索引"
    );
    Ok(indexes)
}

fn image_mime(data: &[u8]) -> Result<&'static str> {
    if data.starts_with(b"\x89PNG\r\n\x1a\n") {
        return Ok("image/png");
    }
    if data.starts_with(&[0xff, 0xd8, 0xff]) {
        return Ok("image/jpeg");
    }
    anyhow::bail!("请选择 JPEG 或 PNG 图片");
}

fn opaque(data: Vec<u8>) -> Result<String> {
    let encoded = STANDARD.encode(data);
    ensure!(
        (1..=48 * 1024).contains(&encoded.len()),
        "MLS 数据超出协议限制"
    );
    Ok(encoded)
}

fn valid_name(name: &str) -> bool {
    (1..=255).contains(&name.len())
        && !name.ends_with(['.', ' '])
        && !name.contains(['<', '>', ':', '"', '/', '\\', '|', '?', '*'])
        && !name.chars().any(char::is_control)
        && !matches!(
            name.split('.')
                .next()
                .unwrap_or("")
                .to_ascii_uppercase()
                .as_str(),
            "CON"
                | "PRN"
                | "AUX"
                | "NUL"
                | "COM1"
                | "COM2"
                | "COM3"
                | "COM4"
                | "COM5"
                | "COM6"
                | "COM7"
                | "COM8"
                | "COM9"
                | "LPT1"
                | "LPT2"
                | "LPT3"
                | "LPT4"
                | "LPT5"
                | "LPT6"
                | "LPT7"
                | "LPT8"
                | "LPT9"
        )
}

struct TemporaryFile(Option<PathBuf>);

impl TemporaryFile {
    async fn open(destination: &Path) -> Result<(Self, File)> {
        let temporary = destination
            .parent()
            .context("保存位置无效")?
            .join(format!(".lan-chat-{}.partial", Uuid::new_v4()));
        let file = OpenOptions::new()
            .create_new(true)
            .write(true)
            .open(&temporary)
            .await?;
        Ok((Self(Some(temporary)), file))
    }

    async fn commit(mut self, file: File, destination: &Path) -> Result<()> {
        file.sync_all().await?;
        drop(file);
        tokio::fs::rename(self.0.as_ref().expect("temporary file"), destination).await?;
        self.0 = None;
        Ok(())
    }
}

impl Drop for TemporaryFile {
    fn drop(&mut self) {
        if let Some(path) = &self.0
            && let Err(error) = std::fs::remove_file(path)
        {
            eprintln!("清理部分下载失败: {error}");
        }
    }
}
