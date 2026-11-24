use std::{
    fs::{self, OpenOptions},
    io::Write,
    path::PathBuf,
    process::Command,
    sync::Arc,
    time::{SystemTime, UNIX_EPOCH},
};

use anyhow::{Context, Result, ensure};
use lan_chat_client_core::Client;
use serde_json::{Value, json};
use tauri::{AppHandle, Emitter, Manager, Url, async_runtime::Mutex};
use tauri_plugin_updater::{Update, UpdaterExt};

const EDITION: &str = if cfg!(feature = "online") {
    "online"
} else {
    "lan"
};
const TARGET: &str = if cfg!(feature = "online") {
    "windows-x86_64-online"
} else {
    "windows-x86_64-lan"
};
const INSTALLER: &str = if cfg!(feature = "online") {
    "LANChat-Online-Setup-x64.exe"
} else {
    "LANChat-Setup-x64.exe"
};
const ZIP: &str = if cfg!(feature = "online") {
    "LANChat-Online-Windows-x64.zip"
} else {
    "LANChat-Desktop-Windows-x64.zip"
};
const CHANNEL: &str = if cfg!(feature = "online") {
    "https://github.com/DUSK1NG/cross-language-lan-chatroom/releases/latest/download/latest-online.json"
} else {
    "https://github.com/DUSK1NG/cross-language-lan-chatroom/releases/latest/download/latest-lan.json"
};
const PUBLIC_KEY: Option<&str> = if cfg!(feature = "online") {
    option_env!("LANCHAT_ONLINE_UPDATER_PUBLIC_KEY")
} else {
    option_env!("LANCHAT_UPDATER_PUBLIC_KEY")
};
const MAX_INSTALLER_BYTES: u64 = 96 * 1024 * 1024;

#[derive(Default)]
pub struct Updates(Mutex<Option<Pending>>);

struct Pending {
    update: Update,
    size: u64,
    bytes: Option<Vec<u8>>,
}

fn executable_dir() -> Result<PathBuf> {
    Ok(std::env::current_exe()?
        .parent()
        .context("无法确定桌面程序目录")?
        .to_path_buf())
}

fn is_installed() -> Result<bool> {
    let path = executable_dir()?.join("lan-chat.install");
    if !path.try_exists()? {
        return Ok(false);
    }
    Ok(fs::read_to_string(path)?
        .lines()
        .any(|line| line.strip_prefix("Edition=") == Some(EDITION)))
}

fn https(url: &Url) -> Result<()> {
    ensure!(
        url.scheme() == "https"
            && url.host().is_some()
            && url.username().is_empty()
            && url.password().is_none(),
        "更新地址须为 HTTPS"
    );
    Ok(())
}

fn tls_roots() -> Result<Vec<reqwest::Certificate>> {
    #[cfg(debug_assertions)]
    if let Some(path) = option_env!("LANCHAT_UPDATER_FIXTURE_CA") {
        return Ok(vec![reqwest::Certificate::from_pem(&fs::read(path)?)?]);
    }
    Ok(Vec::new())
}

fn https_client(
    builder: reqwest::ClientBuilder,
    roots: Vec<reqwest::Certificate>,
) -> reqwest::ClientBuilder {
    builder.https_only(true).tls_certs_merge(roots)
}

