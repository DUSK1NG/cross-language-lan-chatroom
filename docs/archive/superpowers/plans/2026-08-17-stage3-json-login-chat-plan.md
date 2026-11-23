# Stage 3：JSON 登录与单客户端聊天实现计划

> **执行说明：** 按任务逐项执行，每一步都使用复选框跟踪；本阶段完成后再进入 Stage 4。

**目标：** 在 Stage 2 的 4 字节长度分帧之上加入 JSON，使一个 C++ 客户端能够使用用户名登录，并发送一条聊天消息，由 Go 服务端使用服务器绑定的用户名回显。

**架构：** 保持 Stage 2 的 `4 字节大端长度 + UTF-8 JSON`。Go 增加消息模型和 JSON 编解码，C++ 使用 vendored 的 `nlohmann/json` 单头文件。服务端只处理一个 TCP 客户端：登录一次、聊天一次，然后退出。本阶段不加入多客户端、Hub、广播、命令或接收线程。

**技术栈：** Go 1.20 兼容的 `encoding/json`；C++17；Windows Winsock2；MinGW-w64；`client-cpp/third_party/json.hpp`。

## 全局约束

- 保持 Stage 2 的协议：4 字节 `uint32` 大端长度头，后跟 UTF-8 JSON 字节。
- 只处理一个 TCP 客户端，不加入 Hub、广播、多客户端、命令或后台接收线程。
- 支持 `login`、`login_ok`、`login_error`、`chat`、`error`。
- 服务端绑定登录用户名，不能信任客户端聊天消息中的 `username`。
- 用户名不能为空，必须是有效 UTF-8，最多 32 个 UTF-8 字节。
- 聊天内容不能为空，最多 64 KiB UTF-8 字节。
- JSON 字段统一为 `type`、`username`、`content`；不需要的可选字段省略。
- 使用 `nlohmann/json` 单头文件，不手写 JSON 解析器。
- 远程内容只当文本处理，禁止调用 `system()` 或执行命令。

---

### 任务 1：增加 Go JSON 消息测试和实现

**文件：**

- 创建：`server-go/message_test.go`
- 创建：`server-go/message.go`

**接口：**

- `type Message struct { Type string; Username string; Content string }`
- `sendMessage(writer io.Writer, message Message) error`
- `receiveMessage(reader io.Reader) (Message, error)`
- `validateMessage(message Message) error`
- `maxUsernameSize = 32`

- [ ] **步骤 1：先编写失败测试**

测试以下内容：

1. `login` 消息经过 `sendMessage` 和 `receiveMessage` 后字段保持一致；
2. 包含 `你好，这是 Go 和 C++。` 的 `chat` 消息可以正确往返；
3. JSON 中包含 `type`、`username`、`content` 字段；
4. 空 `type`、登录时空用户名、超过 32 字节的用户名、空聊天内容会被拒绝。

测试使用 `bytes.Buffer`，并通过现有的 `writeFrame` / `readFrame` 验证 JSON 层和长度分帧层能够配合工作。

- [ ] **步骤 2：运行测试确认先失败**

在 `server-go` 执行：

```powershell
$env:Path = "C:\Users\jking1\go-sdk\go\bin;$env:Path"
$env:GO111MODULE = "off"
$env:GOCACHE = "C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache"
go test ./...
```

预期：因为 `Message`、`sendMessage`、`receiveMessage` 和 `validateMessage` 尚未定义，编译失败。

- [ ] **步骤 3：实现 Go 消息模型和校验**

使用以下 JSON 标签：

```go
type Message struct {
	Type     string `json:"type"`
	Username string `json:"username,omitempty"`
	Content  string `json:"content,omitempty"`
}
```

使用 `utf8.ValidString` 和字节长度进行校验，不把 Unicode 字符数量当作协议长度。

- [ ] **步骤 4：实现 JSON 与长度帧的组合**

`sendMessage` 使用 `json.Marshal` 后调用 `writeFrame`。`receiveMessage` 调用 `readFrame` 后使用 `json.Unmarshal`，拒绝非法 JSON 和空 `type`。不要拒绝未知 JSON 字段，为后续 `time` 等字段保留扩展空间。

- [ ] **步骤 5：格式化并运行测试**

```powershell
gofmt -w message.go message_test.go protocol.go protocol_test.go
go test ./...
go vet ./...
```

预期：所有 JSON 和长度分帧测试通过。

---

### 任务 2：将 Go 服务端改为登录和单客户端聊天

**文件：**

- 修改：`server-go/main.go`

**行为：**

- 接收一个 `login`；
- 返回 `login_ok` 或 `login_error`；
- 登录成功后接收一个 `chat`；
- 使用服务端绑定的用户名返回一个 `chat`。

- [ ] **步骤 1：实现登录流程**

连接建立后调用 `receiveMessage`。如果收到的不是 `login`，发送：

```json
{"type":"login_error","content":"Expected login message"}
```

用户名非法时发送：

