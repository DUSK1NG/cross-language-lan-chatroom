mod approval;
mod attachments;
mod connection;
mod history;
mod host;
mod messages;
mod mls;
mod presence;
mod rooms;
mod store;

use std::{
    collections::{HashMap, HashSet},
    io::{self, Read},
    sync::{Arc, Mutex},
    time::Duration,
};

use anyhow::{Context, Result, ensure};
use base64::{Engine, engine::general_purpose::URL_SAFE_NO_PAD};
use clap::Args as ClapArgs;
use lan_chat_protocol::{LocalHostCredentials, Message};
use tokio::{
    net::TcpListener,
    sync::{Semaphore, mpsc, oneshot},
};
use uuid::Uuid;

use crate::{
    Args, load_tls,
    store::{Identity, Rejected, Store, user_code},
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

#[derive(Clone)]
struct Room {
    owner: String,
    private: bool,
    allowed: HashMap<String, bool>,
    admins: HashSet<String>,
    password_hash: Option<String>,
    generation: Uuid,
}

struct Peer {
    identity: Identity,
    room: String,
    admin: bool,
    room_roles: bool,
    muted: bool,
    attachment_session: Uuid,
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
    attachments: mpsc::Sender<attachments::Work>,
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

    let mut rooms = store.lan_rooms()?;
    rooms.insert(
        LOBBY.into(),
        Room {
            owner: String::new(),
            private: false,
            allowed: HashMap::new(),
            admins: HashSet::new(),
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
    let (attachments, attachment_jobs) = mpsc::channel(64);
    let server = Arc::new(Server {
        admin_code,
        host_token,
        hub: Mutex::new(Hub {
            store,
            rooms,
            last_message,
            history,
            attachments,
            clients: HashMap::new(),
            codes: HashMap::new(),
            pending: HashMap::new(),
        }),
    });
    attachments::start(&server, &args.db, attachment_jobs)?;
    let cleanup = server.clone();
    tokio::spawn(async move {
        let mut tick = tokio::time::interval(Duration::from_secs(3600));
        tick.tick().await;
        loop {
            tick.tick().await;
            let queue = cleanup
                .hub
                .lock()
                .expect("LAN hub poisoned")
                .attachments
                .clone();
            if queue.send(attachments::Work::Cleanup).await.is_err() {
                eprintln!("局域网附件清理失败: 附件服务已停止");
                return;
            }
        }
    });
    let reader = Store::lan_reader(&args.db)?;
    let worker = server.clone();
    tokio::spawn(async move {
        if let Err(error) = history::worker(worker, reader, jobs).await {
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
            if let Err(error) = connection::serve(socket, address, tls, server).await {
                eprintln!("connection {address}: {error:#}");
            }
        });
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
