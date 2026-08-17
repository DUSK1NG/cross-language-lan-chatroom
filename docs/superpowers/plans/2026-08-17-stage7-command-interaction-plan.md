# Stage 7：聊天室命令与持续交互实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax.

**Goal:** 为 Go + C++ 跨语言聊天室实现 /users、/help、/quit，并将 C++ 客户端改为持续收发消息的 Windows 命令行客户端。

**Architecture:** Go Hub goroutine 继续独占在线客户端 map；users_request 通过 Hub channel 生成排序后的 username#user_code 列表并定向回复；quit 通过 Hub 注销。C++ 登录完成后启动接收线程，主线程处理 UTF-16 控制台输入和发送，主线程统一负责最终 Socket 清理。

**Tech Stack:** Go、net、goroutine、channel、encoding/json、race detector；C++17、Winsock2、std::thread、std::atomic、std::mutex、nlohmann/json、MinGW-w64。

## Global Constraints

- 继续使用 4 字节大端长度头 + UTF-8 JSON payload。
- user_code 只能是 3–16 位 ASCII 字母和数字，比较不区分大小写，显示保留用户输入大小写。
- 聊天身份统一显示为 username#user_code。
- Go Hub goroutine 是 Clients、ActiveCodes、UsedCodes 的唯一读写者。
- users_response.users 由服务端生成，客户端不能伪造在线用户列表。
- /help 只在 C++ 客户端本地处理；/users 使用 users_request；/quit 使用 quit。
- 所有远程消息只当文本处理，禁止 system()、cmd、PowerShell、Shell 或任意代码执行。
- 优先保证 Windows 11 + Go + MinGW-w64/MSVC 环境可运行。
- 不加入私聊、房间、数据库、GUI、TLS 或文件传输。

---

### Task 1: 扩展 Go Message 与命令消息校验

**Files:**
- Modify: server-go/message.go
- Test: server-go/message_test.go

**Interfaces:**
- Message 增加 Users []string 字段，JSON key 为 users，使用 omitempty。
- validateMessage 接受 users_request 和 quit，不要求 content。
- validateMessage 接受 users_response，Users 可以为空或包含有效 UTF-8 字符串。

- [ ] **Step 1: 写失败测试**

在 message_test.go 增加 reflect import 和测试：

~~~go
func TestUsersMessageRoundTrip(t *testing.T) {
    want := Message{
        Type:  "users_response",
        Users: []string{"Alex#A001", "Alex#B002"},
    }
    var stream bytes.Buffer
    if err := sendMessage(&stream, want); err != nil {
        t.Fatal(err)
    }
    got, err := receiveMessage(&stream)
    if err != nil {
        t.Fatal(err)
    }
    if !reflect.DeepEqual(got, want) {
        t.Fatalf("message = %+v, want %+v", got, want)
    }
}

func TestValidateCommandMessages(t *testing.T) {
    for _, message := range []Message{
        {Type: "users_request"},
        {Type: "quit"},
        {Type: "users_response", Users: []string{}},
    } {
        if err := validateMessage(message); err != nil {
            t.Fatalf("validateMessage(%+v) error = %v", message, err)
        }
    }
}
~~~

同时扩展 JSON 字段测试，确认 users 为数组，空 content 仍省略。

- [ ] **Step 2: 运行失败测试**

~~~powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
go test ./... -run 'TestUsersMessageRoundTrip|TestValidateCommandMessages'
~~~

预期因 Users 字段或校验分支不存在而失败。

- [ ] **Step 3: 实现字段和校验**

在 Message 中加入 Users []string，并使用 JSON key users。validateMessage 增加 users_request、quit、users_response 分支；users_response 只检查每个用户字符串为有效 UTF-8，服务端生成的列表才是可信来源。

- [ ] **Step 4: 格式化并测试**

~~~powershell
C:\Users\jking1\go-sdk\go\bin\gofmt.exe -w message.go message_test.go
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
go test ./... -run 'TestUsersMessageRoundTrip|TestValidateCommandMessages|TestMessageJSONContainsExpectedFields'
~~~

- [ ] **Step 5: 提交**

~~~powershell
git add server-go/message.go server-go/message_test.go
git commit -m "feat: add chat command message fields"
~~~

### Task 2: 在 Hub 中实现在线用户列表请求

**Files:**
- Modify: server-go/hub.go
- Test: server-go/hub_test.go

**Interfaces:**
- Hub 增加 RequestUsers chan *Client。
- Hub 收到请求后只向请求客户端发送 users_response。
- 用户列表每项为 username#user_code，并使用 sort.Strings 排序。

- [ ] **Step 1: 写失败测试**

增加 TestHubRespondsWithSortedOnlineUsers：

