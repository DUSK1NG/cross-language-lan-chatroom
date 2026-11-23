# 自定义用户代码身份设计

## 1. 目标

为聊天室增加“显示名 + 自定义身份代码”模型：

- 显示名 `username` 可以重复；
- 身份代码 `user_code` 由用户自定义；
- 身份代码只允许英文字母和数字，长度为 3～16 位；
- 身份代码比较时不区分大小写；
- 同一个 Go 服务进程运行期间，身份代码一旦被使用就永久占用；
- 用户退出后，其他用户仍不能复用该代码；
- 服务重启后，已使用代码记录清空；
- 聊天显示格式为 `username#user_code`。

本设计不引入数据库，因此“永久占用”范围是当前服务器进程生命周期，不包含服务器重启后的持久化唯一性。

## 2. 示例

两个用户可以使用相同显示名：

```text
Alex#A001
Alex#B002
```

聊天消息显示为：

```text
[14:30:20] Alex#A001: 大家好
[14:30:25] Alex#B002: 你好
```

`Alex2026` 和 `alex2026` 视为同一个身份代码，但显示时保留用户原始输入的大小写。

## 3. 协议设计

### 3.1 登录请求

客户端发送：

```json
{
  "type": "login",
  "username": "Alex",
  "user_code": "Alex2026"
}
```

服务端校验：

1. `username` 不为空；
2. `username` 是合法 UTF-8；
3. `username` 最多 32 个 UTF-8 字节；
4. `user_code` 长度为 3～16 位；
5. `user_code` 的每个字符都属于 ASCII 范围的 `A-Z`、`a-z` 或 `0-9`；
6. 将 `user_code` 转为小写后检查唯一性。

显示名不参与唯一性检查，因此不同客户端可以使用同一个 `username`。

### 3.2 登录结果

成功：

```json
{
  "type": "login_ok",
  "username": "Alex",
  "user_code": "Alex2026",
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

### 3.3 聊天消息

客户端发送：

```json
{
  "type": "chat",
  "content": "大家好"
}
```

服务端广播：

```json
{
  "type": "chat",
  "username": "Alex",
  "user_code": "Alex2026",
  "content": "大家好"
}
```

服务端忽略客户端在聊天消息中携带的 `username` 和 `user_code`，始终使用当前 TCP 连接登录时绑定的身份。

### 3.4 上线和离线消息

上线：

```json
{
  "type": "system",
  "username": "Alex",
  "user_code": "Alex2026",
  "content": "Alex#Alex2026 joined the chat"
}
```

离线：

```json
{
  "type": "system",
  "username": "Alex",
  "user_code": "Alex2026",
  "content": "Alex#Alex2026 left the chat"
}
```

## 4. Go 服务端设计

### 4.1 Client 身份字段

`Client` 增加：

```go
Username       string
UserCode       string
NormalizedCode string
```

其中：

- `UserCode` 保存用户原始输入，用于显示；
- `NormalizedCode` 保存小写代码，用于唯一性检查。

### 4.2 Hub 状态

Hub 继续作为客户端 map 的唯一读写者，并新增：

```go
ActiveCodes map[string]*Client
UsedCodes   map[string]struct{}
```

- `ActiveCodes`：当前在线代码到客户端的映射；
- `UsedCodes`：服务器进程运行期间已经占用过的代码；
- 客户端退出时只删除 `ActiveCodes`，不删除 `UsedCodes`。

### 4.3 原子注册

连接 goroutine 不直接读写代码 map，而是向 Hub 发送：

```go
type RegisterRequest struct {
    Client *Client
    Result chan error
}
```

Hub 在自己的 goroutine 内顺序处理注册：

1. 检查 `UsedCodes[NormalizedCode]`；
2. 如果存在，向 `Result` 返回重复代码错误；
3. 如果不存在，同时写入 `UsedCodes` 和 `ActiveCodes`；
4. 向 `Result` 返回成功；
5. 由连接处理 goroutine 在启动写入 goroutine 前发送 `login_ok`；
6. Hub 广播上线系统消息，消息进入各客户端的 `Send` channel。

Hub 不直接对 TCP 连接执行写操作。登录成功响应仍由连接处理流程完成，聊天、上线和离线消息统一由客户端写入 goroutine 发送。

由于检查和写入发生在同一个 Hub goroutine 内，两个连接同时申请同一代码时不会产生竞态，也不会出现两个用户成功使用同一代码。

### 4.4 注销流程

客户端读取失败、写入失败或主动退出时：

1. 向 Hub 发送注销请求；
2. Hub 删除 `ActiveCodes` 中的当前代码；
3. Hub 保留 `UsedCodes` 中的代码；
4. 关闭客户端发送 channel；
5. 广播离线系统消息；
6. 关闭 TCP 连接。

## 5. C++ 客户端设计

客户端登录参数扩展为：

```text
chat-client.exe <server_ip> <port> <username> <user_code> <chat_content>
```

客户端消息结构增加 `user_code` 字段，并在显示时使用：

```text
username#user_code
```

客户端可以在发送登录前做基础格式检查，但最终校验以 Go 服务端为准。收到 `login_error` 后显示服务器返回原因并退出。

## 6. 文件范围

预计修改：

- `server-go/message.go`：增加身份代码字段和校验；
- `server-go/hub.go`：增加代码索引、已用代码集合和原子注册；
- `server-go/client.go`：处理新登录流程、身份绑定和上线/离线消息；
- `client-cpp/include/message.hpp`：增加 `user_code`；
- `client-cpp/src/message.cpp`：序列化和解析 `user_code`；
- `client-cpp/src/main.cpp`：接收自定义代码并显示组合身份。

预计新增测试：

- Go 代码格式和唯一性测试；
- Go Hub 并发抢占同一代码测试；
- Go 用户退出后代码仍不可复用测试；
- C++ 登录和聊天身份字段测试；
- localhost 多客户端联调测试。

## 7. 验收标准

以下场景必须成立：

1. 两个用户使用相同显示名、不同代码时可以同时在线；
2. 两个用户使用相同代码时只有一个登录成功；
3. 代码比较不区分大小写；
4. 用户退出后，原代码仍不能被其他用户使用；
5. 空代码、长度不足、长度超限和特殊字符代码被拒绝；
6. 中文显示名可以正常登录和聊天；
7. 聊天显示包含 `username#user_code`；
8. 客户端伪造聊天身份字段不会改变服务端广播身份；
9. 并发抢占同一代码时结果稳定且不会发生重复；
10. Go 测试、竞态检测、静态检查和 C++ 编译全部通过；
11. Go 服务端重启后，已用代码集合重新开始记录。

## 8. 后续升级

如果未来要求服务器重启后仍保证代码唯一，需要增加持久化存储，例如 SQLite，并将 `UsedCodes` 从内存集合改为数据库唯一索引。本阶段不实现该功能。
