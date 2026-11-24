use anyhow::{Context, Result, bail, ensure};
use lan_chat_protocol::{
    MAX_COMMAND_ID_BYTES, MAX_FRAME_BYTES, Message, is_legacy_generated_message_id,
};
use rusqlite::{OptionalExtension, params};
use uuid::Uuid;

use super::Hub;
use crate::store::{Rejected, conversation, now, user_code};

impl Hub {
    pub(super) fn command(&mut self, id: Uuid, request: &Message) -> Result<()> {
        if !self.clients.contains_key(&id) {
            return Ok(());
        }
        ensure!(
            request.command_id.len() <= MAX_COMMAND_ID_BYTES,
            Rejected("Command ID is too large")
        );
        match request.kind.as_str() {
            "chat" | "private_chat" => self.chat(id, request)?,
            "users_request" => self.users(id),
            "rooms_request" => self.rooms(id),
            "room_create" => self.create_room(id, request)?,
            "room_join" => self.join_room(id, request)?,
            "room_leave" => self.leave_room(id),
            "room_action" => self.room_action(id, request)?,
            "admin_action" => self.admin_action(id, request)?,
            "history_request" => self.history(id, request)?,
            "attachment.init"
            | "attachment.chunk"
            | "attachment.resume"
            | "attachment.commit"
            | "attachment.download" => {
                self.attachment(id, request)?;
            }
            "mls.key_package.publish"
            | "mls.key_package.fetch"
            | "mls.group.proposal"
            | "mls.group.commit"
            | "mls.group.welcome"
            | "mls.group.welcome.accept" => {
                self.mls(id, request)?;
            }
            "dm_contacts" | "dm_lookup" | "dm_received" | "dm_ack_batch" | "dm_image_chunk"
            | "dm_image_get" | "dm_push_register" | "dm_edit" | "dm_file_init"
            | "dm_file_chunk" | "dm_file_commit" | "dm_file_get" | "friend_list"
            | "friend_request" | "friend_respond" | "profile_get" | "profile_save"
            | "group_list" | "group_create" | "group_detail" | "group_send" | "group_history"
            | "group_leave" => bail!(Rejected("此命令需要在线版服务器")),
            _ => bail!(Rejected("Unsupported message type")),
        }
        Ok(())
    }

    fn admin_action(&mut self, id: Uuid, request: &Message) -> Result<()> {
        if request.content == "recall" {
            return self.recall(id, request);
        }
        ensure!(
            self.clients[&id].admin,
            Rejected("Administrator permission required")
        );
        if matches!(
            request.content.as_str(),
            "approve_connection" | "deny_connection"
        ) {
            let approval = self
                .pending
                .remove(&request.message_id)
                .ok_or(Rejected("Connection request is no longer pending"))?;
            let approved = request.content == "approve_connection";
            self.approval_result(
                approval.display,
                if approved { "approved" } else { "denied" },
                &request.command_id,
            );
            let _ = approval.decision.send(approved);
            return Ok(());
        }

        let target = self
            .codes
            .get(&user_code(&request.target_user_code)?)
            .copied()
            .filter(|target| *target != id)
            .ok_or(Rejected("Target user not found"))?;
        let peer = self.clients.get_mut(&target).expect("active client");
        let name = format!("{}#{}", peer.identity.username, peer.identity.display_code);
        match request.content.as_str() {
            "kick" => {
                self.system(target, "", "You were kicked by the administrator");
                self.remove(target, false);
                self.system(id, "", &format!("{name} was kicked"));
            }
            "mute" => {
                let muted = !peer.muted;
                self.store.set_lan_muted(&peer.identity.code, muted)?;
                peer.muted = muted;
                let status = if muted { "muted" } else { "unmuted" };
                self.system(
                    target,
                    "",
                    &format!("You were {status} by the administrator"),
                );
                self.system(id, "", &format!("{name} is {status}"));
                if self.clients[&id].room_roles && !request.command_id.is_empty() {
                    self.deliver(
                        id,
                        Message {
                            kind: "admin_action".into(),
                            command_id: request.command_id.clone(),
                            ..Message::default()
                        },
                    );
                }
            }
            _ => bail!(Rejected("Unsupported administrator action")),
        }
        Ok(())
    }

