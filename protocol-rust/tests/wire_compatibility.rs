use lan_chat_protocol::{
    LocalHostCredentials, Message, is_legacy_generated_message_id, read_message, write_message,
};
use serde_json::Value;

#[tokio::test]
async fn round_trips_legacy_wire_samples() {
    let credentials = include_str!("golden/host-credentials.json");
    let expected: Value = serde_json::from_str(credentials).unwrap();
    let credentials: LocalHostCredentials = serde_json::from_str(credentials).unwrap();
    assert_eq!(serde_json::to_value(credentials).unwrap(), expected);

    for fixture in [
        include_str!("golden/lan.json"),
        include_str!("golden/mls-and-attachments.json"),
        include_str!("golden/online.json"),
    ] {
        let messages: Vec<Value> = serde_json::from_str(fixture).unwrap();
        for expected in messages {
            let message: Message = serde_json::from_value(expected.clone()).unwrap();
            let mut frame = Vec::new();
            write_message(&mut frame, &message).await.unwrap();
            assert_eq!(&frame[..4], &((frame.len() - 4) as u32).to_be_bytes());
            let decoded = read_message(&mut frame.as_slice()).await.unwrap();
            assert_eq!(
                serde_json::to_value(decoded).unwrap(),
                expected,
                "{}",
                message.kind
            );
        }
    }
}

#[test]
fn legacy_generated_ids_are_unsigned_decimal_u64s() {
    for id in ["0", "007", "18446744073709551615"] {
        assert!(is_legacy_generated_message_id(id), "{id}");
    }
    for id in ["", "+1", "-1", "１", "18446744073709551616", "id-1"] {
        assert!(!is_legacy_generated_message_id(id), "{id}");
    }
}
