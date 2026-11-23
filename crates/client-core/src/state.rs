use std::{
    collections::{BTreeMap, HashMap, HashSet},
    path::PathBuf,
};

use chrono::{DateTime, Local};
use lan_chat_protocol::{Message, OnlineUser, RECALLED_MESSAGE, RoomInfo};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use uuid::Uuid;

use crate::{Edition, discovery::Discovered, session::Session};

#[derive(Clone, Serialize, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct ConnectionForm {
    pub server_ip: String,
    pub server_port: u16,
    pub username: String,
    pub user_code: String,
    pub ca_file: PathBuf,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub tls_server_name: String,
}

impl Default for ConnectionForm {
    fn default() -> Self {
        Self {
            server_ip: "127.0.0.1".into(),
            server_port: 8888,
            username: String::new(),
            user_code: String::new(),
            ca_file: PathBuf::new(),
            tls_server_name: String::new(),
        }
    }
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Conversation {
    pub kind: &'static str,
    pub id: String,
    pub title: String,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub user_code: String,
}

impl Conversation {
    pub fn room(room: &str) -> Self {
        Self {
            kind: "room",
            id: room.into(),
            title: room.into(),
            user_code: String::new(),
        }
    }

    pub fn dm(code: &str, name: &str) -> Self {
        Self {
            kind: "dm",
            id: code.to_ascii_lowercase(),
            title: name.into(),
            user_code: code.into(),
        }
    }

    pub fn key(&self) -> String {
        format!("{}:{}", self.kind, self.id)
    }

    pub fn history(&self, query: &str) -> Message {
        Message {
            kind: "history_request".into(),
            search_query: query.into(),
            limit: 100,
            private: self.kind == "dm",
            room: if self.kind == "room" {
                self.id.clone()
            } else {
                String::new()
            },
            target_user_code: self.user_code.clone(),
            ..Message::default()
        }
    }
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Row {
    pub message_id: String,
    pub display_name: String,
    pub user_code: String,
    pub time: String,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub created_at: String,
    pub content: String,
    pub self_message: bool,
    pub system_message: bool,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub delivery_state: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub attachment: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub image_size: Option<i64>,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub image_peer: String,
    #[serde(skip)]
    pub revision: i64,
}

pub struct State {
    pub saved: ConnectionForm,
    pub session: Option<Session>,
    pub generation: Option<Uuid>,
    pub phase: &'static str,
    pub status: &'static str,
    pub last_error: Option<Value>,
    pub reconnect: u32,
    pub identity: Message,
    pub active: Option<Conversation>,
    pub rooms: Vec<RoomInfo>,
    pub members: Vec<OnlineUser>,
    pub direct: BTreeMap<String, Conversation>,
    pub histories: HashMap<String, Vec<Row>>,
    pub unread: HashMap<String, usize>,
    pub approvals: HashMap<String, Message>,
    pub hidden: HashSet<String>,
    pub outgoing: HashMap<String, Message>,
    pub room_commands: HashSet<String>,
    pub query: String,
    pub search: Vec<Row>,
    pub logging: bool,
    pub performance: String,
    pub scan: Option<Uuid>,
    pub discovered: BTreeMap<String, Discovered>,
}

impl State {
    pub fn new(edition: Edition, saved: ConnectionForm) -> Self {
        Self {
            saved,
            session: None,
            generation: None,
            phase: "idle",
            status: "未连接",
            last_error: None,
            reconnect: 0,
            identity: Message::default(),
            active: (edition == Edition::Lan).then(|| Conversation::room("lobby")),
            rooms: Vec::new(),
            members: Vec::new(),
            direct: BTreeMap::new(),
            histories: HashMap::new(),
            unread: HashMap::new(),
            approvals: HashMap::new(),
            hidden: HashSet::new(),
            outgoing: HashMap::new(),
            room_commands: HashSet::new(),
            query: String::new(),
            search: Vec::new(),
            logging: false,
            performance: "Automatic".into(),
            scan: None,
            discovered: BTreeMap::new(),
        }
    }

