mod social;
mod transfer;

use std::{
    collections::HashMap,
    net::IpAddr,
    time::{Duration, Instant},
};

use anyhow::{Result, bail, ensure};
use lan_chat_protocol::{MAX_FRAME_BYTES, Message, is_legacy_generated_message_id};
use rusqlite::{OptionalExtension, params};
use tokio::sync::mpsc::Sender;
use uuid::Uuid;

use crate::{
    push::Push,
    reason,
    store::{Identity, Rejected, Store, conversation, now, user_code},
};

struct Peer {
    identity: Identity,
    sender: Sender<Message>,
    backlog_end: Option<i64>,
    image: Option<transfer::ImageUpload>,
}

pub struct Online {
    store: Store,
    push: Option<Push>,
    peers: HashMap<Uuid, Peer>,
    login_attempts: HashMap<IpAddr, (usize, Instant)>,
}

impl Online {
    pub fn new(store: Store, push: Option<Push>) -> Self {
        Self {
            store,
            push,
            peers: HashMap::new(),
            login_attempts: HashMap::new(),
        }
    }

    pub fn admit_login(&mut self, address: IpAddr, kind: &str) -> Result<()> {
        ensure!(
            matches!(kind, "account_login" | "account_register"),
            Rejected("请使用在线版客户端登录")
        );
        let now = Instant::now();
        self.login_attempts.retain(|_, (_, expiry)| now < *expiry);
        ensure!(
            self.login_attempts.contains_key(&address) || self.login_attempts.len() < 4096,
            Rejected("登录尝试过于频繁，请稍后再试")
        );
        let (count, _) = self
            .login_attempts
            .entry(address)
            .or_insert((0, now + Duration::from_secs(60)));
        ensure!(*count < 20, Rejected("登录尝试过于频繁，请稍后再试"));
        *count += 1;
        Ok(())
    }

    pub fn register(
        &mut self,
        id: Uuid,
        identity: Identity,
        sender: Sender<Message>,
    ) -> Result<()> {
        self.peers.insert(
            id,
            Peer {
                identity,
                sender,
                backlog_end: None,
                image: None,
            },
        );
        self.send_backlog(id)
    }

    pub fn remove(&mut self, id: Uuid) {
        self.peers.remove(&id);
    }

    fn deliver(&mut self, id: Uuid, message: Message) {
        if let Some(peer) = self.peers.get(&id) {
            if let Err(error) = peer.sender.try_send(message) {
                eprintln!("client {id}: {error}");
                self.remove(id);
            }
        }
    }

    fn deliver_account(&mut self, code: &str, message: &Message) {
        self.peers.retain(|id, peer| {
            if peer.identity.code != code {
                return true;
            }
            match peer.sender.try_send(message.clone()) {
                Ok(()) => true,
                Err(error) => {
                    eprintln!("client {id}: {error}");
                    false
                }
            }
        });
    }

    pub fn reply_error(&mut self, id: Uuid, request: &Message, content: &str) {
        self.deliver(
            id,
            Message {
                kind: "error".into(),
                message_id: request.message_id.clone(),
                command_id: request.command_id.clone(),
                content: content.into(),
                ..Message::default()
            },
        );
    }

    pub fn command(&mut self, id: Uuid, request: Message) {
        let Some(peer) = self.peers.get(&id) else {
            return;
        };
        let identity = peer.identity.clone();
        if let Err(error) = self.handle_command(id, &identity, &request) {
            self.reply_error(id, &request, reason(&error, "在线命令处理失败"));
        }
    }

