# LAN Chat v1.0.1 发布包部署说明

发布包分为 `alice-host` 与 `bob-client` 两部分。公开下载包不包含 TLS 证书、私钥、数据库或聊天记录。

## Alice Host：创建局域网聊天室

1. 解压 `LANChat-v1.0.1-alice-host.zip`。
2. 在 Alice 电脑上确认局域网 IPv4 地址，例如 `192.168.1.10`：

   ```powershell
   ipconfig
   ```

3. 安装 OpenSSL 3.x 并在 `alice-host` 目录执行以下命令。将示例 IP 换成 Alice 电脑的真实 IPv4：

   ```powershell
   $ServerIp = "192.168.1.10"
   New-Item -ItemType Directory -Force .\certs
   openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 825 `
     -keyout .\certs\server-lan.key `
     -out .\certs\server-lan.crt `
     -subj "/CN=$ServerIp" `
     -addext "subjectAltName=IP:$ServerIp,IP:127.0.0.1,DNS:localhost"
   ```

4. 启动服务端：

   ```powershell
   .\server-go\chat-server.exe -addr "0.0.0.0:8888" `
     -cert ".\certs\server-lan.crt" `
     -key ".\certs\server-lan.key" `
     -db ".\server-go\chat.db"
   ```

5. 在另一个 PowerShell 窗口运行 `.\lan-chat-gui.exe`，选择“创建本地聊天室”或连接 `127.0.0.1:8888`。

## Bob Client：加入聊天

1. 解压 `LANChat-v1.0.1-bob-client.zip`。
2. 由 Alice 通过可信方式把**公开证书** `server-lan.crt` 复制到 `bob-client\certs\server-lan.crt`。
3. 运行 `.\lan-chat-gui.exe`，输入 Alice 电脑的 IPv4、端口 `8888`、自己的用户名与用户代码，并选择该 CA 文件。

## 安全与网络提示

- 绝不能把 `server-lan.key` 发送给 Bob 或上传到 GitHub。
- 更换 Alice 的 IPv4 后，要重新生成包含新 IP 的证书，并重新分发新的 `.crt` 文件。
- Windows 防火墙应允许服务端程序或 TCP 8888 的 Private Network 入站连接；不要关闭整个防火墙。
- Wi-Fi 与网线不是障碍，只要两台设备位于允许互访的同一局域网，且路由器未开启 AP/Client Isolation。
