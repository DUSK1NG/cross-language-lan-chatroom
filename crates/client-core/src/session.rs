use std::{
    collections::HashMap,
    io,
    path::{Path, PathBuf},
    process::Stdio,
    sync::{Arc, Mutex},
    time::Duration,
};

use anyhow::{Context, Result, ensure};
use base64::{
    Engine,
    engine::general_purpose::{STANDARD, URL_SAFE_NO_PAD},
};
use lan_chat_protocol::{
    HEARTBEAT, HEARTBEAT_INTERVAL_SECS, HEARTBEAT_TIMEOUT_SECS, LAN_HISTORY_PAGES, LAN_ROOM_ROLES,
    LocalHostCredentials, MAX_FRAME_BYTES, Message, ONLINE_ACCOUNTS, ONLINE_BOT_KEY_SYNC,
    ONLINE_BOT_SETTINGS, ONLINE_GROUP_ATTACHMENTS, ONLINE_GROUP_MANAGEMENT, ONLINE_GROUPS,
    ONLINE_SOCIAL, ROOM_PASSWORD, read_message, write_message,
};
use ring::rand::{SecureRandom, SystemRandom};
use serde_json::json;
use tokio::{
    io::{AsyncBufReadExt, AsyncWriteExt, BufReader},
    process::{Child, Command},
    sync::{mpsc, oneshot, watch},
    task::JoinHandle,
    time::{Instant, MissedTickBehavior, interval_at, sleep, timeout},
};
use uuid::Uuid;

use crate::{Client, Edition, state::ConnectionForm, transfers::Cryptography};

#[derive(Clone)]
pub struct Session {
    pub connection: Network,
    shutdown: Arc<Mutex<Option<oneshot::Sender<()>>>>,
    task: Arc<tokio::sync::Mutex<Option<JoinHandle<()>>>>,
    pub capabilities: Vec<String>,
}

impl Session {
    pub fn has(&self, capability: &str) -> bool {
        self.capabilities.iter().any(|item| item == capability)
    }

    pub(crate) async fn stop(self) -> Result<()> {
        if let Some(stop) = self.shutdown.lock().expect("session shutdown").take() {
            let _ = stop.send(());
        }
        if let Some(task) = self.task.lock().await.take() {
            task.await?;
        }
        Ok(())
    }
}

#[derive(Clone)]
pub struct Network {
    pub(crate) generation: Uuid,
    pub(crate) crypto: Option<Arc<Mutex<Cryptography>>>,
    pub(crate) transfer: Arc<tokio::sync::Mutex<()>>,
    sender: mpsc::Sender<Message>,
    epoch: watch::Receiver<Option<u64>>,
    requests: Arc<Mutex<HashMap<String, oneshot::Sender<Message>>>>,
}

#[derive(Debug)]
pub(crate) struct TransportLost;

impl std::fmt::Display for TransportLost {
    fn fmt(&self, output: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        output.write_str("连接中断")
    }
}

impl std::error::Error for TransportLost {}

impl Network {
    pub(crate) fn epoch(&self) -> Option<u64> {
        *self.epoch.borrow()
    }

    pub(crate) async fn reconnected(&self, previous: Option<u64>) -> Result<()> {
        let mut epoch = self.epoch.clone();
        epoch
            .wait_for(|value| value.is_some() && *value != previous)
            .await
            .context("上传会话已结束")?;
        Ok(())
    }

    pub fn send(&self, message: Message) -> Result<()> {
        ensure!(
            serde_json::to_vec(&message)?.len() <= MAX_FRAME_BYTES,
            "消息超过 64 KiB"
        );
        self.sender
            .try_send(message)
            .map_err(|error| anyhow::anyhow!("发送队列不可用: {error}"))
    }

    pub async fn request(&self, mut command: Message) -> Result<Message> {
        let mut connected = self.epoch.clone();
        let epoch = connected.borrow_and_update().ok_or(TransportLost)?;
        let id = Uuid::new_v4().to_string();
        command.command_id.clone_from(&id);
        let kind = command.kind.clone();
        let (reply, received) = oneshot::channel();
        self.requests
            .lock()
            .expect("session requests")
            .insert(id.clone(), reply);
        let result = async {
            if let Err(error) = self.send(command) {
                return Err(if self.sender.is_closed() { TransportLost.into() } else { error });
            }
            let message = tokio::select! {
                reply = timeout(Duration::from_secs(20), received) => {
                    reply.context("服务端命令超时")?.map_err(|_| TransportLost)?
                }
                _ = connected.wait_for(|value| *value != Some(epoch)) => return Err(TransportLost.into()),
            };
            ensure!(message.kind != "error", "{}", message.content);
            ensure!(
                message.kind == kind,
                "服务端返回 {}，预期 {kind}",
                message.kind
            );
            Ok(message)
        }
        .await;
        self.requests.lock().expect("session requests").remove(&id);
        result
    }