    fn handle_command(&mut self, id: Uuid, identity: &Identity, request: &Message) -> Result<()> {
        match request.kind.as_str() {
            "dm_contacts" => {
                let users = self.store.contacts(&identity.code)?;
                self.deliver(
                    id,
                    Message {
                        kind: request.kind.clone(),
                        user_details: users,
                        ..Message::default()
                    },
                );
            }
            "dm_lookup" => {
                let target = private_target(identity, request)?;
                let user = self.store.account(&target)?;
                self.deliver(
                    id,
                    Message {
                        kind: request.kind.clone(),
                        username: user.username,
                        user_code: user.user_code,
                        command_id: request.command_id.clone(),
                        ..Message::default()
                    },
                );
            }
            "private_chat" => self.private_chat(id, identity, request, None)?,
            "dm_image_chunk" => self.receive_image(id, identity, request)?,
            "dm_image_get" => self.read_image(id, identity, request)?,
            "dm_file_init" | "dm_file_chunk" | "dm_file_commit" | "dm_file_get" => {
                self.transfer_file(id, identity, request)?;
            }
            "dm_edit" => self.edit(identity, request)?,
            "dm_received" => self.receipt(identity, request)?,
            "dm_push_register" => {
                ensure!(self.push.is_some(), Rejected("服务器尚未配置系统推送"));
                Push::register(&self.store.0, &identity.code, request)?;
                self.deliver(
                    id,
                    Message {
                        kind: "dm_push_registration".into(),
                        command_id: request.command_id.clone(),
                        ..Message::default()
                    },
                );
            }
            "dm_ack_batch" => self.confirm_batch(id, identity, &request.message_id)?,
            "friend_list" | "friend_request" | "friend_respond" | "profile_get"
            | "profile_save" => {
                self.social(id, identity, request)?;
            }
            "group_list" | "group_create" | "group_detail" | "group_send" | "group_history"
            | "group_leave" => {
                self.group(id, identity, request)?;
            }
            "history_request" if request.private => {
                let target = private_target(identity, request)?;
                let key = conversation(&identity.code, &target);
                self.history(id, "private", &key, request)?;
            }
            "history_request" => bail!(Rejected("在线版仅提供私信")),
            _ => bail!(Rejected("在线版不支持此命令")),
        }
        Ok(())
    }

    fn private_chat(
        &mut self,
        id: Uuid,
        identity: &Identity,
        request: &Message,
        image: Option<&[u8]>,
    ) -> Result<()> {
        let target = private_target(identity, request)?;
        ensure!(
            !request.content.is_empty(),
            Rejected("Invalid private chat content")
        );
        ensure!(
            !request.message_id.is_empty() && !is_legacy_generated_message_id(&request.message_id),
            Rejected("私信缺少有效的消息编号")
        );
        self.store.account(&target)?;
        let message = Message {
            kind: "private_chat".into(),
            message_id: request.message_id.clone(),
            username: identity.username.clone(),
            user_code: identity.display_code.clone(),
            target_user_code: target.clone(),
            content: request.content.clone(),
            image_size: image.map_or(0, |data| data.len() as i64),
            created_at: now(),
            delivery_state: "sent".into(),
            ..Message::default()
        };
        ensure!(
            serde_json::to_vec(&message)?.len() <= MAX_FRAME_BYTES - 1024,
            Rejected("私信超过服务器允许的长度")
        );
        let key = conversation(&identity.code, &target);
        if self
            .store
            .insert_chat(&message, "private", &key, &identity.code, &target, image)?
        {
            self.publish_private(&identity.code, &message);
        } else {
            let state = self.store.0.query_row(
                "SELECT delivery_state FROM chat_messages
                 WHERE kind='private' AND conversation_key=? AND sender_code=? AND message_id=?",
                params![key, identity.code, request.message_id],
                |row| row.get::<_, String>(0),
            )?;
            self.deliver(id, delivery_receipt(message.message_id, &state));
        }
        Ok(())
    }

    fn publish_private(&mut self, sender: &str, message: &Message) {
        self.deliver_account(sender, message);
        self.deliver_account(&message.target_user_code, message);
        if let Some(push) = &self.push {
            push.notify(&message.target_user_code, sender, &message.message_id);
        }
    }

