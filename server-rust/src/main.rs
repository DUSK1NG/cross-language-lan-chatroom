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
    ONLINE_GROUPS, ONLINE_IMAGES, ONLINE_MESSAGE_EDIT, ONLINE_SOCIAL, read_message, write_message,
};
use lan_chat_protocol::{
    ONLINE_BOT_KEY_SYNC, ONLINE_BOT_SETTINGS, ONLINE_GROUP_ATTACHMENTS, ONLINE_GROUP_MANAGEMENT,
    ONLINE_HISTORY_PAGES,
};
use online::{
    AuthConfig, AuthControl, Online, OnlineLimits, auth::PreAuthPermit,
    is_invalid_credentials_error,
};
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
    migrate_storage: bool,
    #[arg(long, conflicts_with = "migrate_storage")]
    vacuum: bool,
    #[arg(long, requires = "migrate_storage")]
    dry_run: bool,
    #[arg(long, requires = "migrate_storage", conflicts_with = "dry_run")]
    prune_legacy: bool,
    #[arg(long)]
    cert: Option<PathBuf>,
    #[arg(long)]
    key: Option<PathBuf>,
    #[arg(long)]
    bot_key_file: Option<PathBuf>,
    #[arg(long, default_value_t = 64)]
    max_clients: usize,
    #[arg(long, default_value_t = 14)]
    undelivered_ttl_days: u32,
    #[arg(long, default_value_t = 200)]
    undelivered_per_target: usize,
    #[arg(long, default_value_t = 64 * 1024 * 1024)]
    undelivered_bytes_per_target: usize,
    #[arg(long, default_value_t = 30)]
    delivered_ttl_days: u32,
    #[arg(long, default_value_t = 2 * 1024 * 1024 * 1024)]
    image_budget_bytes: usize,
    #[arg(long, default_value_t = 128)]
    max_preauth: usize,
    #[arg(long, default_value_t = 8)]
    max_preauth_per_ip: usize,
    #[arg(long, default_value_t = 8)]
    max_sessions_per_account: usize,
    #[arg(long, default_value_t = 5)]
    tls_handshake_timeout_secs: u64,
    #[arg(long, default_value_t = 8)]
    login_frame_timeout_secs: u64,
    #[arg(long, default_value_t = 4)]
    max_auth_concurrency: usize,
    #[arg(long, default_value_t = 3)]
    auth_queue_wait_secs: u64,
    #[arg(long, default_value_t = 64)]
    ipv6_prefix: u8,
    #[arg(long, default_value_t = 20)]
    login_source_limit: u32,
    #[arg(long, default_value_t = 60)]
    login_source_window_secs: u64,
    #[arg(long, default_value_t = 5)]
    register_source_limit: u32,
    #[arg(long, default_value_t = 300)]
    register_source_window_secs: u64,
    #[arg(long, default_value_t = 5)]
    account_fail_threshold: u32,
    #[arg(long, default_value_t = 30)]
    account_cooldown_secs: u64,
    #[arg(long, default_value_t = 900)]
    account_cooldown_max_secs: u64,
    #[arg(long, default_value_t = 8)]
    preauth_fail_limit: u32,
    #[arg(long, default_value_t = 60)]
    preauth_fail_window_secs: u64,
    #[arg(long, default_value_t = 120)]
    preauth_ban_secs: u64,
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
    ensure!(
        args.undelivered_ttl_days <= 36500,
        "undelivered-ttl-days 须在 0–36500 之间"
    );
    ensure!(
        (1..=Semaphore::MAX_PERMITS).contains(&args.max_preauth),
        "max-preauth 须在 1–{} 之间",
        Semaphore::MAX_PERMITS
    );
    ensure!(args.max_preauth_per_ip >= 1, "max-preauth-per-ip 须 >= 1");
    ensure!(
        args.max_sessions_per_account >= 1,
        "max-sessions-per-account 须 >= 1"
    );
    ensure!(
        args.tls_handshake_timeout_secs >= 1 && args.login_frame_timeout_secs >= 1,
        "TLS 与登录帧超时须 >= 1 秒"
    );
    ensure!(
        (1..=Semaphore::MAX_PERMITS).contains(&args.max_auth_concurrency)
            && args.auth_queue_wait_secs >= 1,
        "认证并发名额与等待超时配置无效"
    );
    ensure!(
        (1..=128).contains(&args.ipv6_prefix),
        "ipv6-prefix 须在 1–128 之间"
    );
    ensure!(
        args.login_source_limit >= 1
            && args.login_source_window_secs >= 1
            && args.register_source_limit >= 1
            && args.register_source_window_secs >= 1,
        "来源限流次数与窗口配置须 >= 1"
    );
    ensure!(
        args.account_fail_threshold >= 1
            && args.account_cooldown_secs >= 1
            && args.account_cooldown_max_secs >= args.account_cooldown_secs,
        "账号退避冷却配置无效"
    );
    ensure!(!args.db.as_os_str().is_empty(), "db 路径不能为空");
    if args.migrate_storage {
        ensure!(
            args.online || args.mode == Some(Mode::Online),
            "迁移附件需要 --mode online"
        );
        return online::migrate_storage(&args.db, args.dry_run, args.prune_legacy);
    }
    if args.vacuum {
        ensure!(
            args.online || args.mode == Some(Mode::Online),
            "压缩数据库需要 --mode online"
        );
        let (before_pages, before_free, after_pages, after_free) =
            store::vacuum(&args.db).context("压缩数据库")?;
        println!(
            "VACUUM 完成：{before_pages} 页（空闲 {before_free}）-> {after_pages} 页（空闲 {after_free}）"
        );
        return Ok(());
    }
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
    let bot_key = args
        .bot_key_file
        .or_else(|| env::var_os("CHAT_BOT_KEY_FILE").map(PathBuf::from))
        .as_deref()
        .map(online::BotKey::load)
        .transpose()?;
    let limits = OnlineLimits {
        undelivered_per_target: args.undelivered_per_target,
        undelivered_bytes_per_target: args.undelivered_bytes_per_target,
        undelivered_ttl_days: args.undelivered_ttl_days,
        delivered_ttl_days: args.delivered_ttl_days,
        image_budget_bytes: args.image_budget_bytes,
        max_sessions_per_account: args.max_sessions_per_account,
    };
    let auth = Arc::new(AuthControl::new(AuthConfig {
        max_preauth: args.max_preauth,
        max_preauth_per_source: args.max_preauth_per_ip,
        tls_handshake_timeout: Duration::from_secs(args.tls_handshake_timeout_secs),
        login_frame_timeout: Duration::from_secs(args.login_frame_timeout_secs),
        max_auth_concurrency: args.max_auth_concurrency,
        auth_queue_wait: Duration::from_secs(args.auth_queue_wait_secs),
        ipv6_prefix: args.ipv6_prefix,
        login_source_limit: args.login_source_limit,
        login_source_window: Duration::from_secs(args.login_source_window_secs),
        register_source_limit: args.register_source_limit,
        register_source_window: Duration::from_secs(args.register_source_window_secs),
        account_fail_threshold: args.account_fail_threshold,
        account_cooldown_base: Duration::from_secs(args.account_cooldown_secs),
        account_cooldown_max: Duration::from_secs(args.account_cooldown_max_secs),
        preauth_fail_limit: args.preauth_fail_limit,
        preauth_fail_window: Duration::from_secs(args.preauth_fail_window_secs),
        preauth_ban: Duration::from_secs(args.preauth_ban_secs),
    }));
    let online = Arc::new(Mutex::new(Online::with_limits(
        store, push, bot_key, limits,
    )));
    let database = Arc::new(args.db);
    let maintenance = online.clone();
    let maintenance_auth = auth.clone();
    let maintenance_db = database.clone();
    tokio::spawn(async move {
        let mut tick = tokio::time::interval(Duration::from_secs(3600));
        tick.tick().await;
        loop {
            tick.tick().await;
            let state = maintenance.clone();
            let auth_state = maintenance_auth.clone();
            let db_path = maintenance_db.clone();
            let result =
                spawn_blocking(move || online::cleanup_maintenance(&db_path, &state)).await;
            let metrics = auth_state.metrics();
            if metrics.rejected_source_login_limit > 0
                || metrics.rejected_source_login_capacity > 0
                || metrics.rejected_source_register_limit > 0
                || metrics.rejected_source_register_capacity > 0
                || metrics.rejected_account_cooldown > 0
                || metrics.rejected_account_in_flight > 0
                || metrics.rejected_account_capacity > 0
            {
                eprintln!(
                    "online auth metrics: tables=(login:{},reg:{},acct:{}) rejects=(src_login:{}/{},src_reg:{}/{},acct_cd:{},acct_inflight:{},acct_cap:{})",
                    metrics.source_login_entries,
                    metrics.source_register_entries,
                    metrics.account_failure_entries,
                    metrics.rejected_source_login_limit,
                    metrics.rejected_source_login_capacity,
                    metrics.rejected_source_register_limit,
                    metrics.rejected_source_register_capacity,
                    metrics.rejected_account_cooldown,
                    metrics.rejected_account_in_flight,
                    metrics.rejected_account_capacity
                );
            }
            match result {
                Ok(Ok(stats)) => {
                    if stats.deleted_messages > 0
                        || stats.deleted_file_rows > 0
                        || stats.deleted_orphan_files > 0
                        || stats.skipped_invalid_timestamps > 0
                    {
                        eprintln!(
                            "online maintenance completed: deleted_messages={}, deleted_file_rows={}, deleted_chunks={}, deleted_orphan_files={}, skipped_invalid_timestamps={}",
                            stats.deleted_messages,
                            stats.deleted_file_rows,
                            stats.deleted_chunks,
                            stats.deleted_orphan_files,
                            stats.skipped_invalid_timestamps
                        );
                    }
                }
                Ok(Err(error)) => eprintln!("online file cleanup failed: {error:#}"),
                Err(error) => eprintln!("online file cleanup task failed: {error}"),
            }
        }
    });
    let slots = Arc::new(Semaphore::new(args.max_clients));
    eprintln!("listening on {}", listener.local_addr()?);
    loop {
        let Some((socket, address)) = accept_client(&listener).await? else {
            continue;
        };
        let Some(preauth) = auth.try_acquire_preauth(address.ip()) else {
            eprintln!("preauth rejected: global or per-source preauth limit reached");
            drop(socket);
            continue;
        };
        let tls = tls.clone();
        let online = online.clone();
        let auth = auth.clone();
        let slots = slots.clone();
        let database = database.clone();
        tokio::spawn(async move {
            if let Err(error) =
                serve(socket, address, preauth, tls, online, auth, slots, database).await
            {
                eprintln!("connection {address}: {error:#}");
            }
        });
    }
}

