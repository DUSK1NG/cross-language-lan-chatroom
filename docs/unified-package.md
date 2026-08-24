# LAN Chat Windows 统一包

`LANChat-Windows-x64.zip` 是便携运行包：解压后直接双击 `LANChat.exe`，无需安装 Node.js、Go、CMake 或 Qt。

## 作为房主

1. 双击 `LANChat.exe`，选择“创建本地聊天室”。
2. 填写用户名和用户代码，点击“启动并连接”。
3. 程序启动本机 Go Server，并在首次需要时创建本机专用 TLS 证书、私钥和数据库。
4. 保持程序运行，房间会出现在成员的“附近聊天室”中。

新的房主数据保存在当前用户的 `%LocalAppData%\DUSK1NG\LAN Chat\host\`，其中 `server-lan.key` 绝不能发送给成员或上传 GitHub。

## 作为成员

1. 双击同一个 `LANChat.exe`，选择“加入局域网聊天室”。
2. 从“附近聊天室”选择房主，填写自己的用户名和用户代码。
3. 首次连接未知房主前，通过可信渠道核对指纹并勾选确认，再点击“确认并加入”。

自动发现不可用时，选择“改用手动连接”，填写房主真实 IPv4、端口 `8888` 和公开证书 `server-lan.crt`。跨电脑连接不能使用 `127.0.0.1`。

```powershell
Test-NetConnection <房主IPv4> -Port 8888
```

## 安全

运行包不含任何初始证书、私钥、数据库或聊天记录。TLS 保护传输，但 LAN Chat 只适用于可信局域网，不应向公网转发端口。UDP 自动发现可被同网段设备伪造，首次连接必须人工核对指纹。
