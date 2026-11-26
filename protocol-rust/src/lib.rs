mod message;

use std::io;

use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

pub use message::*;

pub const MAX_FRAME_BYTES: usize = 64 * 1024;
pub const MAX_COMMAND_ID_BYTES: usize = 64;
pub const LOCAL_HOST_PASSWORD_MAX_BYTES: usize = 72;
pub const ROOM_PASSWORD_MAX_BYTES: usize = 72;
pub const ROOM_PASSWORD: &str = "room-password-v1";
pub const LAN_HISTORY_PAGES: &str = "lan-history-pages-v1";
pub const LAN_ROOM_ROLES: &str = "lan-room-roles-v1";
pub const ACCOUNT_PASSWORD_MIN_BYTES: usize = 8;
pub const ACCOUNT_PASSWORD_MAX_BYTES: usize = 72;
pub const ONLINE_ACCOUNTS: &str = "online-accounts-v1";
pub const ONLINE_MESSAGE_EDIT: &str = "online-message-edit-v1";
pub const ONLINE_SOCIAL: &str = "online-social-v1";
pub const ONLINE_BOT_SETTINGS: &str = "online-bot-settings-v1";
pub const ONLINE_BOT_KEY_SYNC: &str = "online-bot-key-sync-v1";
pub const ONLINE_GROUPS: &str = "online-groups-v1";
pub const ONLINE_GROUP_MANAGEMENT: &str = "online-group-management-v1";
pub const ONLINE_GROUP_ATTACHMENTS: &str = "online-group-attachments-v1";
pub const ONLINE_HISTORY_PAGES: &str = "online-history-pages-v1";
pub const ONLINE_IMAGES: &str = "online-images-v1";
pub const ONLINE_FILES: &str = "online-files-v1";
pub const ONLINE_CHUNK_BYTES: i64 = 40 * 1024;
pub const MAX_ONLINE_IMAGE_BYTES: i64 = 4 * 1024 * 1024;
pub const MAX_ONLINE_FILE_BYTES: i64 = 32 * 1024 * 1024;
pub const ATTACHMENT_CHUNK_BYTES: i64 = 47 * 1024;
pub const ATTACHMENT_CIPHER_OVERHEAD: i64 = 64;
pub const ATTACHMENT_TAG_BYTES: i64 = 16;
pub const MAX_ATTACHMENT_BYTES: i64 = 5 * 1024 * 1024 * 1024;
pub const MAX_ROOM_ATTACHMENT_BYTES: i64 = 20 * 1024 * 1024 * 1024;
pub const RECALLED_MESSAGE: &str = "消息已撤回";

/// 应用层心跳能力。客户端在登录消息的 `capabilities` 中声明，服务端在
/// `login_ok.capabilities` 中回显后才启用；未协商的连接行为完全不变。
///
/// 启用后客户端每 [`HEARTBEAT_INTERVAL_SECS`] 秒发送 `{"type":"ping"}`，
/// 服务端立即回复 `{"type":"pong"}`。任一方超过 [`HEARTBEAT_TIMEOUT_SECS`]
/// 秒未收到任何入站帧即判定连接已断开。ping/pong 不计入限流，也不写库。
pub const HEARTBEAT: &str = "heartbeat-v1";
pub const HEARTBEAT_INTERVAL_SECS: u64 = 20;
pub const HEARTBEAT_TIMEOUT_SECS: u64 = 60;

// 超时必须覆盖至少两个心跳周期，避免单个丢失的 ping 误判断线。
const _: () = assert!(HEARTBEAT_INTERVAL_SECS * 2 < HEARTBEAT_TIMEOUT_SECS);

fn check_frame_length(length: usize, kind: io::ErrorKind) -> io::Result<()> {
    if !(1..=MAX_FRAME_BYTES).contains(&length) {
        return Err(io::Error::new(
            kind,
            format!("消息帧长度须在 1–{MAX_FRAME_BYTES} 字节，实际为 {length}"),
        ));
    }
    Ok(())
}

pub async fn read_frame(reader: &mut (impl AsyncRead + Unpin)) -> io::Result<Vec<u8>> {
    let mut header = [0; 4];
    if reader.read(&mut header[..1]).await? == 0 {
        return Err(io::ErrorKind::UnexpectedEof.into());
    }
    reader
        .read_exact(&mut header[1..])
        .await
        .map_err(|error| incomplete_frame("消息帧头", error))?;
    let length = u32::from_be_bytes(header) as usize;
    check_frame_length(length, io::ErrorKind::InvalidData)?;
    let mut payload = vec![0; length];
    reader
        .read_exact(&mut payload)
        .await
        .map_err(|error| incomplete_frame("消息载荷", error))?;
    Ok(payload)
}

