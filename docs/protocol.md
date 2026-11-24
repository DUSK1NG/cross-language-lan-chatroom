# 跨语言通信协议

## 1. 传输层

- 传输协议：TCP
- 服务端监听：`0.0.0.0:8888`
- 安全层：TLS 1.2 或更高版本
- 字符编码：UTF-8
- 消息内容只作为文本数据处理，不执行命令或脚本

TLS 只负责加密 TCP 字节流，不改变应用层消息格式。

## 2. 消息帧

```text
+----------------------+---------------------------+
| 4-byte length        | UTF-8 JSON payload      |
| unsigned, big-endian |                           |
+----------------------+---------------------------+
```

长度字段表示 JSON Payload 的字节数：

```text
1 <= length <= 65536
```

它不是 Unicode 字符数量。长度按 4 字节大端序读写：Go 参考实现使用 `binary.BigEndian`，C++ 历史实现使用 `htonl()` 和 `ntohl()`，Rust 实现使用 `u32::to_be_bytes()`。

TCP 不保证一次 `send()` 对应一次 `recv()`，因此双方必须循环读取完整的 4 字节长度头和完整 Payload。

## 3. 登录

客户端发送：

```json
{
  "type": "login",
  "username": "Bob",
  "user_code": "B001"
}
```

成功：

```json
{
  "type": "login_ok",
  "username": "Bob",
  "user_code": "B001",
  "content": "Login successful"
}
```

失败：

```json
{
  "type": "login_error",
  "content": "User code already exists"
}
```

用户名可以重复，用户代码由服务端按不区分大小写的规则保证唯一。

LAN 客户端和服务端在登录时协商 `heartbeat-v1`。服务端回显该能力后，客户端每 20 秒发送 `{"type":"ping"}`，服务端回复 `{"type":"pong"}`。连续 60 秒无入站帧的连接会关闭。心跳沿用帧格式，业务命令配额保持独立。旧连接沿用原空闲时限，访客重连仍需房主审批。

## 4. 群聊

客户端发送：

```json
{
  "type": "chat",
  "content": "你好，这是中文消息。",
  "room": "lobby"
}
```

服务端广播：

```json
{
  "type": "chat",
  "username": "Alice",
  "user_code": "A001",
  "room": "lobby",
  "content": "你好，这是中文消息。"
}
```

发送者身份由服务端根据当前 TCP 连接绑定，客户端不能伪造。

## 5. 私聊

```json
{
  "type": "private_chat",
  "target_user_code": "B001",
  "content": "你好"
}
```

私聊只发送给发送者和目标用户，不受频道限制。目标代码不存在、目标是自己或消息内容非法时，服务端只向发送者返回 `error`。

## 6. 频道、权限和在线成员

```json
{"type":"room_join","room":"study_group"}
{"type":"room_create","room":"private_group","private":true}
{"type":"room_leave"}
{"type":"rooms_request"}
{"type":"users_request"}
```

频道只能由 `room_create` 创建；`room_join` 只会加入已存在的频道。私有频道仅管理员、频道创建者和被邀请的成员可见、可加入。

协商 `lan-room-roles-v1` 后，`room_action` 支持 `grant_admin`、`revoke_admin` 和 `set_password`。角色命令使用 `target_user_code`，密码命令使用 `room_password`，空密码清除私密频道的密码。服务端提交修改后用原 `command_id` 确认。移除成员和删除频道时，受影响客户端收到切回大厅的 `room_join` 通知。房主的连接审批与全局禁言权限独立于频道管理员。

协商 `lan-history-pages-v1` 后，桌面搜索用 `history_request` 的 `before_message_id` 请求更早一页，用 `history_response.has_more` 与回显的 `command_id` 更新当前查询。LAN 搜索按原会话权限过滤，短关键词使用 `instr`，长关键词使用 FTS5 字面匹配，已撤回内容从搜索结果中排除。普通历史仍保留撤回占位。

Online 历史用 `online-history-pages-v1` 协商搜索分页。`group_history` 使用 `group_id`，支持同样的搜索、游标和 `has_more`。新入群成员只收到加入之后的群历史，退出或移除即撤销后续访问。

Online 群管理协商 `online-group-management-v1`。`group_invite`、`group_kick`、`group_role` 和 `group_transfer` 使用 `group_id` 与 `target_user_code`。`group_role` 的 `content` 为 `admin` 或 `member`。`group_update` 和 `group_announce` 的 `content` 分别为群名和公告。`group_list` 返回当前角色、邀请人和 `joined_after`，`group_detail` 还返回公告和成员角色。

Online 群附件协商 `online-group-attachments-v1`。`dm_file_init`、`dm_image_chunk` 和 `dm_image_get` 附带 `group_id`。文件按 `attachment_id` 下载，服务端对每个分块核对成员权限与历史边界。`dm_file_cancel` 取消发送者尚未发布的上传。附件上限为图片 4 MiB、文件 32 MiB。旧客户端的群附件消息显示为文本。群 FCM 路由包含 `recipient`、`groupId`、`senderUserCode` 与 `messageId`。

客户端可以在 `login.capabilities` 中声明 `room-password-v1`。服务端在 `login_ok.capabilities` 中确认后，私有频道的创建和加入请求可携带 `room_password`，密码留空表示创建普通私有频道。密码最多 72 个 UTF-8 字节。服务端通过 `room_details[].password_required` 提示当前用户需要密码，创建者可直接进入。携带 `command_id` 的创建或加入请求会收到同类型确认，失败时返回对应的 `error`。

频道创建者或全局管理员可发送：

```json
{"type":"room_action","content":"invite","room":"private_group","target_user_code":"B001"}
{"type":"room_action","content":"remove_member","room":"private_group","target_user_code":"B001"}
{"type":"room_action","content":"delete","room":"private_group"}
```

服务端返回：

```json
{
  "type": "rooms_response",
  "rooms": ["lobby", "study_group"],
  "room": "study_group",
  "room_details": [
    {"name":"lobby","private":false,"can_manage":false},
    {"name":"study_group","owner_code":"A001","private":true,"can_manage":true}
  ]
}
```

```json
{
  "type": "users_response",
  "users": ["Alice#A001@lobby", "Bob#B001@study_group"],
  "user_details": [
    {"username":"Alice","user_code":"A001","room":"lobby","is_admin":true}
  ]
}
```

客户端连接时请求成员和频道列表，收到在线状态变化事件时刷新成员列表。收到非当前会话消息时，在对应频道或私聊列表显示未读数量。

## 7. 系统提示

```json
{
  "type": "system",
  "room": "lobby",
  "content": "Bob#B001 joined the chat"
}
```

系统提示带有所属房间，客户端只将其显示在对应频道，不显示在私聊窗口。

## 8. 错误和断线

以下情况会拒绝当前消息或关闭当前连接：

- 长度头不足 4 字节
- 长度为 0 或超过 64 KiB
- Payload 不是合法 UTF-8
- JSON 格式错误
- 缺少必要字段
- 用户代码重复
- 客户端异常断开

单个客户端的异常不能使服务端崩溃，也不能影响其他客户端。
