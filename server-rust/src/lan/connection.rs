use super::{APPROVAL_TIMEOUT, LOBBY, Peer, Server, store::verify_device_binding};
use crate::{
    Budget, LOGIN_TIMEOUT, pong, read_timeout, reason, send, store::Rejected, wants_heartbeat,
};
use anyhow::{Context, Result, ensure};
use lan_chat_protocol::{
    HEARTBEAT, LAN_HISTORY_PAGES, LAN_ROOM_ROLES, MAX_COMMAND_ID_BYTES, MAX_FRAME_BYTES, Message,
    ROOM_PASSWORD, read_lan_command, read_message,
};
use std::{collections::HashSet, future::pending, io, net::SocketAddr, sync::Arc};
use subtle::ConstantTimeEq;
use tokio::{
    io::AsyncReadExt,
    net::TcpStream,
    sync::{mpsc, oneshot},
    task::spawn_blocking,
    time::timeout,
};
use tokio_rustls::TlsAcceptor;
use uuid::Uuid;

pub(super) async fn serve(
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
            .lock_hub()
            .store
            .lan_identity(&request.username, &request.user_code)?;
        let capabilities = [ROOM_PASSWORD, HEARTBEAT, LAN_HISTORY_PAGES, LAN_ROOM_ROLES]
            .into_iter()
            .filter(|supported| {
                request
                    .capabilities
                    .iter()
                    .any(|offered| offered == supported)
            })
            .map(str::to_owned)
            .collect::<Vec<_>>();
        let heartbeat = wants_heartbeat(&request);
        let (identity, device_hash) = identity;
        Ok((
            identity,
            device_hash,
            request.device_secret.clone(),
            admin,
            bootstrap,
            capabilities,
            heartbeat,
        ))
    })
    .await?;
    let (identity, device_hash, device_secret, admin, bootstrap, capabilities, heartbeat) =
        match auth {
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

    // 设备绑定校验要算一次 bcrypt，放在 hub 锁外执行。
    if let Err(error) = verify_device_binding(&device_hash, &device_secret) {
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

    if !bootstrap {
        let (id, decision) = server.lock_hub().request_approval(&identity);
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
        server.lock_hub().expire_approval(&id);
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
    let approved = identity.clone();
    let device_bound = match spawn_blocking(move || {
        approve
            .lock_hub()
            .store
            .approve_identity(&approved, &device_secret)
    })
    .await?
    {
        Ok(device_bound) => device_bound,
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
        let mut hub = register.lock_hub();
        let welcomes = hub.store.pending_lan_welcomes(&identity.code)?;
        // 未绑定设备的旧客户端仍可登录，但不会取走按用户代码即可猜到的离线私信。
        let backlog = if device_bound {
            hub.store.take_lan_private(&identity)?
        } else {
            Vec::new()
        };
        let muted = hub.store.is_lan_muted(&identity.code)?;
        let room_roles = capabilities
            .iter()
            .any(|capability| capability == LAN_ROOM_ROLES);
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
                room_roles,
                send: tx,
                close,
                muted,
                attachment_session: Uuid::new_v4(),
                room: LOBBY.into(),
                capabilities: HashSet::new(),
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
                let mut hub = state.lock_hub();
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
    server.lock_hub().remove(id, true);
    result
}