    fn complete(&self, message: &Message) -> bool {
        if let Some(reply) = self
            .requests
            .lock()
            .expect("session requests")
            .remove(&message.command_id)
        {
            let _ = reply.send(message.clone());
            return true;
        }
        false
    }
}

pub struct Login {
    pub form: ConnectionForm,
    pub password: String,
    pub register: bool,
    pub host: bool,
}

impl Client {
    pub(crate) async fn connect(self: &Arc<Self>, mut login: Login) -> Result<()> {
        login.form.server_ip = login.form.server_ip.trim().into();
        login.form.username = login.form.username.trim().into();
        login.form.user_code = login.form.user_code.trim().into();
        ensure!(
            !login.form.server_ip.is_empty()
                && login.form.server_port != 0
                && (1..=32).contains(&login.form.username.len()),
            "请检查用户名、服务器地址和端口"
        );
        ensure!(
            !login.form.server_ip.contains(['/', ' ', '\\', '?', '#']),
            "请输入服务器域名或 IP，端口请单独填写"
        );
        ensure!(
            login.form.tls_server_name.is_empty() || login.form.tls_server_name == "localhost",
            "TLS 证书名称无效"
        );
        let crypto = if self.edition == Edition::Online {
            ensure!(
                (8..=72).contains(&login.password.len())
                    && (!login.register || !login.form.user_code.is_empty()),
                "请检查账号密码和注册用户代码"
            );
            login.form.tls_server_name.clear();
            login.form.ca_file.clear();
            None
        } else {
            ensure!(!login.form.user_code.is_empty(), "请填写用户代码");
            ensure!(
                !login.form.ca_file.as_os_str().is_empty(),
                "请选择房主提供的公开证书"
            );
            if login.host {
                ensure!(
                    !login.password.trim().is_empty()
                        && login.password.len() <= lan_chat_protocol::LOCAL_HOST_PASSWORD_MAX_BYTES,
                    "房主密码不能为空，最多 72 字节"
                );
            }
            Some(Arc::new(Mutex::new(Cryptography::new(
                &login.form.user_code,
            )?)))
        };
        let generation = Uuid::new_v4();
        let (sender, outgoing) = mpsc::channel(256);
        let (connectivity, epoch) = watch::channel(None);
        let network = Network {
            generation,
            crypto,
            transfer: Arc::new(tokio::sync::Mutex::new(())),
            sender,
            epoch,
            requests: Arc::default(),
        };

        let _lifecycle = self.lifecycle.lock().await;
        self.stop_session().await?;
        let mut state = self.state.lock().expect("client state");
        state.generation = Some(generation);
        state.phase = "connecting";
        state.status = "正在建立 TLS 连接…";
        let client = self.clone();
        let connection = network.clone();
        let (shutdown, stopped) = oneshot::channel();
        let task = tokio::spawn(async move {
            client
                .run_session(login, connection, outgoing, connectivity, stopped)
                .await;
        });
        state.session = Some(Session {
            connection: network,
            shutdown: Arc::new(Mutex::new(Some(shutdown))),
            task: Arc::new(tokio::sync::Mutex::new(Some(task))),
            capabilities: Vec::new(),
        });
        self.publish(&state);
        Ok(())
    }

