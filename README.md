# Go + C++ 跨语言局域网聊天室

一个适合本科生学习和展示的 Windows 局域网聊天室项目：Go 负责服务端，C++/Qt 6 负责客户端 GUI，使用 TCP、TLS 和自定义长度头 + JSON 协议实现跨语言通信。

## 功能

- Go TCP/TLS Server，监听 `0.0.0.0:8888`
- C++20/Qt 6 Windows GUI Client
- 多客户端群聊和 UTF-8 中文消息
- 用户名可以重复，用户代码必须唯一且不区分大小写
- 频道、私聊、在线成员列表
- 离线消息和历史消息
- 未读消息数量提示
- 成员、频道和私聊列表自动刷新
- Alice 本地 Host 模式：当前电脑直接启动 Go Server
- Wi-Fi 与以太网设备互通
- TCP 粘包、拆包、半包和超长消息处理
- Windows 防火墙和异常断线处理

## 架构

```text
C++/Qt Client A ─┐
C++/Qt Client B ─┼── TLS/TCP ── Go Server
C++/Qt Client C ─┘              0.0.0.0:8888
```

Go 服务端使用 goroutine、channel 和 Hub 模型管理客户端。每个客户端有独立的读取和写入流程，Hub 统一管理注册、注销、房间和广播。

## 通信协议

每条 TCP 应用层消息使用：

```text
4 字节无符号大端长度
+
UTF-8 JSON Payload
```

长度表示 JSON 的字节数，不是字符数。允许的 Payload 大小为 1 到 64 KiB。详细定义见 [docs/protocol.md](docs/protocol.md)。

## 目录

```text
server-go/                 Go 服务端
client-cpp/                C++ Socket 客户端和 Qt GUI
client-cpp/gui/            Qt 6 GUI 工程
docs/protocol.md           跨语言通信协议
docs/architecture.md       架构图
docs/testing.md             测试说明
screenshots/               项目截图
```

## Windows 环境

- Windows 11
- Go 1.20 或更高版本
- Qt 6.11.x MinGW 64-bit
- MinGW-w64 或 Visual Studio
- CMake 和 Ninja
- OpenSSL 3.x 运行库

## 构建 Go Server

```powershell
cd server-go
go test ./...
go build -o chat-server.exe .
```

启动 TLS Server：

```powershell
.\chat-server.exe -cert .\certs\server-lan.crt -key .\certs\server-lan.key -db .\chat.db
```

## 构建 Qt GUI

```powershell
cd client-cpp\gui
cmake --build build --config Release --parallel 2
```

生成文件：

```text
client-cpp\gui\build\lan-chat-gui.exe
```

## 运行 GUI

Bob 作为客户端运行：

```powershell
cd C:\Users\jking1\Desktop\LANChatClient_Alice_Bob_v8
.\lan-chat-gui.exe
```

Alice 作为本地 Host 运行：

```powershell
cd C:\Users\jking1\Desktop\LANChatHost_Alice_v8\client-cpp\gui\build
.\lan-chat-gui.exe
```

Alice Host 页面使用：

```text
用户名：Alice
用户代码：A001
Go Server：server-go\chat-server.exe
证书：certs\server-lan.crt
私钥：certs\server-lan.key
```

Bob 连接时填写 Alice 电脑的局域网 IPv4 地址、端口 `8888` 和 CA 文件 `certs\server-lan.crt`。

## 局域网测试

在 Alice 电脑查看 IPv4：

```powershell
ipconfig
```

在 Bob 电脑测试 TCP 端口：

```powershell
Test-NetConnection 192.168.1.100 -Port 8888
```

如果失败，检查 Windows Private Network 防火墙入站规则、路由器 AP Isolation、VLAN 和设备是否处于同一网段。不要关闭整个 Windows Defender Firewall。

## 已完成验收

- localhost 双向通信
- 三个客户端群聊协议测试
- 中文消息测试
- 快速连续发送测试
- 重复用户代码测试
- 客户端异常退出和重新连接测试
- Ethernet Server + Wi-Fi Client 局域网测试
- Alice Host + Bob Client GUI 测试
- 频道、私聊、未读提示和自动刷新测试

## 当前限制

- 主要面向 Windows
- 当前没有公网 NAT 穿透
- 没有文件、图片、语音和视频传输
- 管理员功能仍需继续完善
- 证书为局域网学习项目使用的自签名证书

## 后续方向

- 完善管理员菜单、禁言和踢出功能
- 房间权限和密码
- Qt 设置页和主题完善
- SQLite 聊天记录管理
- 正式 CA 或生产环境证书
- Linux、Android 或 Web 客户端

## 简历描述

独立开发基于 Go 与 C++ 的跨语言局域网多人聊天室，使用 TLS/TCP Socket 和 4 字节大端长度头 + UTF-8 JSON 协议解决跨语言通信与 TCP 粘包拆包问题；Go 服务端采用 goroutine/channel 管理并发客户端、房间和消息广播，C++/Qt 客户端基于 OpenSSL、Winsock2 与 std::thread 实现异步收发，并完成 Wi-Fi 与以太网跨设备通信测试。
