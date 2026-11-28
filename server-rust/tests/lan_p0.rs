use std::{fs, io, net::SocketAddr, path::Path, process::Stdio, sync::Arc, time::Duration};

use anyhow::{Context, Result};
use lan_chat_protocol::{Message, read_message, write_message};
use rcgen::{
    CertificateParams, DistinguishedName, DnType, ExtendedKeyUsagePurpose, KeyPair, KeyUsagePurpose,
};
use rusqlite::{Connection, params};
use tokio::{
    io::{AsyncBufReadExt, BufReader},
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