    async fn run_session(
        self: Arc<Self>,
        mut login: Login,
        network: Network,
        outgoing: mpsc::Receiver<Message>,
        connectivity: watch::Sender<Option<u64>>,
        mut stopped: oneshot::Receiver<()>,
    ) {
        let mut host = if login.host {
            let startup = tokio::select! {
                result = self.start_host(&login) => result,
                _ = &mut stopped => return,
            };
            match startup {
                Ok((process, token)) => {
                    login.password.clear();
                    Some((process, token))
                }
                Err(error) => {
                    self.session_error(network.generation, &error);
                    return;
                }
            }
        } else {
            None
        };

        let token = host.as_ref().map_or("", |(_, token)| token.as_str());
        tokio::select! {
            () = self.maintain_connection(login, &network, outgoing, &connectivity, token) => (),
            _ = &mut stopped => (),
        }
        connectivity.send_replace(None);
        network.requests.lock().expect("session requests").clear();
        if let Some((process, _)) = &mut host
            && let Err(error) = process.kill().await
        {
            self.report("host_shutdown", &error.to_string(), "controller", "");
        }
    }

    async fn maintain_connection(
        &self,
        login: Login,
        network: &Network,
        mut outgoing: mpsc::Receiver<Message>,
        connectivity: &watch::Sender<Option<u64>>,
        token: &str,
    ) {
        let mut previously_connected = false;
        let mut epoch = 0;
        let mut attempt = 0;
        loop {
            epoch += 1;
            let error = match self
                .connection(
                    &login,
                    network,
                    &mut outgoing,
                    token,
                    &mut previously_connected,
                    (connectivity, epoch),
                )
                .await
            {
                Err(error) => error,
                Ok(()) => io::Error::from(io::ErrorKind::UnexpectedEof).into(),
            };
            connectivity.send_replace(None);
            network.requests.lock().expect("session requests").clear();
            let transient = error.chain().any(|error| {
                error.downcast_ref::<io::Error>().is_some_and(|error| {
                    !matches!(
                        error.kind(),
                        io::ErrorKind::InvalidData
                            | io::ErrorKind::InvalidInput
                            | io::ErrorKind::PermissionDenied
                    )
                })
            });
            if !previously_connected || !transient {
                self.session_error(network.generation, &error);
                break;
            }
            {
                let mut state = self.state.lock().expect("client state");
                if state.generation != Some(network.generation) {
                    return;
                }
                attempt = (attempt + 1).min(5);
                state.reconnect += 1;
                state.phase = "reconnecting";
                state.status = "正在恢复连接…";
                state.search_request = None;
                for id in state.room_commands.drain() {
                    self.result(&id, Err(anyhow::anyhow!("连接中断")));
                }
                self.publish(&state);
            }
            while outgoing.try_recv().is_ok() {}
            sleep(Duration::from_millis(500 << (attempt - 1))).await;
        }
    }

