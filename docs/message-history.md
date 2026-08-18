# 历史消息记录

客户端登录后输入：

```text
/history
/history 50
```

服务端返回当前房间最近的群聊消息，默认 20 条，最多 100 条，并按发送时间从旧到新显示：

```text
[History] Alice#A001: 大家好
[History] Bob#B001: 你好
[History] History loaded
```

历史记录存储在服务端的 `chat.db` 中，表名为 `message_history`，按房间隔离。用户切换房间后查询的是新房间的历史。

当前版本只记录群聊历史；私聊使用离线消息机制，不会通过 `/history` 暴露给其他用户。
