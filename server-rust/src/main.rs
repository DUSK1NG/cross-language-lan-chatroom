mod lan;
mod online;
mod push;
mod store;

use std::{
    env,
    fs::File,
    io::{self, BufReader},
    net::SocketAddr,
    path::{Path, PathBuf},
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};

use anyhow::{Context, Result, bail, ensure};
use clap::{Parser, ValueEnum};
use lan_chat_protocol::{
    HEARTBEAT, HEARTBEAT_TIMEOUT_SECS, MAX_FRAME_BYTES, Message, ONLINE_ACCOUNTS, ONLINE_FILES,
    ONLINE_GROUPS, ONLINE_IMAGES, ONLINE_MESSAGE_EDIT, ONLINE_SOCIAL, read_message,
    write_message,
};
use online::Online;
use store::{Rejected, Store};
use tokio::{
    io::AsyncWrite,
    net::{TcpListener, TcpStream},
    sync::{Semaphore, mpsc},
    task::spawn_blocking,
    time::timeout,
};
use tokio_rustls::{TlsAcceptor, rustls::ServerConfig};
use uuid::Uuid;

const LOGIN_TIMEOUT: Duration = Duration::from_secs(10);
const IDLE_TIMEOUT: Duration = Duration::from_secs(5 * 60);
const WRITE_TIMEOUT: Duration = Duration::from_secs(10);
const ONLINE_BACKLOG_LIMIT: usize = 100;

/// 客户端是否在登录消息中声明了心跳能力。
fn wants_heartbeat(request: &Message) -> bool {
    request
        .capabilities
        .iter()
        .any(|capability| capability == HEARTBEAT)
}

/// 已协商心跳的连接用短超时尽快清理断线会话；其余连接保持原有空闲超时。
fn read_timeout(heartbeat: bool) -> Duration {
    if heartbeat {
        Duration::from_secs(HEARTBEAT_TIMEOUT_SECS)
    } else {
        IDLE_TIMEOUT
    }
}

fn pong() -> Message {
    Message {
        kind: "pong".into(),
        ..Message::default()
    }
}

#[derive(Parser)]
#[command(version)]
struct Args {
    #[arg(long, conflicts_with = "mode")]
    online: bool,
    #[arg(long, value_enum)]
    mode: Option<Mode>,
    #[arg(long, alias = "addr", default_value = "0.0.0.0:8888")]
    listen: SocketAddr,
    #[arg(long)]
    db: PathBuf,
    #[arg(long)]
    cert: Option<PathBuf>,
    #[arg(long)]
    key: Option<PathBuf>,
    #[arg(long, default_value_t = 64)]
    max_clients: usize,
    #[arg(long)]
    fcm_service_account: Option<PathBuf>,
    #[arg(long, requires_all = ["fcm_service_account", "fcm_relay_secret_file"])]
    fcm_relay_url: Option<String>,
    #[arg(long, requires_all = ["fcm_service_account", "fcm_relay_url"])]
    fcm_relay_secret_file: Option<PathBuf>,
    #[command(flatten)]
    lan: lan::Options,
}

#[derive(Clone, Copy, PartialEq, Eq, ValueEnum)]
enum Mode {
    Lan,
    Online,
}

fn load_tls(cert: &Path, key: &Path) -> Result<TlsAcceptor> {
    let certificates = rustls_pemfile::certs(&mut BufReader::new(File::open(cert)?))
        .collect::<io::Result<Vec<_>>>()?;
    let key = rustls_pemfile::private_key(&mut BufReader::new(File::open(key)?))?
        .context("PEM 文件不含 TLS 私钥")?;
    let config = ServerConfig::builder()
        .with_no_client_auth()
        .with_single_cert(certificates, key)
        .context("加载 TLS 证书和私钥")?;
    Ok(TlsAcceptor::from(Arc::new(config)))
}