    pub fn select(&mut self, conversation: Conversation) -> Message {
        self.unread.remove(&conversation.key());
        self.query.clear();
        self.search.clear();
        let request = conversation.history("");
        self.active = Some(conversation);
        request
    }

    pub fn snapshot(&self, edition: Edition, host_available: bool) -> Value {
        let rooms = self.rooms.iter().map(|room| json!({
            "roomName":room.name, "memberCount":self.members.iter().filter(|member| member.room == room.name).count(),
            "unreadCount":self.unread.get(&format!("room:{}",room.name)).copied().unwrap_or(0),
            "ownerCode":room.owner_code,"private":room.private,"canManage":room.can_manage,"passwordRequired":room.password_required
        })).collect::<Vec<_>>();
        let direct = self.direct.values().map(|item| json!({
            "displayName":item.title, "userCode":item.user_code, "unreadCount":self.unread.get(&item.key()).copied().unwrap_or(0),
        })).collect::<Vec<_>>();
        let members = self.members.iter().map(|member| json!({
            "displayName":member.username,"userCode":member.user_code,"online":true,"admin":member.is_admin
        })).collect::<Vec<_>>();
        let mut approvals = self.approvals.values().collect::<Vec<_>>();
        approvals.sort_by(|left, right| {
            (&left.content, &left.message_id).cmp(&(&right.content, &right.message_id))
        });
        let approvals = approvals.into_iter().map(|request| json!({
            "id":request.message_id,"displayName":request.username,"userCode":request.user_code,"requestedAt":request.content
        })).collect::<Vec<_>>();
        let active_rows = if self.query.is_empty() {
            self.active
                .as_ref()
                .and_then(|item| self.histories.get(&item.key()))
                .map_or(&[][..], Vec::as_slice)
        } else {
            self.search.as_slice()
        };
        let visible = active_rows
            .iter()
            .filter(|row| !self.hidden.contains(&row.message_id))
            .collect::<Vec<_>>();
        let mut conn = json!({
            "phase":self.phase,"statusText":self.status,"retryable":self.phase == "error","reconnectAttempt":self.reconnect,
        });
        if let Some(error) = &self.last_error {
            conn["lastError"] = error.clone();
        }
        let connected = self.phase == "connected";
        let mut navigation = json!({"page": if connected {"workspace"} else {"mode"}});
        if let Some(active) = &self.active {
            navigation["activeConversation"] = json!(active);
        }
        let mut discovered = self.discovered.values().collect::<Vec<_>>();
        discovered.sort_by(|left, right| left.name.cmp(&right.name));
        let discovered = discovered
            .into_iter()
            .map(Discovered::summary)
            .collect::<Vec<_>>();
        let effects = self.performance != "Power Saving";
        json!({
            "schemaVersion": 1, "edition":edition.as_str(),"connection":conn,
            "onlineFiles":connected && self.session.as_ref().is_some_and(|session| session.has(lan_chat_protocol::ONLINE_FILES)),
            "identity": {"displayName":self.identity.username,"userCode":self.identity.user_code,"admin":self.identity.is_admin},
            "navigation": navigation, "rooms":rooms,"directMessages":direct,"activeMessages":visible,"members":members,
            "permissions":{"activeRoomCanManage": self.active.as_ref().is_some_and(|active| active.kind=="room"
                && self.rooms.iter().any(|room| room.name == active.id && room.can_manage))},
            "connectionApprovals":approvals,"savedConnection":self.saved,
            "hostDefaults":{"available":host_available},"lanDiscovery":{"scanning":self.scan.is_some(),"hosts":discovered},
            "diagnostics":{"enabled":self.logging},
            "performance":{"mode":self.performance,"effectiveMode":self.performance,"effectsEnabled":effects,
                "animationsEnabled":effects,"gradientsEnabled":effects,"animationDurationScale":1,"observedFrameCount":0,
                "observedFps":0,"observedP95FrameMs":0,"observedMaxFrameMs":0,"automaticReason":""},
        })
    }

