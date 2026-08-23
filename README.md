# LAN Chat

面向 Windows 局域网的多人聊天程序。正式桌面客户端使用 **React + Qt WebEngine + QWebChannel**；Go 服务端提供 TLS/TCP、账号、频道、私信、离线私信、管理员与频道权限。

> 旧命令行客户端和旧 QML 界面不再是默认用户入口。它们只保留为内部网络层复用与故障诊断能力。

## 使用方式

| 场景 | 获取内容 | 启动方式 | 是否需要编译器 |
| --- | --- | --- | --- |
| 房主 / 开发者 | `LANChat-Source-Launcher-windows-x64.zip` | 双击 `LANChat-Launcher.exe` | 首次按确认自动准备，之后增量构建 |
| 局域网成员测试 | `LANChat-member-modern-x64.zip` | 双击 `lan-chat-gui.exe` | 不需要 |

GitHub 源码启动器包由维护者在 Windows 发布机构建：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-source-launcher.ps1
```

发布资产尚未生成前，克隆源码后的本地启动命令是：

```powershell
cd C:\path\to\chat_X
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\start-gui.ps1
```

该入口会优先启动已验证的 `out\modern-msvc-x64\lan-chat-gui.exe`；没有可用本地构建时，才会构建 React 界面、Go 服务端和现代 Qt WebEngine GUI。首次构建会安装 Node.js 与 pnpm 等所需工具链，并在安装前征求一次确认；不会静默提权。需要强制重建时使用：`powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\start-gui.ps1 -Rebuild`。

## 连接聊天室

- **创建本地聊天室（房主）**：在现代客户端首页选择“创建本地聊天室”。程序在本机启动 Go Server，并在首次需要时生成本机的证书、私钥和数据库。
- **加入局域网聊天室（成员）**：向房主索取 IPv4、端口 `8888` 与公开证书 `server-lan.crt`；在客户端填写这些信息后连接。
- **远程服务器**：填写已经部署的 Go TLS Server 地址和其公开 CA 证书。

同机测试可以填 `127.0.0.1`；另一台电脑必须填写房主真实的局域网 IPv4。成员机可先运行：

```powershell
Test-NetConnection <房主IPv4> -Port 8888
```

详细的房主与成员操作见 [发布与局域网使用说明](docs/release-setup.md)。

## 功能

- TLS/TCP 安全连接，4-byte big-endian 长度帧与 UTF-8 JSON 协议
- 大厅、公开/私有频道、频道邀请与成员管理
- 一对一私信、离线私信、历史消息与消息撤回
- 管理员禁言、踢出和频道权限
- 中文现代桌面 UI、表情、复制、引用、未读提示与连接状态

当前不提供文件、图片、音频或视频上传。

## 安全边界

`server-lan.key` 是房主的 TLS 私钥。它只能留在房主电脑：

- 可以共享：房主 IPv4、端口与 `server-lan.crt` 公共证书。
- 绝不能共享或提交到 GitHub：`server-lan.key`、`chat.db`、聊天记录、访问令牌、构建输出。

这不是禁止上传 GitHub；源码、构建脚本、测试、文档和公开证书的使用说明都可以公开。限制的是会让他人伪装为房主或泄露本地数据的敏感材料。

## 开发与验证

统一现代构建与测试：

```powershell
cd C:\path\to\chat_X
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test
```

该命令会构建前端、Go Server 和 Qt WebEngine GUI，并运行 CTest。构建产物固定在 `out\modern-msvc-x64`，不会提交到 Git。

架构和开发边界见 [architecture.md](docs/architecture.md)，GitHub 发布清单见 [github-publishing.md](docs/github-publishing.md)，本次发布前验收项见 [release-acceptance.md](docs/release-acceptance.md)。

## 项目结构

```text
frontend/             React / TypeScript / Vite 现代界面
client-cpp/gui/       Qt 6、QWebChannel 与既有 C++ 网络控制器
server-go/            Go TLS/TCP 服务端、Hub 与 SQLite 存储
scripts/              统一构建、启动和发布脚本
tools/bootstrap/      静态源码启动器
docs/                 使用、架构与发布文档
```
