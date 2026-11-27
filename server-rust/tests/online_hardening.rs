use std::{
    fs, io,
    net::SocketAddr,
    path::Path,
    process::Stdio,
    sync::Arc,
    time::{Duration, Instant},
};

use anyhow::{Context, Result};
use lan_chat_protocol::{HEARTBEAT, Message, read_message, write_message};
use rcgen::{
    CertificateParams, DistinguishedName, DnType, ExtendedKeyUsagePurpose, KeyPair, KeyUsagePurpose,
};
use tokio::{
    io::{AsyncBufReadExt, AsyncReadExt, BufReader},
    net::TcpStream,
    process::{Child, Command},
    time::timeout,
};
use tokio_rustls::{
    TlsConnector,
    client::TlsStream,
    rustls::{ClientConfig, RootCertStore, pki_types::ServerName},
};
use uuid::Uuid;

type TlsPeer = TlsStream<TcpStream>;

const WAIT: Duration = Duration::from_secs(6);

fn create_localhost_cert(cert_path: &Path, key_path: &Path) -> Result<()> {
    let mut params = CertificateParams::new(vec!["localhost".into(), "127.0.0.1".into()])?;
    params.distinguished_name = DistinguishedName::new();
    params
        .distinguished_name
        .push(DnType::CommonName, "LAN Chat Test");
    params.key_usages = vec![KeyUsagePurpose::DigitalSignature];
    params.extended_key_usages = vec![ExtendedKeyUsagePurpose::ServerAuth];
    let key_pair = KeyPair::generate()?;
    let cert = params.self_signed(&key_pair)?;
    fs::write(key_path, key_pair.serialize_pem())?;
    fs::write(cert_path, cert.pem())?;
    Ok(())
}

async fn start_online_server(
    root: &Path,
    extra_args: &[&str],
) -> Result<(Child, SocketAddr, TlsConnector)> {
    fs::create_dir_all(root)?;
    let cert = root.join("server.crt");
    let key = root.join("server.key");
    create_localhost_cert(&cert, &key)?;

    let mut command = Command::new(env!("CARGO_BIN_EXE_lan-chat-server"));
    command
        .args(["--mode", "online", "--listen", "127.0.0.1:0"])
        .arg("--db")
        .arg(root.join("online.db"))
        .arg("--cert")
        .arg(&cert)
        .arg("--key")
        .arg(&key)
        .args(extra_args)
        .stdin(Stdio::null())
        .stderr(Stdio::piped())
        .kill_on_drop(true);

    let mut child = command.spawn()?;
    let mut stderr = BufReader::new(child.stderr.take().context("missing stderr")?).lines();
    let address: SocketAddr = timeout(WAIT, async {
        while let Some(line) = stderr.next_line().await? {
            if let Some(addr) = line.strip_prefix("listening on ") {
                return Ok::<SocketAddr, anyhow::Error>(addr.parse()?);
            }
            eprintln!("{line}");
        }
        anyhow::bail!("online server exited before listening")
    })
    .await??;
    tokio::spawn(async move {
        while let Some(line) = stderr.next_line().await? {
            eprintln!("{line}");
        }
        Ok::<(), io::Error>(())
    });

    let mut roots = RootCertStore::empty();
    for item in rustls_pemfile::certs(&mut io::BufReader::new(fs::File::open(cert)?)) {
        roots.add(item?)?;
    }
    let connector = TlsConnector::from(Arc::new(
        ClientConfig::builder()
            .with_root_certificates(roots)
            .with_no_client_auth(),
    ));
    Ok((child, address, connector))
}

async fn tls_connect(connector: &TlsConnector, address: SocketAddr) -> Result<TlsPeer> {
    let socket = TcpStream::connect(address).await?;
    Ok(connector
        .connect(ServerName::try_from("localhost")?, socket)
        .await?)
}

async fn send_auth(
    peer: &mut TlsPeer,
    kind: &str,
    username: &str,
    user_code: &str,
    password: &str,
) -> Result<Message> {
    write_message(
        peer,
        &Message {
            kind: kind.into(),
            username: username.into(),
            user_code: user_code.into(),
            account_password: password.into(),
            capabilities: vec![HEARTBEAT.into()],
            ..Message::default()
        },
    )
    .await?;
    Ok(timeout(WAIT, read_message(peer)).await??)
}

