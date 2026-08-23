# LAN Chat Windows 统一包

此包可在任何 Windows 电脑上使用：房主选择“创建本地聊天室”，成员选择“加入局域网聊天室”。不需要安装 Node.js、Go、CMake 或 Qt。

## 作为房主

1. 双击 `lan-chat-gui.exe`，选择“创建本地聊天室”。
2. 填写用户名和用户代码，点击“启动并连接”。
3. 程序会在 `server-go\certs\` 下首次生成本机专用的 `server-lan.crt` 和 `server-lan.key`，并在 `server-go\chat.db` 创建本地数据。
4. 通过 `ipconfig` 获取真实局域网 IPv4，把 IPv4、端口 `8888` 和 **仅公开证书** `server-lan.crt` 发给成员。

## 作为成员

1. 双击同一个 `lan-chat-gui.exe`，选择“加入局域网聊天室”。
2. 填写房主的真实 IPv4、端口 `8888`、自己的用户名和用户代码。
3. 选择房主提供的 `server-lan.crt` 后连接。

跨电脑连接不能使用 `127.0.0.1`。如需排查网络，请运行：

```powershell
Test-NetConnection <房主IPv4> -Port 8888
```

## 安全

- 包内不含证书、私钥、数据库或聊天记录。
- `server-lan.key` 只留在房主电脑，绝不能发送给成员或上传 GitHub。
- 房主仅分享 IPv4、端口和 `server-lan.crt`。
