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
| 协议 | 4-byte big-endian 长度帧、UTF-8 JSON | 跨 Go/C++ 的确定性消息边界，单条载荷上限 64 KiB。 |
| 构建与交付 | CMake、Ninja、pnpm、PowerShell、GitHub Actions | 统一构建、测试、源码启动器和 Windows 统一运行包打包。 |

核心链路：`React/TypeScript → QWebChannel → C++ 控制器 → TLS/TCP → Go Server → SQLite`

## 选择适合你的包

| 场景 | 获取内容 | 启动方式 | 是否需要编译器 |
| --- | --- | --- | --- |
| 房主和局域网成员（推荐） | `LANChat-Setup-x64.exe` | 双击安装；创建桌面/开始菜单快捷方式 | 不需要；同一程序可创建本地聊天室或加入局域网聊天室 |
| 房主和局域网成员（便携） | `LANChat-Windows-x64.zip` | 解压后双击 `lan-chat-gui.exe` | 不需要；同一程序可创建本地聊天室或加入局域网聊天室 |
| 开发者（可选） | `LANChat-Source-Launcher-windows-x64.zip` | 双击 `LANChat-Launcher.exe` | 首次明确确认后自动准备工具链，后续增量构建 |

标准 Windows 统一运行包由维护者在 Windows 发布机构建：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1
```

GitHub 源码启动器仅供开发者使用，由维护者在 Windows 发布机构建：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-source-launcher.ps1
```

尚未生成发布资产时，可在克隆后的源码目录直接启动：

```powershell
cd C:\path\to\chat_X
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\start-gui.ps1
```

该入口会优先运行已验证的 `out\modern-msvc-x64\lan-chat-gui.exe`；仅在本地没有可用构建时才构建 React 界面、Go 服务端和现代 Qt WebEngine 客户端。首次构建如需安装 Node.js、pnpm 等工具链，会先询问，不会静默提权。强制重建：`powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\start-gui.ps1 -Rebuild`。

## 快速开始

### 房主：创建本地聊天室

1. 安装 `LANChat-Setup-x64.exe` 或完整解压 `LANChat-Windows-x64.zip`，启动现代客户端并选择“创建本地聊天室”。
2. 填写自己的用户名和用户代码，点击“启动并连接”。
3. 第一次打开完整客户端时，程序会先在本机生成服务器证书、私钥和数据库；随后启动 Go Server 并连接。
4. 通过 `ipconfig` 获取本机真实局域网 IPv4，并将 IPv4、端口 `8888` 和公开证书 `server-lan.crt` 提供给成员。

### 成员：加入局域网聊天室

1. 安装 `LANChat-Setup-x64.exe`，或完整解压 `LANChat-Windows-x64.zip`；不要只复制单个 EXE。
2. 启动 `lan-chat-gui.exe`，选择“加入局域网聊天室”。
3. 填写房主的真实局域网 IPv4、端口 `8888`、自己的用户名/用户代码，并选择房主提供的 `server-lan.crt`。

同一台电脑测试可填写 `127.0.0.1`；另一台电脑不能填写 `127.0.0.1` 或 `0.0.0.0`。成员机可先检查端口可达性：

```powershell
Test-NetConnection <房主IPv4> -Port 8888
```

已部署的远程 Go TLS Server 也可通过“远程服务器”入口连接，并需要该服务端对应的公开 CA 证书。更完整的房主、成员与防火墙说明见 [发布与局域网使用说明](docs/release-setup.md)。

## 已实现功能

- TLS/TCP 安全连接，4-byte big-endian 长度帧与 UTF-8 JSON 协议
- 大厅、公开/私有频道、频道邀请与成员管理
- 一对一私信、离线私信、历史消息与消息撤回
- 管理员禁言、踢出和频道权限
- 中文现代桌面 UI、表情、复制、引用、未读提示与连接状态

当前明确不提供文件、图片、音频或视频上传；端到端加密、UDP 自动发现和跨公网连接也不在本版本范围内。

## 安全边界

`server-lan.key` 是房主的 TLS 私钥，只能保留在房主电脑：

- 可以向成员共享：房主 IPv4、端口与 `server-lan.crt` 公共证书。
- 绝不能向成员共享或提交到 GitHub：`server-lan.key`、`chat.db`、聊天记录、访问令牌和构建输出。

这不是“禁止上传 GitHub”。源码、构建脚本、测试和文档都可以公开；被限制的是可能让他人伪装成房主或泄露本地数据的敏感材料。

## 开发与验证

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
