use super::{Approval, Hub};
use crate::store::{Identity, now};
use lan_chat_protocol::Message;
use tokio::sync::oneshot;
use uuid::Uuid;

impl Hub {
    pub(super) fn request_approval(
        &mut self,
        identity: &Identity,
    ) -> (String, oneshot::Receiver<bool>) {
        let id = format!("connection-{}", Uuid::new_v4());
        let (send, receive) = oneshot::channel();
        let display = Message {
            kind: "connection_approval_request".into(),
            message_id: id.clone(),
            username: identity.username.clone(),
            user_code: identity.display_code.clone(),
            content: now(),
            ..Message::default()
        };
        self.pending.insert(
            id.clone(),
            Approval {
                display: display.clone(),
                decision: send,
            },
        );
        self.admins(display);
        (id, receive)
    }

    pub(super) fn expire_approval(&mut self, id: &str) {
        if let Some(approval) = self.pending.remove(id) {
            self.approval_result(approval.display, "expired", "");
        }
    }

    pub(super) fn approval_result(&mut self, request: Message, content: &str, command_id: &str) {
        self.admins(Message {
            kind: "connection_approval_result".into(),
            content: content.into(),
            command_id: command_id.into(),
            ..request
        });
    }

    pub(super) fn admins(&mut self, message: Message) {
        let ids = self
            .clients
            .iter()
            .filter_map(|(id, peer)| peer.admin.then_some(*id))
            .collect::<Vec<_>>();
        for id in ids {
            self.deliver(id, message.clone());
        }
    }
}
