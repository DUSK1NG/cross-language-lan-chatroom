use std::{
    fs,
    io::{BufReader, Cursor},
    path::Path,
    sync::mpsc::{SyncSender, sync_channel},
    thread,
    time::Duration,
};

use anyhow::{Context, Result, bail, ensure};
use base64::{Engine, engine::general_purpose::URL_SAFE_NO_PAD};
use chrono::Utc;
use lan_chat_protocol::Message;
use ring::{
    rand::SystemRandom,
    signature::{RSA_PKCS1_SHA256, RsaKeyPair},
};
use rusqlite::{Connection, OpenFlags, params};
use serde::Deserialize;
use serde_json::{Value, json};
use ureq::{
    Agent,
    http::{StatusCode, Uri},
};

use crate::store::Rejected;

const TOKEN_URL: &str = "https://oauth2.googleapis.com/token";
const RELAY_PROJECT: &str = "lan-chat-9125d";
const MAX_RESPONSE: u64 = 16 * 1024;

pub struct Push(SyncSender<Notification>);

struct Notification {
    recipient: String,
    sender: String,
    message_id: String,
}

#[derive(Deserialize)]
struct Credentials {
    #[serde(rename = "type")]
    kind: String,
    project_id: String,
    client_email: String,
    private_key: String,
}

#[derive(Deserialize)]
struct AccessToken {
    access_token: String,
    expires_in: i64,
}

struct Gateway {
    client: Agent,
    email: String,
    key: RsaKeyPair,
    token_url: String,
    send_url: String,
    relay_secret: Option<String>,
    token: Option<(i64, String)>,
}

impl Push {
    pub fn start(
        store: &Connection,
        database: &Path,
        account: &Path,
        relay: Option<(&str, &Path)>,
    ) -> Result<Self> {
        let mut gateway = Gateway::open(account, relay)?;
        store.execute_batch(
            "CREATE TABLE IF NOT EXISTS online_push_devices (token TEXT PRIMARY KEY, user_code TEXT NOT NULL);
             CREATE INDEX IF NOT EXISTS idx_online_push_devices_user ON online_push_devices(user_code);",
        )?;
        let db = Connection::open_with_flags(database, OpenFlags::SQLITE_OPEN_READ_WRITE)?;
        db.busy_timeout(Duration::from_secs(5))?;
        let (sender, receiver) = sync_channel::<Notification>(256);
        thread::Builder::new().name("fcm".into()).spawn(move || {
            while let Ok(message) = receiver.recv() {
                if let Err(error) = gateway.notify(&db, &message) {
                    eprintln!("FCM notification failed: {error:#}");
                }
            }
        })?;
        Ok(Self(sender))
    }

    pub fn register(store: &Connection, code: &str, request: &Message) -> Result<()> {
        let token = &request.content;
        ensure!(
            (32..=4096).contains(&token.len())
                && token
                    .bytes()
                    .all(|byte| byte.is_ascii_alphanumeric() || b":_-".contains(&byte)),
            Rejected("系统推送注册令牌格式无效")
        );
        if request.enabled {
            store.execute(
                "INSERT INTO online_push_devices (token,user_code) VALUES (?,?)
                 ON CONFLICT(token) DO UPDATE SET user_code=excluded.user_code",
                params![token, code],
            )?;
        } else {
            delete_token(store, token, code)?;
        }
        Ok(())
    }

    pub fn notify(&self, recipient: &str, sender: &str, message_id: &str) {
        if self
            .0
            .try_send(Notification {
                recipient: recipient.into(),
                sender: sender.into(),
                message_id: message_id.into(),
            })
            .is_err()
        {
            eprintln!("FCM notification queue unavailable");
        }
    }
}

