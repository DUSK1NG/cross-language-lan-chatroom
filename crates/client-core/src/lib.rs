mod discovery;
pub mod mls;
mod session;
mod state;
mod transfers;

use std::{
    fs,
    io::Write,
    path::{Path, PathBuf},
    sync::{Arc, Mutex},
};

use anyhow::{Context, Result, bail, ensure};
use chrono::{SecondsFormat, Utc};
use lan_chat_protocol::{
    LAN_HISTORY_PAGES, LAN_ROOM_ROLES, MAX_FRAME_BYTES, Message, ONLINE_BOT_KEY_SYNC,
    ONLINE_BOT_SETTINGS, ONLINE_FILES, ONLINE_GROUP_ATTACHMENTS, ONLINE_GROUP_MANAGEMENT,
    ONLINE_GROUPS, ONLINE_HISTORY_PAGES, ONLINE_IMAGES, ONLINE_SOCIAL, OnlineBotSettings,
    OnlineBotSettingsWithKey, ROOM_PASSWORD,
};
use ring::digest::{SHA256, digest};
use serde::Deserialize;
use serde_json::{Value, json};
use uuid::Uuid;

use session::{Login, Network};
use state::{ConnectionForm, Conversation, State, merge};

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Edition {
    Lan,
    Online,
}

impl Edition {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Lan => "lan",
            Self::Online => "online",
        }
    }
}

pub enum Pick {
    Certificate,
    Attachment,
    Image,
    OnlineFiles,
}

pub trait Ui: Send + Sync {
    fn emit(&self, event: &str, payload: Value);
    fn choose(&self, kind: Pick) -> Result<Vec<PathBuf>>;
    fn save(&self, filename: &str) -> Result<Option<PathBuf>>;
    fn copy(&self, text: &str) -> Result<()>;
}

pub struct Client {
    edition: Edition,
    dir: PathBuf,
    host_dir: PathBuf,
    server_file: PathBuf,
    state: Mutex<State>,
    lifecycle: tokio::sync::Mutex<()>,
    ui: Arc<dyn Ui>,
}

#[derive(Deserialize)]
struct Command {
    id: String,
    #[serde(rename = "type")]
    kind: String,
    payload: Value,
}

#[derive(Default, Deserialize)]
#[serde(default, rename_all = "camelCase")]
struct Payload {
    #[serde(flatten)]
    form: ConnectionForm,
    password: String,
    register: bool,
    room: String,
    room_password: String,
    is_private: bool,
    host_id: String,
    display_name: String,
    target_user_code: String,
    group_id: String,
    users: Vec<String>,
    message_id: String,
    attachment_id: String,
    image_size: i64,
    paths: Vec<PathBuf>,
    content: String,
    openapi_key: Option<String>,
    query: String,
    action: String,
    text: String,
    mode: String,
    enabled: bool,
    revision: i64,
}

/// 在线版首次启动预置的公共服务器；用户保存过的地址始终优先。
const ONLINE_DEFAULT_SERVER: &str = "1.15.129.115";

