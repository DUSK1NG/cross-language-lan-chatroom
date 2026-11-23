mod attachments;
mod host;
mod messages;
mod mls;
mod rooms;
mod store;

use std::{
    collections::HashMap,
    future::pending,
    io::{self, Read},
    net::SocketAddr,
    path::{Path, PathBuf},
    sync::{Arc, Mutex},
    time::Duration,
};

use anyhow::{Context, Result, ensure};
use base64::{Engine, engine::general_purpose::URL_SAFE_NO_PAD};
use clap::Args as ClapArgs;
use lan_chat_protocol::{
    HEARTBEAT, LocalHostCredentials, MAX_COMMAND_ID_BYTES, MAX_FRAME_BYTES, Message, OnlineUser,
    ROOM_PASSWORD, read_lan_command, read_message,
};
use subtle::ConstantTimeEq;
use tokio::{
    io::AsyncReadExt,
    net::{TcpListener, TcpStream},
    sync::{Semaphore, mpsc, oneshot},
    task::spawn_blocking,
    time::timeout,
};
use tokio_rustls::TlsAcceptor;
use uuid::Uuid;

use crate::{
    Args, Budget, LOGIN_TIMEOUT, load_tls, pong, read_timeout, reason, send, wants_heartbeat,
    store::{Identity, Rejected, Store, now, user_code},
};

const LOBBY: &str = "lobby";
const APPROVAL_TIMEOUT: Duration = Duration::from_secs(60);

#[derive(ClapArgs)]
pub(crate) struct Options {
    #[arg(long)]
    auto_cert: bool,
    #[arg(long)]
    initialize_local_host: bool,
    #[arg(long)]
    lan_discovery: bool,
    #[arg(long, default_value = "")]
    discovery_name: String,
    #[arg(long, default_value = "")]
    admin_code: String,
    #[arg(long)]
    local_host_credentials_stdin: bool,
}

impl Options {
    pub(crate) fn enabled(&self) -> bool {
        self.auto_cert
            || self.initialize_local_host
            || self.lan_discovery
            || !self.discovery_name.is_empty()
            || !self.admin_code.is_empty()
            || self.local_host_credentials_stdin
    }
}

struct Room {
    owner: String,
    private: bool,
    allowed: HashMap<String, bool>,
    password_hash: Option<String>,
    generation: Uuid,
}

struct Peer {
    identity: Identity,
    room: String,
    admin: bool,
    muted: bool,
    send: mpsc::Sender<Message>,
    close: oneshot::Sender<()>,
}

struct Approval {
    display: Message,
    decision: oneshot::Sender<bool>,
}

struct HistoryJob {
    peer: Uuid,
    conversation: String,
    room_generation: Option<Uuid>,
    request: Message,
}

struct Hub {
    store: Store,
    attachment_root: PathBuf,
    clients: HashMap<Uuid, Peer>,
    codes: HashMap<String, Uuid>,
    rooms: HashMap<String, Room>,
    pending: HashMap<String, Approval>,
    last_message: u64,
    history: mpsc::Sender<HistoryJob>,
}

struct Server {
    hub: Mutex<Hub>,
    admin_code: String,
    host_token: Option<String>,
}