impl Gateway {
    fn open(account: &Path, relay: Option<(&str, &Path)>) -> Result<Self> {
        let credentials: Credentials =
            serde_json::from_slice(&fs::read(account)?).context("解析 Firebase 服务账号 JSON")?;
        let project = credentials.project_id;
        ensure!(
            credentials.kind == "service_account"
                && !credentials.client_email.is_empty()
                && (6..=30).contains(&project.len())
                && project.as_bytes()[0].is_ascii_lowercase()
                && project
                    .bytes()
                    .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'-')
                && !project.ends_with('-'),
            "Firebase 服务账号的 type、project_id 或 client_email 无效"
        );
        let pem = rustls_pemfile::pkcs8_private_keys(&mut BufReader::new(Cursor::new(
            credentials.private_key,
        )))
        .next()
        .transpose()?
        .context("Firebase private_key 须为 PKCS#8 PEM")?;
        let key = RsaKeyPair::from_pkcs8(pem.secret_pkcs8_der())
            .map_err(|error| anyhow::anyhow!("Firebase private_key 须为 RSA：{error}"))?;
        let (token_url, send_url, relay_secret) = if let Some((origin, secret_file)) = relay {
            let uri: Uri = origin.parse().context("解析 FCM 中继地址")?;
            ensure!(
                uri.scheme_str() == Some("https")
                    && uri.host().is_some()
                    && uri
                        .authority()
                        .is_some_and(|authority| !authority.as_str().contains('@'))
                    && (uri.path().is_empty() || uri.path() == "/")
                    && uri.query().is_none()
                    && !origin.contains('#'),
                "FCM 中继地址须为 HTTPS origin"
            );
            ensure!(
                project == RELAY_PROJECT,
                "FCM 中继要求 Firebase 项目 {RELAY_PROJECT}"
            );
            let raw = fs::read_to_string(secret_file)?;
            let secret = raw.trim();
            ensure!(
                (32..=256).contains(&secret.len())
                    && secret.bytes().all(|byte| byte.is_ascii_graphic()),
                "FCM 中继密钥须为 32–256 个可见 ASCII 字符"
            );
            let origin = origin.trim_end_matches('/');
            (
                format!("{origin}/token"),
                format!("{origin}/messages:send"),
                Some(secret.into()),
            )
        } else {
            (
                TOKEN_URL.into(),
                format!("https://fcm.googleapis.com/v1/projects/{project}/messages:send"),
                None,
            )
        };
        let client = Agent::config_builder()
            .timeout_global(Some(Duration::from_secs(15)))
            .max_redirects(0)
            .http_status_as_error(false)
            .build()
            .into();
        Ok(Self {
            client,
            email: credentials.client_email,
            key,
            token_url,
            send_url,
            relay_secret,
            token: None,
        })
    }

    fn access_token(&mut self) -> Result<&str> {
        let now = Utc::now().timestamp();
        if self
            .token
            .as_ref()
            .is_none_or(|(expiry, _)| now + 30 >= *expiry)
        {
            let header = URL_SAFE_NO_PAD.encode(br#"{"alg":"RS256","typ":"JWT"}"#);
            let payload = URL_SAFE_NO_PAD.encode(serde_json::to_vec(&json!({
                "iss": self.email, "aud": TOKEN_URL, "iat": now, "exp": now + 3600,
                "scope": "https://www.googleapis.com/auth/firebase.messaging"
            }))?);
            let mut assertion = format!("{header}.{payload}");
            let mut signature = vec![0; self.key.public().modulus_len()];
            self.key
                .sign(
                    &RSA_PKCS1_SHA256,
                    &SystemRandom::new(),
                    assertion.as_bytes(),
                    &mut signature,
                )
                .map_err(|_| anyhow::anyhow!("签署 Firebase OAuth JWT 失败"))?;
            assertion.push('.');
            assertion.push_str(&URL_SAFE_NO_PAD.encode(signature));
            let mut request = self.client.post(&self.token_url);
            if let Some(secret) = &self.relay_secret {
                request = request.header("X-Relay-Secret", secret);
            }
            let mut response = request.send_form([
                ("grant_type", "urn:ietf:params:oauth:grant-type:jwt-bearer"),
                ("assertion", assertion.as_str()),
            ])?;
            ensure!(
                response.status() == StatusCode::OK,
                "FCM OAuth HTTP {}",
                response.status().as_u16()
            );
            let token: AccessToken = response
                .body_mut()
                .with_config()
                .limit(MAX_RESPONSE)
                .read_json()?;
            ensure!(
                !token.access_token.is_empty() && (1..=3600).contains(&token.expires_in),
                "FCM OAuth 响应无效"
            );
            self.token = Some((now + token.expires_in, token.access_token));
        }
        Ok(&self.token.as_ref().expect("access token refreshed").1)
    }

    fn notify(&mut self, store: &Connection, message: &Notification) -> Result<()> {
        let tokens = {
            let mut statement =
                store.prepare("SELECT token FROM online_push_devices WHERE user_code=?")?;
            statement
                .query_map([&message.recipient], |row| row.get::<_, String>(0))?
                .collect::<rusqlite::Result<Vec<_>>>()?
        };
        for token in tokens {
            match self.send(message, &token) {
                Ok(true) => delete_token(store, &token, &message.recipient)?,
                Ok(false) => {}
                Err(error) => eprintln!("FCM push failed: {error:#}"),
            }
        }
        Ok(())
    }

    fn send(&mut self, message: &Notification, token: &str) -> Result<bool> {
        let authorization = format!("Bearer {}", self.access_token()?);
        let mut request = self
            .client
            .post(&self.send_url)
            .header("Authorization", authorization);
        if let Some(secret) = &self.relay_secret {
            request = request.header("X-Relay-Secret", secret);
        }
        let mut response = request.send_json(json!({"message": {
            "token": token,
            "data": {
                "type": "direct_message", "recipient": message.recipient, "sender": message.sender,
                "message_id": message.message_id
            },
            "android": {"priority": "HIGH", "ttl": "300s"}
        }}))?;
        if response.status() == StatusCode::OK {
            return Ok(false);
        }
        if response.status() == StatusCode::NOT_FOUND {
            let result: Value = response
                .body_mut()
                .with_config()
                .limit(MAX_RESPONSE)
                .read_json()?;
            if result
                .pointer("/error/details")
                .and_then(Value::as_array)
                .is_some_and(|details| {
                    details
                        .iter()
                        .any(|detail| detail["errorCode"] == "UNREGISTERED")
                })
            {
                return Ok(true);
            }
        }
        bail!("FCM HTTP {}", response.status().as_u16())
    }
}

fn delete_token(store: &Connection, token: &str, code: &str) -> Result<()> {
    store.execute(
        "DELETE FROM online_push_devices WHERE token=? AND user_code=?",
        params![token, code],
    )?;
    Ok(())
}