impl Client {
    pub fn new(
        edition: Edition,
        dir: PathBuf,
        host_dir: PathBuf,
        server_file: PathBuf,
        ui: Arc<dyn Ui>,
    ) -> Result<Self> {
        fs::create_dir_all(&dir)?;
        let path = dir.join("desktop-connection.json");
        let saved = match fs::read(&path) {
            Ok(bytes) => serde_json::from_slice(&bytes).context("读取连接设置")?,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                let mut form = ConnectionForm::default();
                if edition == Edition::Online {
                    // Ship the public server so a fresh install can log in without typing an address.
                    form.server_ip = ONLINE_DEFAULT_SERVER.into();
                }
                form
            }
            Err(error) => return Err(error).context("读取连接设置"),
        };
        Ok(Self {
            edition,
            dir,
            host_dir,
            server_file,
            state: Mutex::new(State::new(edition, saved)),
            lifecycle: tokio::sync::Mutex::new(()),
            ui,
        })
    }

    pub fn state(&self) -> Value {
        self.snapshot(&self.state.lock().expect("client state"))
    }

    pub async fn close(&self) -> Result<()> {
        let _lifecycle = self.lifecycle.lock().await;
        self.stop_session().await?;
        self.publish(&self.state.lock().expect("client state"));
        Ok(())
    }

    pub async fn dispatch(self: &Arc<Self>, raw: &str) -> Result<()> {
        let command: Command = serde_json::from_str(raw).context("读取桌面命令")?;
        ensure!(
            (1..=48).contains(&command.id.len())
                && !command.kind.is_empty()
                && command.payload.is_object(),
            "桌面命令需要有效的 id、type 和 payload"
        );
        let payload =
            serde_json::from_value(command.payload.clone()).context("读取桌面命令参数")?;
        match self.execute(&command, payload).await {
            Ok(Some(mut fields)) => {
                fields["id"] = json!(command.id);
                fields["ok"] = json!(true);
                self.ui.emit("chat:result", fields);
            }
            Ok(None) => (),
            Err(error) => self.result(&command.id, Err(error)),
        }
        Ok(())
    }

    async fn execute(
        self: &Arc<Self>,
        command: &Command,
        mut payload: Payload,
    ) -> Result<Option<Value>> {
        let kind = command.kind.as_str();
        let id = &command.id;
        match kind {
            "session.connectOnline"
            | "session.connectRemote"
            | "session.connectLocalHost"
            | "session.connectDiscoveredHost" => {
                let online = kind == "session.connectOnline";
                ensure!(
                    online == (self.edition == Edition::Online),
                    "所选连接方式与应用版本不符"
                );
                let host = kind == "session.connectLocalHost";
                if host {
                    ensure!(self.server_file.is_file(), "找不到 Rust 房主服务端");
                    payload.form.server_ip = "127.0.0.1".into();
                    payload.form.server_port = 8888;
                    payload.form.tls_server_name = "localhost".into();
                    payload.form.ca_file = self.host_dir.join("certs/server-lan.crt");
                }
                if kind == "session.connectDiscoveredHost" {
                    let discovered = self
                        .state
                        .lock()
                        .expect("client state")
                        .discovered
                        .get(&payload.host_id)
                        .cloned()
                        .filter(|host| host.recent())
                        .context("房主已离开，请重新搜索")?;
                    fs::create_dir_all(
                        discovered
                            .path
                            .parent()
                            .expect("trusted certificate parent"),
                    )?;
                    fs::write(&discovered.path, discovered.certificate)?;
                    payload.form.server_ip = discovered.address;
                    payload.form.server_port = discovered.port;
                    payload.form.ca_file = discovered.path;
                    payload.form.tls_server_name = "localhost".into();
                }
                self.connect(Login {
                    form: payload.form,
                    password: payload.password,
                    register: payload.register,
                    host,
                })
                .await?;
            }
            "session.disconnect" => {
                self.close().await?;
            }
            "session.discoverLanHosts" => {
                ensure!(self.edition == Edition::Lan, "在线版使用服务器地址");
                self.scan().await?;
            }
            "session.chooseCertificate" => {
                let file = self.choose(Pick::Certificate).await?.into_iter().next();
                return Ok(Some(json!({"certificateFile":file})));
            }
            "message.copy" => self.ui.copy(&payload.text)?,
            "settings.setPerformanceMode" => {
                ensure!(
                    matches!(
                        payload.mode.as_str(),
                        "Automatic" | "High" | "Balanced" | "Power Saving"
                    ),
                    "性能模式无效"
                );
                let mut state = self.state.lock().expect("client state");
                state.performance = payload.mode;
                self.publish(&state);
            }
            "settings.setConnectionLogging" => {
                let mut state = self.state.lock().expect("client state");
                state.logging = payload.enabled;
                self.publish(&state);
            }
            "diagnostics.frontendError" => {
                if self.state.lock().expect("client state").logging {
                    let path = self.dir.join("frontend-errors.log");
                    let mut file = fs::OpenOptions::new()
                        .create(true)
                        .append(true)
                        .open(path)?;
                    writeln!(file, "{}", json!({"at":now(),"error":command.payload}))?;
                }
            }
            "performance.reportFrameTimes" => (),
            "attachment.chooseUpload"
            | "attachment.download"
            | "onlineFile.chooseUpload"
            | "onlineFile.dropUpload"
            | "image.chooseUpload"
            | "image.read" => {
                let session = self.active_session()?;
                if kind == "attachment.chooseUpload" {
                    ensure!(self.edition == Edition::Lan, "在线文件请选择在线传输入口");
                    let _transfer = session
                        .connection
                        .transfer
                        .try_lock()
                        .context("正在发送附件")?;
                    if let Some(path) = self.choose(Pick::Attachment).await?.into_iter().next() {
                        let result = self
                            .upload(&session.connection, id, &payload.room, &path)
                            .await;
                        if let Err(error) = &result {
                            self.attachment_event(id, json!({"type":"error","code":"attachment_upload","message":error.to_string()}));
                        }
                        result?;
                    } else {
                        self.attachment_event(
                            id,
                            json!({"type":"attachment.commit","content":"cancelled"}),
                        );
                    }
                } else if kind == "attachment.download" {
                    let result = if self.edition == Edition::Online {
                        ensure!(session.has(ONLINE_FILES), "服务器尚未支持文件传输");
                        self.download_online(&session.connection, &payload.attachment_id)
                            .await
                    } else {
                        self.download(&session.connection, &payload.attachment_id)
                            .await
                    };
                    if let Err(error) = &result {
                        self.report("attachment_download", &error.to_string(), "controller", id);
                    }
                    result?;
                } else if kind == "image.read" {
                    ensure!(session.has(ONLINE_IMAGES), "当前在线服务器不支持图片私信");
                    ensure!(
                        payload.group_id.is_empty() || session.has(ONLINE_GROUP_ATTACHMENTS),
                        "当前服务端不支持群图片"
                    );
                    let url = self.read_image(&session.connection, payload).await?;
                    return Ok(Some(json!({"imageUrl":url})));
                } else if kind == "image.chooseUpload" {
                    ensure!(session.has(ONLINE_IMAGES), "当前在线服务器不支持图片私信");
                    ensure!(
                        payload.group_id.is_empty() || session.has(ONLINE_GROUP_ATTACHMENTS),
                        "当前服务端不支持群图片"
                    );
                    self.validate_online_scope(&payload.target_user_code, &payload.group_id)?;
                    if let Some(path) = self.choose(Pick::Image).await?.into_iter().next() {
                        self.upload_image(
                            &session.connection,
                            &payload.target_user_code,
                            &payload.group_id,
                            id,
                            &path,
                        )
                        .await?;
                    }
                } else {
                    ensure!(session.has(ONLINE_FILES), "服务器尚未支持文件传输");
                    let _transfer = session
                        .connection
                        .transfer
                        .try_lock()
                        .context("正在发送附件")?;
                    ensure!(
                        payload.group_id.is_empty() || session.has(ONLINE_GROUP_ATTACHMENTS),
                        "当前服务端不支持群文件"
                    );
                    self.validate_online_scope(&payload.target_user_code, &payload.group_id)?;
                    let paths = if kind == "onlineFile.chooseUpload" {
                        self.choose(Pick::OnlineFiles).await?
                    } else {
                        payload.paths
                    };
                    for path in paths {
                        self.upload_online(
                            &session.connection,
                            id,
                            &payload.target_user_code,
                            &payload.group_id,
                            &path,
                        )
                        .await?;
                    }
                    self.attachment_event(
                        id,
                        json!({"type":"attachment.commit","content":"finished"}),
                    );
                }
            }
            _ => {
                let mut state = self.state.lock().expect("client state");
                ensure!(state.phase == "connected", "聊天室未连接");
                let session = state.session.as_ref().expect("connected session").clone();
                let mut message = Message {
                    ..Message::default()
                };
                let mut delayed = false;
                match kind {
                    "directory.refreshRooms" => message.kind = "rooms_request".into(),
                    "directory.refreshGroups" => {
                        ensure!(session.has(ONLINE_GROUPS), "服务器不支持群聊");
                        message.kind = "group_list".into();
                        if session.has(ONLINE_SOCIAL) {
                            session.connection.send(Message {
                                kind: "friend_list".into(),
                                ..Message::default()
                            })?;
                        }
                    }
                    "directory.refreshUsers" => {
                        message.kind = if self.edition == Edition::Online {
                            "dm_contacts"
                        } else {
                            "users_request"
                        }
                        .into()
                    }
                    "conversation.selectRoom" | "room.create" => {
                        ensure!(!payload.room.is_empty(), "频道名称不能为空");
                        let create = kind == "room.create";
                        message.kind = if create { "room_create" } else { "room_join" }.into();
                        message.room = payload.room.clone();
                        message.room_password = payload.room_password;
                        message.private = payload.is_private;
                        if session.has(ROOM_PASSWORD) {
                            delayed = true;
                            message.command_id = id.clone();
                        } else {
                            ensure!(message.room_password.is_empty(), "当前服务端不支持频道密码");
                            session.connection.send(message)?;
                            message = state.select(Conversation::room(&payload.room));
                        }
                    }
                    "conversation.selectDirect" | "conversation.openPrivate" => {
                        ensure!(!payload.form.user_code.is_empty(), "私信对象代码不能为空");
                        if self.edition == Edition::Online && kind == "conversation.openPrivate" {
                            message.kind = "dm_lookup".into();
                            message.target_user_code = payload.form.user_code;
                            message.command_id = id.clone();
                            delayed = true;
                        } else {
                            let code = payload.form.user_code;
                            let name = if payload.display_name.is_empty() {
                                &code
                            } else {
                                &payload.display_name
                            };
                            let reference = Conversation::dm(&code, name);
                            let reference = state
                                .direct
                                .entry(reference.id.clone())
                                .or_insert(reference)
                                .clone();
                            message = state.select(reference);
                        }
                    }
                    "conversation.selectGroup" => {
                        ensure!(session.has(ONLINE_GROUPS), "服务器不支持群聊");
                        let group = state
                            .groups
                            .get(&payload.group_id)
                            .context("这个群聊已不可访问")?;
                        let reference = Conversation::group(&group.id, &group.name);
                        session.connection.send(Message {
                            kind: "group_detail".into(),
                            group_id: reference.id.clone(),
                            ..Message::default()
                        })?;
                        message = state.select(reference);
                    }
                    "group.create" | "group.action" => {
                        ensure!(session.has(ONLINE_GROUPS), "服务器不支持群聊");
                        message.kind = if kind == "group.create" {
                            "group_create"
                        } else {
                            let action = match payload.action.as_str() {
                                "leave" => "group_leave",
                                "invite" => "group_invite",
                                "kick" => "group_kick",
                                "role" => "group_role",
                                "transfer" => "group_transfer",
                                "rename" => "group_update",
                                "announce" => "group_announce",
                                _ => bail!("未知群管理操作"),
                            };
                            ensure!(
                                action == "group_leave" || session.has(ONLINE_GROUP_MANAGEMENT),
                                "服务器不支持群管理"
                            );
                            ensure!(
                                state.groups.contains_key(&payload.group_id),
                                "这个群聊已不可访问"
                            );
                            action
                        }
                        .into();
                        message.group_id = payload.group_id;
                        message.room = payload.room;
                        message.users = payload.users;
                        message.target_user_code = payload.target_user_code;
                        message.content = payload.content;
                        message.command_id = id.clone();
                        delayed = true;
                    }
                    "friend.request" | "friend.respond" => {
                        ensure!(session.has(ONLINE_SOCIAL), "服务器不支持好友操作");
                        message.kind = if kind == "friend.request" {
                            "friend_request"
                        } else {
                            "friend_respond"
                        }
                        .into();
                        message.target_user_code = payload.target_user_code;
                        message.action = payload.action;
                        message.command_id = id.clone();
                        delayed = true;
                    }
                    "botSettings.get" | "botSettings.save" => {
                        ensure!(
                            session.has(ONLINE_BOT_SETTINGS),
                            "服务器不支持机器人设置同步"
                        );
                        let save = kind == "botSettings.save";
                        let with_key = session.has(ONLINE_BOT_KEY_SYNC)
                            && (!save || payload.openapi_key.is_some());
                        message.kind = match (save, with_key) {
                            (false, false) => "bot_settings_get",
                            (true, false) => "bot_settings_save",
                            (false, true) => "bot_settings_get_with_key",
                            (true, true) => "bot_settings_save_with_key",
                        }
                        .into();
                        message.command_id = id.clone();
                        message.content = if save && with_key {
                            serde_json::to_string(&OnlineBotSettingsWithKey {
                                settings: serde_json::from_str(&payload.content)
                                    .context("机器人设置格式无效")?,
                                openapi_key: payload
                                    .openapi_key
                                    .context("缺少 OpenAPI Key 字段")?,
                            })?
                        } else {
                            payload.content
                        };
                        message.revision = payload.revision;
                        delayed = true;
                    }
                    "history.groupMore" => {
                        let group = state
                            .active
                            .as_ref()
                            .filter(|active| active.kind == "group")
                            .context("请先选择群聊")?;
                        message = group.history("");
                        message.before_message_id = state
                            .group_before
                            .get(&group.id)
                            .cloned()
                            .context("没有更早的群消息")?;
                    }
                    "history.search" | "history.searchMore" => {
                        let reference = state.active.as_ref().context("请选择会话")?;
                        let paged =
                            session.has(LAN_HISTORY_PAGES) || session.has(ONLINE_HISTORY_PAGES);
                        let append = kind == "history.searchMore";
                        let query = if append { &state.query } else { &payload.query };
                        let mut request =
                            reference.history(if paged { query.trim() } else { query });
                        if append {
                            ensure!(
                                paged && state.search_request.is_none(),
                                "当前不能加载更多搜索结果"
                            );
                            request.before_message_id =
                                state.search_before.clone().context("没有更早的搜索结果")?;
                        }
                        let track = paged && !query.trim().is_empty();
                        if track {
                            request.command_id.clone_from(id);
                        }
                        session.connection.send(request)?;
                        if !append {
                            state.query = if paged {
                                payload.query.trim().into()
                            } else {
                                payload.query
                            };
                            state.search.clear();
                            state.search_before = None;
                        }
                        state.search_request = track.then(|| (id.clone(), append));
                        self.publish(&state);
                        return Ok(Some(json!({})));
                    }
                    "chat.sendRoom" | "chat.sendPrivate" | "chat.sendGroup" => {
                        ensure!(
                            !payload.content.trim().is_empty()
                                && payload.content.len() <= MAX_FRAME_BYTES,
                            "消息为空或超过长度上限"
                        );
                        message.kind = match kind {
                            "chat.sendRoom" => "chat",
                            "chat.sendGroup" => {
                                ensure!(
                                    session.has(ONLINE_GROUPS)
                                        && state.groups.contains_key(&payload.group_id),
                                    "这个群聊已不可访问"
                                );
                                "group_send"
                            }
                            _ => "private_chat",
                        }
                        .into();
                        message.room = payload.room;
                        message.target_user_code = payload.target_user_code;
                        message.group_id = payload.group_id;
                        message.content = payload.content;
                        message.message_id = id.clone();
                        message.command_id = id.clone();
                        session.connection.send(message.clone())?;
                        state.outgoing.insert(id.clone(), message.clone());
                        message.username.clone_from(&state.identity.username);
                        message.user_code.clone_from(&state.identity.user_code);
                        message.created_at = now();
                        message.delivery_state = "queued".into();
                        state.add(message, self.edition, None, false);
                        self.publish(&state);
                        return Ok(Some(json!({})));
                    }
                    "room.action" => {
                        message.kind = "room_action".into();
                        message.room = payload.room;
                        message.content = payload.action;
                        message.target_user_code = payload.target_user_code;
                        message.room_password = payload.room_password;
                        if session.has(LAN_ROOM_ROLES) {
                            message.command_id = id.clone();
                            delayed = true;
                        }
                    }
                    "admin.action" | "message.recall"
                        if kind == "message.recall" && self.edition == Edition::Online =>
                    {
                        message.kind = "message_recall".into();
                        message.message_id = payload.message_id;
                        message.command_id = id.clone();
                        delayed = true;
                    }
                    "admin.action" | "message.recall" => {
                        message.kind = "admin_action".into();
                        message.content = if kind == "message.recall" {
                            "recall".into()
                        } else {
                            payload.action
                        };
                        message.target_user_code = payload.target_user_code;
                        message.message_id = payload.message_id;
                        message.command_id = id.clone();
                        delayed = kind == "message.recall"
                            || (session.has(LAN_ROOM_ROLES) && message.content == "mute");
                    }
                    "message.removeLocal" => {
                        state.hidden.insert(payload.message_id);
                        self.publish(&state);
                        return Ok(Some(json!({})));
                    }
                    "message.retry" => {
                        message = state
                            .outgoing
                            .get(&payload.message_id)
                            .context("找不到可重新发送的消息")?
                            .clone();
                        state.update_delivery(&payload.message_id, "queued");
                    }
                    _ => bail!("桌面命令 {kind} 不受支持"),
                }
                session.connection.send(message)?;
                if delayed {
                    state.room_commands.insert(id.clone());
                }
                self.publish(&state);
                if delayed {
                    return Ok(None);
                }
            }
        }
        Ok(Some(json!({})))
    }

    fn received(&self, network: &Network, mut message: Message) {
        let mut state = self.state.lock().expect("client state");
        if state.generation != Some(network.generation) {
            return;
        }
        let result = self.apply(&mut state, network, &mut message);
        if let Err(error) = result {
            self.report(
                "message_receive",
                &format!("{error:#}"),
                "controller",
                &message.command_id,
            );
        }
        self.publish(&state);
    }

    fn apply(&self, state: &mut State, network: &Network, message: &mut Message) -> Result<()> {
        match message.kind.as_str() {
            "chat" | "private_chat" | "offline_message" | "dm_edited" | "group_message" => {
                let attachment = self.received_attachment(network, message);
                let offline = message.kind == "offline_message";
                if self.edition == Edition::Online
                    && message.kind == "private_chat"
                    && !message
                        .user_code
                        .eq_ignore_ascii_case(&state.identity.user_code)
                {
                    network.send(Message {
                        kind: "dm_received".into(),
                        message_id: message.message_id.clone(),
                        target_user_code: message.user_code.clone(),
                        ..Message::default()
                    })?;
                }
                if message.kind == "group_message"
                    && state
                        .identity
                        .user_code
                        .eq_ignore_ascii_case(&message.user_code)
                {
                    state.outgoing.remove(&message.message_id);
                }
                state.add(message.clone(), self.edition, attachment, !offline);
            }
            "dm_backlog_end" => network.send(Message {
                kind: "dm_ack_batch".into(),
                message_id: message.message_id.clone(),
                ..Message::default()
            })?,
            "system" if !message.room.is_empty() => {
                self.ui.emit("chat:presence", json!(message.content));
                network.send(Message {
                    kind: "users_request".into(),
                    ..Message::default()
                })?;
            }
            "system" => state.add(message.clone(), self.edition, None, false),
            "delivery_receipt" => {
                state.update_delivery(&message.message_id, &message.delivery_state);
                if message.delivery_state == "delivered" {
                    state.outgoing.remove(&message.message_id);
                }
            }
            "rooms_response" => state.rooms = std::mem::take(&mut message.room_details),
            "users_response" => state.members = std::mem::take(&mut message.user_details),
            "dm_contacts" => {
                for user in &message.user_details {
                    let reference = Conversation::dm(&user.user_code, &user.username);
                    state.direct.insert(reference.id.clone(), reference);
                }
            }
            "group_list" => {
                state.replace_groups(std::mem::take(&mut message.groups));
                if let Some(active) = state
                    .active
                    .as_ref()
                    .filter(|active| active.kind == "group")
                {
                    network.send(Message {
                        kind: "group_detail".into(),
                        group_id: active.id.clone(),
                        ..Message::default()
                    })?;
                    if !state.histories.contains_key(&active.key()) {
                        network.send(active.history(""))?;
                    }
                }
            }
            "group_detail" => {
                if state
                    .active
                    .as_ref()
                    .is_some_and(|active| active.kind == "group" && active.id == message.group_id)
                    && let Some(group) = message.groups.pop()
                {
                    state.groups.insert(group.id.clone(), group);
                    state.group_members = std::mem::take(&mut message.profiles);
                }
            }
            "groups_changed" => network.send(Message {
                kind: "group_list".into(),
                ..Message::default()
            })?,
            "friends_changed" => network.send(Message {
                kind: "friend_list".into(),
                ..Message::default()
            })?,
            "friend_list" => state.friends = std::mem::take(&mut message.profiles),
            "group_create" => {
                if state.room_commands.remove(&message.command_id) {
                    let group = message.groups.pop().context("服务器未返回新建群聊")?;
                    let reference = Conversation::group(&group.id, &group.name);
                    state.groups.insert(group.id.clone(), group);
                    network.send(Message {
                        kind: "group_detail".into(),
                        group_id: reference.id.clone(),
                        ..Message::default()
                    })?;
                    self.result(&message.command_id, network.send(state.select(reference)));
                }
            }
            "dm_lookup" | "room_join" | "room_create" => {
                let requested = state.room_commands.remove(&message.command_id);
                let reassigned = message.kind == "room_join"
                    && message.command_id.is_empty()
                    && state
                        .session
                        .as_ref()
                        .is_some_and(|session| session.has(LAN_ROOM_ROLES));
                if requested || reassigned {
                    let reference = if message.kind == "dm_lookup" {
                        let item = Conversation::dm(&message.user_code, &message.username);
                        state.direct.insert(item.id.clone(), item.clone());
                        item
                    } else {
                        Conversation::room(&message.room)
                    };
                    let result = network.send(state.select(reference));
                    if requested {
                        self.result(&message.command_id, result);
                    } else {
                        result?;
                    }
                }
            }
            "room_action" | "admin_action" | "friend_request" | "friend_respond"
            | "group_leave" | "group_invite" | "group_kick" | "group_role" | "group_transfer"
            | "group_update" | "group_announce" => {
                if state.room_commands.remove(&message.command_id) {
                    self.result(&message.command_id, Ok(()));
                }
            }
            "bot_settings_get"
            | "bot_settings_save"
            | "bot_settings_get_with_key"
            | "bot_settings_save_with_key" => {
                if state.room_commands.remove(&message.command_id) {
                    let account = if message.kind.ends_with("_with_key") {
                        serde_json::from_str::<OnlineBotSettingsWithKey>(&message.content).map(|data| {
                            json!({ "revision": message.revision, "settings": data.settings, "openapiKey": data.openapi_key })
                        })
                    } else {
                        serde_json::from_str::<OnlineBotSettings>(&message.content).map(|settings| {
                            json!({ "revision": message.revision, "settings": settings })
                        })
                    };
                    match account {
                        Ok(bot_settings) => self.ui.emit(
                            "chat:result",
                            json!({
                                "id": message.command_id, "ok": true, "botSettings": bot_settings
                            }),
                        ),
                        Err(_) => self.result(
                            &message.command_id,
                            Err(anyhow::anyhow!("账号设置响应格式无效")),
                        ),
                    };
                }
            }
            "history_response" => {
                let reference = if !message.group_id.is_empty() {
                    let Some(group) = state.groups.get(&message.group_id) else {
                        return Ok(());
                    };
                    Conversation::group(&group.id, &group.name)
                } else if message.private {
                    Conversation::dm(&message.target_user_code, "")
                } else {
                    Conversation::room(&message.room)
                };
                let key = reference.key();
                let online = self.edition == Edition::Online;
                let mut rows = Vec::new();
                for item in std::mem::take(&mut message.messages) {
                    let attachment = self.received_attachment(network, &item);
                    let row = state.row(item, attachment);
                    merge(&mut rows, row, online);
                }
                if message.search_query.is_empty() {
                    if reference.kind == "group" {
                        if let Some(first) = rows.first().filter(|_| message.has_more) {
                            state
                                .group_before
                                .insert(reference.id, first.message_id.clone());
                        } else {
                            state.group_before.remove(&reference.id);
                        }
                    }
                    if let Some(previous) = state.histories.get(&key) {
                        for row in previous {
                            if online || !rows.iter().any(|item| item.message_id == row.message_id)
                            {
                                merge(&mut rows, row.clone(), online);
                            }
                        }
                    }
                    state.histories.insert(key, rows);
                } else if state
                    .active
                    .as_ref()
                    .is_some_and(|active| active.key() == key)
                    && state.query == message.search_query
                {
                    if state.session.as_ref().is_some_and(|session| {
                        session.has(LAN_HISTORY_PAGES) || session.has(ONLINE_HISTORY_PAGES)
                    }) {
                        let Some((request_id, append)) = &state.search_request else {
                            return Ok(());
                        };
                        if request_id != &message.command_id {
                            return Ok(());
                        }
                        state.search_before = message
                            .has_more
                            .then(|| rows.first().map(|row| row.message_id.clone()))
                            .flatten();
                        if *append {
                            for previous in std::mem::take(&mut state.search) {
                                merge(&mut rows, previous, online);
                            }
                        }
                        state.search_request = None;
                    }
                    state.search = rows;
                }
            }
            "connection_approval_request" => {
                state
                    .approvals
                    .insert(message.message_id.clone(), message.clone());
            }
            "connection_approval_result" => {
                state.approvals.remove(&message.message_id);
            }
            "message_recalled" => {
                state.hidden.insert(message.message_id.clone());
                if state.room_commands.remove(&message.command_id) {
                    self.result(&message.command_id, Ok(()));
                }
            }
            "error" => {
                if state
                    .search_request
                    .as_ref()
                    .is_some_and(|(id, _)| id == &message.command_id)
                {
                    state.search_request = None;
                }
                if !message.message_id.is_empty() {
                    state.update_delivery(&message.message_id, "failed");
                }
                if state.room_commands.remove(&message.command_id) {
                    self.result(
                        &message.command_id,
                        Err(anyhow::anyhow!("{}", message.content)),
                    );
                }
                self.report(
                    "server_error",
                    &message.content,
                    "server",
                    &message.command_id,
                );
            }
            "mls.group.welcome" | "mls.group.proposal" | "mls.group.commit" => {
                self.mls_event(network, message)?
            }
            _ => (),
        }
        Ok(())
    }

    fn active_session(&self) -> Result<session::Session> {
        let state = self.state.lock().expect("client state");
        ensure!(state.phase == "connected", "聊天室未连接");
        Ok(state.session.as_ref().expect("connected session").clone())
    }

    fn received_attachment(&self, network: &Network, message: &Message) -> Option<Value> {
        match self.attachment_summary(network, message) {
            Ok(attachment) => attachment,
            Err(error) => {
                self.report(
                    "attachment_receive",
                    &format!("{error:#}"),
                    "controller",
                    &message.command_id,
                );
                None
            }
        }
    }

    fn validate_online_scope(&self, target: &str, group: &str) -> Result<()> {
        ensure!(
            self.state
                .lock()
                .expect("client state")
                .active
                .as_ref()
                .is_some_and(|active| if group.is_empty() {
                    active.kind == "dm" && active.user_code.eq_ignore_ascii_case(target)
                } else {
                    active.kind == "group" && active.id == group
                }),
            "请先选择该在线会话"
        );
        Ok(())
    }

    async fn stop_session(&self) -> Result<()> {
        let session = {
            let mut state = self.state.lock().expect("client state");
            let session = state.session.take();
            let mut next = State::new(self.edition, state.saved.clone());
            next.logging = state.logging;
            next.performance.clone_from(&state.performance);
            *state = next;
            session
        };
        if let Some(session) = session {
            session.stop().await?;
        }
        Ok(())
    }

    fn snapshot(&self, state: &State) -> Value {
        state.snapshot(
            self.edition,
            self.edition == Edition::Lan && self.server_file.is_file(),
        )
    }

    fn publish(&self, state: &State) {
        self.ui.emit("chat:state", self.snapshot(state));
    }

    fn save_connection(&self, form: &ConnectionForm) -> Result<()> {
        let path = self.dir.join("desktop-connection.json");
        let temporary = self.dir.join(format!(".connection-{}.tmp", Uuid::new_v4()));
        let body = serde_json::to_vec(form)?;
        fs::write(&temporary, body)?;
        if let Err(error) = fs::rename(&temporary, path) {
            fs::remove_file(&temporary)?;
            return Err(error.into());
        }
        Ok(())
    }

    async fn choose(&self, kind: Pick) -> Result<Vec<PathBuf>> {
        let ui = self.ui.clone();
        tokio::task::spawn_blocking(move || ui.choose(kind)).await?
    }

    async fn save(&self, name: &str) -> Result<Option<PathBuf>> {
        let name = name.to_owned();
        let ui = self.ui.clone();
        tokio::task::spawn_blocking(move || ui.save(&name)).await?
    }

    fn result(&self, id: &str, result: Result<()>) {
        let message = match result {
            Ok(()) => json!({"id":id,"ok":true}),
            Err(error) => {
                json!({"id":id,"ok":false,"error":error_payload("command_failed",&error.to_string(),"controller",id,false)})
            }
        };
        self.ui.emit("chat:result", message);
    }

    fn report(&self, code: &str, detail: &str, source: &str, id: &str) {
        self.ui
            .emit("chat:error", error_payload(code, detail, source, id, false));
    }

    fn attachment_event(&self, id: &str, payload: Value) {
        self.ui.emit(
            "chat:attachment",
            json!({"type":"attachment.event","id":id,"payload":payload}),
        );
    }
}

fn error_payload(code: &str, detail: &str, source: &str, id: &str, retryable: bool) -> Value {
    json!({"code":code,"message":detail,"source":source,"retryable":retryable,"commandId":id})
}

fn now() -> String {
    Utc::now().to_rfc3339_opts(SecondsFormat::AutoSi, true)
}

fn checksum(bytes: &[u8]) -> String {
    digest(&SHA256, bytes)
        .as_ref()
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect()
}

fn filename(path: &Path) -> Result<&str> {
    path.file_name()
        .and_then(|name| name.to_str())
        .context("文件名不是有效 UTF-8")
}
