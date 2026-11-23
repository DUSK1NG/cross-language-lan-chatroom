use std::{
    path::{Path, PathBuf},
    sync::{Arc, Mutex},
    time::Duration,
};

use anyhow::Result;
use lan_chat_client_core::{Client, Edition, Pick, Ui};
use serde_json::{Value, json};
use uuid::Uuid;

struct Desktop {
    selected: PathBuf,
    download: PathBuf,
    effects: Mutex<Vec<Value>>,
}

impl Desktop {
    fn new(dir: &Path) -> Arc<Self> {
        Arc::new(Self {
            selected: dir.join("input.bin"),
            download: dir.join("output.bin"),
            effects: Mutex::default(),
        })
    }
}

impl Ui for Desktop {
    fn emit(&self, event: &str, payload: Value) {
        if event == "chat:error" || event == "chat:result" && payload["ok"] == false {
            self.effects.lock().unwrap().push(payload);
        }
    }

    fn choose(&self, _: Pick) -> Result<Vec<PathBuf>> {
        Ok(vec![self.selected.clone()])
    }

    fn save(&self, _: &str) -> Result<Option<PathBuf>> {
        Ok(Some(self.download.clone()))
    }

    fn copy(&self, _: &str) -> Result<()> {
        Ok(())
    }
}

async fn dispatch(client: &Arc<Client>, kind: &str, payload: Value) -> Result<()> {
    client
        .dispatch(
            &json!({"id":Uuid::new_v4().to_string(),"type":kind,"payload":payload}).to_string(),
        )
        .await
}

async fn await_state(client: &Client, ui: &Desktop, expected: impl Fn(&Value) -> bool) -> Value {
    tokio::time::timeout(Duration::from_secs(15), async {
        loop {
            let state = client.state();
            assert_ne!(
                state["connection"]["phase"], "error",
                "{}",
                state["connection"]
            );
            {
                let effects = ui.effects.lock().unwrap();
                assert!(effects.is_empty(), "{effects:?}");
            }
            if expected(&state) {
                return state;
            }
            tokio::time::sleep(Duration::from_millis(30)).await;
        }
    })
    .await
    .unwrap_or_else(|_| panic!("desktop state was not updated: {}", client.state()))
}

#[tokio::test]
async fn lan_desktop_hosts_approves_and_transfers_encrypted_attachment() -> Result<()> {
    let tmp = std::env::temp_dir().join(format!("lan-chat-lan-test-{}", Uuid::new_v4()));
    let server = PathBuf::from(env!("CARGO_BIN_EXE_lan-chat-server"));
    let owner_dir = tmp.join("owner");
    let guest_dir = tmp.join("guest");
    let host_dir = owner_dir.join("host");
    let owner_ui = Desktop::new(&owner_dir);
    let guest_ui = Desktop::new(&guest_dir);
    let owner = Arc::new(Client::new(
        Edition::Lan,
        owner_dir,
        host_dir.clone(),
        server,
        owner_ui.clone(),
    )?);
    let guest = Arc::new(Client::new(
        Edition::Lan,
        guest_dir.clone(),
        guest_dir.join("host"),
        PathBuf::new(),
        guest_ui.clone(),
    )?);
    let contents: Vec<_> = (0..100_000).map(|index| (index % 251) as u8).collect();
    std::fs::write(&owner_ui.selected, &contents)?;
    dispatch(
        &owner,
        "session.connectLocalHost",
        json!({"username":"Owner","userCode":"Owner1","password":"fixture-password"}),
    )
    .await?;
    await_state(&owner, &owner_ui, |state| {
        state["identity"]["admin"] == true
    })
    .await;

    dispatch(
        &guest,
        "session.connectRemote",
        json!({"username":"Guest","userCode":"Guest1",
        "serverIp":"127.0.0.1","serverPort":8888,"tlsServerName":"localhost",
        "caFile":host_dir.join("certs/server-lan.crt")}),
    )
    .await?;
    let state = await_state(&owner, &owner_ui, |state| {
        state["connectionApprovals"][0]["id"].is_string()
    })
    .await;
    assert_eq!(guest.state()["connection"]["phase"], "connecting");
    dispatch(
        &owner,
        "admin.action",
        json!({"action":"approve_connection",
        "targetUserCode":"Guest1","messageId":state["connectionApprovals"][0]["id"]}),
    )
    .await?;
    await_state(&guest, &guest_ui, |state| {
        state["connection"]["phase"] == "connected"
    })
    .await;

    dispatch(
        &owner,
        "room.create",
        json!({"room":"migration","isPrivate":true,"roomPassword":"fixture-room"}),
    )
    .await?;
    await_state(&owner, &owner_ui, |state| {
        state["navigation"]["activeConversation"]["id"] == "migration"
    })
    .await;
    dispatch(&owner, "directory.refreshRooms", json!({})).await?;
    dispatch(
        &owner,
        "room.action",
        json!({"room":"migration","action":"invite","targetUserCode":"Guest1"}),
    )
    .await?;
    await_state(&guest, &guest_ui, |state| {
        state["rooms"]
            .as_array()
            .unwrap()
            .iter()
            .any(|room| room["roomName"] == "migration")
    })
    .await;
    dispatch(
        &guest,
        "conversation.selectRoom",
        json!({"room":"migration","roomPassword":"fixture-room"}),
    )
    .await?;
    await_state(&owner, &owner_ui, |state| {
        state["rooms"]
            .as_array()
            .unwrap()
            .iter()
            .any(|room| room["roomName"] == "migration" && room["memberCount"] == 2)
    })
    .await;
    dispatch(
        &guest,
        "chat.sendRoom",
        json!({"room":"migration","content":"Rust LAN bridge fixture"}),
    )
    .await?;
    await_state(&owner, &owner_ui, |state| {
        state["activeMessages"]
            .as_array()
            .unwrap()
            .iter()
            .any(|message| message["content"] == "Rust LAN bridge fixture")
    })
    .await;

    dispatch(
        &owner,
        "attachment.chooseUpload",
        json!({"room":"migration"}),
    )
    .await?;
    let state = await_state(&guest, &guest_ui, |state| {
        state["activeMessages"]
            .as_array()
            .unwrap()
            .iter()
            .any(|message| message["attachment"]["fileName"] == "input.bin")
    })
    .await;
    let attachment = state["activeMessages"]
        .as_array()
        .unwrap()
        .iter()
        .find(|message| message["attachment"]["fileName"] == "input.bin")
        .unwrap();
    dispatch(
        &guest,
        "attachment.download",
        json!({"attachmentId":attachment["attachment"]["attachmentId"]}),
    )
    .await?;
    assert_eq!(std::fs::read(&guest_ui.download)?, contents);
    guest.close().await?;
    dispatch(&owner, "session.disconnect", json!({})).await?;
    std::fs::remove_dir_all(tmp)?;
    Ok(())
}
