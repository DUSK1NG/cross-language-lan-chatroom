use std::{fs, io, net::SocketAddr, path::Path, process::Stdio, sync::Arc, time::Duration};

use anyhow::{Context, Result};
use lan_chat_protocol::{Message, read_message, write_message};
use rcgen::{
    CertificateParams, DistinguishedName, DnType, ExtendedKeyUsagePurpose, KeyPair, KeyUsagePurpose,
};
use rusqlite::{Connection, params};
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

type Peer = TlsStream<TcpStream>;

const WAIT: Duration = Duration::from_secs(6);
const OWNER_CODE: &str = "OWNER1";
const GUEST_CODE: &str = "ALICE1";
const OFFLINE_CONTENT: &str = "offline fixture for the approved device";

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

async fn start_lan_server(root: &Path) -> Result<(Child, SocketAddr, TlsConnector)> {
    start_lan_server_with_args(root, &[]).await
}

async fn start_lan_server_with_args(
    root: &Path,
    extra_args: &[&str],
) -> Result<(Child, SocketAddr, TlsConnector)> {
    fs::create_dir_all(root)?;
    let cert = root.join("server.crt");
    let key = root.join("server.key");
    create_localhost_cert(&cert, &key)?;

    let mut child = Command::new(env!("CARGO_BIN_EXE_lan-chat-server"))
        .args(["--mode", "lan", "--listen", "127.0.0.1:0"])
        .arg("--db")
        .arg(root.join("chat.db"))
        .arg("--cert")
        .arg(&cert)
        .arg("--key")
        .arg(&key)
        .arg("--admin-code")
        .arg(OWNER_CODE)
        .args(extra_args)
        .stdin(Stdio::null())
        .stderr(Stdio::piped())
        .kill_on_drop(true)
        .spawn()?;
    let mut stderr = BufReader::new(child.stderr.take().context("missing stderr")?).lines();
    let address: SocketAddr = timeout(WAIT, async {
        while let Some(line) = stderr.next_line().await? {
            if let Some(address) = line.strip_prefix("listening on ") {
                return Ok::<SocketAddr, anyhow::Error>(address.parse()?);
            }
            eprintln!("{line}");
        }
        anyhow::bail!("LAN server exited before listening")
    })
    .await??;
    tokio::spawn(async move {
        while let Some(line) = stderr.next_line().await? {
            eprintln!("{line}");
        }
        Ok::<(), io::Error>(())
    });

    let mut roots = RootCertStore::empty();
    for certificate in rustls_pemfile::certs(&mut io::BufReader::new(fs::File::open(cert)?)) {
        roots.add(certificate?)?;
    }
    let connector = TlsConnector::from(Arc::new(
        ClientConfig::builder()
            .with_root_certificates(roots)
            .with_no_client_auth(),
    ));
    Ok((child, address, connector))
}

async fn connect(
    connector: &TlsConnector,
    address: SocketAddr,
    username: &str,
    user_code: &str,
    device_secret: &str,
) -> Result<Peer> {
    let socket = TcpStream::connect(address).await?;
    let mut peer = connector
        .connect(ServerName::try_from("localhost")?, socket)
        .await?;
    write_message(
        &mut peer,
        &Message {
            kind: "login".into(),
            username: username.into(),
            user_code: user_code.into(),
            device_secret: device_secret.into(),
            ..Message::default()
        },
    )
    .await?;
    Ok(peer)
}

async fn expect_disconnected<R>(stream: &mut R, wait: Duration) -> Result<()>
where
    R: tokio::io::AsyncRead + Unpin,
{
    let mut byte = [0];
    match timeout(wait, stream.read(&mut byte)).await {
        Ok(Ok(0)) => Ok(()),
        Ok(Err(error))
            if matches!(
                error.kind(),
                io::ErrorKind::ConnectionReset
                    | io::ErrorKind::ConnectionAborted
                    | io::ErrorKind::UnexpectedEof
            ) =>
        {
            Ok(())
        }
        Ok(Ok(_)) => anyhow::bail!("expected preauth connection to close"),
        Ok(Err(error)) => Err(error.into()),
        Err(_) => anyhow::bail!("preauth connection was not closed promptly"),
    }
}

async fn next_message(peer: &mut Peer) -> Result<Message> {
    Ok(timeout(WAIT, read_message(peer)).await??)
}

async fn next_kind(peer: &mut Peer, kind: &str) -> Result<Message> {
    timeout(WAIT, async {
        loop {
            let message = read_message(peer).await?;
            if message.kind == kind {
                return Ok(message);
            }
        }
    })
    .await?
}

