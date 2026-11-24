use std::collections::{HashMap, HashSet};

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

    fn can_administer(&self, peer: &Peer) -> bool {
        peer.admin || self.owner == peer.identity.code
    }

    pub(super) fn can_manage(&self, peer: &Peer) -> bool {
        self.can_administer(peer) || (peer.room_roles && self.admins.contains(&peer.identity.code))
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
            admins: HashSet::new(),
            password_hash: hash,
            generation: Uuid::new_v4(),
        };
        self.store.save_lan_room(&request.room, &room)?;
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
        let actor = &self.clients[&id];
        let administrator = room.can_administer(actor);
        let roles = actor.room_roles;
        ensure!(
            room.can_manage(actor),
            Rejected("Channel owner or administrator permission required")
        );

        if request.content == "delete" {
            ensure!(
                administrator,
                Rejected("Only the channel owner or host can delete this channel")
            );
            self.store.delete_lan_room(&request.room)?;
            let members = self.room_clients(&request.room);
            self.rooms.remove(&request.room);
            for member in members {
                self.evict_room_member(member);
                self.system(
                    member,
                    "",
                    &format!("Channel #{} was deleted", request.room),
                );
            }
            self.refresh_room_directories();
            if roles {
                self.confirm_room(id, request);
            }
            return Ok(());
        }

        let mut updated = room.clone();
        let mut target = None;
        let feedback = match request.content.as_str() {
            "set_password" => {
                ensure!(
                    roles && administrator && room.private,
                    Rejected("Only the channel owner or host can set a private channel password")
                );
                validate_password(&request.room_password)?;
                updated.password_hash = if request.room_password.is_empty() {
                    None
                } else {
                    Some(password_hash(&request.room_password)?)
                };
                "Channel password updated"
            }
            action @ ("invite" | "remove_member" | "grant_admin" | "revoke_admin") => {
                let code = user_code(&request.target_user_code)?;
                target = self.codes.get(&code).copied();
                match action {
                    "invite" => {
                        updated.allowed.insert(code, true);
                        "Member invited to"
                    }
                    "remove_member" => {
                        ensure!(
                            code != room.owner
                                && (administrator || !room.admins.contains(&code))
                                && !target.is_some_and(|target| self.clients[&target].admin),
                            Rejected("Cannot remove that channel member")
                        );
                        updated.allowed.remove(&code);
                        updated.admins.remove(&code);
                        "Member removed from"
                    }
                    "grant_admin" => {
                        ensure!(
                            roles
                                && administrator
                                && code != room.owner
                                && (!room.private || room.allowed.get(&code) == Some(&true)),
                            Rejected(
                                "Invite the member before granting channel administrator permissions"
                            )
                        );
                        updated.admins.insert(code);
                        "Channel administrator appointed in"
                    }
                    "revoke_admin" => {
                        ensure!(
                            roles && administrator,
                            Rejected(
                                "Only the channel owner or host can revoke channel administrator permissions"
                            )
                        );
                        updated.admins.remove(&code);
                        "Channel administrator revoked in"
                    }
                    _ => unreachable!("room action"),
                }
            }
            _ => return Err(Rejected("Unsupported room action").into()),
        };
        self.store.save_lan_room(&request.room, &updated)?;
        self.rooms.insert(request.room.clone(), updated);
        if let Some(target) = target {
            if request.content == "remove_member" && self.clients[&target].room == request.room {
                self.evict_room_member(target);
                self.system(
                    target,
                    "",
                    &format!("You were removed from #{}", request.room),
                );
            }
            self.rooms(target);
        }
        if request.content == "set_password" {
            self.refresh_room_directories();
        }
        self.system(id, "", &format!("{feedback} #{}", request.room));
        if roles {
            self.confirm_room(id, request);
        }
        Ok(())
    }

    fn refresh_room_directories(&mut self) {
        let clients: Vec<_> = self
            .clients
            .iter()
            .filter_map(|(id, peer)| peer.room_roles.then_some(*id))
            .collect();
        for id in clients {
            self.rooms(id);
        }
    }

    fn evict_room_member(&mut self, id: Uuid) {
        let peer = self.clients.get_mut(&id).expect("active room member");
        peer.room = LOBBY.into();
        peer.attachment_session = Uuid::new_v4();
        if peer.room_roles {
            self.deliver(
                id,
                Message {
                    kind: "room_join".into(),
                    room: LOBBY.into(),
                    ..Message::default()
                },
            );
        }
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
            peer.attachment_session = Uuid::new_v4();
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
