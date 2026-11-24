use super::{Hub, LOBBY, Peer};
use lan_chat_protocol::{Message, OnlineUser};
use uuid::Uuid;

impl Hub {
    pub(super) fn register(&mut self, id: Uuid, peer: Peer) {
        if let Some(previous) = self.codes.get(&peer.identity.code).copied() {
            self.remove(previous, true);
        }
        let presence = format!(
            "{}#{} joined the chat",
            peer.identity.username, peer.identity.display_code
        );
        self.codes.insert(peer.identity.code.clone(), id);
        self.clients.insert(id, peer);
        self.room_system(LOBBY, &presence);
        if self.clients.get(&id).is_some_and(|peer| peer.admin) {
            let pending = self
                .pending
                .values()
                .map(|approval| approval.display.clone())
                .collect::<Vec<_>>();
            for request in pending {
                self.deliver(id, request);
            }
        }
    }

    pub(super) fn remove(&mut self, id: Uuid, immediate: bool) {
        let Some(peer) = self.clients.remove(&id) else {
            return;
        };
        self.codes.remove(&peer.identity.code);
        if immediate {
            let _ = peer.close.send(());
        }
        self.room_system(
            &peer.room,
            &format!(
                "{}#{} left the chat",
                peer.identity.username, peer.identity.display_code,
            ),
        );
    }

    pub(super) fn deliver(&mut self, id: Uuid, message: Message) -> bool {
        let Some(peer) = self.clients.get(&id) else {
            return false;
        };
        if peer.send.try_send(message).is_err() {
            self.remove(id, true);
            return false;
        }
        true
    }

    pub(super) fn error(&mut self, id: Uuid, request: &Message, content: &str) {
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

    pub(super) fn users(&mut self, id: Uuid) {
        let mut users = Vec::new();
        let mut details = Vec::new();
        for peer in self.clients.values() {
            users.push(format!(
                "{}#{}@{}",
                peer.identity.username, peer.identity.display_code, peer.room
            ));
            details.push(OnlineUser {
                username: peer.identity.username.clone(),
                user_code: peer.identity.display_code.clone(),
                room: peer.room.clone(),
                is_admin: peer.admin,
            });
        }
        users.sort();
        details.sort_by(|first, second| {
            (&first.username, &first.user_code).cmp(&(&second.username, &second.user_code))
        });
        self.deliver(
            id,
            Message {
                kind: "users_response".into(),
                users,
                user_details: details,
                ..Message::default()
            },
        );
    }

    pub(super) fn system(&mut self, id: Uuid, room: &str, content: &str) {
        self.deliver(
            id,
            Message {
                kind: "system".into(),
                room: room.into(),
                content: content.into(),
                ..Message::default()
            },
        );
    }

    pub(super) fn room_system(&mut self, room: &str, content: &str) {
        for id in self.room_clients(room) {
            self.system(id, room, content);
        }
    }

    pub(super) fn room_clients(&self, room: &str) -> Vec<Uuid> {
        self.clients
            .iter()
            .filter_map(|(id, peer)| (peer.room == room).then_some(*id))
            .collect()
    }
}