pub(crate) async fn run(args: Args) -> Result<()> {
    let cert = args.cert.context("需要 --cert 或 CHAT_TLS_CERT")?;
    let key = args.key.context("需要 --key 或 CHAT_TLS_KEY")?;
    if args.lan.auto_cert || args.lan.initialize_local_host {
        host::ensure_certificate(&cert, &key)?;
    }
    let tls = load_tls(&cert, &key)?;
    if let Some(parent) = args
        .db
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
    {
        std::fs::create_dir_all(parent)?;
    }
    let store = Store::open_lan(&args.db).context("打开局域网 SQLite 数据库")?;
    if args.lan.initialize_local_host {
        eprintln!("local host files initialized");
        return Ok(());
    }

    let admin_code = if args.lan.admin_code.is_empty() {
        String::new()
    } else {
        user_code(&args.lan.admin_code).context("房主用户代码无效")?
    };
    let host_token = if args.lan.local_host_credentials_stdin {
        ensure!(!admin_code.is_empty(), "读取房主凭据需要 --admin-code");
        let credentials: LocalHostCredentials =
            serde_json::from_reader(io::stdin().take(1024)).context("无法读取房主凭据")?;
        let token = URL_SAFE_NO_PAD
            .decode(&credentials.session_token)
            .context("房主会话凭据无效")?;
        ensure!(token.len() == 32, "房主会话凭据无效");
        store.host_password(&credentials.password)?;
        Some(credentials.session_token)
    } else {
        None
    };

    let mut rooms = store.protected_rooms()?;
    rooms.insert(
        LOBBY.into(),
        Room {
            owner: String::new(),
            private: false,
            allowed: HashMap::new(),
            password_hash: None,
            generation: Uuid::new_v4(),
        },
    );
    let last_message = store.0.query_row(
        "SELECT COALESCE(MAX(id), 0) FROM chat_messages",
        [],
        |row| row.get(0),
    )?;
    let (history, jobs) = mpsc::channel(64);
    let server = Arc::new(Server {
        admin_code,
        host_token,
        hub: Mutex::new(Hub {
            store,
            rooms,
            last_message,
            history,
            attachment_root: args
                .db
                .parent()
                .unwrap_or(Path::new("."))
                .join("attachments"),
            clients: HashMap::new(),
            codes: HashMap::new(),
            pending: HashMap::new(),
        }),
    });
    let cleanup = server.clone();
    tokio::spawn(async move {
        let mut tick = tokio::time::interval(Duration::from_secs(3600));
        tick.tick().await;
        loop {
            tick.tick().await;
            let state = cleanup.clone();
            let result = spawn_blocking(move || {
                state
                    .hub
                    .lock()
                    .expect("LAN hub poisoned")
                    .cleanup_attachments()
            })
            .await;
            match result {
                Ok(Ok(())) => (),
                Ok(Err(error)) => eprintln!("局域网附件清理失败: {error:#}"),
                Err(error) => eprintln!("局域网附件清理任务失败: {error}"),
            }
        }
    });
    let reader = Store::lan_reader(&args.db)?;
    let worker = server.clone();
    tokio::spawn(async move {
        if let Err(error) = history_worker(worker, reader, jobs).await {
            eprintln!("局域网历史服务停止: {error:#}");
        }
    });

    let listener = TcpListener::bind(args.listen)
        .await
        .context("绑定 TCP 地址")?;
    let local = listener.local_addr()?;
    if args.lan.lan_discovery {
        tokio::spawn(async move {
            if let Err(error) = host::discover(&cert, local.port(), &args.lan.discovery_name).await
            {
                eprintln!("局域网发现停止: {error:#}");
            }
        });
    }
    let slots = Arc::new(Semaphore::new(args.max_clients));
    eprintln!("listening on {local}");
    loop {
        let (socket, address) = listener.accept().await?;
        let Ok(slot) = slots.clone().try_acquire_owned() else {
            eprintln!(
                "connection rejected: maximum of {} clients reached",
                args.max_clients
            );
            continue;
        };
        let tls = tls.clone();
        let server = server.clone();
        tokio::spawn(async move {
            let _slot = slot;
            if let Err(error) = serve(socket, address, tls, server).await {
                eprintln!("connection {address}: {error:#}");
            }
        });
    }
}

async fn history_worker(
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
            let result = reader.history(kind, &job.conversation, &job.request);
            let mut hub = server.hub.lock().expect("LAN hub poisoned");
            hub.history_result(job, result);
            reader
        })
        .await?;
    }
    Ok(())
}