#[tokio::main]
async fn main() -> Result<()> {
    let mut args = Args::parse_from(env::args_os().map(|arg| {
        const LEGACY_FLAGS: &[&str] = &[
            "online",
            "listen",
            "db",
            "cert",
            "key",
            "max-clients",
            "fcm-service-account",
            "fcm-relay-url",
            "fcm-relay-secret-file",
            "addr",
            "auto-cert",
            "initialize-local-host",
            "lan-discovery",
            "discovery-name",
            "admin-code",
            "local-host-credentials-stdin",
        ];
        if let Some(text) = arg.to_str().and_then(|arg| arg.strip_prefix('-')) {
            let flag = text.split('=').next().expect("flag");
            if LEGACY_FLAGS.contains(&flag) {
                return format!("--{text}").into();
            }
        }
        arg
    }));
    ensure!(
        (1..=Semaphore::MAX_PERMITS).contains(&args.max_clients),
        "max-clients 须在 1–{} 之间",
        Semaphore::MAX_PERMITS
    );
    ensure!(!args.db.as_os_str().is_empty(), "db 路径不能为空");
    args.cert = args
        .cert
        .or_else(|| env::var_os("CHAT_TLS_CERT").map(PathBuf::from));
    args.key = args
        .key
        .or_else(|| env::var_os("CHAT_TLS_KEY").map(PathBuf::from));
    if !args.online && args.mode != Some(Mode::Online) {
        ensure!(args.fcm_service_account.is_none(), "FCM 需要在线模式");
        return lan::run(args).await;
    }
    ensure!(!args.lan.enabled(), "在线模式不能启用局域网房主选项");
    let tls = load_tls(
        args.cert
            .as_deref()
            .context("需要 --cert 或 CHAT_TLS_CERT")?,
        args.key.as_deref().context("需要 --key 或 CHAT_TLS_KEY")?,
    )?;
    let listener = TcpListener::bind(args.listen)
        .await
        .context("绑定 TCP 地址")?;
    let store = Store::open(&args.db).context("打开在线 SQLite 数据库")?;
    let push = args
        .fcm_service_account
        .as_deref()
        .map(|account| {
            push::Push::start(
                &store.0,
                &args.db,
                account,
                args.fcm_relay_url
                    .as_deref()
                    .zip(args.fcm_relay_secret_file.as_deref()),
            )
        })
        .transpose()
        .context("配置 FCM 推送")?;
    let online = Arc::new(Mutex::new(Online::new(store, push)));
    let database = Arc::new(args.db);
    let maintenance = online.clone();
    tokio::spawn(async move {
        let mut tick = tokio::time::interval(Duration::from_secs(3600));
        tick.tick().await;
        loop {
            tick.tick().await;
            let state = maintenance.clone();
            let result = spawn_blocking(move || {
                state.lock().expect("online state poisoned").cleanup_files()
            })
            .await;
            match result {
                Ok(Ok(())) => {}
                Ok(Err(error)) => eprintln!("online file cleanup failed: {error:#}"),
                Err(error) => eprintln!("online file cleanup task failed: {error}"),
            }
        }
    });
    let slots = Arc::new(Semaphore::new(args.max_clients));
    eprintln!("listening on {}", listener.local_addr()?);
    loop {
        let (socket, address) = listener.accept().await.context("接受客户端连接")?;
        let Ok(slot) = slots.clone().try_acquire_owned() else {
            eprintln!(
                "connection rejected: maximum of {} clients reached",
                args.max_clients
            );
            continue;
        };
        let tls = tls.clone();
        let online = online.clone();
        let database = database.clone();
        tokio::spawn(async move {
            let _slot = slot;
            if let Err(error) = serve(socket, address, tls, online, database).await {
                eprintln!("connection {address}: {error:#}");
            }
        });
    }
}