#[tokio::test]
async fn preauth_separation_timeouts_and_session_full_fast_reject() -> Result<()> {
    let root = std::env::temp_dir().join(format!("lan-chat-e2e-preauth-{}", Uuid::new_v4()));
    let (mut child, address, connector) = start_online_server(
        &root,
        &[
            "--max-clients",
            "1",
            "--max-preauth",
            "4",
            "--max-preauth-per-ip",
            "2",
            "--tls-handshake-timeout-secs",
            "1",
            "--login-frame-timeout-secs",
            "1",
            "--account-fail-threshold",
            "2",
        ],
    )
    .await?;

    // Register two users first, keeping slots available later.
    let mut reg_a = tls_connect(&connector, address).await?;
    assert_eq!(
        send_auth(
            &mut reg_a,
            "account_register",
            "Alice",
            "ALICE1",
            "pass12345"
        )
        .await?
        .kind,
        "login_ok"
    );
    let mut reg_b = tls_connect(&connector, address).await?;
    // First disconnect reg_a so the 1 max-clients slot is free for reg_b.
    drop(reg_a);
    tokio::time::sleep(Duration::from_millis(80)).await;
    assert_eq!(
        send_auth(&mut reg_b, "account_register", "Bob", "BOB001", "pass12345")
            .await?
            .kind,
        "login_ok"
    );
    drop(reg_b);
    tokio::time::sleep(Duration::from_millis(80)).await;

    // Occupy the single authenticated session slot with Alice.
    let mut alice = tls_connect(&connector, address).await?;
    assert_eq!(
        send_auth(&mut alice, "account_login", "Alice", "", "pass12345")
            .await?
            .kind,
        "login_ok"
    );

    // Bob completes TLS + login frame while session slots are full: receives login_error busy
    // without touching Bob's account failure counter even when using wrong password.
    let mut bob_full = tls_connect(&connector, address).await?;
    let full_reply = send_auth(&mut bob_full, "account_login", "Bob", "", "wrongpass").await?;
    assert_eq!(full_reply.kind, "login_error");
    assert!(
        full_reply.content.contains("已满"),
        "{}",
        full_reply.content
    );
    drop(bob_full);

    // Exhaust per-source pre-auth concurrency (2): 1 stalled raw TCP + 1 stalled TLS without frame.
    let mut raw_slow = TcpStream::connect(address).await?;
    let mut tls_slow = tls_connect(&connector, address).await?;

    // 3rd pre-auth connection from same IP must be closed before TLS.
    let extra = TcpStream::connect(address).await?;
    let tls_rejected = timeout(
        WAIT,
        connector.connect(ServerName::try_from("localhost")?, extra),
    )
    .await?;
    assert!(
        tls_rejected.is_err(),
        "3rd preauth connection should be rejected before TLS"
    );

    // Existing authenticated session Alice stays alive while pre-auth slots are full.
    write_message(
        &mut alice,
        &Message {
            kind: "ping".into(),
            ..Message::default()
        },
    )
    .await?;
    let pong = timeout(WAIT, read_message(&mut alice)).await??;
    assert_eq!(pong.kind, "pong");

    // Both slow connections must be dropped cleanly by 1s timeouts, freeing pre-auth slots.
    let mut buf = [0u8; 16];
    let n1 = timeout(WAIT, raw_slow.read(&mut buf)).await?;
    let n2 = timeout(WAIT, tls_slow.read(&mut buf)).await?;
    assert!(matches!(n1, Ok(0) | Err(_)));
    assert!(matches!(n2, Ok(0) | Err(_)));
    drop((raw_slow, tls_slow));

    // Free Alice's session slot and verify Bob can immediately log in (never locked by session-full attempts).
    drop(alice);
    tokio::time::sleep(Duration::from_millis(80)).await;
    let mut bob = tls_connect(&connector, address).await?;
    assert_eq!(
        send_auth(&mut bob, "account_login", "Bob", "", "pass12345")
            .await?
            .kind,
        "login_ok"
    );

    child.kill().await?;
    fs::remove_dir_all(root)?;
    Ok(())
}

