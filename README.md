# LAN Chat

面向 Windows 局域网的多人桌面聊天程序。它提供一个可由房主自行启动的 TLS 聊天服务，以及一个开箱即用的中文桌面客户端。

> 正式用户入口是现代 React + Qt WebEngine 客户端。旧 CLI 和旧 QML 仅保留给内部复用与故障诊断，不作为下载包或日常启动入口。

## 技术栈

| 层级 | 使用技术 | 作用 |
| --- | --- | --- |
| 桌面界面 | React、TypeScript、Vite、GSAP、Motion、Lucide | 中文聊天界面、交互状态与轻量动画；发布时静态资源嵌入客户端，不要求最终用户安装 Node.js。 |
| 桌面宿主 | Qt 6 WebEngine、QWebChannel、C++20 | 承载 React 界面，并把界面操作安全地转交给既有 C++ 业务层。 |
| 网络与加密 | C++、OpenSSL 3、TLS/TCP | 负责连接、证书校验、消息帧读写与后台工作线程。 |
| 服务端与存储 | Go 1.20、SQLite（modernc.org/sqlite） | 负责账号、房间、私信、权限、离线消息和本地持久化。 |
| 局域网发现 | Go UDP 广播、Qt Network | 房主自动公告公开证书与服务端口；成员自动发现同网段聊天室。 |
| 协议 | 4-byte big-endian 长度帧、UTF-8 JSON | 跨 Go/C++ 的确定性消息边界，单条载荷上限 64 KiB。 |
| 构建与交付 | CMake、Ninja、pnpm、PowerShell、GitHub Actions | 统一构建、测试、源码启动器和 Windows 统一运行包打包。 |

核心链路：`React/TypeScript → QWebChannel → C++ 控制器 → TLS/TCP → Go Server → SQLite`

## v1.2.0 更新内容

- 房主必须显式允许或拒绝每次成员连接；审批提示不会因点击页面空白处、按 `Esc` 或切换页面而消失。
- 已批准会话支持断线后的受控恢复；聊天页面补齐连续消息定位、未读计数与新消息提示。
- 增加连接诊断日志、性能档位与帧时间采样，便于定位 TLS、重连和渲染问题；日志不记录聊天内容、证书或私钥。
- 局域网可自动发现房主；不同物理网络可使用可信虚拟局域网，或通过 TCP 隧道手动填写主机名/IP、端口和房主公开证书。
- Windows 安装包与便携包已重新构建；不包含房主私钥、证书、数据库、聊天记录或日志。

## 下载与安装

| 适合谁 | 下载内容 | 如何启动 |
| --- | --- | --- |
| 房主和局域网成员（推荐） | `LANChat-Setup-x64.exe` | 双击安装。安装程序会创建桌面与开始菜单快捷方式。 |
| 不希望安装的用户 | `LANChat-Windows-x64.zip` | 完整解压后双击 `LANChat.exe`；不要只复制其中一个 EXE。 |

两个运行包都不需要编译器，也都可以创建本地聊天室或加入局域网聊天室。源码构建、发布脚本和自动准备工具链仅面向开发者，见文末“开发与验证”。

## 快速开始

### 房主：创建本地聊天室

1. 安装 `LANChat-Setup-x64.exe` 或完整解压 `LANChat-Windows-x64.zip`，启动现代客户端并选择“创建本地聊天室”。
2. 填写自己的用户名和用户代码，点击“启动并连接”。
3. 第一次打开完整客户端时，程序会先在本机生成服务器证书、私钥和数据库；随后启动 Go Server 并连接。
4. 房间会自动在当前局域网广播。保持房主程序运行，成员即可在“附近聊天室”看到它。

### 成员：加入局域网聊天室