async fn wait_for_approval(owner: &mut Peer, approval_id: &str) -> Result<()> {
    timeout(WAIT, async {
        loop {
            let message = read_message(owner).await?;
            if message.kind == "connection_approval_request" && message.message_id == approval_id {
                return Ok(());
            }
        }
    })
    .await?
}

async fn approve(owner: &mut Peer, approval_id: &str) -> Result<()> {
    write_message(
        owner,
        &Message {
            kind: "admin_action".into(),
            content: "approve_connection".into(),
            message_id: approval_id.into(),
            ..Message::default()
        },
    )
    .await?;
    Ok(())
}

#[tokio::test]
async fn concurrent_approvals_bind_only_one_device() -> Result<()> {
    let root = std::env::temp_dir().join(format!("lan-p0-bind-{}", Uuid::new_v4()));
    let (mut child, address, connector) = start_lan_server(&root).await?;
    let mut owner = connect(&connector, address, "Owner", OWNER_CODE, "").await?;
    let owner_login = next_kind(&mut owner, "login_ok").await?;
    assert!(owner_login.is_admin);

    let mut device_a = connect(&connector, address, "Alice", GUEST_CODE, "device-a").await?;
    let pending_a = next_kind(&mut device_a, "login_pending").await?;
    let mut device_b = connect(&connector, address, "Alice", GUEST_CODE, "device-b").await?;
    let pending_b = next_kind(&mut device_b, "login_pending").await?;
    wait_for_approval(&mut owner, &pending_a.message_id).await?;
    wait_for_approval(&mut owner, &pending_b.message_id).await?;

    let db = Connection::open(root.join("chat.db"))?;
    db.busy_timeout(WAIT)?;
    db.execute(
        "INSERT INTO offline_messages
         (target_code,sender_username,sender_code,content,message_id,created_at)
         VALUES (?,?,?,?,?,?)",
        params![
            "alice1",
            "Sender",
            "sender",
            OFFLINE_CONTENT,
            "lan-p0-offline",
            "2026-11-28T00:00:00Z"
        ],
    )?;

    approve(&mut owner, &pending_a.message_id).await?;
    let login_a = next_message(&mut device_a).await?;
    assert_eq!(login_a.kind, "login_ok");
    let offline_a = next_kind(&mut device_a, "offline_message").await?;
    assert_eq!(offline_a.content, OFFLINE_CONTENT);

    approve(&mut owner, &pending_b.message_id).await?;
    let login_b = next_message(&mut device_b).await?;
    assert_eq!(login_b.kind, "login_error");

    let remaining: i64 = db.query_row(
        "SELECT COUNT(*) FROM offline_messages WHERE target_code=?",
        ["alice1"],
        |row| row.get(0),
    )?;
    assert_eq!(remaining, 0);

    drop((owner, device_a, device_b, db));
    child.kill().await?;
    fs::remove_dir_all(root)?;
    Ok(())
}

#[tokio::test]
async fn lan_preauth_exhaustion_preserves_existing_session_and_approval() -> Result<()> {
    let root = std::env::temp_dir().join(format!("lan-p0-preauth-{}", Uuid::new_v4()));
    let (mut child, address, connector) = start_lan_server_with_args(
        &root,
        &[
            "--lan-max-preauth",
            "2",
            "--lan-max-preauth-per-ip",
            "2",
            "--lan-tls-handshake-timeout-secs",
            "30",
            "--max-clients",
            "4",
        ],
    )
    .await?;
    let mut owner = connect(&connector, address, "Owner", OWNER_CODE, "").await?;
    next_kind(&mut owner, "login_ok").await?;

    let stalled_one = TcpStream::connect(address).await?;
    let stalled_two = TcpStream::connect(address).await?;
    let mut rejected = TcpStream::connect(address).await?;
    expect_disconnected(&mut rejected, Duration::from_secs(1)).await?;

    write_message(
        &mut owner,
        &Message {
            kind: "users_request".into(),
            ..Message::default()
        },
    )
    .await?;
    let users = next_kind(&mut owner, "users_response").await?;
    assert!(users.users.iter().any(|user| user == "Owner#OWNER1@lobby"));

    drop((stalled_one, stalled_two));
    let mut restored = timeout(WAIT, async {
        loop {
            match connect(&connector, address, "Bob", "BOB1", "device-bob").await {
                Ok(peer) => return Ok::<_, anyhow::Error>(peer),
                Err(_) => tokio::time::sleep(Duration::from_millis(25)).await,
            }
        }
    })
    .await??;
    let pending = next_kind(&mut restored, "login_pending").await?;
    wait_for_approval(&mut owner, &pending.message_id).await?;
    approve(&mut owner, &pending.message_id).await?;
    next_kind(&mut restored, "login_ok").await?;

    drop((owner, restored));
    child.kill().await?;
    child.wait().await?;
    fs::remove_dir_all(root)?;
    Ok(())
}

