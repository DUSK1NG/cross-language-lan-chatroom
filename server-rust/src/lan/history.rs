use super::{HistoryJob, Hub, Server};
use crate::store::{Rejected, Store, conversation, user_code};
use anyhow::{Result, ensure};
use lan_chat_protocol::{MAX_FRAME_BYTES, Message};
use std::sync::Arc;
use tokio::{sync::mpsc, task::spawn_blocking};
use uuid::Uuid;

pub(super) async fn worker(
    server: Arc<Server>,
    reader: Store,
    mut jobs: mpsc::Receiver<HistoryJob>,
) -> Result<()> {
    let mut reader = reader;
    while let Some(job) = jobs.recv().await {
        let server = server.clone();
        reader = spawn_blocking(move || {
            let kind = if job.request.private {
                "private"
            } else {
                "room"
            };
            let result = reader.lan_history(kind, &job.conversation, &job.request);
            let mut hub = server.hub.lock().expect("LAN hub poisoned");
            hub.history_result(job, result);
            reader
        })
        .await?;
    }
    Ok(())
}

impl Hub {
    pub(super) fn history(&mut self, id: Uuid, request: &Message) -> Result<()> {
        let peer = &self.clients[&id];
        ensure!(
            (0..=100).contains(&request.limit),
            Rejected("历史记录页大小须在 0–100 之间")
        );
        let mut request = request.clone();
        let (conversation, room_generation) = if request.private {
            let target = user_code(&request.target_user_code)?;
            ensure!(
                peer.identity.code != target,
                Rejected("Invalid private conversation")
            );
            let key = conversation(&peer.identity.code, &target);
            request.target_user_code = target;
            request.room.clear();
            (key, None)
        } else {
            let denied = Rejected("You do not have access to this channel");
            let room = self.rooms.get(&request.room).ok_or(denied)?;
            ensure!(room.can_read(&request.room, peer), denied);
            request.target_user_code.clear();
            (request.room.clone(), Some(room.generation))
        };
        self.history
            .try_send(HistoryJob {
                peer: id,
                conversation,
                room_generation,
                request,
            })
            .map_err(|_| Rejected("History service is busy"))?;
        Ok(())
    }

    pub(super) fn history_result(&mut self, job: HistoryJob, result: Result<(Vec<Message>, bool)>) {
        let Some(peer) = self.clients.get(&job.peer) else {
            return;
        };
        if let Some(generation) = job.room_generation
            && !self.rooms.get(&job.request.room).is_some_and(|room| {
                room.generation == generation && room.can_read(&job.request.room, peer)
            })
        {
            self.error(
                job.peer,
                &job.request,
                "You do not have access to this channel",
            );
            return;
        }
        let response = result.and_then(|(items, more)| {
            let mut message = Message {
                kind: "history_response".into(),
                room: job.request.room.clone(),
                private: job.request.private,
                target_user_code: job.request.target_user_code.clone(),
                command_id: job.request.command_id.clone(),
                search_query: job.request.search_query.clone(),
                has_more: more,
                ..Message::default()
            };
            let mut remaining = MAX_FRAME_BYTES
                .checked_sub(serde_json::to_vec(&message)?.len() + 64)
                .ok_or(Rejected("History response is too large"))?;
            let mut start = items.len();
            while start > 0 {
                let size = serde_json::to_vec(&items[start - 1])?.len() + 1;
                if size > remaining {
                    message.has_more = true;
                    break;
                }
                remaining -= size;
                start -= 1;
            }
            ensure!(
                items.is_empty() || start != items.len(),
                Rejected("History message is too large")
            );
            message.messages = items.into_iter().skip(start).collect();
            Ok(message)
        });
        match response {
            Ok(message) => {
                self.deliver(job.peer, message);
            }
            Err(error) => self.error(
                job.peer,
                &job.request,
                crate::reason(&error, "Failed to load message history"),
            ),
        }
    }
}
