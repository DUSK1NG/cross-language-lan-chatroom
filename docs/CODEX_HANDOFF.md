# LAN Chat 项目交接说明

更新：2026-08-21。此文档供新的 Codex 对话直接接手项目；以仓库实际代码为准。

## 1. 仓库与当前状态

- 仓库：[DUSK1NG/cross-language-lan-chatroom](https://github.com/DUSK1NG/cross-language-lan-chatroom)
- 主工作目录：`C:\Users\jking1\Desktop\my-project\chat_X`
- 当前主分支：`master`
- 已推送的最新主线提交：`f001339 feat: generate local TLS certificate automatically`
- 当前 GUI 是 **C++20 + Qt 6 Quick/QML**，不是 QWidget。
- 服务端是 Go TLS/TCP + SQLite。
- 未跟踪的 `.tools/`、`.gocache/`、`.superpowers/brainstorm/`、构建目录和临时 exe 都是本机工具、缓存或原型，不应提交。

## 2. 已完成且已进入主线的功能

### 网络、协议与并发

- Go 服务端监听 `0.0.0.0:8888`，支持 Wi-Fi 与以太网设备局域网互通。
- 协议为 `4 字节 uint32 大端长度头 + UTF-8 JSON`，消息体上限 64 KiB。
- Go 使用 Hub goroutine 管理注册、注销、广播、频道及在线用户；每个客户端独立读写流程和发送通道。
- C++ 使用 Winsock2/OpenSSL；GUI 网络由 Worker Thread 处理，避免阻塞界面线程。
- 已覆盖异常断线、非法长度、非法 JSON、发送缓冲区满、重复用户代码等基础处理。

### TLS、本地房主与连接记忆

- TLS 客户端验证房主提供的 `.crt`；成员不应获得或携带 `.key`。
- GUI 支持：创建本地聊天室（房主）、加入局域网聊天室（成员）、连接远程服务器。
- 最新功能：Go 以 `-auto-cert` 启动时，若证书和私钥都不存在，则自动生成自签名证书：
  - `certs/server-lan.crt`
  - `certs/server-lan.key`
  - SAN 包含 `localhost`、`127.0.0.1` 和本机局域网 IPv4。
  - 已有完整证书对不会被覆盖；只剩其中一个文件会明确报错。
- GUI 记住服务器 IP、端口、用户名、用户代码和 CA 路径；不保存密码。

### 聊天业务

- 用户名可重复；用户代码大小写不敏感且全局唯一，显示为 `Alice#A001`。
- 已实现注册/登录、SQLite 账号密码、群聊、私聊、上线/离线、在线成员、自动刷新、未读数量。
- 已实现公共/私有频道：创建、加入、邀请、移除成员和删除频道。
- 私聊和频道会话隔离；离线私聊消息以 SQLite 保存并在目标用户下次登录时投递。
- 已实现表情输入、复制、引用、本地删除消息。
- 管理员/频道所有者的服务端代码已包含踢出、禁言、撤回、私有频道成员管理；进行重大 UI 改造前应做一次真实双客户端回归。

### 现有 QML GUI 与发布

- GUI 入口：`client-cpp/gui/src/main.cpp`（`QGuiApplication + QQmlApplicationEngine`）。
- 控制器：`client-cpp/gui/src/gui_chat_controller.*`。
- 网络工作线程：`client-cpp/gui/src/gui_connection_worker.*`。
- 模型：`client-cpp/gui/src/chat_model.*`。
- 页面和组件：`client-cpp/gui/qml/`。
- 发布脚本：`scripts/package-release.ps1`，使用 `windeployqt` 并验证 Qt Core/SVG、platforms/qwindows、imageformats/qsvg、OpenSSL DLL。
- 部署说明：`docs/release-setup.md`。旧包缺 DLL 时应重跑发布脚本，不要手工随机复制 DLL。

## 3. 明确未开发或未完成的内容

- 真正端到端加密：当前仅 TLS 加密客户端到服务端，服务端仍可读取消息。
- 文件、图片、语音、视频传输和图片缩略图。
- UDP 局域网自动发现。
- 可搜索的本地历史记录 UI。用户之前要求移除历史消息展示，未经再次确认不要恢复。
- 频道定义、邀请名单、频道权限、管理审计在服务端重启后的持久化。
- 公网 NAT 穿透、P2P、移动端、Web 客户端。
- Inno Setup 安装 EXE、桌面快捷方式、开始菜单、卸载项（当前只有 ZIP/目录打包）。
- React/QWebEngine 新 GUI：只有设计决策，尚未写入产品代码。

## 4. 已确认的新 UI 升级决策（尚未实现）

目标架构：

```text
React + TypeScript + Vite + Tailwind + Motion + Lucide
                 ↓ QWebChannel
           ChatBridge（薄桥接层）
                 ↓
现有 GuiChatController / GuiConnectionWorker
                 ↓
现有 C++ TLS/TCP 与 Go 服务端
```

约束：

- 不使用 Electron、Next.js、Redux、Material UI、Ant Design。
- 不重写 Go 服务端、C++ 协议、Socket、TLS、线程或 SQLite 业务规则。
- React 只能经 QWebChannel 调用 `ChatBridge`，不能直接访问 Socket、证书、密码、线程或数据库。
- 开发期加载 Vite `http://127.0.0.1:5173`；发布期把 React 静态资源嵌入 Qt `.qrc`，最终用户不需安装 Node。
- 按页面渐进迁移并保留 QML 回退，直到真实 TLS 双客户端验收完成后才能移除 QML。
- Qt WebEngine/Chromium 默认保留 GPU 加速；动画优先 `transform`、`opacity`。只在标题栏、侧栏、Modal/Popover 少量使用 blur；消息列表与每个消息项不使用实时 blur。
- 消息多于约 500 条时应设计虚拟列表或分页，避免大量 DOM 节点。

视觉已确认：**丰富版极光玻璃**。

- 深午夜背景、蓝紫极光、青绿色在线状态、少量粉色点缀；保持专业克制。
- 布局：窄工作区轨道 + 频道/私聊侧栏 + 中央聊天区 + 右侧成员栏。
- 身份卡固定左下角，例如 `Alice #A001 / 在线 · 管理员`。
- 自己发送的消息：头像、名称、用户代码、时间、气泡是一个完整右对齐块；长消息也不能让名称回到左侧。
- 人机可见文案以中文为主，未来再预留 i18n。

## 5. 新 UI 工作目前所处阶段

已经完成：

1. 架构审计：确认项目实际为 QML，而不是 QWidget；没有现成 UDP 或文件传输代码。
2. 渐进迁移方案、React/QWebChannel/ChatBridge 边界、Vite 开发与 qrc 发布策略均已获用户确认。
3. 视觉方向已确认，并确认身份卡在左下角。
4. 本机已准备可携带 Node `v24.19.0`；系统 Node MSI 曾失败（1603），不要假设 `node` 已在系统 PATH。

下一步必须先完成的设计工作（尚未获得实现授权）：

1. 输出 React 页面、组件、状态模型、QWebChannel 事件/命令契约、错误边界与测试方案。
2. 写入 `docs/superpowers/specs/YYYY-MM-DD-react-webengine-ui-design.md`。
3. 让用户确认设计文档。
4. 拆分实施计划，再开始新增 Qt WebEngine/WebChannel、ChatBridge 与 React 代码。

不要跳过这些步骤，也不要直接删除 QML。

## 6. 常用构建与验证命令

```powershell
cd C:\Users\jking1\Desktop\my-project\chat_X

# Go 测试
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\.tools\gocache125'
$Go = 'C:\Users\jking1\Desktop\my-project\chat_X\.tools\go1.25.5\go\bin\go.exe'
Push-Location .\server-go
& $Go test -vet=off ./...
Pop-Location

# Qt 6.11 / MinGW GUI 构建
$env:Path = 'C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.2\mingw_64\bin;' + $env:Path
cmake -S .\client-cpp\gui -B .\client-cpp\gui\build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER='C:\Qt\Tools\mingw1310_64\bin\g++.exe' `
  -DCMAKE_PREFIX_PATH='C:\Qt\6.11.2\mingw_64'
cmake --build .\client-cpp\gui\build --config Release --parallel 2

# 可携带 Node（不在系统 PATH）
$NodeRoot = 'C:\Users\jking1\Desktop\my-project\chat_X\.tools\node-v24.19.0-win-x64'
& "$NodeRoot\node.exe" --version
& "$NodeRoot\node_modules\npm\bin\npm.cmd" --version
```

## 7. 给新 Codex 对话的复制提示

```text
请接手 C:\Users\jking1\Desktop\my-project\chat_X。先完整阅读 docs/CODEX_HANDOFF.md，检查 git 状态并以实际代码为准。

项目主线已包含 Go TLS/TCP 服务端、Qt Quick/QML GUI、局域网房主自动生成 TLS 证书、聊天/私聊/频道/离线私信和发布打包脚本。不要重写网络、协议、TLS、Go Hub 或现有业务逻辑。

下一目标是渐进把 UI 升级为 Qt WebEngine + QWebChannel + React/TypeScript，不使用 Electron；但当前尚处设计阶段。请先完成页面/组件/状态/QWebChannel 契约/错误与测试方案，写入设计文档让我确认。保留 QML 回退，不要直接实现或删除 QML，直到我批准实施计划。

视觉采用丰富版极光玻璃：深午夜、蓝紫极光、克制玻璃效果，身份卡固定左下，自己发送的消息整块右对齐。中文优先。
```
