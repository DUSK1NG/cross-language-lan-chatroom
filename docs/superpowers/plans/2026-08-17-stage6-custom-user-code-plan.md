# Stage 6：自定义用户代码与身份广播实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 允许显示名重复，为每个用户绑定一个由用户自定义、在服务器进程生命周期内唯一且大小写不敏感的字母数字身份代码，并以 `username#user_code` 显示聊天身份。

**Architecture:** Go Hub goroutine 独占客户端、在线代码和已使用代码集合。连接 goroutine 负责登录握手，向 Hub 提交带结果 channel 的原子注册请求；Hub 决定代码是否可用。C++ 客户端通过 JSON 的 `user_code` 字段传递身份，并使用服务端广播的身份显示消息。

**Tech Stack:** Go 1.20、`encoding/json`、goroutine、channel、`sync.Once`、C++17、Windows Winsock2、nlohmann/json、MinGW-w64。

## Global Constraints

- `username` 可以重复；
- `user_code` 只能包含 ASCII 英文字母和数字，长度为 3～16 位；
- `user_code` 比较时不区分大小写，显示时保留原始大小写；
- 代码在服务器进程运行期间一旦使用就永久占用，客户端退出后不能复用；
- 服务器重启后，内存中的已使用代码集合清空；本阶段不引入数据库；
- 服务端不信任客户端聊天消息中的身份字段，始终使用当前 TCP 连接绑定的身份；
- 继续使用 4 字节大端长度头 + UTF-8 JSON；
- 先完成 localhost 测试，再进行局域网测试；
- 每个任务先写失败测试或可复现失败步骤，再实现最小改动并验证；
- 继续使用 `GO111MODULE=off` 和项目内 `.go-build-cache` 运行 Go 命令；
- 不实现 `/users`、`/help`、`/quit`、私聊、数据库、TLS、GUI。

---

### Task 1: 扩展消息结构与用户代码校验

**Files:**
- Modify: `server-go/message.go`
- Test: `server-go/message_test.go`

**Interfaces:**
- Produces `Message.UserCode string`，JSON 字段名为 `user_code`；
- Produces `normalizeUserCode(code string) (string, error)`；
- `validateMessage` 对 `login` 消息同时校验 `Username` 和 `UserCode`；
- `chat` 消息继续只要求合法非空内容，客户端携带的身份字段由连接层覆盖。

- [ ] **Step 1: 添加失败测试，明确代码规则和登录字段**

在 `message_test.go` 增加以下测试：

```go
func TestValidateUserCode(t *testing.T) {
	tests := []struct {
		name    string
		code    string
		wantErr bool
	}{
		{name: "too short", code: "A1", wantErr: true},
		{name: "valid", code: "Alex2026"},
		{name: "too long", code: strings.Repeat("a", 17), wantErr: true},
		{name: "special character", code: "Alex-01", wantErr: true},
		{name: "non ASCII", code: "小明01", wantErr: true},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			err := validateUserCode(test.code)
			if (err != nil) != test.wantErr {
				t.Fatalf("validateUserCode(%q) error = %v, wantErr = %v", test.code, err, test.wantErr)
			}
		})
	}
}

func TestNormalizeUserCodeIsCaseInsensitive(t *testing.T) {
	got, err := normalizeUserCode("AlEx2026")
	if err != nil {
		t.Fatal(err)
	}
	if got != "alex2026" {
		t.Fatalf("normalized code = %q, want alex2026", got)
	}
}
```

同时把已有的有效登录测试改为：

```go
Message{Type: "login", Username: "Alice", UserCode: "A001"}
```

并增加登录缺少代码时必须失败的测试。

- [ ] **Step 2: 运行失败测试，确认当前协议尚未支持代码**

运行：

```powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
go test ./... -run 'TestValidateUserCode|TestNormalizeUserCodeIsCaseInsensitive'
```

预期：失败，原因包括 `Message` 没有 `UserCode` 或校验函数不存在。