~~~go
func TestHubRespondsWithSortedOnlineUsers(t *testing.T) {
    hub := NewHub()
    go hub.Run()

    first := newTestClient(t, "Zoe", "Z001")
    second := newTestClient(t, "Alex", "A001")
    if err := registerForTest(t, hub, first); err != nil {
        t.Fatal(err)
    }
    if err := registerForTest(t, hub, second); err != nil {
        t.Fatal(err)
    }
    drainMessages(t, first.Send, 1)
    drainMessages(t, second.Send, 2)

    hub.RequestUsers <- first
    got := receiveMessageFromChannel(t, first.Send)
    want := Message{
        Type:  "users_response",
        Users: []string{"Alex#A001", "Zoe#Z001"},
    }
    if !reflect.DeepEqual(got, want) {
        t.Fatalf("users response = %+v, want %+v", got, want)
    }
}
~~~

测试只从 Send channel 读取，不直接访问 Hub map；补充超时读取辅助函数并确认 second.Send 没收到 users_response。

- [ ] **Step 2: 运行失败测试**

~~~powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
go test ./... -run TestHubRespondsWithSortedOnlineUsers
~~~

- [ ] **Step 3: 实现 Hub 请求 channel**

在 Hub 和 NewHub 中加入 RequestUsers chan *Client，在 Run 中处理：

~~~go
case client := <-h.RequestUsers:
    h.respondWithUsers(client)
~~~

respondWithUsers 必须在 Hub goroutine 内遍历 h.Clients，构造 username#user_code，排序后只通过请求客户端的 enqueue 发送 users_response。请求客户端已注销时直接返回。

- [ ] **Step 4: 运行检查**

~~~powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
go test ./... -run 'TestHubRespondsWithSortedOnlineUsers|TestHubBroadcasts|TestHubRejects'
go test -race ./...
go vet ./...
~~~

- [ ] **Step 5: 提交**

~~~powershell
git add server-go/hub.go server-go/hub_test.go
git commit -m "feat: serve sorted online user lists"
~~~

### Task 3: 在 Go readPump 中处理 /users 与 /quit

**Files:**
- Modify: server-go/client.go
- Test: server-go/client_test.go

**Interfaces:**
- readPump 对 users_request 发送 hub.RequestUsers <- c。
- readPump 对 quit 发送 hub.Unregister <- c 并返回。
- chat 的身份覆盖行为保持不变。

- [ ] **Step 1: 添加连接级测试**

增加两个 net.Pipe 测试：

1. 登录后发送 Message{Type: "users_request"}，确认收到排序后的 users_response。
2. 登录后发送 Message{Type: "quit"}，确认 handler 在 1 秒内退出，并由其他客户端收到 username#code left the chat。

测试不得直接读写 Hub map。

- [ ] **Step 2: 运行失败测试**

~~~powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
go test ./... -run 'TestHandleConnectionUsersRequest|TestHandleConnectionQuit'
~~~

- [ ] **Step 3: 实现 readPump 分支**

将消息处理改为：

~~~go
switch message.Type {
case "chat":
    // 保持现有校验、身份覆盖和 Broadcast
case "users_request":
    hub.RequestUsers <- c
case "quit":
    hub.Unregister <- c
    return
default:
    // enqueue error: Expected chat, users_request, or quit message
}
~~~

如果客户端已注销，必须安全返回；不得直接读写 Hub map。现有幂等注销逻辑继续生效。

- [ ] **Step 4: 运行 Go 全量检查**

~~~powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
go test ./...
go test -race ./...
go vet ./...
~~~

- [ ] **Step 5: 提交**

~~~powershell
git add server-go/client.go server-go/client_test.go
git commit -m "feat: handle users and quit commands"
~~~

### Task 4: 扩展 C++ Message 的 users 序列化

**Files:**
- Modify: client-cpp/include/message.hpp
- Modify: client-cpp/src/message.cpp

**Interfaces:**
- message::Message 增加 std::vector<std::string> users。
- send_message 在 users 非空时写出 JSON 数组 users。
- receive_message 只接受字符串数组并填充 Message.users；字段类型错误返回 false。

- [ ] **Step 1: 修改结构和 JSON 转换**

加入 vector 头文件，发送时使用：

~~~cpp
if (!message.users.empty()) {
    object["users"] = message.users;
}
~~~

解析时检查 is_array，再逐项检查 is_string。

- [ ] **Step 2: 编译**

~~~powershell
$out = Join-Path $env:TEMP 'chat_X-stage7-message-check.exe'
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o $out -municode -lws2_32
$exit = $LASTEXITCODE
Remove-Item -LiteralPath $out -Force -ErrorAction SilentlyContinue
exit $exit
~~~

- [ ] **Step 3: 提交**