#[tokio::test]
async fn rate_limiting_per_account_session_cap_and_android_safe_errors() -> Result<()> {
    let root = std::env::temp_dir().join(format!("lan-chat-e2e-rate-{}", Uuid::new_v4()));
    let (mut child, address, connector) = start_online_server(
        &root,
        &[
            "--max-clients",
            "8",
            "--max-sessions-per-account",
            "2",
            "--login-source-limit",
            "40",
            "--register-source-limit",
            "2",
            "--account-fail-threshold",
            "3",
            "--account-cooldown-secs",
            "2",
            "--account-cooldown-max-secs",
            "8",
        ],
    )
    .await?;

    // Separate register bucket (limit 2).
    for (u, c) in [("Alice", "ALICE1"), ("Bob", "BOB001")] {
        let mut p = tls_connect(&connector, address).await?;
        assert_eq!(
            send_auth(&mut p, "account_register", u, c, "pass12345")
                .await?
                .kind,
            "login_ok"
        );
    }
    let mut reg_blocked = tls_connect(&connector, address).await?;
    let reg_err = send_auth(
        &mut reg_blocked,
        "account_register",
        "Carol",
        "CAROL1",
        "pass12345",
    )
    .await?;
    assert_eq!(reg_err.kind, "login_error");
    assert_ne!(reg_err.content, "用户名或密码不正确");

    // Login still works because source_login is independent from source_register,
    // and Alice can open up to max_sessions_per_account = 2 concurrent sessions.
    let mut alice1 = tls_connect(&connector, address).await?;
    assert_eq!(
        send_auth(&mut alice1, "account_login", "Alice", "", "pass12345")
            .await?
            .kind,
        "login_ok"
    );
    let mut alice2 = tls_connect(&connector, address).await?;
    assert_eq!(
        send_auth(&mut alice2, "account_login", "Alice", "", "pass12345")
            .await?
            .kind,
        "login_ok"
    );
    let mut alice3 = tls_connect(&connector, address).await?;
    let cap_err = send_auth(&mut alice3, "account_login", "Alice", "", "pass12345").await?;
    assert_eq!(cap_err.kind, "login_error");
    assert_ne!(cap_err.content, "用户名或密码不正确");
    assert!(cap_err.content.contains("上限"), "{}", cap_err.content);
    drop(alice3);

    // Dropping one Alice session frees an account slot cleanly.
    drop(alice1);
    tokio::time::sleep(Duration::from_millis(80)).await;
    let mut alice4 = tls_connect(&connector, address).await?;
    assert_eq!(
        send_auth(&mut alice4, "account_login", "Alice", "", "pass12345")
            .await?
            .kind,
        "login_ok"
    );

    // Fail Bob 3 times with wrong password -> 4th attempt returns non-blocking cooldown promptly
    // with rate-limit message (never "用户名或密码不正确" so Android keeps saved credentials).
    for _ in 0..3 {
        let mut p = tls_connect(&connector, address).await?;
        let wrong = send_auth(&mut p, "account_login", "Bob", "", "wrongpass").await?;
        assert_eq!(wrong.kind, "login_error");
        assert_eq!(wrong.content, "用户名或密码不正确");
    }
    let started = Instant::now();
    let mut cooled = tls_connect(&connector, address).await?;
    let cooled_err = send_auth(&mut cooled, "account_login", "Bob", "", "pass12345").await?;
    assert!(
        started.elapsed() < Duration::from_secs(1),
        "cooldown must be non-blocking"
    );
    assert_eq!(cooled_err.kind, "login_error");
    assert_ne!(cooled_err.content, "用户名或密码不正确");
    assert!(
        cooled_err.content.contains("频繁"),
        "{}",
        cooled_err.content
    );

    // Wait out 2s cooldown -> valid credentials succeed and reset failures.
    tokio::time::sleep(Duration::from_millis(2200)).await;
    let mut bob_ok = tls_connect(&connector, address).await?;
    assert_eq!(
        send_auth(&mut bob_ok, "account_login", "Bob", "", "pass12345")
            .await?
            .kind,
        "login_ok"
    );

    child.kill().await?;
    fs::remove_dir_all(root)?;
    Ok(())
}