    pub fn add(
        &mut self,
        message: Message,
        edition: Edition,
        attachment: Option<Value>,
        unread: bool,
    ) {
        let conversation = match message.kind.as_str() {
            "chat" => Conversation::room(if message.room.is_empty() {
                "lobby"
            } else {
                &message.room
            }),
            "private_chat" | "offline_message" | "dm_edited" => {
                let side = if self
                    .identity
                    .user_code
                    .eq_ignore_ascii_case(&message.user_code)
                {
                    Conversation::dm(&message.target_user_code, &message.target_user_code)
                } else {
                    Conversation::dm(&message.user_code, &message.username)
                };
                self.direct.entry(side.id.clone()).or_insert(side).clone()
            }
            _ => {
                let Some(active) = &self.active else { return };
                active.clone()
            }
        };
        let key = conversation.key();
        let online = edition == Edition::Online && conversation.kind == "dm";
        let row = self.row(message, attachment);
        let increase = unread
            && !row.self_message
            && !row.system_message
            && self.active.as_ref().is_none_or(|r| r.key() != key);
        if merge(self.histories.entry(key.clone()).or_default(), row, online) && increase {
            *self.unread.entry(key).or_default() += 1;
        }
    }

    pub fn row(&self, message: Message, attachment: Option<Value>) -> Row {
        let time = DateTime::parse_from_rfc3339(&message.created_at)
            .map(|date| date.with_timezone(&Local).format("%H:%M").to_string())
            .unwrap_or_default();
        let own = !message.user_code.is_empty()
            && message
                .user_code
                .eq_ignore_ascii_case(&self.identity.user_code);
        let attachment = if !message.recalled && !message.file_name.is_empty() {
            Some(
                json!({"attachmentId":message.attachment_id,"fileName":message.file_name,"logicalSize":message.logical_size,
                "status":"available","transport":"tls"}),
            )
        } else {
            attachment
        };
        let content = if message.recalled {
            RECALLED_MESSAGE.into()
        } else if message.crypto.is_some() && attachment.is_none() {
            "加密内容暂不可读取".into()
        } else {
            message.content
        };
        Row {
            message_id: if message.message_id.is_empty() {
                format!("event-{}", Uuid::new_v4())
            } else {
                message.message_id
            },
            display_name: message.username,
            user_code: message.user_code.clone(),
            time,
            created_at: message.created_at,
            content,
            self_message: own,
            system_message: message.recalled || message.kind == "system",
            delivery_state: message.delivery_state,
            attachment,
            image_size: (message.image_size > 0).then_some(message.image_size),
            image_peer: if message.image_size <= 0 {
                String::new()
            } else if own {
                message.target_user_code
            } else {
                message.user_code
            },
            revision: message.revision,
        }
    }

    pub fn update_delivery(&mut self, id: &str, delivery: &str) {
        for rows in self.histories.values_mut() {
            for row in rows {
                if row.message_id == id && row.self_message {
                    row.delivery_state = delivery.into();
                }
            }
        }
    }
}

pub fn merge(rows: &mut Vec<Row>, mut row: Row, online: bool) -> bool {
    if let Some(prior) = rows.iter_mut().find(|prior| {
        prior.message_id == row.message_id
            && (!online || prior.user_code.eq_ignore_ascii_case(&row.user_code))
    }) {
        if row.created_at.is_empty() {
            row.created_at.clone_from(&prior.created_at);
            row.time.clone_from(&prior.time);
        }
        if row.delivery_state.is_empty() || (online && prior.delivery_state == "delivered") {
            row.delivery_state.clone_from(&prior.delivery_state);
        }
        if !online
            || row.system_message
            || (!prior.system_message && row.revision >= prior.revision)
        {
            *prior = row;
        } else if row.delivery_state == "delivered" {
            prior.delivery_state = row.delivery_state;
        }
        return false;
    }
    rows.push(row);
    if online {
        rows.sort_by(|left, right| {
            let stamp = |row: &Row| DateTime::parse_from_rfc3339(&row.created_at).ok();
            stamp(left).cmp(&stamp(right)).then_with(|| {
                (&left.user_code, &left.message_id).cmp(&(&right.user_code, &right.message_id))
            })
        });
    }
    true
}