- [ ] **Step 3: 实现消息字段和 ASCII 代码校验**

在 `message.go` 中增加：

```go
const (
	minUserCodeSize = 3
	maxUserCodeSize = 16
)

type Message struct {
	Type     string `json:"type"`
	Username string `json:"username,omitempty"`
	UserCode string `json:"user_code,omitempty"`
	Content  string `json:"content,omitempty"`
}
```

`validateUserCode` 必须按 ASCII 字节逐个检查 `A-Z`、`a-z`、`0-9`，不能用 Unicode 字符数替代字节规则；`normalizeUserCode` 先校验，再使用 `strings.ToLower` 返回规范化代码。

在 `validateMessage` 中让 `login` 同时调用用户名和代码校验，并继续支持 `login_ok`、`login_error`、`chat`、`system`、`error` 消息。

- [ ] **Step 4: 运行消息测试并提交**

运行：

```powershell
go test ./... -run 'TestMessage|TestValidate|TestNormalize'
```

预期：全部通过。

提交：

```powershell
git add server-go/message.go server-go/message_test.go
git commit -m "feat: validate custom user codes"
```

### Task 2: 为 Hub 增加原子代码注册和进程级占用记录

**Files:**
- Modify: `server-go/hub.go`
- Test: `server-go/hub_test.go`
- Modify: `server-go/client.go`（把登录代码写入 Client、把旧注册发送改为 `RegisterRequest`；完整连接流程整理留给 Task 3）

**Interfaces:**
- Produces `ErrUserCodeAlreadyUsed`；
- Produces `RegisterRequest{Client *Client, Result chan error}`；
- `Hub.Register` 类型改为 `chan RegisterRequest`；
- `Hub` 增加 `ActiveCodes map[string]*Client` 和 `UsedCodes map[string]struct{}`；
- `Client` 增加 `UserCode` 和 `NormalizedCode`；保留现有 `newClient` 签名，构造函数签名在 Task 3 与登录流程一起更新；Task 2 允许修改 `client.go` 在注册前填充登录身份并使用 `RegisterRequest`，以保证真实登录路径可运行。

- [ ] **Step 1: 添加失败测试，验证大小写不敏感和退出后不可复用**

在 `hub_test.go` 增加测试辅助函数和测试：

```go
func registerForTest(t *testing.T, hub *Hub, client *Client) error {
	t.Helper()
	request := RegisterRequest{
		Client: client,
		Result: make(chan error, 1),
	}
	hub.Register <- request
	return <-request.Result
}

func TestHubRejectsUserCodeWithoutCaseSensitivity(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	first := &Client{
		Username: "Alex",
		UserCode: "Alex2026",
		NormalizedCode: "alex2026",
		Send: make(chan Message, 8),
	}
	second := &Client{
		Username: "Bob",
		UserCode: "alex2026",
		NormalizedCode: "alex2026",
		Send: make(chan Message, 8),
	}
	if err := registerForTest(t, hub, first); err != nil {
		t.Fatal(err)
	}
	if err := registerForTest(t, hub, second); !errors.Is(err, ErrUserCodeAlreadyUsed) {
		t.Fatalf("second registration error = %v, want ErrUserCodeAlreadyUsed", err)
	}
}

func TestHubDoesNotReuseCodeAfterUnregister(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	first := &Client{
		UserCode: "A001",
		NormalizedCode: "a001",
		Send: make(chan Message, 8),
	}
	if err := registerForTest(t, hub, first); err != nil {
		t.Fatal(err)
	}
	hub.Unregister <- first

	second := &Client{
		UserCode: "a001",
		NormalizedCode: "a001",
		Send: make(chan Message, 8),
	}
	if err := registerForTest(t, hub, second); !errors.Is(err, ErrUserCodeAlreadyUsed) {
		t.Fatalf("reused code error = %v, want ErrUserCodeAlreadyUsed", err)
	}
}
```