~~~powershell
git add client-cpp/include/message.hpp client-cpp/src/message.cpp
git commit -m "feat: encode online users in cpp messages"
~~~

### Task 5: 将 C++ 客户端改为持续交互式命令行

**Files:**
- Modify: client-cpp/src/main.cpp

**Interfaces:**
- 命令行参数为 chat-client.exe server_ip port username user_code；不再要求固定 chat_content。
- 默认值保持 127.0.0.1、8888、Alice、ALICE001。
- main 启动 std::thread receive_thread，主线程处理输入并发送。
- 接收线程使用 std::atomic<bool> running 和输出 std::mutex。

- [ ] **Step 1: 实现 UTF-16 控制台输入**

保持 wmain。控制台模式使用 GetStdHandle(STD_INPUT_HANDLE)、WaitForSingleObject 100ms 和 ReadConsoleW；每行通过已有 utf8_from_wide 转为 UTF-8；running 为 false 时返回。非控制台输入回退到 std::getline(std::wcin, line)。

- [ ] **Step 2: 实现接收线程**

登录成功后启动接收线程。必须处理：

~~~cpp
if (incoming.type == "users_response") {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << "Online Users:\n";
    for (std::size_t index = 0; index < incoming.users.size(); ++index) {
        std::cout << (index + 1) << ". " << incoming.users[index] << '\n';
    }
    continue;
}
~~~

system、chat、error 按设计文档显示。接收失败时打印 Connection to server lost.，设置 running=false，调用 shutdown，结束线程。

- [ ] **Step 3: 实现主线程命令循环和清理**

主循环必须区分 /help、/users、/quit、未知 slash 命令和普通文本：

~~~cpp
while (running && read_input_line(running, input_line)) {
    if (input_line == "/help") {
        print_help();
    } else if (input_line == "/users") {
        send_message(socket_handle, Message{"users_request", {}, {}, {}});
    } else if (input_line == "/quit") {
        send_message(socket_handle, Message{"quit", {}, {}, {}});
        running = false;
        shutdown(socket_handle, SD_BOTH);
    } else if (!input_line.empty() && input_line.front() == '/') {
        print_unknown_command();
    } else if (!input_line.empty()) {
        send_message(socket_handle, Message{"chat", {}, {}, input_line});
    }
}
~~~

循环结束后设置 running=false，必要时 shutdown，receive_thread.join()，最后由主线程执行 closesocket 和 WSACleanup。发送失败必须停止循环并清理。

- [ ] **Step 4: 编译和手工检查**

~~~powershell
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client.exe -municode -lws2_32
~~~

启动 Go 服务端，验证登录、普通聊天、/help、/users、/quit，并确认服务端突然关闭时客户端能结束。测试完成后删除生成的 exe。

- [ ] **Step 5: 提交**

~~~powershell
git add client-cpp/src/main.cpp
git commit -m "feat: add interactive cpp chat commands"
~~~

### Task 6: Stage 7 localhost 集成验收

**Files:**
- Test: server-go/message_test.go
- Test: server-go/hub_test.go
- Test: server-go/client_test.go
- Modify only if a protocol/documentation mismatch is found: docs/superpowers/specs/2026-08-17-stage7-command-interaction-design.md

**Interfaces:**
- Go server listens on 0.0.0.0:8888.
- C++ client connects to 127.0.0.1:8888.
- No generated exe, cache, log, or test process remains after verification.

- [ ] **Step 1: 运行 Go 检查**

~~~powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
go test ./...
go test -race ./...
go vet ./...
go build -o chat-server.exe
~~~

- [ ] **Step 2: 编译 C++ 客户端**

~~~powershell
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client.exe -municode -lws2_32
~~~

- [ ] **Step 3: 运行双客户端交互测试**

启动服务端和两个 C++ 客户端，分别使用 Alex A001、Alex B002。验证双方中文聊天、/users 列表、/quit 离线广播和服务器继续运行。

- [ ] **Step 4: 运行错误与断线测试**

验证服务端突然关闭、客户端重复 /quit、未知 /bad 命令和空行。客户端不能崩溃，服务端不能 panic。

- [ ] **Step 5: 清理并完成检查**

~~~powershell
Get-Process chat-server,chat-client -ErrorAction SilentlyContinue
git diff --check
git status --short
~~~

预期没有残留测试进程、生成物或未预期源码改动。没有必要的源代码修复时不创建空提交。

## Stage 7 最终验收

- /users、/help、/quit 可实际使用；
- C++ 客户端可持续收发聊天消息；
- 用户列表由 Go Hub 安全生成并排序；
- quit 注销和离线广播幂等；
- Go 测试、竞态检查、静态检查和 C++ 编译通过；
- localhost 双客户端集成通过；
- 工作区无残留测试进程和生成产物。