fn is_transient_accept_error(error: &io::Error) -> bool {
    if matches!(
        error.kind(),
        io::ErrorKind::ConnectionRefused
            | io::ErrorKind::ConnectionAborted
            | io::ErrorKind::ConnectionReset
            | io::ErrorKind::Interrupted
            | io::ErrorKind::WouldBlock
            | io::ErrorKind::TimedOut
    ) {
        return true;
    }
    #[cfg(unix)]
    if matches!(
        error.raw_os_error(),
        Some(4 | 23 | 24 | 53 | 54 | 55 | 71 | 103 | 104 | 105)
    ) {
        return true;
    }
    #[cfg(windows)]
    if matches!(error.raw_os_error(), Some(10024 | 10052 | 10053 | 10054)) {
        return true;
    }
    false
}

pub(crate) async fn accept_client(
    listener: &TcpListener,
) -> Result<Option<(TcpStream, SocketAddr)>> {
    match listener.accept().await {
        Ok(accepted) => Ok(Some(accepted)),
        Err(error) if is_transient_accept_error(&error) => {
            eprintln!("accept error (retrying): {error}");
            tokio::time::sleep(Duration::from_millis(50)).await;
            Ok(None)
        }
        Err(error) => Err(error).context("接受客户端连接"),
    }
}

