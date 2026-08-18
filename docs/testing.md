# 构建与测试说明

本文档记录 Windows 11 下的可复现验证流程。所有局域网测试前，先完成 localhost 测试。

## 1. 环境

- Windows 11
- Go SDK（本文示例使用 `C:\Users\jking1\go-sdk\go\bin`）
- MinGW-w64 g++
- CMake 3.20+
- CTest（随 CMake 安装）
- MinGW Make
- C++17
- PowerShell

本机已配置并验证：CMake/CTest 4.3.3，MinGW Make 4.4.1；`C:\msys64\mingw64\bin` 已加入当前 Windows 用户的 `PATH`。

进入项目目录：

```powershell
cd C:\Users\jking1\Desktop\my-project\chat_X
```

## 2. Go 全量验证

```powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
cd server-go
gofmt -w *.go
go test ./...
go test -race ./...
go vet ./...
go build -o chat-server.exe .
```

TLS 服务端启动需要证书和私钥：

```powershell
.\chat-server.exe -cert .\certs\server.crt -key .\certs\server.key
```

缺少参数和环境变量时，预期服务端输出包含 `TLS certificate and private key are required` 并退出。

重点覆盖：

- 4-byte header 不完整
- payload 截断
- `length == 0`
- `length > 64 KiB`
- back-to-back frames
- 非法 UTF-8
- malformed JSON
- 缺少 `type`
- `users` 不是字符串数组
- 登录前 / 后 EOF
- 非法连接注销
- Hub outbound 晚到、写失败、满队列和慢客户端清理

## 3. C++ 构建

### 3.1 MinGW 直接构建

```powershell
cd ..\client-cpp
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\command.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client.exe -municode -lws2_32
```

预期：编译成功且没有 warning。

### 3.2 CMake 构建

```powershell
cd ..\client-cpp
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --config Release
```

预期：生成 `chat-client.exe` 与 `protocol-tests.exe`。

本机验证结果：CMake 配置、构建均成功。

## 4. C++ 协议测试（CTest）

```powershell
cd ..\client-cpp
ctest --test-dir build --output-on-failure
```

当前 CMake 注册两个测试目标：`command-tests` 和 `protocol-tests`；构建目录应包含 `chat-client.exe`、`command-tests.exe` 和 `protocol-tests.exe`。私聊功能的直接测试为：`protocol-tests` 15 个场景、`command-tests` 4 个命令解析场景。

`protocol-tests` 是 C++ 协议层的 loopback TCP 自动化测试目标，当前共有 13 个直接测试，覆盖：

- `send_frame` 拒绝空 payload
- `send_frame` 拒绝超长 payload
- `recv_frame` 拒绝 0 长度、超长长度、短 header 和截断 payload
- `recv_all` 在发送线程延迟分段发送时读取完整已知字节序列
- `recv_frame` 在 header 与 payload 均分段到达时接收合法 frame
- `receive_message` 拒绝 malformed JSON、缺少字符串 `type`、数值型 `content`
- 合法 UTF-8 消息往返保持字段一致

本机验证结果：CTest 报告 `100% tests passed, 0 tests failed out of 1`，协议测试内部 13 个场景全部通过。
- 三个合法 frame 按发送顺序被接收

分段接收测试设置了有限接收超时，避免网络回归导致 CI 无限阻塞；其中还包含对 UTF-8 round-trip 和 `recv_all` 的直接覆盖。

## 5. localhost 基础测试

启动 Server：

```powershell
cd ..\server-go
.\chat-server.exe -cert .\certs\server.crt -key .\certs\server.key
```

确认日志包含：

```text
listening on 0.0.0.0:8888
```

另开终端启动两个 Client：

```powershell
cd ..\client-cpp
.\chat-client.exe 127.0.0.1 8888 Alice ALICE001
.\chat-client.exe 127.0.0.1 8888 Bob BOB001
```