async fn serve(
    socket: TcpStream,
    address: SocketAddr,
    tls: TlsAcceptor,
    online: Arc<Mutex<Online>>,
    database: Arc<PathBuf>,
) -> Result<()> {
    socket.set_nodelay(true)?;
    let (mut stream, request) = timeout(LOGIN_TIMEOUT, async {
        let mut stream = tls.accept(socket).await?;
        let request = read_message(&mut stream).await?;
        Ok::<_, io::Error>((stream, request))
    })
    .await
    .context("TLS 握手或认证消息超时")??;

    let heartbeat = wants_heartbeat(&request);
    let login_state = online.clone();
    let identity = spawn_blocking(move || {
        login_state
            .lock()
            .expect("online state poisoned")
            .admit_login(address.ip(), &request.kind)?;
        Store::authenticate(&database, request)
    })
    .await?;
    let identity = match identity {
        Ok(identity) => identity,
        Err(error) => {
            let content = reason(&error, "账号服务暂不可用");
            send(
                &mut stream,
                &Message {
                    kind: "login_error".into(),
                    content: content.into(),
                    ..Message::default()
                },
            )
            .await?;
            return Ok(());
        }
    };
    let mut capabilities = [
        ONLINE_ACCOUNTS,
        ONLINE_IMAGES,
        ONLINE_MESSAGE_EDIT,
        ONLINE_FILES,
        ONLINE_SOCIAL,
        ONLINE_GROUPS,
    ]
    .map(str::to_owned)
    .to_vec();
    if heartbeat {
        capabilities.push(HEARTBEAT.into());
    }
    send(
        &mut stream,
        &Message {
            kind: "login_ok".into(),
            username: identity.username.clone(),
            user_code: identity.display_code.clone(),
            content: "Login successful".into(),
            capabilities,
            ..Message::default()
        },
    )
    .await?;

    let id = Uuid::new_v4();
    let (mut reader, mut writer) = tokio::io::split(stream);
    let (tx, mut outgoing) = mpsc::channel(256);
    // 弱引用：会话被服务端移除（如同账号在别处登录）时发送端随之释放，
    // transmit 循环才能照常结束并关闭连接。
    let heartbeat_reply = tx.downgrade();
    let register_state = online.clone();
    let register_result = spawn_blocking(move || {
        register_state
            .lock()
            .expect("online state poisoned")
            .register(id, identity, tx)
    })
    .await?;
    let result = match register_result {
        Err(error) => Err(error),
        Ok(()) => {
            let receive = async {
                let mut normal = Budget::new(16.0, 8.0);
                let mut receipts = Budget::new(ONLINE_BACKLOG_LIMIT as f64 + 16.0, 8.0);
                let mut transfers = Budget::new(4.0 * 1024.0 * 1024.0, 32.0 * 1024.0 * 1024.0);
                loop {
                    let request = match timeout(read_timeout(heartbeat), read_message(&mut reader))
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
                        // 队列满时丢弃 pong 即可：说明连接仍在传输数据，客户端
                        // 会因收到其他帧而刷新心跳期限。
                        if let Some(reply) = heartbeat_reply.upgrade() {
                            let _ = reply.try_send(pong());
                        }
                        continue;
                    }
                    let allowed = match request.kind.as_str() {
                        "dm_image_chunk" | "dm_image_get" | "dm_file_chunk" | "dm_file_get" => {
                            let delay = transfers.delay(MAX_FRAME_BYTES as f64);
                            if !delay.is_zero() {
                                tokio::time::sleep(delay).await;
                            }
                            true
                        }
                        "dm_received" | "dm_ack_batch" => receipts.allow(),
                        _ => normal.allow(),
                    };
                    let state = online.clone();
                    spawn_blocking(move || {
                        let mut state = state.lock().expect("online state poisoned");
                        if allowed {
                            state.command(id, request);
                        } else {
                            state.reply_error(
                                id,
                                &request,
                                "Rate limit exceeded; please slow down",
                            );
                        }
                    })
                    .await?;
                }
            };
            let transmit = async {
                while let Some(message) = outgoing.recv().await {
                    send(&mut writer, &message).await?;
                }
                bail!("客户端发送队列已关闭");
            };
            tokio::select! {
                result = receive => result,
                result = transmit => result,
            }
        }
    };
    spawn_blocking(move || online.lock().expect("online state poisoned").remove(id)).await?;
    result
}

async fn send(writer: &mut (impl AsyncWrite + Unpin), message: &Message) -> Result<()> {
    timeout(WRITE_TIMEOUT, write_message(writer, message))
        .await
        .context("客户端写入超时")??;
    Ok(())
}

fn reason<'a>(error: &'a anyhow::Error, fallback: &'a str) -> &'a str {
    if let Some(error) = error.downcast_ref::<Rejected>() {
        return error.0;
    }
    eprintln!("{error:#}");
    fallback
}

struct Budget {
    last: Instant,
    tokens: f64,
    capacity: f64,
    rate: f64,
}

impl Budget {
    fn new(capacity: f64, rate: f64) -> Self {
        Self {
            last: Instant::now(),
            tokens: capacity,
            capacity,
            rate,
        }
    }

    fn replenish(&mut self) {
        let now = Instant::now();
        self.tokens = (self.tokens + now.duration_since(self.last).as_secs_f64() * self.rate)
            .min(self.capacity);
        self.last = now;
    }

    fn allow(&mut self) -> bool {
        self.replenish();
        if self.tokens < 1.0 {
            return false;
        }
        self.tokens -= 1.0;
        true
    }

    fn delay(&mut self, tokens: f64) -> Duration {
        self.replenish();
        self.tokens -= tokens;
        Duration::from_secs_f64((-self.tokens / self.rate).max(0.0))
    }
}