    fn chat(&mut self, id: Uuid, request: &Message) -> Result<()> {
        let sender = &self.clients[&id];
        let private = request.kind == "private_chat";
        if private {
            ensure!(
                !request.content.is_empty(),
                Rejected("Invalid private chat content")
            );
        } else {
            ensure!(!sender.muted, Rejected("You are muted"));
            ensure!(
                match &request.crypto {
                    Some(value) => value
                        .as_object()
                        .is_some_and(|envelope| !envelope.is_empty()),
                    None => !request.content.is_empty(),
                },
                Rejected("Invalid chat content")
            );
        }
        let author = sender.identity.code.clone();
        let (kind, key, target, target_code, room) = if private {
            let code = user_code(&request.target_user_code)?;
            ensure!(
                code != author,
                Rejected("Cannot send private message to yourself")
            );
            let target = self.codes.get(&code).copied();
            if target.is_none() {
                let known: bool = self.store.0.query_row(
                    "SELECT EXISTS(SELECT 1 FROM accounts WHERE normalized_code=?)",
                    [&code],
                    |row| row.get(0),
                )?;
                ensure!(known, Rejected("Target user not found"));
            }
            (
                "private",
                conversation(&author, &code),
                target,
                code,
                String::new(),
            )
        } else {
            (
                "room",
                sender.room.clone(),
                None,
                String::new(),
                sender.room.clone(),
            )
        };
        let supplied = !request.message_id.is_empty();
        let tracked = supplied && !is_legacy_generated_message_id(&request.message_id);
        let message_id = if supplied {
            request.message_id.clone()
        } else {
            self.last_message = self
                .last_message
                .checked_add(1)
                .context("消息编号超出范围")?;
            self.last_message.to_string()
        };
        let mut message = Message {
            kind: request.kind.clone(),
            message_id,
            command_id: if private {
                String::new()
            } else {
                request.command_id.clone()
            },
            username: sender.identity.username.clone(),
            user_code: sender.identity.display_code.clone(),
            room,
            target_user_code: target_code.clone(),
            content: request.content.clone(),
            crypto: if private {
                None
            } else {
                request.crypto.clone()
            },
            created_at: now(),
            delivery_state: "sent".into(),
            ..Message::default()
        };
        ensure!(
            serde_json::to_vec(&message)?.len() <= MAX_FRAME_BYTES,
            Rejected("Message is too large")
        );
        if !self.store.insert_lan_chat(
            &message,
            kind,
            &key,
            &author,
            &target_code,
            private && target.is_none(),
        )? {
            if tracked {
                let delivery: String = self.store.0.query_row(
                    "SELECT delivery_state FROM chat_messages WHERE sender_code=? AND conversation_key=? AND message_id=?",
                    params![author, key, message.message_id],
                    |row| row.get(0),
                )?;
                self.receipt(id, &message.message_id, &delivery);
            }
            return Ok(());
        }

        if !tracked {
            message.created_at.clear();
            message.delivery_state.clear();
        }
        let delivered = if private {
            if let Some(target) = target {
                message.target_user_code = self.clients[&target].identity.display_code.clone();
            }
            if self.deliver(id, message.clone()) {
                if let Some(target) = target {
                    self.deliver(target, message.clone())
                } else {
                    self.system(id, "", "Private message saved for offline user");
                    false
                }
            } else {
                false
            }
        } else {
            let mut delivered = false;
            for peer in self.room_clients(&message.room) {
                if self.deliver(peer, message.clone()) && peer != id {
                    delivered = true;
                }
            }
            delivered
        };

        if delivered && (tracked || private) {
            self.store.0.execute(
                "UPDATE chat_messages SET delivery_state='delivered'
                 WHERE sender_code=? AND conversation_key=? AND message_id=?",
                params![author, key, message.message_id],
            )?;
        }
        if tracked {
            self.receipt(
                id,
                &message.message_id,
                if delivered { "delivered" } else { "sent" },
            );
        }
        Ok(())
    }

    fn receipt(&mut self, id: Uuid, message_id: &str, state: &str) {
        self.deliver(
            id,
            Message {
                kind: "delivery_receipt".into(),
                message_id: message_id.into(),
                delivery_state: state.into(),
                content: state.into(),
                ..Message::default()
            },
        );
    }

    fn recall(&mut self, id: Uuid, request: &Message) -> Result<()> {
        let sender = &self.clients[&id];
        let tx = self.store.0.transaction()?;
        let (row, author, room, target, recalled): (i64, String, String, String, bool) = tx.query_row(
            "SELECT id,sender_code,COALESCE(room,''),COALESCE(target_code,''),recalled FROM chat_messages
             WHERE message_id=? ORDER BY id DESC LIMIT 1",
            [&request.message_id],
            |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?, row.get(4)?)),
        ).optional()?.ok_or(Rejected("Message not found"))?;
        ensure!(!recalled, Rejected("Message already recalled"));
        ensure!(
            sender.admin
                || sender.identity.code == author
                || (sender.room_roles
                    && target.is_empty()
                    && sender.room == room
                    && self
                        .rooms
                        .get(&room)
                        .is_some_and(|channel| channel.can_manage(sender))),
            Rejected("Only the message author or an administrator can recall this message")
        );
        tx.execute("UPDATE chat_messages SET recalled=1 WHERE id=?", [row])?;
        tx.execute(
            "DELETE FROM offline_messages WHERE message_id=? AND lower(sender_code)=? AND target_code=?",
            params![request.message_id, author, target],
        )?;
        tx.commit()?;
        let peers = if target.is_empty() {
            self.room_clients(&room)
        } else {
            [&author, &target]
                .into_iter()
                .filter_map(|code| self.codes.get(code).copied())
                .collect()
        };
        let recalled = Message {
            kind: "message_recalled".into(),
            message_id: request.message_id.clone(),
            command_id: request.command_id.clone(),
            ..Message::default()
        };
        for peer in peers {
            self.deliver(peer, recalled.clone());
        }
        Ok(())
    }
}