async fn serve(
    socket: TcpStream,
    address: SocketAddr,
    tls: TlsAcceptor,
    server: Arc<Server>,
) -> Result<()> {
    socket.set_nodelay(true)?;
    let (mut stream, request) = timeout(LOGIN_TIMEOUT, async {
        let mut stream = tls.accept(socket).await?;
        let request = read_message(&mut stream).await?;
        Ok::<_, io::Error>((stream, request))
    })
    .await
    .context("TLS 握手或认证消息超时")??;

    let init = server.clone();
    let auth = spawn_blocking(move || {
        ensure!(
            request.command_id.len() <= MAX_COMMAND_ID_BYTES,
            Rejected("Command ID is too large")
        );
        match request.kind.as_str() {
            "login" => (),
            "register" => {
                return Err(Rejected(
                    "Password registration has been removed; use username and user code",
                )
                .into());
            }
            "login_auth" => {
                return Err(Rejected(
                    "Password login has been removed; use username and user code",
                )
                .into());
            }
            _ => return Err(Rejected("Expected login message").into()),
        }
        let admin =
            !init.admin_code.is_empty() && request.user_code.eq_ignore_ascii_case(&init.admin_code);
        let bootstrap = admin && address.ip().is_loopback();
        if admin && let Some(token) = &init.host_token {
            ensure!(
                bootstrap
                    && bool::from(
                        token
                            .as_bytes()
                            .ct_eq(request.host_session_token.as_bytes())
                    ),
                Rejected("该用户代码用于本机房主，请使用其他用户代码")
            );
        }
        let identity = init
            .hub
            .lock()
            .expect("LAN hub poisoned")
            .store
            .lan_identity(&request.username, &request.user_code)?;
        let room_password = request
            .capabilities
            .iter()
            .any(|capability| capability == ROOM_PASSWORD);
        let heartbeat = wants_heartbeat(&request);
        Ok((identity, admin, bootstrap, room_password, heartbeat))
    })
    .await?;
    let (identity, admin, bootstrap, room_password, heartbeat) = match auth {
        Ok(auth) => auth,
        Err(error) => {
            return send(
                &mut stream,
                &Message {
                    kind: "login_error".into(),
                    content: reason(&error, "Connection identity failed").into(),
                    ..Message::default()
                },
            )
            .await;
        }
    };

    if !bootstrap {
        let (id, decision) = server
            .hub
            .lock()
            .expect("LAN hub poisoned")
            .request_approval(&identity);
        let mut incoming = [0];
        let outcome = async {
            send(
                &mut stream,
                &Message {
                    kind: "login_pending".into(),
                    message_id: id.clone(),
                    content: "Waiting for the room owner to approve this connection".into(),
                    ..Message::default()
                },
            )
            .await?;
            tokio::select! {
                result = timeout(APPROVAL_TIMEOUT, decision) => Ok(match result {
                    Ok(Ok(true)) => None,
                    Ok(Ok(false)) => Some("The room owner declined this connection"),
                    _ => Some("The room owner did not approve the connection in time"),
                }),
                result = stream.read(&mut incoming) => {
                    result?;
                    Err(anyhow::anyhow!("连接在审批期间关闭或提前发送了请求"))
                },
            }
        }
        .await;
        server
            .hub
            .lock()
            .expect("LAN hub poisoned")
            .expire_approval(&id);
        if let Some(content) = outcome? {
            return send(
                &mut stream,
                &Message {
                    kind: "login_error".into(),
                    content: content.into(),
                    ..Message::default()
                },
            )
            .await;
        }
    }

    let approve = server.clone();
    let identity = match spawn_blocking(move || {
        approve
            .hub
            .lock()
            .expect("LAN hub poisoned")
            .store
            .approve_identity(&identity)
    })
    .await?
    {
        Ok(identity) => identity,
        Err(error) => {
            return send(
                &mut stream,
                &Message {
                    kind: "login_error".into(),
                    content: reason(&error, "Approved connection could not be completed").into(),
                    ..Message::default()
                },
            )
            .await;
        }
    };
    let id = Uuid::new_v4();
    let (tx, mut outgoing) = mpsc::channel(256);
    let (close, closed) = oneshot::channel();
    let (mut reader, mut writer) = tokio::io::split(stream);
    // 弱引用，避免 Hub 移除该会话后发送端仍被心跳回复持有。
    let heartbeat_reply = tx.downgrade();
    let register = server.clone();
    let initial = spawn_blocking(move || {
        let mut hub = register.hub.lock().expect("LAN hub poisoned");
        let welcomes = hub.store.pending_lan_welcomes(&identity.code)?;
        let backlog = hub.store.take_lan_private(&identity)?;
        let mut capabilities = Vec::new();
        if room_password {
            capabilities.push(ROOM_PASSWORD.into());
        }
        if heartbeat {
            capabilities.push(HEARTBEAT.into());
        }
        let mut initial = vec![Message {
            kind: "login_ok".into(),
            username: identity.username.clone(),
            user_code: identity.display_code.clone(),
            is_admin: admin,
            content: "Login successful".into(),
            capabilities,
            ..Message::default()
        }];
        initial.extend(backlog);
        initial.extend(welcomes);
        hub.register(
            id,
            Peer {
                identity,
                admin,
                send: tx,
                close,
                muted: false,
                room: LOBBY.into(),
            },
        );
        Ok::<_, anyhow::Error>(initial)
    })
    .await??;

    let receive = async {
        let mut normal = Budget::new(16.0, 8.0);
        let mut transfer = Budget::new(4.0 * 1024.0 * 1024.0, 32.0 * 1024.0 * 1024.0);
        loop {
            let request = match timeout(read_timeout(heartbeat), read_lan_command(&mut reader))
                .await
                .context("客户端读取超时")?
            {
                Ok(message) => message,
                Err(error) if error.kind() == io::ErrorKind::UnexpectedEof => return Ok(()),
                Err(error) => return Err(error.into()),
            };
            if request.kind == "quit" {
                return Ok(());
            }
            if heartbeat && request.kind == "ping" {
                if let Some(reply) = heartbeat_reply.upgrade() {
                    let _ = reply.try_send(pong());
                }
                continue;
            }
            let allowed = match (request.kind.as_str(), request.crypto.is_some()) {
                ("attachment.chunk" | "attachment.download", _) => {
                    let delay = transfer.delay(MAX_FRAME_BYTES as f64);
                    if !delay.is_zero() {
                        tokio::time::sleep(delay).await;
                    }
                    true
                }
                (
                    "attachment.init"
                    | "attachment.resume"
                    | "attachment.commit"
                    | "mls.key_package.fetch"
                    | "mls.group.proposal"
                    | "mls.group.commit"
                    | "mls.group.welcome"
                    | "mls.group.welcome.accept",
                    _,
                )
                | ("chat", true) => {
                    // A single upload sends several control frames in succession.
                    // Pace them at the normal rate so a busy connection can finish the upload.
                    let delay = normal.delay(1.0);
                    if !delay.is_zero() {
                        tokio::time::sleep(delay).await;
                    }
                    true
                }
                _ => normal.allow(),
            };
            let state = server.clone();
            spawn_blocking(move || {
                let mut hub = state.hub.lock().expect("LAN hub poisoned");
                let result = if allowed {
                    hub.command(id, &request)
                } else {
                    Err(Rejected("Rate limit exceeded; please slow down").into())
                };
                if let Err(error) = result {
                    hub.error(id, &request, reason(&error, "局域网命令处理失败"));
                }
            })
            .await?;
        }
    };
    let transmit = async {
        for message in initial {
            send(&mut writer, &message).await?;
        }
        while let Some(message) = outgoing.recv().await {
            send(&mut writer, &message).await?;
        }
        Ok(())
    };
    let result = tokio::select! {
        result = receive => result,
        result = transmit => result,
        () = async { if closed.await.is_err() { pending().await } } => Ok(()),
    };
    server
        .hub
        .lock()
        .expect("LAN hub poisoned")
        .remove(id, true);
    result
}