struct PeerRemoveGuard {
    id: Uuid,
    online: Option<Arc<Mutex<Online>>>,
}

impl PeerRemoveGuard {
    fn disarm(&mut self) {
        self.online = None;
    }
}

impl Drop for PeerRemoveGuard {
    fn drop(&mut self) {
        if let Some(online) = self.online.take()
            && let Ok(mut guard) = online.lock()
        {
            guard.remove(self.id);
        }
    }
}

#[allow(clippy::too_many_arguments)]
async fn serve(
    socket: TcpStream,
    _address: SocketAddr,
    preauth: PreAuthPermit,
    tls: TlsAcceptor,
    online: Arc<Mutex<Online>>,
    auth: Arc<AuthControl>,
    slots: Arc<Semaphore>,
    database: Arc<PathBuf>,
) -> Result<()> {
    let address = _address;
    let preauth = Some(preauth);
    socket.set_nodelay(true)?;
    let config = auth.config();
    let mut stream = match timeout(config.tls_handshake_timeout, tls.accept(socket)).await {
        Ok(Ok(stream)) => stream,
        Ok(Err(error)) => {
            auth.record_preauth_failure(address.ip());
            return Err(error.into());
        }
        Err(_) => {
            auth.record_preauth_failure(address.ip());
            anyhow::bail!("TLS 握手超时");
        }
    };
    let request = match timeout(config.login_frame_timeout, read_message(&mut stream)).await {
        Ok(Ok(request)) => request,
        Ok(Err(error)) => {
            auth.record_preauth_failure(address.ip());
            return Err(error.into());
        }
        Err(_) => {
            auth.record_preauth_failure(address.ip());
            anyhow::bail!("认证消息超时");
        }
    };

    let reservation =
        match auth.reserve_attempt(address.ip(), &request, slots.available_permits() == 0) {
            Ok(reservation) => reservation,
            Err(error) => {
                let content = reason(&error, "登录请求无法受理");
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

    let bcrypt_permit = match auth.acquire_bcrypt().await {
        Ok(permit) => permit,
        Err(error) => {
            drop(reservation);
            let content = reason(&error, "认证服务繁忙，请稍后再试");
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

    let db_path = database.clone();
    let auth_request = request.clone();
    let auth_outcome = spawn_blocking(move || {
        let _permit = bcrypt_permit;
        Store::authenticate(&db_path, auth_request)
    })
    .await;

    let (identity, mut reservation) = match auth_outcome {
        Ok(Ok(identity)) => (identity, Some(reservation)),
        Ok(Err(error)) => {
            if is_invalid_credentials_error(&error) {
                let delay = reservation.record_invalid_credentials();
                if !delay.is_zero() {
                    tokio::time::sleep(delay).await;
                }
            } else {
                reservation.record_service_error();
            }
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
        Err(join_error) => {
            reservation.record_service_error();
            eprintln!("auth blocking task failed: {join_error}");
            send(
                &mut stream,
                &Message {
                    kind: "login_error".into(),
                    content: "认证服务繁忙，请稍后再试".into(),
                    ..Message::default()
                },
            )
            .await?;
            return Ok(());
        }
    };

    let Ok(_session_slot) = slots.try_acquire_owned() else {
        if let Some(reservation) = reservation.take() {
            reservation.record_service_error();
        }
        send(
            &mut stream,
            &Message {
                kind: "login_error".into(),
                content: "服务器当前在线人数已满，请稍后再试".into(),
                ..Message::default()
            },
        )
        .await?;
        return Ok(());
    };

    let heartbeat = wants_heartbeat(&request);
    let group_management = request
        .capabilities
        .iter()
        .any(|value| value == ONLINE_GROUP_MANAGEMENT);
    let group_attachments = request
        .capabilities
        .iter()
        .any(|value| value == ONLINE_GROUP_ATTACHMENTS);
    let bot_settings = request
        .capabilities
        .iter()
        .any(|value| value == ONLINE_BOT_SETTINGS);
    let bot_key_sync = request
        .capabilities
        .iter()
        .any(|value| value == ONLINE_BOT_KEY_SYNC);

    let id = Uuid::new_v4();
    let (tx, mut outgoing) = mpsc::channel(256);
    // 弱引用：会话被服务端移除（如同账号在别处登录）时发送端随之释放，
    // transmit 循环才能照常结束并关闭连接。
    let heartbeat_reply = tx.downgrade();
    let register_state = online.clone();
    let register_identity = identity.clone();
    let register_result = spawn_blocking(move || {
        let mut state = register_state
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let bot_sync = state.supports_bot_key_sync();
        state.register(
            id,
            register_identity,
            tx,
            group_management,
            group_attachments,
        )?;
        Ok::<bool, anyhow::Error>(bot_sync)
    })
    .await?;

    let bot_sync_supported = match register_result {
        Ok(bot_sync) => bot_sync,
        Err(error) => {
            if let Some(reservation) = reservation.take() {
                reservation.record_service_error();
            }
            let content = reason(&error, "该账号无法建立新会话");
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

    let mut remove_guard = PeerRemoveGuard {
        id,
        online: Some(online.clone()),
    };
    drop(preauth);

    let mut capabilities = [
        ONLINE_ACCOUNTS,
        ONLINE_IMAGES,
        ONLINE_MESSAGE_EDIT,
        ONLINE_FILES,
        ONLINE_SOCIAL,
        ONLINE_GROUPS,
        ONLINE_HISTORY_PAGES,
    ]
    .map(str::to_owned)
    .to_vec();
    if heartbeat {
        capabilities.push(HEARTBEAT.into());
    }
    if group_management {
        capabilities.push(ONLINE_GROUP_MANAGEMENT.into());
    }
    if group_attachments {
        capabilities.push(ONLINE_GROUP_ATTACHMENTS.into());
    }
    if bot_settings {
        capabilities.push(ONLINE_BOT_SETTINGS.into());
        if bot_key_sync && bot_sync_supported {
            capabilities.push(ONLINE_BOT_KEY_SYNC.into());
        }
    }

    let session_result: Result<()> = async {
        send(
            &mut stream,
            &Message {
                kind: "login_ok".into(),
                username: identity.username,
                user_code: identity.display_code,
                content: "Login successful".into(),
                capabilities,
                ..Message::default()
            },
        )
        .await?;
        if let Some(reservation) = reservation.take() {
            reservation.record_success();
        }

        let (mut reader, mut writer) = tokio::io::split(stream);
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
                    let mut state = state
                        .lock()
                        .unwrap_or_else(std::sync::PoisonError::into_inner);
                    if allowed {
                        state.command(id, request);
                    } else {
                        state.reply_error(id, &request, "Rate limit exceeded; please slow down");
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
    .await;

    remove_guard.disarm();
    spawn_blocking(move || {
        online
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .remove(id)
    })
    .await?;
    session_result
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
