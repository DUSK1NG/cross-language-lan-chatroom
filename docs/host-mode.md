# 无服务器部署的局域网模式

本项目支持两种启动模式：

- `server` 模式：客户端连接已经部署好的 Go 服务端，适合云服务器或一台长期运行的电脑。
- `host` 模式：某个用户的 C++ 客户端自动启动本机的 Go 服务端子进程，自己成为房主；其他用户连接房主的局域网 IPv4 地址。

注意：`host` 模式并不是没有服务器，而是把服务器临时运行在房主电脑上。房主退出后，聊天室也会结束。

## 房主启动

默认假设当前目录为 `client-cpp`，并且存在：

```text
..\server-go\chat-server.exe
..\certs\server.crt
..\certs\server.key
```

启动房主：

```powershell
.\chat-client.exe --host Alice --ca-file ..\certs\server.crt
```

也可以显式指定服务端和证书：

```powershell
.\chat-client.exe --host Alice `
  --server-exe ..\server-go\chat-server.exe `
  --cert ..\certs\server.crt `
  --key ..\certs\server.key `
  --ca-file ..\certs\server.crt
```

房主需要在 Windows 防火墙中允许 TCP `8888` 入站访问，并把自己的局域网 IPv4 地址告诉其他用户，例如 `192.168.1.100`。

## 访客加入

访客只需要输入房主 IP、端口和自定义名称：

```powershell
.\chat-client.exe --guest 192.168.1.100 8888 Bob --ca-file ..\certs\server.crt
```

访客模式自动生成唯一临时代码，例如 `Bob#GUEST7K2P9M`，不需要注册、密码或 SQLite 账号数据库。

## 部署服务器开关

以后部署到云服务器时，继续使用普通 server 模式：

```powershell
.\chat-client.exe 203.0.113.10 8888 Alice ALICE001 --password password123 --ca-file .\certs\lan-ca.pem
```

因此客户端有明确的模式开关：`--host` 使用本机临时聊天服务，普通参数连接远程服务端，`--guest` 加入其他用户创建的局域网聊天室。