更新现有广播和注销测试：注册后先读取并丢弃上线 `system` 消息，再断言聊天广播或 `Send` channel 关闭，避免上线消息干扰测试顺序。

- [ ] **Step 2: 运行失败测试，确认 Hub 尚未提供代码注册结果**

运行：

```powershell
go test ./... -run 'TestHubRejectsUserCodeWithoutCaseSensitivity|TestHubDoesNotReuseCodeAfterUnregister'
```

预期：编译失败，原因包括 `RegisterRequest`、`ErrUserCodeAlreadyUsed` 和代码索引不存在。

- [ ] **Step 3: 实现 Hub 的原子注册和注销逻辑**

在 `hub.go` 中增加：

```go
var ErrUserCodeAlreadyUsed = errors.New("user code already exists")

type RegisterRequest struct {
	Client *Client
	Result chan error
}
```

`NewHub` 初始化两个 map；`Run` 收到注册请求时只在 Hub goroutine 内检查和写入 `UsedCodes`、`ActiveCodes`。注册成功后向 `Result` 发送 `nil`，然后广播包含 `username`、`user_code` 和 `username#user_code joined the chat` 的 `system` 消息。

注销时删除 `ActiveCodes`，保留 `UsedCodes`，关闭 `Send` channel，并向剩余客户端广播离线消息。重复注销必须安全，不得重复关闭 channel。删除兼容旧注册事件和空代码身份的旁路，所有注册都必须经过 `RegisterRequest`。写入 goroutine 只能在注册成功后启动，注册失败不能留下阻塞 goroutine。

- [ ] **Step 4: 运行 Hub 测试、竞态检测并提交**

运行：

```powershell
go test ./...
go test -race ./...
go vet ./...
```

预期：全部通过，没有数据竞争。

提交：

```powershell
git add server-go/hub.go server-go/hub_test.go
git commit -m "feat: reserve user codes in hub"
```

### Task 3: 重构 Go 登录流程并绑定服务端身份

**Files:**
- Modify: `server-go/client.go`
- Modify: `server-go/hub.go`（更新 `newClient` 构造函数签名）
- Modify: `server-go/hub_test.go`（更新 Task 2 连接测试的 `login_ok` 期望字段）
- Test: `server-go/client_test.go`

**Interfaces:**
- `handleConnection(conn net.Conn, hub *Hub)` 继续作为连接入口；
- 登录成功后 `Client` 持有原始 `UserCode` 和 `NormalizedCode`；
- `readPump` 广播消息前强制写入 `c.Username` 和 `c.UserCode`；
- `Hub` 是唯一负责代码登记和上线/离线广播的组件。

- [ ] **Step 1: 先更新连接流程的失败联调步骤**

在服务端仍未修改前，使用带代码的新客户端命令：

```powershell
.\chat-client.exe 127.0.0.1 8888 Alex A001 "Hello"
```

预期：客户端无法按新参数完成正确登录，记录这个失败作为本任务的基线。

- [ ] **Step 2: 实现带结果的登录注册流程**

在 `handleConnection` 中：

1. 接收 `login`；
2. 调用 `validateMessage` 校验显示名和代码；
3. 规范化代码并创建 `Client`；
4. 发送 `RegisterRequest` 给 Hub 并等待 `Result`；
5. 代码冲突时发送 `login_error` 并关闭连接；
6. 注册成功后由连接处理 goroutine 发送带身份字段的 `login_ok`；
7. 启动唯一的 `writePump`；
8. 进入 `readPump`。

同时更新构造函数：

```go
func newClient(conn net.Conn, username, userCode, normalizedCode string) *Client
```

如果 `login_ok` 发送失败，必须向 Hub 发送注销请求，避免代码进入在线索引但没有可用连接。`readPump` 退出时继续使用 Hub 注销流程。

聊天处理必须覆盖身份：

```go
message.Username = c.Username
message.UserCode = c.UserCode
hub.Broadcast <- message
```

