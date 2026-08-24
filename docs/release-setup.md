# 发布包与局域网使用说明

## 选择运行包

| 场景 | 文件 | 说明 |
| --- | --- | --- |
| 推荐安装 | `LANChat-Setup-x64.exe` | 为当前 Windows 用户安装，提供开始菜单、可选桌面快捷方式和卸载项。 |
| 便携运行 | `LANChat-Windows-x64.zip` | 完整解压后双击 `LANChat.exe`。不要只复制其中一个 EXE。 |

两个包都包含同一套 GUI、Go Server 和运行库。它们不包含房主证书、私钥、数据库或聊天记录。

## 房主：创建本地聊天室

1. 运行 `LANChat.exe`，选择“创建本地聊天室”。
2. 填写用户名和用户代码并点击“启动并连接”。
3. 程序首次启动时自动创建：

```text
%LocalAppData%\DUSK1NG\LAN Chat\host\certs\server-lan.crt
%LocalAppData%\DUSK1NG\LAN Chat\host\certs\server-lan.key
%LocalAppData%\DUSK1NG\LAN Chat\host\chat.db
```

房主会成为管理员。首次看到 Windows 防火墙提示时，只允许 `chat-server.exe` 通过“专用网络”；不要关闭整个防火墙，也不要把 TCP `8888` 映射到公网。

## 成员：加入局域网聊天室

1. 运行同一个 `LANChat.exe`，选择“加入局域网聊天室”。
2. 在“附近聊天室”中选择房主，填写用户名和用户代码。
3. 通过当面、电话或其他可信渠道核对房主名称和页面显示的证书指纹，然后确认加入。
4. 房主在聊天窗口右上角打开“连接审批”，核实后点击“允许连接”。成员会在当前连接中直接进入聊天室，不需要再次点击加入。

如果附近列表为空，请用“手动连接”填写房主真实局域网 IPv4、端口 `8888` 和公开证书 `server-lan.crt`。跨电脑时不能使用 `127.0.0.1` 或 `0.0.0.0`。成员机可先检查：

```powershell
Test-NetConnection <房主IPv4> -Port 8888
```

## 卸载与安全

从 Windows“设置 → 应用”卸载 `LAN Chat`，或运行安装目录中的 `unins000.exe`。卸载不会删除房主数据；如需彻底清除身份和聊天记录，请在确认不再需要后手动删除 `%LocalAppData%\DUSK1NG\LAN Chat\host\`。

LAN Chat 适用于彼此信任的局域网。TLS 会加密并校验证书，但不是端到端加密，房主服务端可以读取并保存聊天内容。用户名与用户代码不是密码；每一次成员连接均须由房主确认，批准只对当前连接有效。`server-lan.key`、`chat.db` 和聊天记录不能发送给成员、放入同步盘或提交到 GitHub。