    async fn connection(
        &self,
        login: &Login,
        network: &Network,
        outgoing: &mut mpsc::Receiver<Message>,
        host_token: &str,
        previously_connected: &mut bool,
        (connectivity, epoch): (&watch::Sender<Option<u64>>, u64),
    ) -> Result<()> {
        let form = &login.form;
        let certificate = (!form.ca_file.as_os_str().is_empty()).then_some(form.ca_file.as_path());
        let name = if form.tls_server_name.is_empty() {
            &form.server_ip
        } else {
            &form.tls_server_name
        };
        let mut stream =
            lan_chat_transport::connect(&form.server_ip, form.server_port, name, certificate)
                .await?;
        let message = if self.edition == Edition::Online {
            let register = login.register && !*previously_connected;
            Message {
                kind: if register {
                    "account_register"
                } else {
                    "account_login"
                }
                .into(),
                username: form.username.clone(),
                user_code: if register {
                    form.user_code.clone()
                } else {
                    String::new()
                },
                account_password: login.password.clone(),
                capabilities: vec![
                    HEARTBEAT.into(),
                    ONLINE_GROUP_MANAGEMENT.into(),
                    ONLINE_GROUP_ATTACHMENTS.into(),
                    ONLINE_BOT_SETTINGS.into(),
                    ONLINE_BOT_KEY_SYNC.into(),
                ],
                ..Message::default()
            }
        } else {
            Message {
                kind: "login".into(),
                username: form.username.clone(),
                user_code: form.user_code.clone(),
                host_session_token: host_token.into(),
                capabilities: vec![
                    ROOM_PASSWORD.into(),
                    HEARTBEAT.into(),
                    LAN_HISTORY_PAGES.into(),
                    LAN_ROOM_ROLES.into(),
                ],
                ..Message::default()
            }
        };
        timeout(
            Duration::from_secs(15),
            write_message(&mut stream, &message),
        )
        .await??;
        let mut response = timeout(Duration::from_secs(15), read_message(&mut stream))
            .await
            .context("服务器登录超时")??;
        if response.kind == "login_pending" {
            {
                let mut state = self.state.lock().expect("client state");
                ensure!(state.generation == Some(network.generation), TransportLost);
                state.status = "等待房主批准…";
                self.publish(&state);
            }
            response = timeout(Duration::from_secs(65), read_message(&mut stream))
                .await
                .context("房主审批超时")??;
        }
        ensure!(
            response.kind == "login_ok",
            "服务器拒绝登录: {}",
            response.content
        );
        ensure!(
            self.edition != Edition::Online
                || response
                    .capabilities
                    .iter()
                    .any(|cap| cap == ONLINE_ACCOUNTS),
            "服务器不支持在线账号"
        );
        // 只有服务端回显了心跳能力才启用；旧服务端（含 Go 版）保持原行为。
        let heartbeat = response
            .capabilities
            .iter()
            .any(|capability| capability == HEARTBEAT);
        let reconnecting = *previously_connected;
        *previously_connected = true;
        let save_error;
        {
            let mut state = self.state.lock().expect("client state");
            ensure!(state.generation == Some(network.generation), TransportLost);
            // A reachable server must not be dropped just because the address could not be saved.
            save_error = self.save_connection(form).err();
            state.identity = response.clone();
            state.phase = "connected";
            state.status = if save_error.is_some() { "已连接 · 地址未保存" } else { "已连接" };
            state.reconnect = 0;
            state.saved = form.clone();
            if reconnecting && self.edition == Edition::Online {
                state.histories.retain(|key, _| !key.starts_with("group:"));
                state.unread.retain(|key, _| !key.starts_with("group:"));
                state.group_before.clear();
                state.group_members.clear();
                if state
                    .active
                    .as_ref()
                    .is_some_and(|conversation| conversation.kind == "group")
                {
                    state.query.clear();
                    state.search.clear();
                    state.search_before = None;
                    state.search_request = None;
                }
            }
            state.session.as_mut().expect("active session").capabilities = response.capabilities;
        }
        if let Some(error) = save_error {
            eprintln!("保存连接设置失败: {error}");
            self.ui.emit("chat:presence", json!("本次已连接，但连接地址保存失败，下次启动需要重新填写。"));
        }

        if let Some(crypto) = &network.crypto {
            let package = crypto.lock().expect("MLS state").mls.key_package()?;
            network.send(Message {
                kind: "mls.key_package.publish".into(),
                protocol_version: "mls-v1".into(),
                key_package: STANDARD.encode(package),
                ..Message::default()
            })?;
        }
        if self.edition == Edition::Online {
            network.send(Message {
                kind: "dm_contacts".into(),
                ..Message::default()
            })?;
            let state = self.state.lock().expect("client state");
            let session = state.session.as_ref().expect("online session");
            if session.has(ONLINE_GROUPS) {
                network.send(Message {
                    kind: "group_list".into(),
                    ..Message::default()
                })?;
            }
            if session.has(ONLINE_SOCIAL) {
                network.send(Message {
                    kind: "friend_list".into(),
                    ..Message::default()
                })?;
            }
        } else {
            network.send(Message {
                kind: "rooms_request".into(),
                ..Message::default()
            })?;
            network.send(Message {
                kind: "users_request".into(),
                ..Message::default()
            })?;
        }
        {
            let state = self.state.lock().expect("client state");
            if let Some(conversation) = &state.active {
                if reconnecting
                    && self.edition == Edition::Lan
                    && conversation.kind == "room"
                    && conversation.id != "lobby"
                {
                    network.send(Message {
                        kind: "room_join".into(),
                        room: conversation.id.clone(),
                        ..Message::default()
                    })?;
                }
                if conversation.kind != "group" {
                    network.send(conversation.history(""))?;
                }
            }
            for message in state.outgoing.values() {
                network.send(message.clone())?;
            }
            connectivity.send_replace(Some(epoch));
            self.publish(&state);
        }

        let (mut reader, mut writer) = tokio::io::split(stream);
        let input = async {
            let deadline = Duration::from_secs(HEARTBEAT_TIMEOUT_SECS);
            loop {
                let message = if heartbeat {
                    // TimedOut 不在不可恢复错误之列，maintain_connection 会按瞬时
                    // 断线处理并自动重连。
                    timeout(deadline, read_message(&mut reader))
                        .await
                        .map_err(|_| io::Error::new(io::ErrorKind::TimedOut, "服务器心跳超时"))??
                } else {
                    read_message(&mut reader).await?
                };
                if message.kind == "pong" {
                    continue;
                }
                if !network.complete(&message) {
                    self.received(network, message);
                }
            }
        };
        let output = async {
            let period = Duration::from_secs(HEARTBEAT_INTERVAL_SECS);
            let mut beat = interval_at(Instant::now() + period, period);
            beat.set_missed_tick_behavior(MissedTickBehavior::Delay);
            loop {
                let message = tokio::select! {
                    message = outgoing.recv() => match message {
                        Some(message) => message,
                        None => return Ok(()),
                    },
                    _ = beat.tick(), if heartbeat => Message {
                        kind: "ping".into(),
                        ..Message::default()
                    },
                };
                timeout(
                    Duration::from_secs(15),
                    write_message(&mut writer, &message),
                )
                .await
                .context("向服务器发送超时")??;
            }
        };
        tokio::select! { result = input => result, result = output => result }
    }

