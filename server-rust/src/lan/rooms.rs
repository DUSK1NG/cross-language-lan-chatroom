use std::collections::HashMap;

use anyhow::{Result, ensure};
use lan_chat_protocol::{Message, ROOM_PASSWORD_MAX_BYTES, RoomInfo};
use uuid::Uuid;

use super::{Hub, LOBBY, Peer, Room, store::password_hash, validate_room_name};
use crate::store::{Rejected, user_code};

impl Room {
    pub(super) fn can_join(&self, peer: &Peer) -> bool {
        !self.private
            || peer.admin
            || self.owner == peer.identity.code
            || self.allowed.get(&peer.identity.code) == Some(&true)
    }

    pub(super) fn can_read(&self, name: &str, peer: &Peer) -> bool {
        self.can_join(peer)
            && (self.password_hash.is_none()
                || self.owner == peer.identity.code
                || peer.room == name)
    }

    fn can_manage(&self, peer: &Peer) -> bool {
        peer.admin || self.owner == peer.identity.code
    }
}

impl Hub {
    pub(super) fn rooms(&mut self, id: Uuid) {
        let peer = &self.clients[&id];
        let mut details = self
            .rooms
            .iter()
            .filter(|(_, room)| room.can_join(peer))
            .map(|(name, room)| RoomInfo {
                name: name.clone(),
                owner_code: room.owner.clone(),
                private: room.private,
                can_manage: room.can_manage(peer),
                password_required: room.password_hash.is_some() && room.owner != peer.identity.code,
            })
            .collect::<Vec<_>>();
        details.sort_by(|first, second| first.name.cmp(&second.name));
        self.deliver(
            id,
            Message {
                kind: "rooms_response".into(),
                room: peer.room.clone(),
                rooms: details.iter().map(|room| room.name.clone()).collect(),
                room_details: details,
                ..Message::default()
            },
        );
    }

    pub(super) fn create_room(&mut self, id: Uuid, request: &Message) -> Result<()> {
        validate_room_name(&request.room)?;
        ensure!(
            !self.rooms.contains_key(&request.room),
            Rejected("A channel with this name already exists")
        );
        validate_password(&request.room_password)?;
        ensure!(
            request.room_password.is_empty() || request.private,
            Rejected("只有私有频道可以设置密码")
        );
        let owner = self.clients[&id].identity.code.clone();
        let hash = if request.room_password.is_empty() {
            None
        } else {
            Some(password_hash(&request.room_password)?)
        };
        let room = Room {
            owner: owner.clone(),
            private: request.private,
            allowed: HashMap::from([(owner, true)]),
            password_hash: hash,
            generation: Uuid::new_v4(),
        };
        if let Some(hash) = &room.password_hash {
            self.store.save_protected_room(&request.room, &room, hash)?;
        }
        self.rooms.insert(request.room.clone(), room);
        self.system(id, "", &format!("Channel #{} created", request.room));
        self.move_room(id, &request.room);
        self.confirm_room(id, request);
        Ok(())
    }

    pub(super) fn join_room(&mut self, id: Uuid, request: &Message) -> Result<()> {
        validate_room_name(&request.room)?;
        validate_password(&request.room_password)?;
        let room = self
            .rooms
            .get(&request.room)
            .ok_or(Rejected("Room not found. Create it first."))?;
        let peer = &self.clients[&id];
        ensure!(
            room.can_join(peer),
            Rejected("This is a private channel. Ask the owner for an invitation.")
        );
        if peer.room == request.room {
            if request.command_id.is_empty() {
                self.system(id, "", &format!("Already in room {}", request.room));
            }
        } else {
            if let Some(hash) = &room.password_hash
                && room.owner != peer.identity.code
            {
                ensure!(
                    bcrypt::verify(&request.room_password, hash)?,
                    Rejected("频道密码缺失或不正确")
                );
            }
            self.move_room(id, &request.room);
        }
        self.confirm_room(id, request);
        Ok(())
    }

    pub(super) fn leave_room(&mut self, id: Uuid) {
        if self.clients[&id].room == LOBBY {
            self.system(id, "", "Already in room lobby");
        } else {
            self.move_room(id, LOBBY);
        }
    }

    pub(super) fn room_action(&mut self, id: Uuid, request: &Message) -> Result<()> {
        let missing = Rejected("Channel not found or cannot be changed");
        ensure!(request.room != LOBBY, missing);
        let room = self.rooms.get(&request.room).ok_or(missing)?;
        ensure!(
            room.can_manage(&self.clients[&id]),
            Rejected("Channel owner or administrator permission required")
        );

        if request.content == "delete" {
            if room.password_hash.is_some() {
                self.store.delete_protected_room(&request.room)?;
            }
            let members = self.room_clients(&request.room);
            for member in &members {
                self.clients
                    .get_mut(member)
                    .expect("room client exists")
                    .room = LOBBY.into();
            }
            for member in members {
                self.system(
                    member,
                    "",
                    &format!("Channel #{} was deleted", request.room),
                );
            }
            self.rooms.remove(&request.room);
            return Ok(());
        }

        let invite = match request.content.as_str() {
            "invite" => true,
            "remove_member" => false,
            _ => return Err(Rejected("Unsupported room action").into()),
        };
        let code = user_code(&request.target_user_code)?;
        let room = self.rooms.get_mut(&request.room).expect("channel exists");
        let previous = if invite {
            room.allowed.insert(code.clone(), true)
        } else {
            room.allowed.remove(&code)
        };
        let protected = room.password_hash.is_some();
        if let Some(hash) = &room.password_hash
            && let Err(error) = self.store.save_protected_room(&request.room, room, hash)
        {
            if let Some(value) = previous {
                room.allowed.insert(code, value);
            } else {
                room.allowed.remove(&code);
            }
            return Err(error);
        }

        let target = self.codes.get(&code).copied();
        if invite {
            self.system(id, "", &format!("Member invited to #{}", request.room));
            if let Some(target) = target
                && protected
                && self.clients.contains_key(&target)
            {
                self.rooms(target);
            }
        } else {
            if let Some(target) = target
                && self.clients[&target].room == request.room
            {
                self.clients.get_mut(&target).expect("target exists").room = LOBBY.into();
                self.system(
                    target,
                    "",
                    &format!("You were removed from #{}", request.room),
                );
            }
            self.system(id, "", &format!("Member removed from #{}", request.room));
        }
        Ok(())
    }

    fn move_room(&mut self, id: Uuid, room: &str) {
        let Some(peer) = self.clients.get(&id) else {
            return;
        };
        let old = peer.room.clone();
        let name = format!("{}#{}", peer.identity.username, peer.identity.display_code);
        self.room_system(&old, &format!("{name} left room {old}"));
        if let Some(peer) = self.clients.get_mut(&id) {
            peer.room = room.into();
            self.room_system(room, &format!("{name} joined room {room}"));
        }
    }

    fn confirm_room(&mut self, id: Uuid, request: &Message) {
        if !request.command_id.is_empty() {
            self.deliver(
                id,
                Message {
                    kind: request.kind.clone(),
                    room: request.room.clone(),
                    command_id: request.command_id.clone(),
                    ..Message::default()
                },
            );
        }
    }
}

fn validate_password(password: &str) -> Result<()> {
    ensure!(
        password.is_empty()
            || (!password.trim().is_empty() && password.len() <= ROOM_PASSWORD_MAX_BYTES),
        Rejected("频道密码不能仅含空白，且最多 72 字节")
    );
    Ok(())
}