impl Hub {
    fn register(&mut self, id: Uuid, peer: Peer) {
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

    fn remove(&mut self, id: Uuid, immediate: bool) {
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

    fn deliver(&mut self, id: Uuid, message: Message) -> bool {
        let Some(peer) = self.clients.get(&id) else {
            return false;
        };
        if peer.send.try_send(message).is_err() {
            self.remove(id, true);
            return false;
        }
        true
    }

    fn error(&mut self, id: Uuid, request: &Message, content: &str) {
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

    fn request_approval(&mut self, identity: &Identity) -> (String, oneshot::Receiver<bool>) {
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

    fn expire_approval(&mut self, id: &str) {
        if let Some(approval) = self.pending.remove(id) {
            self.approval_result(approval.display, "expired", "");
        }
    }

    fn approval_result(&mut self, request: Message, content: &str, command_id: &str) {
        self.admins(Message {
            kind: "connection_approval_result".into(),
            content: content.into(),
            command_id: command_id.into(),
            ..request
        });
    }

    fn admins(&mut self, message: Message) {
        let ids = self
            .clients
            .iter()
            .filter_map(|(id, peer)| peer.admin.then_some(*id))
            .collect::<Vec<_>>();
        for id in ids {
            self.deliver(id, message.clone());
        }
    }

    fn users(&mut self, id: Uuid) {
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

    fn system(&mut self, id: Uuid, room: &str, content: &str) {
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

    fn room_system(&mut self, room: &str, content: &str) {
        for id in self.room_clients(room) {
            self.system(id, room, content);
        }
    }

    fn room_clients(&self, room: &str) -> Vec<Uuid> {
        self.clients
            .iter()
            .filter_map(|(id, peer)| (peer.room == room).then_some(*id))
            .collect()
    }
}

fn validate_room_name(name: &str) -> Result<()> {
    ensure!(
        (1..=32).contains(&name.len())
            && name
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || byte == b'_'),
        Rejected("Invalid room name")
    );
    Ok(())
}
