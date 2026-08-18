# 构建与测试说明

本文档记录 Windows 11 下的可复现验证流程。所有局域网测试前，先完成 localhost 测试。

## 1. 环境

- Windows 11
- Go SDK，本文示例使用 C:\Users\jking1\go-sdk\go\bin
- MinGW-w64 g++
- C++17
- PowerShell

进入项目目录：

~~~powershell
cd C:\Users\jking1\Desktop\my-project\chat_X
~~~

## 2. Go 全量验证

~~~powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
cd server-go
gofmt -w *.go
go test ./...
go test -race ./...
go vet ./...
go build -o chat-server.exe .
~~~

重点覆盖：

- 4-byte header 不完整；
- payload 截断；
- length == 0；
- length > 64 KiB；
- back-to-back frames；
- 非法 UTF-8；
- malformed JSON；
- 缺少 type；
- users 非数组或数组元素不是字符串；
- 登录前/后 EOF；
- 非法连接注销；
- Hub outbound 晚到、写失败、满队列和慢客户端清理。

## 3. C++ 严格编译

~~~powershell
cd ..\client-cpp
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client.exe -municode -lws2_32
~~~

预期：编译成功且没有 warning。此构建使用 Winsock2，并链接 ws2_32。

## 4. localhost 基础测试

启动 Server：

~~~powershell
cd ..\server-go
.\chat-server.exe
~~~

确认日志包含：

~~~text
listening on 0.0.0.0:8888
~~~

另开终端启动两个 Client：

~~~powershell
cd ..\client-cpp
.\chat-client.exe 127.0.0.1 8888 Alice ALICE001
.\chat-client.exe 127.0.0.1 8888 Bob BOB001
~~~

分别检查：登录、中文聊天、/users、/help、/quit 和其他客户端继续工作。

## 5. localhost 异常测试矩阵

| 场景 | 操作 | 预期结果 | 当前记录方式 |
|---|---|---|---|
| 不存在端口 | Client 连接未监听端口 | 显示 connect failed，安全退出 | 可自动复现 |
| 客户端强制关闭 | 结束一个 Client 进程 | Server 注销该连接，其他 Client 继续聊天 | Go 测试覆盖，C++ 交互需手工复核 |
| Server 突然关闭 | 停止 Server | Client 显示 Connection to server lost. 并退出 | 需本机手工复核 |
| 短 header | 发送少于 4 bytes | 当前连接被拒绝，不影响 Server | Go protocol test 覆盖 |
| 0 长度 | header 为 00 00 00 00 | 当前连接被拒绝 | Go protocol test 覆盖；C++ frame 防御代码已审查，需手工或辅助工具复核 |
| 超长 frame | length 大于 65536 | 不分配 payload，当前连接结束 | Go protocol test 覆盖；C++ frame 防御代码已审查，需手工或辅助工具复核 |
| payload 截断 | 声明长度大于实际数据后关闭 | io.ReadFull/recv_all 返回失败 | Go protocol test 覆盖，C++ 需手工辅助复核 |
| 非法 JSON/UTF-8 | 发送 malformed payload | 消息解析失败，连接不继续处理 | Go message test 覆盖 |
| 异常后重连 | 异常 Client 退出后启动新 Client | 新连接可以登录 | 需本机手工复核 |

本项目不把“没有崩溃”当成所有协议测试的充分条件；应同时检查连接关闭、Server 继续接受新连接、其他 Client 仍能收发，以及测试后无残留进程。

## 6. 进程与端口清理

测试结束后，先正常退出 Client 和 Server。然后检查：

~~~powershell
Get-Process chat-server,chat-client -ErrorAction SilentlyContinue
Get-NetTCPConnection -LocalPort 8888 -ErrorAction SilentlyContinue
~~~

预期没有残留进程，也没有 8888 监听。若测试程序异常退出，只结束本项目启动的进程，不要关闭无关程序。

## 7. 局域网测试

先在 Server 电脑执行：

~~~powershell
ipconfig
~~~

假设 Ethernet Server 的 IPv4 是 192.168.1.100，另一台 Wi-Fi 电脑执行：

~~~powershell
.\chat-client.exe 192.168.1.100 8888 Alice ALICE001
~~~

第二台设备使用不同的 user_code。可先测试：

~~~powershell
ping 192.168.1.100
Test-NetConnection 192.168.1.100 -Port 8888
~~~

ping 不通不一定代表 TCP 一定不通，因为 ICMP 可能被防火墙禁止；Test-NetConnection 更适合检查 TCP 端口。

若 TCP 不通，依次检查：

1. Server 是否监听 0.0.0.0:8888；
2. Windows 网络配置是否为 Private Network；
3. 是否允许 Go Server 通过 Private Network；
4. 是否创建 TCP 8888 入站规则；
5. 路由器是否开启 AP Isolation / Client Isolation；
6. 是否存在 VLAN 或访客网络隔离；
7. Ethernet 与 Wi-Fi 设备的 IPv4 路由是否互通。

不要关闭整个 Windows Defender Firewall。只为本项目允许 Go Server 或创建必要的 Private Network TCP 8888 入站规则。

## 8. 验收边界与未完成项

已纳入自动化验证的内容包括 Go 协议/JSON/连接/Hub 异常测试、race/vet、Go build，以及 C++17 严格编译和连接失败路径。

C++ Client 在登录成功后的接收线程中遇到 recv == 0、SOCKET_ERROR 或 frame/JSON 接收解析失败时统一显示 `Connection to server lost.`；登录阶段接收失败会显示对应的登录错误。当前没有独立的 C++ 非法帧自动化测试工具；交互式控制台的“自然输入退出”完整自动化测试也没有完成，不能写成“自动化测试通过”。这些场景仍应在真实 Windows Terminal 中手工执行或使用辅助工具复核，并记录结果。

当前版本没有自动重连、TLS、持久化数据库，也没有把这些能力纳入 Stage 8 验收。