```json
{"type":"login_error","content":"Invalid username"}
```

用户名有效时保存到本地变量 `username`，并发送：

```json
{"type":"login_ok","content":"Login successful"}
```

Stage 3 只处理一次登录尝试；登录失败后关闭连接，重试和重复用户名放到后续阶段。

- [ ] **步骤 2：实现聊天流程**

再接收一条消息。如果类型不是 `chat` 或内容非法，发送 `error`。否则服务端自行创建响应：

```go
Message{
	Type:     "chat",
	Username: username,
	Content:  incoming.Content,
}
```

不能把 `incoming.Username` 复制到响应中。

- [ ] **步骤 3：编译和测试 Go 服务端**

```powershell
$env:Path = "C:\Users\jking1\go-sdk\go\bin;$env:Path"
$env:GO111MODULE = "off"
$env:GOCACHE = "C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache"
gofmt -w main.go message.go protocol.go message_test.go protocol_test.go
go test ./...
go vet ./...
go build -o chat-server.exe .
```

---

### 任务 3：引入 nlohmann/json 并实现 C++ 消息模块

**文件：**

- 创建：`client-cpp/third_party/json.hpp`
- 创建：`client-cpp/include/message.hpp`
- 创建：`client-cpp/src/message.cpp`

**接口：**

- `struct Message { std::string type; std::string username; std::string content; }`
- `bool send_message(SOCKET socket_handle, const Message& message);`
- `bool receive_message(SOCKET socket_handle, Message& message);`

- [ ] **步骤 1：加入官方单头文件 JSON 库**

把 `nlohmann/json` 的单头文件放到：

```text
client-cpp/third_party/json.hpp
```

代码使用：

```cpp
#include "json.hpp"
```

不引入包管理器，也不手写 JSON 解析器。

- [ ] **步骤 2：定义 C++ 消息类型和函数**

`message.hpp` 引入 `protocol.hpp`、`<string>`，并在独立命名空间中声明 `Message`、`send_message` 和 `receive_message`。

- [ ] **步骤 3：实现序列化**

构造 JSON 对象，始终写入 `type`；只有字符串非空时才写入 `username` 和 `content`。调用 `dump()` 后，将 UTF-8 字节交给 `protocol::send_frame`。

- [ ] **步骤 4：实现反序列化**

调用 `protocol::recv_frame` 获得 JSON 字符串，再调用 `nlohmann::json::parse`。要求 `type` 为字符串；字段缺失、类型错误或 JSON 格式错误时返回失败。

---

### 任务 4：改造 C++ 客户端并完成 Stage 3 验收

**文件：**

- 修改：`client-cpp/src/main.cpp`

**命令行参数：**

```text
chat-client.exe [server_ip] [port] [username] [chat_content]
```

默认值：`127.0.0.1`、`8888`、`Alice`、`Hello from C++`。

- [ ] **步骤 1：发送登录 JSON**

连接后发送：

```cpp
Message login{"login", username, ""};
```

收到响应后，只有类型为 `login_ok` 才继续；否则打印 `content`，清理资源并以失败退出。

- [ ] **步骤 2：发送和接收聊天 JSON**

发送：

```cpp
Message chat{"chat", "", chat_content};
```

收到类型为 `chat` 的响应后显示：

```text
Logged in as Alice
Server echoed: Hello from C++
```

- [ ] **步骤 3：编译 C++ 客户端**

在 `client-cpp` 执行：

```powershell
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client.exe -municode -lws2_32
```

- [ ] **步骤 4：运行成功登录和聊天测试**

启动 Go 服务端，再运行默认 C++ 客户端。

预期客户端显示登录成功和聊天回显；服务端显示 `Alice` 发送的聊天内容。

- [ ] **步骤 5：运行非法用户名测试**

启动新的服务端后执行一个超过 32 字节的用户名：

```powershell
$longName = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
chat-client.exe 127.0.0.1 8888 $longName
```

预期：客户端收到 `login_error` 和 `Invalid username`，不会发送聊天消息。空用户名也由 Go 单元测试覆盖。

- [ ] **步骤 6：提交 Stage 3**

所有测试通过后执行：

```powershell
git add server-go client-cpp/include client-cpp/src client-cpp/third_party/json.hpp docs/superpowers/plans/2026-08-17-stage3-json-login-chat-plan.md
git commit -m "feat: add stage3 json login and chat"
```

## Stage 3 验收清单

- [ ] JSON 通过已有 4 字节长度帧传输；
- [ ] Go 使用 `encoding/json`；
- [ ] C++ 使用 vendored `nlohmann/json`；
- [ ] 合法用户名可以登录；
- [ ] 非法用户名返回 `login_error`；
- [ ] 聊天内容使用服务端绑定用户名回显；
- [ ] UTF-8 中文可以完成 Go/C++ 往返；
- [ ] 非法 JSON 和错误字段类型不会导致程序崩溃；
- [ ] 没有加入多客户端、Hub、广播、命令或接收线程。