- [ ] **Step 3: 添加连接级测试，验证服务端不会接受伪造身份**

在 `server-go/client_test.go` 增加使用 `net.Pipe` 的测试。测试必须启动 Hub 和 `handleConnection`，完成登录后发送带伪造身份字段的聊天消息，并检查服务端返回的聊天消息：

```json
{
  "type": "chat",
  "username": "Fake",
  "user_code": "FAKE01",
  "content": "真实消息"
}
```

测试核心流程如下：

```go
func TestHandleConnectionUsesBoundIdentity(t *testing.T) {
	serverConn, clientConn := net.Pipe()
	hub := NewHub()
	go hub.Run()
	done := make(chan struct{})
	go func() {
		handleConnection(serverConn, hub)
		close(done)
	}()

	if err := sendMessage(clientConn, Message{
		Type: "login", Username: "Alex", UserCode: "A001",
	}); err != nil {
		t.Fatal(err)
	}
	loginOK, err := receiveMessage(clientConn)
	if err != nil {
		t.Fatal(err)
	}
	if loginOK.Type != "login_ok" || loginOK.Username != "Alex" || loginOK.UserCode != "A001" {
		t.Fatalf("login response = %+v", loginOK)
	}

	if err := sendMessage(clientConn, Message{
		Type: "chat", Username: "Fake", UserCode: "FAKE01", Content: "真实消息",
	}); err != nil {
		t.Fatal(err)
	}
	for {
		message, err := receiveMessage(clientConn)
		if err != nil {
			t.Fatal(err)
		}
		if message.Type != "chat" {
			continue
		}
		if message.Username != "Alex" || message.UserCode != "A001" {
			t.Fatalf("broadcast identity = %+v", message)
		}
		break
	}

	_ = clientConn.Close()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("connection handler did not stop")
	}
}
```

预期：广播身份始终为 `Alex#A001`，不能出现 `Fake#FAKE01`。

- [ ] **Step 4: 运行 Go 全量检查并提交**

运行：

```powershell
go test ./...
go test -race ./...
go vet ./...
```

提交：

```powershell
git add server-go/client.go server-go/client_test.go
git commit -m "feat: bind chat messages to client identity"
```

### Task 4: 更新 C++ 协议结构和身份显示

**Files:**
- Modify: `client-cpp/include/message.hpp`
- Modify: `client-cpp/src/message.cpp`
- Modify: `client-cpp/src/main.cpp`

**Interfaces:**
- `message::Message` 增加 `std::string user_code`；
- JSON 字段名固定为 `user_code`；
- 客户端命令行格式为 `<server_ip> <port> <username> <user_code> <chat_content>`；
- 输出身份统一使用 `username#user_code`。

- [ ] **Step 1: 修改消息结构和序列化字段**

在 `message.hpp` 中增加：

```cpp
std::string user_code;
```

在 `message.cpp` 中发送非空 `user_code`，接收时要求字段存在则必须是 JSON 字符串，并保存到 `Message.user_code`。

- [ ] **Step 2: 更新 C++ 登录参数和输出**

在 `main.cpp` 中把参数位置改为：

```text
argv[2] -> port
argv[3] -> username
argv[4] -> user_code
argv[5] -> chat_content
```

默认值设置为：

```cpp
constexpr const char* kDefaultUsername = "Alice";
constexpr const char* kDefaultUserCode = "ALICE001";
```

登录消息构造为：

```cpp
const message::Message login{"login", username, user_code, ""};
```

收到 `login_ok` 后使用服务端返回的 `username` 和 `user_code` 拼接显示身份。收到聊天或系统消息时显示：

```text
Alex#A001: Hello
Alex#A001 joined the chat
```

由于 Stage 6 会产生 `system` 消息，客户端发送聊天后必须循环接收：遇到 `system` 就显示并继续，遇到 `chat` 才结束本次一次性测试；收到 `error` 或连接断开则退出失败。