1. 安装 `LANChat-Setup-x64.exe`，或完整解压 `LANChat-Windows-x64.zip`；不要只复制单个 EXE。
2. 启动 `LANChat.exe`，选择“加入局域网聊天室”。
3. 在“附近聊天室”中选择房主，填写自己的用户名/用户代码、核对公开证书指纹并点击“确认并加入”。每次成员连接都会进入待确认状态；房主在聊天界面右上角的“连接审批”中允许后，成员会在当前连接中直接进入聊天室。房主 IPv4 变化时会自动重新发现。

若列表为空（例如访客网络、隔离 VLAN 或 UDP 被阻止），可点“改用手动连接”，填写房主真实局域网 IPv4、端口 `8888` 和公开证书 `server-lan.crt`。另一台电脑不能填写 `127.0.0.1` 或 `0.0.0.0`。成员机可先检查端口可达性：

```powershell
Test-NetConnection <房主IPv4> -Port 8888
```

已部署的远程 Go TLS Server 也可通过“远程服务器”入口连接，并需要该服务端对应的公开 CA 证书。更完整的房主、成员与防火墙说明见 [发布与局域网使用说明](docs/release-setup.md)。

## 已实现功能

- TLS/TCP 传输加密与证书校验，4-byte big-endian 长度帧与 UTF-8 JSON 协议
- 大厅、公开/私有频道、频道邀请与成员管理
- 一对一私信、离线私信、历史消息与消息撤回
- 管理员禁言、踢出、频道权限与新设备接入审批
- 中文现代桌面 UI、表情、复制、引用、未读提示、连接状态与局域网自动发现

当前明确不提供文件、图片、音频或视频上传；端到端加密和跨公网连接也不在本版本范围内。

## 安全边界与使用前提

LAN Chat 适用于彼此信任的局域网，不应直接暴露为公网聊天服务：

- TLS 会加密传输并校验服务端证书，但本版本没有端到端加密。
- 用户名和用户代码是显示/路由标识，不是密码。除房主本机首次启动外，每一次成员连接都必须由在线房主确认；批准只对当前 TLS 连接有效。因此仍应只在可信网络使用，并在 Windows 防火墙中限制 TCP `8888`，不要做公网端口映射。
- 自动发现只广播房主的**公开**证书与指纹，但 UDP 广播本身不具备身份认证。首次加入前，请通过电话、当面或其他可信渠道核对房主名称与短指纹；不一致时不要连接。手动连接时只共享房主 IPv4、端口和 `server-lan.crt` 公共证书。
- `server-lan.key` 与 `chat.db` 分别包含房主私钥和本地聊天数据。请保存到仅房主账户可访问的本地目录，不要放在共享盘或同步盘，更不能提交到 GitHub、发送给成员或打包进发布资产。

源码、构建脚本、测试和文档可以公开；不能公开的是私钥、数据库、聊天记录和访问令牌等敏感材料。

## 开发与验证

本节仅面向贡献者和从源码构建的开发者；普通用户请使用上方运行包。源码目录可通过以下命令启动，首次构建如需准备工具链会明确询问：

```powershell
cd C:\path\to\chat_X
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\start-gui.ps1
```

强制重建时追加 `-Rebuild`。发布维护者可使用 [发布清单](docs/github-publishing.md) 中的打包命令。

统一现代构建与测试：

```powershell
cd C:\path\to\chat_X
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test
```

该命令会构建前端、Go Server 和 Qt WebEngine 客户端，并运行 CTest。构建产物固定在 `out\modern-msvc-x64`，由 Git 忽略。

更多资料： [架构说明](docs/architecture.md) · [发布清单](docs/github-publishing.md) · [发布前验收](docs/release-acceptance.md) · [局域网使用说明](docs/release-setup.md)

## 项目结构

```text
frontend/             React / TypeScript / Vite 现代界面
client-cpp/gui/       Qt 6、QWebChannel 与既有 C++ 网络控制器
server-go/            Go TLS/TCP 服务端、Hub 与 SQLite 存储
scripts/              统一构建、启动和发布脚本
tools/bootstrap/      静态源码启动器
docs/                 使用、架构与发布文档
```