#[tokio::test]
async fn lan_formal_slot_is_acquired_after_approval_and_device_binding() -> Result<()> {
    let root = std::env::temp_dir().join(format!("lan-p0-session-slot-{}", Uuid::new_v4()));
    let (mut child, address, connector) = start_lan_server_with_args(
        &root,
        &[
            "--lan-max-preauth",
            "2",
            "--lan-max-preauth-per-ip",
            "2",
            "--max-clients",
            "1",
        ],
    )
    .await?;
    let mut owner = connect(&connector, address, "Owner", OWNER_CODE, "").await?;
    next_kind(&mut owner, "login_ok").await?;

    let mut guest = connect(&connector, address, "Alice", GUEST_CODE, "device-a").await?;
    let pending = next_kind(&mut guest, "login_pending").await?;
    wait_for_approval(&mut owner, &pending.message_id).await?;
    approve(&mut owner, &pending.message_id).await?;
    let full = next_message(&mut guest).await?;
    assert_eq!(full.kind, "login_error");
    assert_eq!(full.content, "服务器当前在线人数已满，请稍后再试");

    let db = Connection::open(root.join("chat.db"))?;
    db.busy_timeout(WAIT)?;
    let device_hash: String = db.query_row(
        "SELECT device_secret_hash FROM accounts WHERE normalized_code=?",
        ["alice1"],
        |row| row.get(0),
    )?;
    assert!(!device_hash.is_empty());

    write_message(
        &mut owner,
        &Message {
            kind: "users_request".into(),
            ..Message::default()
        },
    )
    .await?;
    let users = next_kind(&mut owner, "users_response").await?;
    assert!(users.users.iter().any(|user| user == "Owner#OWNER1@lobby"));

    drop((owner, guest, db));
    child.kill().await?;
    child.wait().await?;
    fs::remove_dir_all(root)?;
    Ok(())
}

#[tokio::test]
async fn lan_preauth_timeouts_release_permits() -> Result<()> {
    let root = std::env::temp_dir().join(format!("lan-p0-preauth-timeout-{}", Uuid::new_v4()));
    let (mut child, address, connector) = start_lan_server_with_args(
        &root,
        &[
            "--lan-max-preauth",
            "1",
            "--lan-max-preauth-per-ip",
            "1",
            "--lan-tls-handshake-timeout-secs",
            "1",
            "--lan-login-frame-timeout-secs",
            "1",
            "--max-clients",
            "1",
        ],
    )
    .await?;

    let mut handshake_stall = TcpStream::connect(address).await?;
    expect_disconnected(&mut handshake_stall, Duration::from_secs(3)).await?;

    let socket = TcpStream::connect(address).await?;
    let mut login_stall = connector
        .connect(ServerName::try_from("localhost")?, socket)
        .await?;
    expect_disconnected(&mut login_stall, Duration::from_secs(3)).await?;

    let mut owner = connect(&connector, address, "Owner", OWNER_CODE, "").await?;
    next_kind(&mut owner, "login_ok").await?;

    drop(owner);
    child.kill().await?;
    child.wait().await?;
    fs::remove_dir_all(root)?;
    Ok(())
}

#[tokio::test]
async fn lan_rejects_invalid_admission_configuration() -> Result<()> {
    let root = std::env::temp_dir().join(format!("lan-p0-invalid-admission-{}", Uuid::new_v4()));
    fs::create_dir_all(&root)?;
    let cert = root.join("server.crt");
    let key = root.join("server.key");
    create_localhost_cert(&cert, &key)?;

    for flag in [
        "--lan-max-preauth",
        "--lan-max-preauth-per-ip",
        "--lan-tls-handshake-timeout-secs",
        "--lan-login-frame-timeout-secs",
    ] {
        let mut command = Command::new(env!("CARGO_BIN_EXE_lan-chat-server"));
        command
            .args(["--mode", "lan", "--listen", "127.0.0.1:0"])
            .arg("--db")
            .arg(root.join("chat.db"))
            .arg("--cert")
            .arg(&cert)
            .arg("--key")
            .arg(&key)
            .arg(flag)
            .arg("0")
            .kill_on_drop(true);
        let output = timeout(WAIT, command.output()).await??;
        assert!(!output.status.success());
        let stderr = String::from_utf8_lossy(&output.stderr);
        assert!(!stderr.contains("unexpected argument"), "{stderr}");
        assert!(stderr.contains("须"), "{stderr}");
    }

    fs::remove_dir_all(root)?;
    Ok(())
}
