#![cfg_attr(
    all(not(debug_assertions), target_os = "windows"),
    windows_subsystem = "windows"
)]

use std::{path::PathBuf, sync::Arc};

use anyhow::{Result, ensure};
use lan_chat_client_core::{Client, Edition, Pick, Ui};
use serde_json::Value;
use tauri::{AppHandle, Emitter, Manager, State, Url};
use tauri_plugin_clipboard_manager::ClipboardExt;
use tauri_plugin_dialog::DialogExt;
use tauri_plugin_opener::OpenerExt;

struct Desktop(AppHandle);

impl Ui for Desktop {
    fn emit(&self, event: &str, payload: Value) {
        if let Err(error) = self.0.emit(event, payload) {
            eprintln!("发送窗口事件 {event} 失败: {error}");
        }
    }

    fn choose(&self, kind: Pick) -> Result<Vec<PathBuf>> {
        let dialog = self.0.dialog().file();
        if matches!(kind, Pick::OnlineFiles) {
            let paths = dialog.set_title("选择要发送的文件").blocking_pick_files();
            return paths
                .unwrap_or_default()
                .into_iter()
                .map(|path| Ok(path.into_path()?))
                .collect();
        }
        let dialog = match kind {
            Pick::Certificate => dialog
                .set_title("选择公开证书")
                .add_filter("公开证书", &["crt", "pem", "cer"]),
            Pick::Attachment => dialog.set_title("选择要加密发送的文件"),
            Pick::Image => dialog
                .set_title("选择图片")
                .add_filter("图片", &["jpg", "jpeg", "png"]),
            Pick::OnlineFiles => unreachable!(),
        };
        dialog
            .blocking_pick_file()
            .into_iter()
            .map(|path| Ok(path.into_path()?))
            .collect()
    }

    fn save(&self, filename: &str) -> Result<Option<PathBuf>> {
        Ok(self
            .0
            .dialog()
            .file()
            .set_title("保存附件")
            .set_file_name(filename)
            .blocking_save_file()
            .map(|path| path.into_path())
            .transpose()?)
    }

    fn copy(&self, text: &str) -> Result<()> {
        self.0.clipboard().write_text(text)?;
        Ok(())
    }
}

#[tauri::command]
fn chat_state(client: State<'_, Arc<Client>>) -> Value {
    client.state()
}

#[tauri::command]
async fn chat_dispatch(client: State<'_, Arc<Client>>, json: String) -> Result<(), String> {
    client
        .inner()
        .dispatch(&json)
        .await
        .map_err(|error| format!("{error:#}"))
}

#[tauri::command]
fn chat_open_url(app: AppHandle, url: String) -> Result<(), String> {
    let url = Url::parse(&url).map_err(|error| error.to_string())?;
    ensure_web_url(&url).map_err(|error| error.to_string())?;
    app.opener()
        .open_url(url.as_str(), None::<&str>)
        .map_err(|error| error.to_string())
}

fn ensure_web_url(url: &Url) -> Result<()> {
    ensure!(
        matches!(url.scheme(), "http" | "https") && url.host().is_some(),
        "仅允许打开 HTTP(S) 链接"
    );
    Ok(())
}

fn main() {
    let edition = if cfg!(feature = "online") {
        Edition::Online
    } else {
        Edition::Lan
    };
    tauri::Builder::default()
        .plugin(tauri_plugin_clipboard_manager::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![
            chat_state,
            chat_dispatch,
            chat_open_url
        ])
        .setup(move |app| {
            let name = if edition == Edition::Online {
                "LAN Chat Online"
            } else {
                "LAN Chat"
            };
            let base = if cfg!(windows) {
                std::env::var_os("LOCALAPPDATA")
                    .map(PathBuf::from)
                    .ok_or_else(|| anyhow::anyhow!("LOCALAPPDATA 未设置"))?
            } else {
                app.path().local_data_dir()?
            };
            let data = base.join("DUSK1NG").join(name);
            let host = data.join("host");
            let executable = std::env::current_exe()?;
            let directory = executable.parent().expect("executable directory");
            let server = directory.join(if cfg!(windows) {
                "lan-chat-server.exe"
            } else {
                "lan-chat-server"
            });
            app.manage(Arc::new(Client::new(
                edition,
                data,
                host,
                server,
                Arc::new(Desktop(app.handle().clone())),
            )?));
            let window = app
                .get_webview_window("main")
                .expect("main window declared in Tauri configuration");
            window.set_title(name)?;
            window.show()?;
            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("构建 LAN Chat 桌面应用")
        .run(|app, event| {
            if matches!(event, tauri::RunEvent::Exit)
                && let Err(error) =
                    tauri::async_runtime::block_on(app.state::<Arc<Client>>().close())
            {
                eprintln!("关闭 LAN Chat 会话失败: {error:#}");
            }
        });
}