分别检查：登录、中文聊天、`/users`、`/help`、`/quit` 和其他客户端继续工作。

### 5.1 三客户端 localhost 私聊验收

保持 Server 运行，再打开三个 PowerShell 窗口：

```powershell
cd ..\client-cpp
.\chat-client.exe 127.0.0.1 8888 Alice ALICE001
```

```powershell
cd ..\client-cpp
.\chat-client.exe 127.0.0.1 8888 Bob BOB001
```

```powershell
cd ..\client-cpp
.\chat-client.exe 127.0.0.1 8888 Charlie CHARLIE001
```

按以下顺序验收：

1. Alice 输入 `/users`，确认列表中包含 `Alice#ALICE001`、`Bob#BOB001` 和 `Charlie#CHARLIE001`。
2. Alice 输入 `/msg Bob#BOB001 你好，这是私聊消息。`。
3. Alice 应看到类似 `[Private -> Bob#BOB001] 你好，这是私聊消息。`，Bob 应看到类似 `[Private from Alice#ALICE001] 你好，这是私聊消息。`，Charlie 不应看到这条 `private_chat`。
4. Bob 输入 `/msg alice#alice001 你好，代码大小写不敏感。`，确认 Alice 能收到，说明目标代码比较不区分大小写。
5. Alice 输入 `/msg Nobody#NOUSER01 测试`，只在 Alice 端确认 `Target user not found`；然后输入 `/msg Alice#ALICE001 测试`，确认收到 `Cannot send private message to yourself`。
6. 再发送一条普通中文消息，确认群聊仍然发送给 Alice、Bob 和 Charlie 三个客户端。

私聊验收的关键结论是：成功消息只到达发送者和目标用户；未知目标和自己发送只返回发送者错误；第三个客户端不会收到私聊内容。

### 5.2 三客户端房间验收

1. 三个客户端分别登录后，确认初始房间都是 `lobby`。
2. Bob 输入 `/join study_room`，Alice 和 Charlie 仍留在 `lobby`。
3. Bob 发送群聊，只有 Bob 能看到；Alice 和 Charlie 不应收到。
4. Alice 输入 `/rooms`，确认列表包含 `lobby` 和 `study_room`。
5. Alice 输入 `/join study_room` 后，Alice 与 Bob 可以互相群聊。
6. Alice 输入 `/leave` 返回 `lobby`；私聊仍可使用 `/msg Bob#BOB001 message` 跨房间发送。

## 6. localhost 异常测试矩阵

| 场景 | 操作 | 预期结果 | 当前记录方式 |
|---|---|---|---|
| 不存在端口 | Client 连接未监听端口 | 显示 `connect failed`，安全退出 | 可自动复现 |
| 客户端强制关闭 | 结束一个 Client 进程 | Server 注销该连接，其他 Client 继续聊天 | Go 测试覆盖；C++ 交互需手工复核 |
| Server 突然关闭 | 停止 Server | Client 显示 `Connection to server lost.` 并退出 | 需本机手工复核 |
| 短 header | 发送少于 4 bytes | 当前连接被拒绝，不影响 Server | Go protocol test 与 C++ `protocol-tests` 自动化覆盖 |
| 0 长度 | header 为 `00 00 00 00` | 当前连接被拒绝 | Go protocol test 与 C++ `protocol-tests` 自动化覆盖 |
| 超长 frame | `length > 65536` | 不分配 payload，当前连接结束 | Go protocol test 与 C++ `protocol-tests` 自动化覆盖 |
| payload 截断 | 声明长度大于实际数据后关闭 | `io.ReadFull` / `recv_all` 返回失败 | Go protocol test 与 C++ `protocol-tests` 自动化覆盖 |
| 非法 JSON / UTF-8 | 发送 malformed payload | 消息解析失败，连接不继续处理 | Go message test 覆盖；C++ `protocol-tests` 自动化覆盖 malformed JSON，非法 UTF-8 仍由 Go 自动化覆盖 |
| 异常后重连 | Server 运行期间停止后重新启动 | Client 显示 `Connection to server lost.`、`Reconnecting...`，按 1/2/4/8/16/30 秒退避，重新登录后恢复聊天 | 需本机手工复核 |