- [ ] **Step 3: 编译 C++ 客户端**

运行：

```powershell
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client.exe -municode -lws2_32
```

预期：无编译错误和警告。

- [ ] **Step 4: 提交 C++ 身份协议改动**

```powershell
git add client-cpp/include/message.hpp client-cpp/src/message.cpp client-cpp/src/main.cpp
git commit -m "feat: show custom user code in cpp client"
```

### Task 5: localhost 集成验证和阶段文档更新

**Files:**
- Modify: `docs/superpowers/specs/2026-08-17-custom-user-code-design.md`（只在测试发现协议表述需要修正时修改）
- Test: `server-go/message_test.go`
- Test: `server-go/hub_test.go`

**Interfaces:**
- Go 服务端监听 `0.0.0.0:8888`；
- C++ 客户端使用 `127.0.0.1:8888`；
- 测试结果必须能证明代码唯一性、身份覆盖和 `username#user_code` 显示。

- [ ] **Step 1: 构建并启动 Go 服务端**

运行：

```powershell
cd server-go
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
go test ./...
go test -race ./...
go vet ./...
go build -o chat-server.exe
.\chat-server.exe
```

预期：显示 `listening on 0.0.0.0:8888`。

- [ ] **Step 2: 测试重复显示名和不同代码**

在另一个终端依次运行：

```powershell
cd client-cpp
.\chat-client.exe 127.0.0.1 8888 Alex A001 "来自 A001 的消息"
.\chat-client.exe 127.0.0.1 8888 Alex B002 "来自 B002 的消息"
```

预期：两次都登录成功，输出中分别出现 `Alex#A001` 和 `Alex#B002`。

- [ ] **Step 3: 测试代码唯一性和退出后不可复用**

依次运行：

```powershell
.\chat-client.exe 127.0.0.1 8888 Bob A001 "duplicate code"
.\chat-client.exe 127.0.0.1 8888 Bob A003 "first use"
.\chat-client.exe 127.0.0.1 8888 Charlie a003 "reuse after disconnect"
.\chat-client.exe 127.0.0.1 8888 Dave A-04 "invalid code"
```

预期：`A001`、`a003` 重复使用和 `A-04` 均登录失败；`A003` 首次使用成功。

- [ ] **Step 4: 测试中文和身份覆盖**

运行：

```powershell
.\chat-client.exe 127.0.0.1 8888 小明 CN001 "你好，这是带身份代码的中文消息。"
```

预期：消息显示为 `小明#CN001: 你好，这是带身份代码的中文消息。`；Task 3 的 `net.Pipe` 测试同时证明客户端伪造身份字段不会改变服务端广播身份。

- [ ] **Step 5: 测试服务器重启后的记录范围**

停止 `chat-server.exe` 后重新启动，再运行：

```powershell
.\chat-client.exe 127.0.0.1 8888 Restarted A001 "code after restart"
```

预期：重启后 `A001` 可以重新使用，证明本阶段的唯一性范围是服务器进程生命周期。

- [ ] **Step 6: 关闭测试进程、检查工作区并提交阶段结果**

确认没有遗留 `chat-server.exe` 测试进程，运行：

```powershell
git diff --check
git status --short
```

预期：没有未预期的临时文件或未提交源码改动。

提交：

```powershell
git add docs/superpowers/specs/2026-08-17-custom-user-code-design.md server-go/message_test.go server-go/hub_test.go
git commit -m "test: verify custom user code identity flow"
```

## Stage 6 最终验收

- 显示名允许重复；
- 同一服务器进程中身份代码大小写不敏感且不可重复；
- 用户退出后代码仍不可复用；
- 服务器重启后代码记录清空；
- 聊天、上线和离线消息都显示 `username#user_code`；
- 服务端不接受客户端伪造身份；
- Go 单元测试、竞态检测、静态检查通过；
- C++17 + Winsock2 编译通过；
- localhost 多客户端测试通过；
- 工作区无遗留测试服务进程。
