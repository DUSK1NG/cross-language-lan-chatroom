# LAN Chat 发布与局域网使用说明

LAN Chat 使用现代 React + Qt WebEngine 客户端。标准 Windows 交付是统一运行包：同一份包可在任意电脑上作为房主创建本地聊天室，也可作为成员加入局域网聊天室。

| 包 | 用途 | 包含内容 | 不包含内容 |
| --- | --- | --- | --- |
| `LANChat-Setup-x64.exe` | 房主和局域网成员（推荐） | 统一运行包的安装器、开始菜单/桌面快捷方式、卸载项 | 初始私钥、证书、数据库、源码、编译器、自动编译入口 |
| `LANChat-Windows-x64.zip` | 房主和局域网成员（便携） | 现代 GUI、Qt 运行时与 `server-go\chat-server.exe` | 初始私钥、证书、数据库、源码、编译器、自动编译入口 |
| `LANChat-Source-Launcher-windows-x64.zip` | 开发者（可选） | 源码与 `LANChat-Launcher.exe` | 私钥、数据库、聊天记录 |

维护者生成标准统一运行包的命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1
```

统一运行包面向所有普通用户：双击即可运行，不需要构建工具。源码启动器仍作为开发者可选工具，双击后可以在明确确认依赖安装后自动构建。

## 房主：创建本地聊天室

1. 运行 `LANChat-Setup-x64.exe` 安装，或解压 `LANChat-Windows-x64.zip` 后双击根目录 `LANChat.exe`。
2. 选择“创建本地聊天室”，填写自己的用户名和用户代码。
3. 点击“启动并连接”。程序会启动本机 Go Server。第一次打开完整客户端时，已在 `server-go\certs` 与 `server-go\chat.db` 自动生成证书、私钥和数据库。
4. 程序每秒自动广播本机房间。保持房主程序运行，成员便会在“附近聊天室”中看到该房间。

首次 Windows 防火墙询问时，只允许 `chat-server.exe` 通过“专用网络”。不要关闭整个防火墙。

## 成员：加入局域网聊天室

1. 运行 `LANChat-Setup-x64.exe` 安装，或解压 `LANChat-Windows-x64.zip` 后双击根目录 `LANChat.exe`。
2. 选择“加入局域网聊天室”。
3. 等待“附近聊天室”扫描完成，选择房主，填写自己的用户名和用户代码后点击“确认并加入”。
4. 首次加入会将房主的**公开证书**保存在本机配置目录，并固定校验证书指纹；网络重新连接导致 IPv4 改变时，无需重新填写地址或传送证书。

若“附近聊天室”没有房间，请先点“刷新”。访客网络、AP/Client Isolation、隔离 VLAN 或阻止 UDP 广播的网络不支持自动发现；此时点“改用手动连接”，填写房主的真实 IPv4、端口 `8888` 和公开证书 `server-lan.crt`。跨电脑手动测试不能使用 `127.0.0.1`。

## 卸载

通过 Windows“设置 → 应用 → 已安装的应用”卸载 `LAN Chat`，或运行安装目录中的 `unins000.exe`。卸载器会删除程序与快捷方式，但会保留房主运行后生成的证书、私钥和数据库，避免静默删除聊天数据；确认不再需要时再手动删除安装目录残留的 `server-go` 文件夹。

## 连接失败排查

在成员电脑运行：

```powershell
Test-NetConnection <房主IPv4> -Port 8888
```

若失败，按顺序检查：

1. 房主客户端是否仍在运行，且 Go Server 是否监听 `0.0.0.0:8888`。
2. 两台电脑是否处于同一可互访局域网，且没有 AP/Client Isolation、访客网络或隔离 VLAN。
3. Windows 防火墙是否允许 `chat-server.exe` 通过专用网络的 TCP 8888；成员机是否允许 `lan-chat-gui.exe` 接收 UDP 38888 广播。
4. 自动发现失败时，使用“改用手动连接”并以 `Test-NetConnection <房主IPv4> -Port 8888` 验证 TCP 可达性。
5. 如果房主重装程序、手动删除证书/私钥或更换了身份，成员会看到新的证书指纹，需再次确认后加入。

## 私钥规则

`server-lan.key` 是服务器身份凭据，只能保存在房主电脑。不要发送给成员，不要放进压缩包，不要上传 GitHub。房主 IP 变化**不需要**删除或重建证书：自动发现会用新的 IPv4 与已固定的公开证书重新连接。只有在明确要更换房主身份时，才删除旧的证书/私钥；届时成员应核对新指纹并重新确认。
