# LAN Chat 发布与局域网使用说明

LAN Chat 使用现代 React + Qt WebEngine 客户端。发布时分为两个用途明确的包，避免把房主密钥或开发工具发给成员。

| 包 | 用途 | 包含内容 | 不包含内容 |
| --- | --- | --- | --- |
| `LANChat-Source-Launcher-windows-x64.zip` | 房主、开发者 | 源码与 `LANChat-Launcher.exe` | 私钥、数据库、聊天记录 |
| `LANChat-member-modern-x64.zip` | 另一台电脑的成员测试 | 已部署现代 GUI、Qt 运行时、公开 CA 证书位置说明 | 源码、编译器、Go Server、私钥、数据库、自动编译入口 |

维护者生成两个包的命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-source-launcher.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-release.ps1
```

第一个包面向 GitHub 下载者：双击后可以在明确确认依赖安装后自动构建。第二个包只面向局域网成员测试：双击即可运行，但绝不包含自动编译或服务端能力。

## 房主：创建本地聊天室

1. 解压源码启动器包，双击 `LANChat-Launcher.exe`。
2. 第一次启动会检查 Node.js、pnpm、Go、MSVC、Qt WebEngine 与 OpenSSL。缺失时会说明将安装的内容，输入 `Y` 后才会继续。
3. 现代客户端启动后选择“创建本地聊天室”，填写自己的用户名和用户代码。
4. 点击“启动并连接”。程序会启动本机 Go Server；首次使用时生成证书、私钥和数据库。
5. 用 `ipconfig` 查看房主的 IPv4，把 **IPv4、端口 `8888`、`server-lan.crt`** 发给成员。

首次 Windows 防火墙询问时，只允许 `chat-server.exe` 通过“专用网络”。不要关闭整个防火墙。

## 成员：加入局域网聊天室

1. 解压 `LANChat-member-modern-x64.zip`，双击根目录 `lan-chat-gui.exe`。
2. 选择“加入局域网聊天室”。
3. 填写房主的真实 IPv4、端口 `8888`、自己的用户名和用户代码。
4. 选择房主提供的公开证书 `server-lan.crt`，然后连接。

同机测试可填 `127.0.0.1`；跨电脑测试不能填该地址。成员包不启动服务器，也不执行自动编译。

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