    fn session_error(&self, generation: Uuid, error: &anyhow::Error) {
        let mut state = self.state.lock().expect("client state");
        if state.generation != Some(generation) {
            return;
        }
        state.phase = "error";
        state.status = "连接中断";
        state.last_error = Some(crate::error_payload(
            "connection_failed",
            &format!("{error:#}"),
            "controller",
            "",
            true,
        ));
        for id in state.room_commands.drain() {
            self.result(&id, Err(anyhow::anyhow!("连接中断")));
        }
        self.publish(&state);
    }

    async fn start_host(&self, login: &Login) -> Result<(Child, String)> {
        let cert = self.host_dir.join("certs/server-lan.crt");
        let key = self.host_dir.join("certs/server-lan.key");
        let db = self.host_dir.join("chat.db");
        let args = [
            "--cert",
            path_arg(&cert)?,
            "--key",
            path_arg(&key)?,
            "--db",
            path_arg(&db)?,
        ];
        let mut setup = server_process(&self.server_file);
        let output = setup
            .args(args)
            .arg("--initialize-local-host")
            .output()
            .await
            .context("启动本机 Rust 房主服务")?;
        ensure!(
            output.status.success(),
            "初始化本地聊天室失败: {}",
            String::from_utf8_lossy(&output.stderr).trim()
        );

        let mut raw = [0; 32];
        SystemRandom::new()
            .fill(&mut raw)
            .map_err(|_| anyhow::anyhow!("生成房主会话令牌失败"))?;
        let token = URL_SAFE_NO_PAD.encode(raw);
        let mut command = server_process(&self.server_file);
        let mut child = command
            .args(args)
            .args([
                "--lan-discovery",
                "--discovery-name",
                &login.form.username,
                "--admin-code",
                &login.form.user_code,
                "--local-host-credentials-stdin",
            ])
            .stdin(Stdio::piped())
            .stdout(Stdio::null())
            .stderr(Stdio::piped())
            .spawn()?;
        {
            let mut input = child.stdin.take().expect("piped stdin");
            let credentials = LocalHostCredentials {
                password: login.password.clone(),
                session_token: token.clone(),
            };
            input.write_all(&serde_json::to_vec(&credentials)?).await?;
            input.shutdown().await?;
        }
        let mut lines = BufReader::new(child.stderr.take().expect("piped stderr")).lines();
        let ready = timeout(Duration::from_secs(15), async {
            let mut last = String::new();
            while let Some(line) = lines.next_line().await? {
                if line.starts_with("listening on ") {
                    return Ok(());
                }
                last = line;
            }
            anyhow::bail!("本地聊天室未启动: {last}");
        })
        .await
        .context("启动本地聊天室超时")?;
        ready?;
        tokio::spawn(async move { while let Ok(Some(_)) = lines.next_line().await {} });
        Ok((child, token))
    }
}

fn path_arg(path: &Path) -> Result<&str> {
    path.to_str().context("本机路径无法编码")
}

fn server_process(path: &PathBuf) -> Command {
    let mut command = Command::new(path);
    command.kill_on_drop(true);
    #[cfg(windows)]
    command.creation_flags(0x08000000);
    command
}
