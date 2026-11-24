use std::{io, net::SocketAddr, path::Path, process::Stdio, sync::Arc, time::Duration};

use anyhow::{Context, Result};
use base64::{Engine, engine::general_purpose::URL_SAFE_NO_PAD};
use lan_chat_protocol::{
    HEARTBEAT, HEARTBEAT_TIMEOUT_SECS, LocalHostCredentials, Message, read_message, write_message,
};
use tokio::{
    io::{AsyncBufReadExt, AsyncWriteExt, BufReader},
    net::TcpStream,
    process::{Child, Command},
    time::{Instant, timeout},
};
use tokio_rustls::{
    TlsConnector,
    client::TlsStream,
    rustls::{ClientConfig, RootCertStore, pki_types::ServerName},
};
use uuid::Uuid;

type Peer = TlsStream<TcpStream>;

const WAIT: Duration = Duration::from_secs(5);
const OWNER: &str = "HeartOwner";
const GUEST: &str = "HeartGuest";

async fn start(root: &Path, session_token: String) -> Result<(Child, SocketAddr, TlsConnector)> {
    let cert = root.join("server.crt");
    let mut child = Command::new(env!("CARGO_BIN_EXE_lan-chat-server"))
        .args([
            "--mode",
            "lan",
            "--listen",
            "127.0.0.1:0",
            "--auto-cert",
            "--admin-code",
            OWNER,
        ])
        .arg("--local-host-credentials-stdin")
        .arg("--db")
        .arg(root.join("chat.db"))
        .arg("--cert")
        .arg(&cert)
        .arg("--key")
        .arg(root.join("server.key"))
        .stdin(Stdio::piped())
        .stderr(Stdio::piped())
        .kill_on_drop(true)
        .spawn()?;
    let mut input = child.stdin.take().context("missing server input")?;
    input
        .write_all(&serde_json::to_vec(&LocalHostCredentials {
            password: "fixture-password".into(),
            session_token,
        })?)
        .await?;
    drop(input);
    let mut stderr = BufReader::new(child.stderr.take().context("missing server output")?).lines();
    let address: SocketAddr = timeout(WAIT, async {
        while let Some(line) = stderr.next_line().await? {
            if let Some(address) = line.strip_prefix("listening on ") {
                return Ok::<SocketAddr, anyhow::Error>(address.parse()?);
            }
            eprintln!("{line}");
        }
        anyhow::bail!("server exited before listening")
    })
    .await??;
    tokio::spawn(async move {
        while let Some(line) = stderr.next_line().await? {
            eprintln!("{line}");
        }
        Ok::<(), io::Error>(())
    });
    let mut roots = RootCertStore::empty();
    for certificate in rustls_pemfile::certs(&mut io::BufReader::new(std::fs::File::open(cert)?)) {
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
    code: &str,
    token: &str,
    capabilities: Vec<String>,
) -> Result<Peer> {
    let socket = TcpStream::connect(address).await?;
    let mut peer = connector
        .connect(ServerName::try_from("localhost")?, socket)
        .await?;
    write_message(
        &mut peer,
        &Message {
            kind: "login".into(),
            username: code.into(),
            user_code: code.into(),
            host_session_token: token.into(),
            capabilities,
            ..Message::default()
        },
    )
    .await?;
    Ok(peer)
}

async fn next(
    peer: &mut Peer,
    wait: Duration,
    matches: impl Fn(&Message) -> bool,
) -> Result<Message> {
    timeout(wait, async {
        loop {
            let message = read_message(peer).await?;
            if matches(&message) {
                return Ok(message);
            }
        }
    })
    .await
    .context("expected heartbeat fixture message")?
}

async fn approve(owner: &mut Peer, guest: &mut Peer) -> Result<Message> {
    let pending = next(guest, WAIT, |message| message.kind == "login_pending").await?;
    next(owner, WAIT, |message| {
        message.kind == "connection_approval_request" && message.message_id == pending.message_id
    })
    .await?;
    write_message(
        owner,
        &Message {
            kind: "admin_action".into(),
            content: "approve_connection".into(),
            message_id: pending.message_id,
            ..Message::default()
        },
    )
    .await?;
    next(guest, WAIT, |message| message.kind == "login_ok").await
}

#[tokio::test]
async fn lan_heartbeat_negotiation_timeout_and_approval_on_reconnect() -> Result<()> {
    let root = std::env::temp_dir().join(format!("lan-chat-heartbeat-{}", Uuid::new_v4()));
    let token = URL_SAFE_NO_PAD.encode([19_u8; 32]);
    let (mut child, address, connector) = start(&root, token.clone()).await?;
    let mut owner = connect(&connector, address, OWNER, &token, Vec::new()).await?;
    let login = next(&mut owner, WAIT, |message| message.kind == "login_ok").await?;
    assert!(login.is_admin && !login.capabilities.iter().any(|cap| cap == HEARTBEAT));

    let ping = Message {
        kind: "ping".into(),
        ..Message::default()
    };
    write_message(&mut owner, &ping).await?;
    assert_eq!(
        next(&mut owner, WAIT, |message| matches!(
            message.kind.as_str(),
            "error" | "pong"
        ))
        .await?
        .kind,
        "error"
    );

    let mut guest = connect(&connector, address, GUEST, "", vec![HEARTBEAT.into()]).await?;
    assert!(
        approve(&mut owner, &mut guest)
            .await?
            .capabilities
            .iter()
            .any(|cap| cap == HEARTBEAT)
    );
    for _ in 0..20 {
        write_message(&mut guest, &ping).await?;
        assert_eq!(
            next(&mut guest, WAIT, |message| matches!(
                message.kind.as_str(),
                "error" | "pong"
            ))
            .await?
            .kind,
            "pong"
        );
    }
    let chat = Message {
        kind: "chat".into(),
        content: "heartbeat budget fixture".into(),
        ..Message::default()
    };
    write_message(&mut guest, &chat).await?;
    next(&mut owner, WAIT, |message| {
        message.kind == "chat" && message.content == chat.content
    })
    .await?;
    next(&mut guest, WAIT, |message| {
        message.kind == "chat" && message.content == chat.content
    })
    .await?;

    // Use the production timeout. The legacy owner stays connected over the same idle period.
    let idle = Duration::from_secs(HEARTBEAT_TIMEOUT_SECS);
    let started = Instant::now();
    next(&mut owner, idle + WAIT, |message| {
        message.kind == "system" && message.content.contains("left the chat")
    })
    .await?;
    assert!(started.elapsed() >= idle - WAIT);
    let close = timeout(WAIT, read_message(&mut guest)).await?;
    assert!(
        close.is_err(),
        "expired connection still received a message"
    );

    let mut guest = connect(&connector, address, GUEST, "", Vec::new()).await?;
    let login = approve(&mut owner, &mut guest).await?;
    assert!(!login.capabilities.iter().any(|cap| cap == HEARTBEAT));
    write_message(&mut guest, &chat).await?;
    next(&mut owner, WAIT, |message| {
        message.kind == "chat" && message.content == chat.content
    })
    .await?;
    child.kill().await?;
    std::fs::remove_dir_all(root)?;
    Ok(())
}
