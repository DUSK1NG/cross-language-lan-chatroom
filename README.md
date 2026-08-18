# Go + C++ LAN Chat

一个面向 Windows 11 的跨语言局域网多人聊天室：Go 负责 TCP Server，C++ 负责 Winsock2 Client。项目使用自定义的应用层 framing 和 JSON 协议，适合学习 TCP 字节流、并发、跨语言协议和异常清理。

## 项目特点

- Go Server：net、goroutine、channel；监听 0.0.0.0:8888。
- C++ Client：C++17、Winsock2、std::thread、std::atomic。
- TCP 多客户端群聊，支持 Wi-Fi 与 Ethernet 在同一局域网内互通。
- 中文消息统一使用 UTF-8。
- 用户名可以重复；用户通过自定义的 ASCII 字母/数字 user_code 区分身份。
- user_code 大小写不敏感，服务端在当前进程内保证唯一，并显示为 username#user_code。
- 支持 /help、/users、/quit。
- 支持长度头、半包、粘包、非法 JSON、非法 UTF-8、超长帧和异常断线处理。

## Architecture

~~~text
C++ Client A ─┐
C++ Client B ─┼── TCP: 4-byte big-endian length + UTF-8 JSON ── Go Server
C++ Client C ─┘                                      0.0.0.0:8888
~~~

Go Server 使用一个 Hub goroutine 管理客户端 map、注册、注销、广播和定向响应。每个已登录客户端有读取路径和写入路径；Client.Send 只由 Hub 写入和关闭，避免多个 goroutine 同时操作共享 map 或触发 send on closed channel。

## 目录结构

~~~text
server-go/                 Go TCP Server
client-cpp/                C++17 Windows Client
docs/protocol.md           framing 和 JSON 协议
docs/testing.md             构建、异常测试和局域网测试
docs/superpowers/           设计、实现计划和任务记录
~~~

## Protocol 简介

每条 TCP 应用层消息都是：

~~~text
4-byte unsigned length header, big-endian
        +
UTF-8 JSON payload
~~~

长度表示 JSON payload 的字节数，不是字符数。payload 必须满足 1 <= length <= 64 KiB。接收端先读取完整 4 字节 header，再读取完整 payload；因此 TCP 的粘包和拆包不会改变消息边界。

支持的主要消息类型包括：login、login_ok、login_error、chat、system、users_request、users_response、quit、error。完整字段和异常行为见 [docs/protocol.md](docs/protocol.md)。

## Windows 环境

推荐环境：

- Windows 11
- Go 1.20 或更高版本
- MinGW-w64 g++，支持 C++17
- Windows Terminal 或 PowerShell，并使用 UTF-8 code page
- C++ 依赖：client-cpp/third_party/json.hpp（nlohmann/json 单头文件）

## Build Server

PowerShell：

~~~powershell
cd server-go
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
go build -o chat-server.exe .
~~~

如果 Go 已经在系统 PATH 中，也可以直接运行：

~~~powershell
go build -o chat-server.exe .
~~~

## Build Client

在 client-cpp 目录执行：

~~~powershell
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client.exe -municode -lws2_32
~~~

使用 Visual Studio Developer PowerShell 时，可使用等价的 MSVC 构建方式：

~~~powershell
cl /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE src\main.cpp src\message.cpp src\protocol.cpp /Iinclude /Ithird_party ws2_32.lib /Fe:chat-client.exe
~~~

## Run：localhost

先启动 Server：

~~~powershell
cd server-go
.\chat-server.exe
~~~

另开 PowerShell 启动 Client：

~~~powershell
cd client-cpp
.\chat-client.exe 127.0.0.1 8888 Alice ALICE001
~~~

启动第二个客户端时必须使用不同的 user_code，例如：

~~~powershell
.\chat-client.exe 127.0.0.1 8888 Bob BOB001
~~~

登录后可以输入：

~~~text
/help
/users
你好，这是 Go 和 C++ 的跨语言聊天。
/quit
~~~

## Run：LAN

服务端必须监听所有网卡，而不是只监听 127.0.0.1。当前 Server 使用：

~~~text
0.0.0.0:8888
~~~

在服务端电脑执行 ipconfig，找到 Ethernet 或 Wi-Fi 网卡的 IPv4，例如 192.168.1.100。客户端使用该地址：

~~~powershell
.\chat-client.exe 192.168.1.100 8888 Alice ALICE001
~~~

Wi-Fi 和 Ethernet 可以互通，因为 TCP 建立在 IP 之上；只要两台设备路由可达即可。若连接失败，检查 Windows Private Network 防火墙、TCP 8888 入站规则、路由器 AP Isolation / Client Isolation、VLAN 和设备是否在同一网段。不要为了测试而关闭整个 Windows Defender Firewall。

详细测试流程见 [docs/testing.md](docs/testing.md)。

## Stage 8 稳定性行为

- 长度头不足 4 字节、payload 截断、长度为 0 或超过 64 KiB：拒绝当前 frame。
- 非法 UTF-8、malformed JSON、缺少 type、字段类型错误：拒绝当前消息；协议错误会终止当前连接。
- 客户端 EOF、connection reset、recv == 0、SOCKET_ERROR：清理当前连接，不影响 Server 和其他客户端。
- Server 突然关闭，或 C++ Client 在登录成功后的接收线程中遇到 frame/JSON 接收解析失败：C++ Client 统一显示 Connection to server lost.，设置 running = false，shutdown socket，等待接收线程 join，最后关闭 socket。登录阶段接收失败会显示对应的登录错误。
- Go Server 的 Send channel 由 Hub 统一写入和关闭；异常客户端不会使整个 Server 崩溃。
- C++ 不执行远程消息中的命令，不使用 system()、shell 或远程代码执行。

## 当前限制

第一版明确不包含：

- 自动重连；
- TLS；
- 账号密码和持久化数据库；
- 私聊、聊天室房间、文件/图片/音视频传输；
- GUI。

Stage 8 的 C++ 连接失败路径、协议防御代码审查和严格编译路径已经覆盖；当前没有独立的 C++ 非法帧自动化测试工具。C++ 交互式控制台“自然输入退出”的完整自动化证明仍未完成，不能将其标记为自动化通过。相关项目可按 [docs/testing.md](docs/testing.md) 手工或使用辅助工具复核。

## 后续方向

私聊、聊天室房间、Qt GUI、SQLite 聊天记录、TLS，以及 Android/Web 客户端。

## Resume 项目描述

独立开发基于 Go 与 C++ 的跨语言局域网多人聊天室，使用 TCP Socket 和 4-byte big-endian length + UTF-8 JSON 应用层协议处理粘包与拆包；Go Server 采用 goroutine/channel 管理并发客户端和广播，C++ Client 基于 Winsock2 与 std::thread 实现异步收发，并完成协议异常、断线清理和局域网通信设计。
