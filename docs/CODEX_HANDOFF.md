# LAN Chat 项目交接说明

更新：2026-08-23。此文档供新的 Codex 对话接手项目；以仓库实际代码和 `git status` 为准。

## 当前状态

- 仓库：<https://github.com/DUSK1NG/cross-language-lan-chatroom>
- 主工作目录：`C:\Users\jking1\Desktop\my-project\chat_X`
- 主分支：`master`；不要在未检查工作树前假设提交号或构建产物仍然有效。
- 正式桌面客户端：**React + TypeScript + Vite + Qt 6 WebEngine + QWebChannel + C++20**。
- Go 服务端：TLS/TCP + SQLite，固定监听 `0.0.0.0:8888`。
- 旧 CLI 不属于用户入口；旧 QML 仅保留为故障诊断回退，可通过 `lan-chat-gui.exe --legacy-qml` 显式进入。

## 架构边界

```text
React UI
  -> QWebChannel / ChatBridge
  -> GuiChatController / GuiConnectionWorker
  -> C++ OpenSSL TLS/TCP
  -> Go Hub + SQLite
```

- React 只处理界面状态与交互，不能直接访问 socket、TLS、证书、线程或数据库。
- `ChatBridge` 是 Web UI 与 C++ 业务层的唯一边界；网络和业务规则继续由既有 C++、Go 代码负责。
- 发布构建将 Vite 资源嵌入 `frontend.qrc`，最终用户不需要 Node.js。
- 动画只使用短时 `transform`/`opacity` 过渡；不要在消息列表、逐消息项或大面积容器上实时 blur。

## 已实现能力

- TLS 连接、4-byte big-endian 帧、账号/频道/私信/离线消息、权限管理、撤回、引用、复制、表情和消息持久化。
- 中文现代聊天 UI、设置与管理员入口、连接状态、未读提示和性能采样（性能面板仅用于测试，不进发布包）。
- 房主、局域网成员和远程服务器三种连接入口。
- `-auto-cert`：房主首次创建本地聊天室时，服务端在本机生成完整 TLS 证书对和 SQLite 数据库；只有半个证书对时会报错，不会覆盖现有身份文件。
- 局域网自动发现：本地房主每秒通过 UDP `38888` 公告公开证书、证书 SHA-256 指纹和 TCP 端口；成员从“附近聊天室”确认加入后仅在本机保存公开证书，使用 `localhost` TLS 身份校验，IPv4 变化可自动重新发现。手动 IPv4/证书入口保留为 UDP 被阻止时的回退。

## 当前 Windows 交付

标准交付是 **一个统一运行包**：`LANChat-Windows-x64.zip`。

- 所有电脑都解压后运行同一个 `LANChat.exe`；它在存在本地服务端时先完成本机 TLS 身份初始化，再启动 `lan-chat-gui.exe`。
- 选择“创建本地聊天室”时，包内 `server-go\chat-server.exe` 在该房主电脑生成 `server-go\certs\server-lan.crt`、`server-go\certs\server-lan.key` 和 `server-go\chat.db`。
- 选择“加入局域网聊天室”时，同一 GUI 作为成员连接房主。
- 统一包不含源码、Node、Go SDK、编译器、自动编译启动器、初始证书、私钥、数据库或聊天记录。
- `LANChat-Source-Launcher-windows-x64.zip` 仅作为开发者可选源码启动工具，不是普通用户的房主包。
- `server-lan.key`、`chat.db`、聊天记录、令牌永远不进入 GitHub 或 Release；默认成员通过自动发现获得公开证书，只有手动回退时才获取房主 IPv4、端口和公开 `server-lan.crt`。

生成统一包：

```powershell
cd C:\Users\jking1\Desktop\my-project\chat_X
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1
```

输出：`release\LANChat-Windows-x64.zip`。

## 关键文件

```text
frontend/src/app/App.tsx                    React 主界面和本地主机表单
client-cpp/gui/src/chat_bridge.*            QWebChannel 命令/状态桥接
client-cpp/gui/src/lan_discovery_service.*  UDP 房主发现与公开证书固定
client-cpp/gui/src/gui_connection_worker.*  本地服务端进程与 TLS 连接
client-cpp/gui/src/host_path_resolver.*     统一包/开发目录的服务端路径解析
server-go/main.go                           Go Server 参数与 TLS 监听
server-go/lan_discovery.go                  UDP 房间公告
server-go/auto_cert.go                      首次 TLS 证书生成
scripts/build-modern.ps1                    现代构建、CTest
scripts/package-unified-release.ps1         单一房主/成员运行包
scripts/package-installer.ps1                Inno Setup 安装器构建与预检
installer/LANChat.iss                        每用户安装、快捷方式和卸载项定义
scripts/test-member-package.ps1             运行包安全与烟雾检查
docs/release-setup.md                       面向用户的局域网使用说明
```

## 构建与验证

```powershell
cd C:\Users\jking1\Desktop\my-project\chat_X
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test

# 前端单独测试
$env:PATH = "$PWD\.tools\node-v24.19.0-win-x64;$env:PATH"
pnpm.cmd --dir .\frontend test -- --run
```

如果系统 `go.exe` 与 `GOROOT` 版本不匹配，请优先使用仓库 `.tools\go1.25.5\go\bin\go.exe`，并把 `GOROOT` 指向对应的 `.tools\go1.25.5\go`。若 PowerShell 找不到 Node，将 `.tools\node-v24.19.0-win-x64` 临时加入 `PATH`。不要提交 `.tools/`、`out/`、`release/`、缓存、证书、数据库或日志。

## 后续协作约束

- 先检查 `git status --short`，保留用户已有改动；不要使用 `git reset --hard`。
- 更新前端后必须重新构建 Vite，确保 `frontend.qrc` 指向新的哈希资源。
- 任何打包改动都必须验证 ZIP 中没有 `.key`、`.pem`、`.crt`、`.db`、源码或工具链。
- 变更本地房主流程时，保留“先启动服务端，再检查自动生成的证书，最后连接”的顺序。
- 文件、图片、音频和视频上传仍明确不在本版本范围内。
