# LAN Chat Windows 单包使用说明

下载并解压 `LANChat-v1.0.1-Windows-x64.zip` 后，直接运行根目录的 `lan-chat-gui.exe`。同一个程序既可以作为房主创建聊天室，也可以作为成员加入已有聊天室。

公开发布包不包含 TLS 证书、私钥、数据库或聊天记录。这样可以避免房主的私钥和本地数据被意外分享。

## 先判断你的角色

| 你的目的 | 在程序首页选择 | 是否需要 Go Server |
| --- | --- | --- |
| 这台电脑创建并托管聊天室 | **创建本地聊天室** | 需要，包内已提供 |
| 加入朋友已经创建的聊天室 | **加入局域网聊天室** | 不需要 |
| 连接已经部署好的服务端 | **远程服务器** | 不需要 |

## 房主：创建本地聊天室

1. 确认当前电脑在局域网中的 IPv4 地址，例如 `192.168.1.10`：

   ```powershell
   ipconfig
   ```

2. 在解压后的程序目录打开 PowerShell，安装 OpenSSL 3.x 后生成证书。将示例 IP 改为本机真实 IPv4：

   ```powershell
   $ServerIp = "192.168.1.10"
   New-Item -ItemType Directory -Force .\certs
   openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 825 `
     -keyout .\certs\server-lan.key `
     -out .\certs\server-lan.crt `
     -subj "/CN=$ServerIp" `
     -addext "subjectAltName=IP:$ServerIp,IP:127.0.0.1,DNS:localhost"
   ```

3. 双击或运行 `lan-chat-gui.exe`，首页选择 **创建本地聊天室**。
4. 输入自己的用户名和用户代码。其余四个路径默认指向发布包中的正确位置，通常无需改动：

   ```text
   server-go\chat-server.exe
   certs\server-lan.crt
   certs\server-lan.key
   server-go\chat.db
   ```

5. 点击 **启动本地聊天室**。程序会启动本机 Go TLS Server 并自动以房主身份连接。
6. 将以下两项告诉成员：本机 IPv4 地址和端口 `8888`；并通过可信方式只发送 `certs\server-lan.crt`。
7. 首次被 Windows 防火墙询问时，只允许 `chat-server.exe` 通过**专用网络**；不要关闭整个防火墙。

## 成员：加入房主的聊天室

1. 从房主获得两项信息：房主的 IPv4 地址，例如 `192.168.1.10`，以及公开证书 `server-lan.crt`。
2. 将收到的证书保存为本程序目录中的 `certs\server-lan.crt`。不要索取或保存 `server-lan.key`。
3. 运行 `lan-chat-gui.exe`，选择 **加入局域网聊天室**。
4. 填写房主 IPv4、端口 `8888`、自己的用户名和用户代码，并选择刚保存的 `.crt` 文件。
5. 点击 **连接**。成功后即可加入 `lobby`、创建/加入频道、发送群聊或私聊消息。

## 本机测试与局域网测试

- 同一台电脑测试时，成员可以填写 `127.0.0.1`；证书仍应由房主按上方步骤生成。
- 两台电脑测试时，成员必须填写房主电脑的真实 IPv4，不能填写 `127.0.0.1`。
- Wi-Fi 与网线设备可以互通；连接失败时可先在成员电脑运行：

  ```powershell
  Test-NetConnection <房主IPv4> -Port 8888
  ```

  然后检查防火墙、路由器 AP/Client Isolation 与 VLAN 设置。

## 安全规则

- `server-lan.key` 只能留在房主电脑，不能发给成员，也不能上传 GitHub。
- 房主 IPv4 变更后，应重新生成证书，并把新的 `.crt` 发给成员。
- 每个在线用户的“用户代码”不区分大小写且必须唯一；用户名可重复。