    fn edit(&mut self, identity: &Identity, request: &Message) -> Result<()> {
        let target = private_target(identity, request)?;
        ensure!(
            !request.message_id.is_empty()
                && (0..i64::MAX).contains(&request.revision)
                && !request.content.trim().is_empty(),
            Rejected("私信编辑请求无效")
        );
        let mut edited = Message {
            kind: "dm_edited".into(),
            message_id: request.message_id.clone(),
            command_id: request.command_id.clone(),
            username: identity.username.clone(),
            user_code: identity.code.clone(),
            target_user_code: target.clone(),
            content: request.content.clone(),
            created_at: now(),
            delivery_state: "delivered".into(),
            revision: request.revision + 1,
            ..Message::default()
        };
        ensure!(
            serde_json::to_vec(&edited)?.len() <= MAX_FRAME_BYTES - 1024,
            Rejected("编辑后的私信超过服务器允许的长度")
        );
        (edited.username, edited.created_at, edited.delivery_state, edited.revision) = self.store.0.query_row(
            "UPDATE chat_messages SET content=?,revision=revision+1
             WHERE kind='private' AND conversation_key=? AND sender_code=? AND target_code=?
               AND message_id=? AND revision=? AND recalled=0 AND COALESCE(length(online_image),0)=0 AND crypto_json IS NULL
               AND NOT EXISTS (SELECT 1 FROM online_files WHERE chat_id=chat_messages.id)
             RETURNING sender_username,created_at,delivery_state,revision",
            params![request.content, conversation(&identity.code, &target), identity.code, target, request.message_id, request.revision],
            |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?)),
        ).optional()?.ok_or(Rejected("这条消息无法编辑或已被更新，请刷新后重试"))?;
        self.deliver_account(&identity.code, &edited);
        edited.command_id.clear();
        self.deliver_account(&target, &edited);
        Ok(())
    }

    fn receipt(&mut self, identity: &Identity, request: &Message) -> Result<()> {
        let author = private_target(identity, request)?;
        ensure!(!request.message_id.is_empty(), Rejected("无效的私信回执"));
        let changed = self.store.0.execute(
            "UPDATE chat_messages SET delivery_state='delivered'
             WHERE kind='private' AND sender_code=? AND target_code=? AND message_id=? AND delivery_state='sent'",
            params![author, identity.code, request.message_id],
        )?;
        if changed != 0 {
            self.deliver_account(
                &author,
                &delivery_receipt(request.message_id.clone(), "delivered"),
            );
        }
        Ok(())
    }

    fn send_backlog(&mut self, id: Uuid) -> Result<()> {
        let Some(peer) = self.peers.get(&id) else {
            return Ok(());
        };
        let messages = self.store.backlog(&peer.identity.code)?;
        let end = messages.last().map(|(end, _)| *end);
        self.peers
            .get_mut(&id)
            .expect("registered client")
            .backlog_end = end;
        for (_, message) in messages {
            self.deliver(id, message);
        }
        if let Some(end) = end {
            self.deliver(
                id,
                Message {
                    kind: "dm_backlog_end".into(),
                    message_id: end.to_string(),
                    ..Message::default()
                },
            );
        }
        Ok(())
    }

    fn confirm_batch(&mut self, id: Uuid, identity: &Identity, batch_id: &str) -> Result<()> {
        let end = self.peers[&id]
            .backlog_end
            .filter(|end| end.to_string() == batch_id)
            .ok_or(Rejected("无效的离线消息回执"))?;
        let receipts = {
            let mut statement = self.store.0.prepare(
                "UPDATE chat_messages SET delivery_state='delivered'
                 WHERE kind='private' AND target_code=? AND id<=? AND delivery_state='sent' AND recalled=0
                 RETURNING message_id,sender_code",
            )?;
            statement
                .query_map(params![identity.code, end], |row| {
                    Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
                })?
                .collect::<rusqlite::Result<Vec<_>>>()?
        };
        for (message_id, author) in receipts {
            self.deliver_account(&author, &delivery_receipt(message_id, "delivered"));
        }
        self.send_backlog(id)
    }

    fn history(&mut self, id: Uuid, kind: &str, key: &str, request: &Message) -> Result<()> {
        let (messages, has_more) = self.store.history(kind, key, request)?;
        let mut reply = Message {
            kind: "history_response".into(),
            command_id: request.command_id.clone(),
            private: request.private,
            room: request.room.clone(),
            group_id: request.group_id.clone(),
            target_user_code: request.target_user_code.clone(),
            search_query: request.search_query.trim().into(),
            has_more,
            ..Message::default()
        };
        let mut remaining = MAX_FRAME_BYTES
            .checked_sub(serde_json::to_vec(&reply)?.len() + 64)
            .ok_or(Rejected("无法编码私信历史"))?;
        let mut start = messages.len();
        for message in messages.iter().rev() {
            let size = serde_json::to_vec(message)?.len() + 1;
            if size > remaining {
                reply.has_more = true;
                break;
            }
            remaining -= size;
            start -= 1;
        }
        reply.messages = messages.into_iter().skip(start).collect();
        self.deliver(id, reply);
        Ok(())
    }
}

fn private_target(identity: &Identity, request: &Message) -> Result<String> {
    let code = user_code(&request.target_user_code)
        .map_err(|_| Rejected("请输入其他用户的有效用户代码"))?;
    ensure!(
        code != identity.code,
        Rejected("请输入其他用户的有效用户代码")
    );
    Ok(code)
}

fn delivery_receipt(message_id: String, state: &str) -> Message {
    Message {
        kind: "delivery_receipt".into(),
        message_id,
        delivery_state: state.into(),
        content: state.into(),
        ..Message::default()
    }
}
