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

1. 运行 `LANChat-Setup-x64.exe` 安装，或解压 `LANChat-Windows-x64.zip` 后双击根目录 `lan-chat-gui.exe`。
2. 选择“创建本地聊天室”，填写自己的用户名和用户代码。
3. 点击“启动并连接”。程序会启动本机 Go Server；首次使用时在 `server-go\certs` 与 `server-go\chat.db` 自动生成证书、私钥和数据库。
4. 用 `ipconfig` 查看房主的 IPv4，把 **IPv4、端口 `8888`、`server-lan.crt`** 发给成员。

首次 Windows 防火墙询问时，只允许 `chat-server.exe` 通过“专用网络”。不要关闭整个防火墙。

## 成员：加入局域网聊天室

1. 运行 `LANChat-Setup-x64.exe` 安装，或解压 `LANChat-Windows-x64.zip` 后双击根目录 `lan-chat-gui.exe`。
2. 选择“加入局域网聊天室”。
3. 填写房主的真实 IPv4、端口 `8888`、自己的用户名和用户代码。
4. 选择房主提供的公开证书 `server-lan.crt`，然后连接。

同机测试可填 `127.0.0.1`；跨电脑测试不能填该地址。成员使用同一程序加入房主，只有房主选择“创建本地聊天室”时才会启动本地服务端。

## 卸载

通过 Windows“设置 → 应用 → 已安装的应用”卸载 `LAN Chat`，或运行安装目录中的 `unins000.exe`。卸载器会删除程序与快捷方式，但会保留房主运行后生成的证书、私钥和数据库，避免静默删除聊天数据；确认不再需要时再手动删除安装目录残留的 `server-go` 文件夹。

## 连接失败排查

在成员电脑运行：

```powershell
Test-NetConnection <房主IPv4> -Port 8888
```

若失败，按顺序检查：

1. 房主客户端是否仍在运行，且 Go Server 是否监听 `0.0.0.0:8888`。
2. 房主 IPv4 是否正确，而不是 `127.0.0.1`。
3. 房主网络是否为“专用网络”，防火墙是否允许 TCP 8888。
4. 路由器是否开启 AP/Client Isolation，或成员是否在访客网络、隔离 VLAN。
5. 成员选择的是否为当前房主提供的 `server-lan.crt`。

## 私钥规则

`server-lan.key` 是服务器身份凭据，只能保存在房主电脑。不要发送给成员，不要放进压缩包，不要上传 GitHub。房主 IP 变化后，可在房主机删除旧的 `server-lan.crt` 与 `server-lan.key`，再创建本地聊天室生成新的证书；随后重新把新的 `.crt` 发给成员。