fn incomplete_frame(field: &str, error: io::Error) -> io::Error {
    if error.kind() == io::ErrorKind::UnexpectedEof {
        return io::Error::new(io::ErrorKind::InvalidData, format!("{field}未读完"));
    }
    error
}

pub async fn read_message(reader: &mut (impl AsyncRead + Unpin)) -> io::Result<Message> {
    decode_message(&read_frame(reader).await?)
}

fn decode_message(payload: &[u8]) -> io::Result<Message> {
    let message: Message = serde_json::from_slice(payload)
        .map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))?;
    if message.kind.is_empty() {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "消息缺少 type"));
    }
    Ok(message)
}

pub async fn read_lan_command(reader: &mut (impl AsyncRead + Unpin)) -> io::Result<Message> {
    let payload = read_frame(reader).await?;
    let message = decode_message(&payload)?;
    let fields: &[&str] = match message.kind.as_str() {
        "attachment.init" => &["room", "logical_size"],
        "attachment.chunk" => &["upload_id", "chunk_index", "ciphertext", "cipher_sha256"],
        "attachment.resume" | "attachment.commit" => &["upload_id"],
        "attachment.download" => &["attachment_id", "chunk_index"],
        _ => return Ok(message),
    };
    let object: serde_json::Map<String, serde_json::Value> = serde_json::from_slice(&payload)
        .map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))?;
    if let Some(name) = object.keys().find(|name| {
        !matches!(name.as_str(), "type" | "command_id") && !fields.contains(&name.as_str())
    }) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("{} 包含不支持的字段 {name}", message.kind),
        ));
    }
    Ok(message)
}

pub async fn write_message(
    writer: &mut (impl AsyncWrite + Unpin),
    message: &Message,
) -> io::Result<()> {
    let payload = serde_json::to_vec(message).map_err(io::Error::other)?;
    write_frame(writer, &payload).await
}

pub async fn write_frame(writer: &mut (impl AsyncWrite + Unpin), payload: &[u8]) -> io::Result<()> {
    check_frame_length(payload.len(), io::ErrorKind::InvalidInput)?;
    writer.write_u32(payload.len() as u32).await?;
    writer.write_all(payload).await?;
    writer.flush().await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn round_trips_go_wire_fields_and_explicit_chunk_zero() {
        let payload = r#"{"type":"dm_image_chunk","message_id":"image-1","image_data":"AAE=","created_at":"2026-11-21T04:10:00Z","chunk_index":0}"#.as_bytes();
        let mut frame = Vec::new();
        write_frame(&mut frame, payload).await.unwrap();
        assert_eq!(&frame[..4], &(payload.len() as u32).to_be_bytes());
        let message = read_message(&mut frame.as_slice()).await.unwrap();
        assert_eq!(message.chunk_index, Some(0));
        assert_eq!(message.image_data, "AAE=");
        assert_eq!(message.created_at, "2026-11-21T04:10:00Z");
        let mut encoded = Vec::new();
        write_message(&mut encoded, &message).await.unwrap();
        let value: serde_json::Value = serde_json::from_slice(&encoded[4..]).unwrap();
        assert_eq!(value["chunk_index"], 0);
    }

    #[tokio::test]
    async fn heartbeat_frames_are_minimal_and_pass_lan_command_validation() {
        for kind in ["ping", "pong"] {
            let message = Message {
                kind: kind.into(),
                ..Message::default()
            };
            let mut frame = Vec::new();
            write_message(&mut frame, &message).await.unwrap();
            assert_eq!(&frame[4..], format!(r#"{{"type":"{kind}"}}"#).as_bytes());
            assert_eq!(
                read_lan_command(&mut frame.as_slice()).await.unwrap().kind,
                kind
            );
        }
    }

    #[tokio::test]
    async fn rejects_oversize_empty_and_truncated_frames() {
        for bytes in [&[0, 1, 0, 1][..], &[0, 0, 0, 0], &[0, 0], &[0, 0, 0, 2, 1]] {
            assert_eq!(
                read_frame(&mut &bytes[..]).await.unwrap_err().kind(),
                io::ErrorKind::InvalidData
            );
        }
    }
}
