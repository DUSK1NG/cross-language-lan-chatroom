# LAN Chat Windows 单包使用说明

下载并解压发布包后，运行根目录的 `lan-chat-gui.exe`。同一个程序既可作为房主创建聊天室，也可作为成员加入已有聊天室。

发布包不包含 TLS 证书、私钥、数据库或聊天记录，避免房主私钥和本地数据被意外分享。

## 先判断你的角色

| 你的目的 | 在程序首页选择 | 是否需要 Go Server |
| --- | --- | --- |
| 在本机创建聊天室，供局域网成员加入 | 创建本地聊天室 | 程序会自动启动包内 Go Server |
| 加入其他电脑创建的聊天室 | 加入局域网聊天室 | 不需要，本机只运行 GUI |

## 房主：创建本地聊天室

1. 确认当前电脑在局域网中的 IPv4 地址，例如 `192.168.1.10`：

   ```powershell
   ipconfig
   ```

2. 不需要安装 OpenSSL，也不需要手动创建证书。运行 `lan-chat-gui.exe`，首页选择 **创建本地聊天室**。
3. 输入自己的用户名和用户代码。下列路径默认指向发布包中的正确位置，通常无需改动：

   ```text
   server-go\chat-server.exe
   certs\server-lan.crt
   certs\server-lan.key
   server-go\chat.db
   ```

4. 点击 **启动本地聊天室**。当证书和私钥都不存在时，程序会自动生成 `certs\server-lan.crt` 与 `certs\server-lan.key`，证书包含 `localhost`、`127.0.0.1` 和当前电脑的局域网 IPv4 地址。
5. 程序会启动本机 Go TLS Server 并自动以房主身份连接。将本机 IPv4 地址、端口 `8888` 和 `certs\server-lan.crt` 发给成员。只发送 `.crt`，绝不发送 `.key`。
6. 首次被 Windows 防火墙询问时，只允许 `chat-server.exe` 通过**专用网络**；不要关闭整个防火墙。

## 成员：加入房主的聊天室

1. 从房主获得房主的 IPv4 地址（例如 `192.168.1.10`）和公开证书 `server-lan.crt`。
2. 将收到的证书保存为程序目录中的 `certs\server-lan.crt`。不要索取或保存 `server-lan.key`。
3. 运行 `lan-chat-gui.exe`，选择 **加入局域网聊天室**。
4. 填写房主 IPv4、端口 `8888`、自己的用户名和用户代码，并选择刚保存的 `.crt` 文件。
5. 点击 **连接**。成功后即可加入 `lobby`、创建/加入频道、发送群聊或私聊消息。

## 本机测试与局域网测试

- 同一台电脑测试时，成员可以填写 `127.0.0.1`；证书会在房主首次启动本地聊天室时自动生成。
- 两台电脑测试时，成员必须填写房主电脑的真实 IPv4，不能填写 `127.0.0.1`。
- Wi-Fi 与网线设备可以互通；连接失败时可先在成员电脑运行：

  ```powershell
  Test-NetConnection <房主IPv4> -Port 8888
  ```

  然后检查防火墙、路由器 AP/Client Isolation 与 VLAN 设置。

## 安全规则

- `server-lan.key` 只能留在房主电脑，不能发给成员，也不能上传 GitHub。
- 房主 IPv4 变更后，删除房主包内的 `certs\server-lan.crt` 和 `certs\server-lan.key`，再次启动本地聊天室会生成新证书；随后把新的 `.crt` 发给成员。
- 每个在线用户的“用户代码”不区分大小写且必须唯一；用户名可重复。