不要把“没有崩溃”当成全部协议测试通过；还要同时检查连接关闭、Server 继续接受新连接、其他 Client 仍能收发，以及测试后没有残留进程。

## 7. 进程与端口清理

测试结束后，先正常退出 Client 和 Server。然后检查：

```powershell
Get-Process chat-server,chat-client -ErrorAction SilentlyContinue
Get-NetTCPConnection -LocalPort 8888 -ErrorAction SilentlyContinue
```

预期没有残留进程，也没有 `8888` 监听。若测试程序异常退出，只结束本项目启动的进程，不要关闭无关程序。

## 8. 局域网测试

先在 Server 电脑执行：

```powershell
ipconfig
```

假设 Ethernet Server 的 IPv4 是 `192.168.1.100`，另一台 Wi-Fi 电脑执行：

```powershell
.\chat-client.exe 192.168.1.100 8888 Alice ALICE001
```

第二台设备使用不同的 `user_code`。可先测试：

```powershell
ping 192.168.1.100
Test-NetConnection 192.168.1.100 -Port 8888
```

如果 TCP 不通，依次检查：

1. Server 是否监听 `0.0.0.0:8888`
2. Windows 网络配置是否为 Private Network
3. 是否允许 Go Server 通过 Private Network
4. 是否创建 TCP 8888 入站规则
5. 路由器是否开启 AP Isolation / Client Isolation
6. 是否存在 VLAN 或访客网络隔离
7. Ethernet 与 Wi-Fi 设备的 IPv4 路由是否互通

不要关闭整个 Windows Defender Firewall。只为本项目允许 Go Server 或创建必要的 Private Network TCP 8888 入站规则。

## 9. GitHub Actions 覆盖范围

仓库当前配置了两个 GitHub Actions job：

- Windows：验证 Go `test` / `test -race` / `vet` / `build`，再执行 C++ `cmake` configure、build 和 `ctest`
- Ubuntu：只验证 Go Server，不编译或测试 Windows C++ 客户端

发布前应同时参考本地检查和最新一次 GitHub Actions 结果。

## 10. 当前限制与诚实说明

- C++ 客户端仍是 Windows-only；Ubuntu job 不负责验证 `client-cpp`
- 交互式控制台“自然输入 / 退出”、真实局域网组网和截图核对仍需真实 Windows 环境手工复核
- 当前版本已经支持 C++ Client 自动重连；Go Server TLS 已实现，但 C++ Client 尚未支持 TLS，因此 TLS 聊天联调不在本次验收范围内
## TLS 客户端构建验证

使用 MSYS2 MinGW64 OpenSSL 3.x 时，先确保编译器、OpenSSL DLL 和 CMake 在 PATH 中：

    $env:Path = "C:\msys64\mingw64\bin;C:\msys64\ucrt64\bin;$env:Path"
    cmake -S client-cpp -B client-cpp/build -G "MinGW Makefiles"
    cmake --build client-cpp/build --config Release
    ctest --test-dir client-cpp/build --output-on-failure

如果 CTest 报 0xc0000135，说明测试进程找不到 libssl-3-x64.dll 或 libcrypto-3-x64.dll；将包含这些 DLL 的 OpenSSL bin 目录加入当前 PowerShell 的 PATH 后重新执行。TLS 聊天联调时，先启动已配置证书和私钥的 Go Server，再用 --ca-file 指定签发服务端证书的 CA。

TLS 当前已完成 Go Server 与 C++ Client 的端到端 localhost 联调；自签名证书必须通过 `--ca-file` 显式指定，不提供关闭证书验证的模式。
