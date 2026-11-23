use serde::{Deserialize, Serialize};
use serde_json::Value;

#[derive(Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LocalHostCredentials {
    pub password: String,
    pub session_token: String,
}

#[derive(Clone, Default, Deserialize, Serialize)]
#[serde(default)]
pub struct OnlineUser {
    pub username: String,
    pub user_code: String,
    pub room: String,
    pub is_admin: bool,
}

#[derive(Clone, Default, Deserialize, Serialize)]
#[serde(default)]
pub struct OnlineProfile {
    pub user_code: String,
    pub display_name: String,
    pub bio: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub friendship: String,
}

#[derive(Clone, Default, Deserialize, Serialize)]
#[serde(default)]
pub struct OnlineGroup {
    pub id: String,
    pub name: String,
    pub member_count: i64,
}

#[derive(Clone, Default, Deserialize, Serialize)]
#[serde(default)]
pub struct RoomInfo {
    pub name: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub owner_code: String,
    pub private: bool,
    pub can_manage: bool,
    #[serde(default, skip_serializing_if = "is_false")]
    pub password_required: bool,
}

#[derive(Clone, Default, Deserialize, Serialize)]
pub struct Message {
    #[serde(rename = "type")]
    pub kind: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub message_id: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub command_id: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub username: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub user_code: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub account_password: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub host_session_token: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub target_user_code: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub room: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub room_password: String,
    #[serde(default, skip_serializing_if = "is_false")]
    pub private: bool,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub users: Vec<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub rooms: Vec<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub user_details: Vec<OnlineUser>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub profile: Option<OnlineProfile>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub profiles: Vec<OnlineProfile>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub groups: Vec<OnlineGroup>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub room_details: Vec<RoomInfo>,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub content: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub delivery_state: String,
    #[serde(default, skip_serializing_if = "is_zero")]
    pub image_size: i64,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub image_data: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub file_name: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub file_data: String,
    #[serde(default, skip_serializing_if = "is_false")]
    pub enabled: bool,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub created_at: String,
    #[serde(default, skip_serializing_if = "is_zero")]
    pub revision: i64,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub before_message_id: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub search_query: String,
    #[serde(default, skip_serializing_if = "is_zero")]
    pub limit: i64,
    #[serde(default, skip_serializing_if = "is_false")]
    pub has_more: bool,
    #[serde(default, skip_serializing_if = "is_false")]
    pub recalled: bool,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub messages: Vec<Message>,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub protocol_version: String,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub capabilities: Vec<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub crypto: Option<Value>,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub group_id: String,
    #[serde(default, skip_serializing_if = "is_zero_epoch")]
    pub epoch: u64,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub proposal_id: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub action: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub key_package: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub proposal: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub commit: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub welcome: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub welcome_digest: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub attachment_id: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub upload_id: String,
    #[serde(default, skip_serializing_if = "is_zero")]
    pub logical_size: i64,
    #[serde(default, skip_serializing_if = "is_zero")]
    pub chunk_size: i64,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub expires_at: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub chunk_index: Option<i64>,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub ciphertext: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub cipher_sha256: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub whole_sha256: String,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub received_indexes: Vec<i64>,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub received_bitmap: String,
    #[serde(default, skip_serializing_if = "is_false")]
    pub is_admin: bool,
}

fn is_false(value: &bool) -> bool {
    !value
}

fn is_zero(value: &i64) -> bool {
    *value == 0
}

fn is_zero_epoch(value: &u64) -> bool {
    *value == 0
}

pub fn is_legacy_generated_message_id(id: &str) -> bool {
    id.bytes().all(|byte| byte.is_ascii_digit()) && id.parse::<u64>().is_ok()
}
