use std::{io::BufReader, path::PathBuf};

use anyhow::{Result, ensure};
use base64::{Engine, engine::general_purpose::STANDARD};
use serde::Deserialize;
use serde_json::{Value, json};
use tokio::{
    net::UdpSocket,
    time::{Duration, Instant, timeout_at},
};
use uuid::Uuid;

use crate::{Client, checksum};

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct Announcement {
    service: String,
    version: u8,
    host_name: String,
    port: u16,
    certificate_base64: String,
    fingerprint_sha256: String,
}

#[derive(Clone)]
pub struct Discovered {
    pub id: String,
    pub name: String,
    pub address: String,
    pub port: u16,
    pub certificate: Vec<u8>,
    pub path: PathBuf,
    last_seen: Instant,
}

impl Discovered {
    pub fn summary(&self) -> Value {
        json!({"id": self.id, "hostName":self.name,"serverIp":self.address,"serverPort":self.port,
            "fingerprintSha256":self.id,"known":self.path.is_file()})
    }

    pub fn recent(&self) -> bool {
        self.last_seen.elapsed() <= Duration::from_secs(6)
    }
}

impl Client {
    pub(crate) async fn scan(self: &std::sync::Arc<Self>) -> Result<()> {
        let socket = UdpSocket::bind(("0.0.0.0", 38888)).await?;
        let id = Uuid::new_v4();
        {
            let mut state = self.state.lock().expect("client state");
            ensure!(state.scan.is_none(), "正在搜索附近聊天室");
            state.scan = Some(id);
            // Keep hosts that still advertise: a new scan starts before the owner's next broadcast,
            // so clearing the list here would blank the UI and cancel the user's selection.
            state.discovered.retain(|_, host| host.recent());
            self.publish(&state);
        }

        let client = self.clone();
        tokio::spawn(async move {
            let result = client.listen_discovery(socket, id).await;
            let mut state = client.state.lock().expect("client state");
            if state.scan != Some(id) {
                return;
            }
            state.scan = None;
            if let Err(error) = result {
                client.report("discovery_failed", &error.to_string(), "controller", "");
            }
            client.publish(&state);
        });
        Ok(())
    }

    async fn listen_discovery(&self, socket: UdpSocket, id: Uuid) -> Result<()> {
        let stop = Instant::now() + Duration::from_secs(5);
        let mut buffer = [0; 24 * 1024];
        while let Ok(datagram) = timeout_at(stop, socket.recv_from(&mut buffer)).await {
            let (length, from) = datagram?;
            let Ok(announce) = serde_json::from_slice::<Announcement>(&buffer[..length]) else {
                continue;
            };
            if announce.service != "lan-chat"
                || announce.version != 1
                || announce.port == 0
                || announce.host_name.is_empty()
            {
                continue;
            }
            let Ok(certificate) = STANDARD.decode(announce.certificate_base64) else {
                continue;
            };
            if !(1..=16 * 1024).contains(&certificate.len())
                || checksum(&certificate) != announce.fingerprint_sha256
            {
                continue;
            }
            if !rustls_pemfile::certs(&mut BufReader::new(certificate.as_slice()))
                .next()
                .is_some_and(|entry| entry.is_ok())
            {
                continue;
            }
            let fingerprint = announce.fingerprint_sha256;
            let host = Discovered {
                id: fingerprint.clone(),
                name: announce.host_name,
                address: from.ip().to_string(),
                port: announce.port,
                certificate,
                path: self
                    .dir
                    .join("trusted-hosts")
                    .join(format!("{fingerprint}.crt")),
                last_seen: Instant::now(),
            };
            let mut state = self.state.lock().expect("client state");
            if state.scan != Some(id) {
                break;
            }
            let changed = state.discovered.get(&fingerprint).is_none_or(|old| {
                old.address != host.address || old.name != host.name || old.port != host.port
            });
            state.discovered.insert(fingerprint, host);
            if changed {
                self.publish(&state);
            }
        }
        Ok(())
    }
}