#[tauri::command]
pub async fn check_desktop_update(app: AppHandle) -> Result<Option<Value>, String> {
    async {
        let state = app.state::<Updates>();
        let mut pending = state.0.lock().await;
        *pending = None;
        let key = PUBLIC_KEY
            .filter(|key| !key.trim().is_empty())
            .context("当前构建未配置签名更新公钥")?;
        let endpoint = Url::parse(option_env!("LANCHAT_UPDATER_ENDPOINT").unwrap_or(CHANNEL))?;
        https(&endpoint)?;
        let roots = tls_roots()?;
        let updater = app
            .updater_builder()
            .pubkey(key.trim().to_owned())
            .target(TARGET)
            .endpoints(vec![endpoint])?
            .configure_client(move |builder| https_client(builder, roots.clone()))
            .build()?;
        let Some(update) = updater.check().await? else {
            return Ok(None);
        };
        https(&update.download_url)?;
        ensure!(
            update.download_url.path_segments().and_then(Iterator::last) == Some(INSTALLER),
            "更新资产与当前安装版不匹配"
        );
        let size = update.raw_json["platforms"][TARGET]["size"]
            .as_u64()
            .context("更新清单缺少包大小")?;
        ensure!(
            (1..=MAX_INSTALLER_BYTES).contains(&size),
            "更新包大小超出范围"
        );
        let summary = json!({
            "version": update.version,
            "notes": update.body,
            "date": update.raw_json["pub_date"],
            "size": size,
            "installed": is_installed()?,
            "portableUrl": update.download_url.join(ZIP)?,
        });
        *pending = Some(Pending {
            update,
            size,
            bytes: None,
        });
        Ok(Some(summary))
    }
    .await
    .map_err(|error: anyhow::Error| format!("{error:#}"))
}

#[tauri::command]
pub async fn download_desktop_update(app: AppHandle, version: String) -> Result<(), String> {
    async {
        ensure!(is_installed()?, "便携版请下载新版 ZIP");
        let state = app.state::<Updates>();
        let mut pending = state.0.lock().await;
        let pending = pending.as_mut().context("请先检查更新")?;
        ensure!(pending.update.version == version, "待下载版本已变更");
        pending.bytes = None;
        let mut response = https_client(reqwest::Client::builder(), tls_roots()?)
            .build()?
            .get(pending.update.download_url.as_str())
            .send()
            .await?
            .error_for_status()?;
        if let Some(length) = response.content_length() {
            ensure!(length == pending.size, "更新包大小与清单不一致");
        }
        let mut bytes = Vec::with_capacity(pending.size as usize);
        while let Some(chunk) = response.chunk().await? {
            ensure!(
                bytes.len() + chunk.len() <= pending.size as usize,
                "更新包超过清单大小"
            );
            bytes.extend_from_slice(&chunk);
            app.emit("desktop-update:progress", bytes.len())?;
        }
        ensure!(bytes.len() as u64 == pending.size, "更新包大小与清单不一致");
        pending.update.verify(&bytes)?;
        pending.bytes = Some(bytes);
        Ok(())
    }
    .await
    .map_err(|error: anyhow::Error| format!("{error:#}"))
}

#[tauri::command]
pub async fn install_desktop_update(app: AppHandle, version: String) -> Result<(), String> {
    async {
        ensure!(is_installed()?, "便携版请下载新版 ZIP");
        let state = app.state::<Updates>();
        let pending = state.0.lock().await;
        let pending = pending.as_ref().context("请先检查并下载更新")?;
        ensure!(pending.update.version == version, "待安装版本已变更");
        let bytes = pending.bytes.as_ref().context("更新包尚未下载完成")?;
        pending.update.verify(bytes)?;
        let sequence = SystemTime::now().duration_since(UNIX_EPOCH)?.as_nanos();
        let dir = app
            .path()
            .temp_dir()?
            .join(format!("LANChat-{}-{sequence}", std::process::id()));
        fs::create_dir(&dir)?;
        let installer = dir.join(INSTALLER);
        let mut file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&installer)?;
        file.write_all(bytes)?;
        file.sync_all()?;
        drop(file);

        let mut directory = std::ffi::OsString::from("/DIR=");
        directory.push(executable_dir()?);
        app.state::<Arc<Client>>().close().await?;
        Command::new(installer)
            .args(["/SP-", "/SILENT", "/NORESTART", "/LANCHATUPDATE=1"])
            .arg(directory)
            .spawn()
            .context("启动更新安装器失败")?;
        app.exit(0);
        Ok(())
    }
    .await
    .map_err(|error: anyhow::Error| format!("{error:#}"))
}
