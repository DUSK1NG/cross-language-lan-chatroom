# 构建与测试说明

本文档记录 Windows 11 下的可复现验证流程。所有局域网测试前，先完成 localhost 测试。

## 1. 环境

- Windows 11
- Go SDK（本文示例使用 `C:\Users\jking1\go-sdk\go\bin`）
- MinGW-w64 g++
- CMake 3.20+
- C++17
- PowerShell

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
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client.exe -municode -lws2_32
```

预期：编译成功且没有 warning。

### 3.2 CMake 构建

```powershell
cd ..\client-cpp
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --config Release
```

预期：生成 `chat-client.exe` 与 `protocol-tests.exe`。

## 4. C++ 协议测试（CTest）

```powershell
cd ..\client-cpp
ctest --test-dir build --output-on-failure
```

`protocol-tests` 是 C++ 协议层的 loopback TCP 自动化测试目标，当前共有 12 个直接测试，覆盖：

- `send_frame` 拒绝空 payload
- `send_frame` 拒绝超长 payload
- `recv_frame` 拒绝 0 长度、超长长度、短 header 和截断 payload
- `recv_all` 读取完整已知字节序列
- `receive_message` 拒绝 malformed JSON、缺少字符串 `type`、数值型 `content`
- 合法 UTF-8 消息往返保持字段一致
- 三个合法 frame 按发送顺序被接收

其中包含对 UTF-8 round-trip 和 `recv_all` 的直接覆盖。

## 5. localhost 基础测试

启动 Server：

```powershell
cd ..\server-go
.\chat-server.exe
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

## 6. localhost 异常测试矩阵

| 场景 | 操作 | 预期结果 | 当前记录方式 |
|---|---|---|---|
| 不存在端口 | Client 连接未监听端口 | 显示 `connect failed`，安全退出 | 可自动复现 |
| 客户端强制关闭 | 结束一个 Client 进程 | Server 注销该连接，其他 Client 继续聊天 | Go 测试覆盖；C++ 交互需手工复核 |
| Server 突然关闭 | 停止 Server | Client 显示 `Connection to server lost.` 并退出 | 需本机手工复核 |
| 短 header | 发送少于 4 bytes | 当前连接被拒绝，不影响 Server | Go protocol test 覆盖 |
| 0 长度 | header 为 `00 00 00 00` | 当前连接被拒绝 | Go protocol test 覆盖；C++ frame 防御代码已审查，需手工或辅助工具复核 |
| 超长 frame | `length > 65536` | 不分配 payload，当前连接结束 | Go protocol test 覆盖；C++ frame 防御代码已审查，需手工或辅助工具复核 |
| payload 截断 | 声明长度大于实际数据后关闭 | `io.ReadFull` / `recv_all` 返回失败 | Go protocol test 覆盖；C++ 需手工辅助复核 |
| 非法 JSON / UTF-8 | 发送 malformed payload | 消息解析失败，连接不继续处理 | Go message test 覆盖 |
| 异常后重连 | 异常 Client 退出后启动新 Client | 新连接可以登录 | 需本机手工复核 |

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
- 本次文档更新任务没有在当前环境里重新运行本地 CMake / CTest，因此不能把它们写成“本地已再次验证通过”
- 当前版本没有自动重连、TLS 或持久化数据库，这些能力也不在现有验收范围内
