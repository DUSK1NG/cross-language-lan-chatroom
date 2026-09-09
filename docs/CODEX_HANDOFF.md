# LAN Chat 项目交接说明

更新：2026-09-09。此文档供新的 Codex 对话接手；历史实施记录见 `docs/superpowers/`，当前状态以仓库代码、Git 状态与本次验证结果为准。

## 1. 当前交付状态

- 仓库：https://github.com/DUSK1NG/cross-language-lan-chatroom
- 工作目录：使用当前本地克隆目录；通过 `git rev-parse --show-toplevel` 确认，勿复制历史机器路径。
- 主分支：master；接手时用 `git log -1 --oneline` 获取当前提交。
- 发布入口：[GitHub Releases](https://github.com/DUSK1NG/cross-language-lan-chatroom/releases)。
- 用户入口：React + TypeScript + Vite + Qt 6 WebEngine + QWebChannel + C++20 的现代客户端。
- 服务端：Go TLS/TCP + SQLite，默认 TCP 8888；局域网发现使用 UDP 38888。
- 旧 CLI 不是用户入口；旧 QML / Flutter 界面已移除，仅保留 React + Qt WebEngine 界面。

### 发布资产

| 资产 | 用途 |
| --- | --- |
| LANChat-Setup-x64.exe | 每用户安装、开始菜单/桌面快捷方式、Windows 卸载项 |
| LANChat-Windows-x64.zip | 便携运行包；完整解压后运行 LANChat.exe |

每次发布重新记录测试结果、产物 SHA-256、包安全检查与人工验收结果，见 [发布清单](release-checklist.md)。源码改动不会自动更新已有 ZIP 或安装器；历史测试数量和哈希不能证明当前产物有效。

## 2. 架构与不可突破的边界

~~~text
React UI
  -> QWebChannel / ChatBridge
  -> GuiChatController / GuiConnectionWorker
  -> C++ OpenSSL TLS/TCP
  -> Go Hub + SQLite
~~~

- React 仅管理界面和交互；不能直接访问 socket、TLS、证书、私钥、线程或 SQLite。
- ChatBridge 是 Web UI 和 C++ 业务层唯一边界。发布构建把 Vite 产物写入 Qt Resource System，最终用户不需要 Node.js。
- 动画只允许短时 transform / opacity；不要在消息列表、消息项或大面积容器上实时 blur、stagger 或持续动画。
- 性能面板/基准门禁仅供测试，不能进入发布 UI。
- 附件传输、房间管理、禁言和踢出已属于现有功能；精简时保留相应命令、权限校验与测试。自动更新、P2P/NAT 穿透等新功能按用户授权范围实施。

## 3. 已实现功能

- TLS/TCP 连接、4-byte big-endian 长度帧、UTF-8 JSON 协议。
- 大厅、公开/私有频道、一对一私信、离线私信、历史消息、复制、引用、撤回与表情。
- 加密附件：单文件上限 5 GiB、每房间配额 20 GiB；客户端 AES-256-GCM 分块、MLS manifest、上传进度、续传与下载解密落盘。
- 房主连接审批：每个成员连接必须由在线房主明确允许或拒绝；审批入口固定存在，弹窗不能通过点击空白处、Esc 或切换页面绕过。
- 已批准会话的受控断线恢复；连续消息自动定位、历史阅读时的新消息计数与提示。
- 房主首次启动自动生成 TLS 证书对与 SQLite 数据库；半个证书对会报错且不覆盖既有身份。
- 局域网自动发现并固定房主公开证书指纹；IPv4 变化后可重新发现。成员可用手动 IPv4/端口/公开 server-lan.crt 回退。
- 可信虚拟局域网（如 Radmin VPN）支持自动发现；TCP 隧道只接受主机名/IP、端口和公开证书，隧道令牌/配置/私钥始终留在外部工具中。
- 连接诊断日志、性能档位、帧时间采样、历史窗口化和模型行数上限。

## 4. Windows 本地数据与安全

- 普通用户统一运行 LANChat.exe：同一套包既可创建本地聊天室，也可作为成员加入。
- 房主首次创建本地聊天室时，运行数据在 %LocalAppData%\DUSK1NG\LAN Chat\host\ 生成：
  - certs\server-lan.crt：可公开分发给成员的证书；
  - certs\server-lan.key：房主私钥；
  - chat.db：本地聊天数据库。
- server-lan.key、chat.db、聊天记录、日志和访问令牌不得进入 Git、同步盘、GitHub 或 Release。成员只可获得公开 .crt。
- 诊断日志目录：%LocalAppData%\DUSK1NG\LAN Chat\logs\。日志只应包含时间、端点、重连次数与 TLS/审批结果，不应写入聊天内容、证书或私钥。

## 5. 已知问题与搁置项

| 项目 | 状态 | 接手要求 |
| --- | --- | --- |
| Windows“设置”卸载遗留少量安装目录文件 | 已知，暂不修复 | 安装目录内 unins000.exe 可完整卸载；不得为此改动误删 %LocalAppData% 中的房主身份/聊天数据。 |
| TCP 隧道（Sakura Frp 等）端到端双机验收 | Phase 4B 搁置 | 已有手动连接入口和文档；恢复前先验证隧道端点、公开证书与房主审批全链路。 |
| 性能硬件采样 | Phase 6 搁置 | 自动化门禁已完成；参考机器上的 500/2,000 消息交互采样尚未完成。不能把 GamePP 仅采集父进程的约 22 FPS 当作 WebEngine 实际帧率结论。 |
| TLS certificate verify failed | 待复现/诊断 | 曾在跨网络测试中记录，后续连接未稳定复现；优先检查日志、公开 .crt 是否对应当前房主身份、TLS 主机名和隧道端点。 |

历史验收的 Phase 1（会话恢复）、Phase 2（投递状态/搜索）和 Phase 4A（虚拟局域网）若出现回归，应补充复现证据。以上搁置项是历史记录，恢复工作前须重新确认状态。

## 6. 实施经过、历史错误与防回归规则

以下记录包含此前实施中出现过的错误；它们不是当前已确认的全部缺陷，但下一位接手时必须先排除这些因素。

| 历史症状/错误 | 根因或边界 | 以后正确做法 |
| --- | --- | --- |
| 启动后出现旧 QML 页面、旧深色 UI 或测试性能面板 | 我曾让用户从不同的 build 目录、诊断构建或旧 EXE 启动，导致“源码已更新”和“正在运行的程序”不一致。 | 普通用户只启动安装/ZIP 根目录的 `LANChat.exe`；源码调试只使用 `scripts\start-gui.ps1`。每次排查先记录 EXE 完整路径、提交号、构建时间和 GUI 哈希。 |
| React 修改后发布包仍显示旧页面 | Vite 会生成新哈希资源；若 `out\modern-msvc-x64\frontend.qrc` 仍引用旧文件，WebEngine 会加载旧前端。 | 由 CMake 根据 frontend.qrc.in 自动生成 qrc；前端生产构建后重新构建 C++，再重新打包；不要只复制前端目录或 GUI EXE。 |
| 双击单独的 GUI EXE 报缺 Qt DLL，例如 `Qt6QuickControls2d.dll` | 我曾提供过裸 EXE 测试路径；Qt WebEngine 依赖 DLL、plugins、`QtWebEngineProcess.exe`、resources 和 translations。 | 交付只能用 `package-unified-release.ps1` 或安装器；不得把 `lan-chat-gui.exe` 单独发给用户。 |
| 关闭 GUI 后出现 MSVC “stack around variable bridge/lock was corrupted” 或运行时访问异常 | 发生在过期/调试构建及进程未彻底退出的迭代阶段，不能根据弹窗直接归因于网络逻辑。 | 先关闭所有 LAN Chat/QtWebEngineProcess，再重新构建；保留崩溃时间、构建目录和日志。没有可重复最小复现前，不得声称已找到根因。 |
| PowerShell 启动脚本报 `Wait` 参数转换错误 | 我曾把开关参数按字符串/布尔值错误传递给 PowerShell 脚本。 | PowerShell `[switch]` 参数只写 `-Wait`，或明确写 `-Wait:$true/$false`；不要传递字符串。修改启动脚本后先复制实际命令在新 PowerShell 验证。 |
| 源码启动提示缺 Node、pnpm 或 OpenSSL | 源码启动器和发布运行包的依赖边界不同；发布运行包不应自动下载构建工具。 | 源码目录先运行 `scripts\bootstrap-github.ps1` 并重开终端；发布包只运行，不要求 Node/Go/CMake。不要把开发工具塞进用户包。 |
| 本地主机第一次连接出现 CA 文件不存在、SSL read 失败或证书路径为空 | 早期流程中证书生成和 GUI 连接顺序不够明确，且有人直接复用不存在或不配对的旧路径。 | 房主首次从 `LANChat.exe` 选择“创建本地聊天室”，让程序在 `%LocalAppData%` 生成完整身份后再连接；不能手工只复制 `.crt` 或 `.key` 的一半。 |
| 成员把“已信任房主”误认为“已获准加入” | 曾经 UI 文案和流程混淆了两道独立检查。证书信任只验证服务器身份，不是房主批准成员。 | 始终保留两道门：成员核对公开证书/指纹；房主明确允许当前连接。成员等待时显示“等待房主批准”，房主弹窗不可随意关闭。 |
| 同一用户名/代码重连时被拒或审批不出现 | 会话恢复、过期连接、旧服务端进程与房主审批状态在早期多次迭代；有一次问题被记录后未稳定复现。 | 先检查房主是否仍运行、是否存在旧 `chat-server.exe`、日志中的 connection/message id、证书指纹是否一致；不要用更换用户代码掩盖协议问题。复现后再开修复。 |
| 设置返回聊天或大列表消息动画卡顿 | 我曾试过较多 GSAP/子节点动画；它们会把布局、绘制和 WebEngine 通信放大。 | 仅对页面根节点做一次短 transform/opacity 过渡；消息初始历史不动画，新增消息才做极轻量效果。设置页不做滚动动画。 |
| GamePP 显示约 22 FPS | 采样选择了 `lan-chat-gui.exe` 父进程，实际 React 页面由 `QtWebEngineProcess.exe` 渲染，结果不能代表 UI 实际帧率。 | 使用应用内 requestAnimationFrame 采样和 WebEngine 图形后端信息；GamePP 结果只能作为异常线索，不能直接作为 P6 验收失败结论。 |
| Git 提交/推送偶尔长时间没有立即返回 | `git commit` 会触发后台 `git gc` 自动整理对象；我曾需要等待它释放锁。 | 不要终止 Git 进程，也不要重复执行破坏性 Git 命令；确认没有 `.git` 锁后再推送。 |

### 仍必须遵守的安全和发布边界

- 私钥只在房主离线设备上生成和保存；成员、隧道服务、GitHub、ZIP、安装器和日志均不得获得它。
- TCP 隧道的主机名不是 IPv4；输入校验与 TLS 主机名校验必须同时支持已批准的域名/IP 用例，不能因一个 UI 正则就否定真实隧道端点。
- 任何“修复 TLS”的方案都不能通过关闭证书验证、改为明文、忽略主机名或自动接受未知证书实现。
- 任何“修复卸载”的方案都不能静默删除用户的 `%LocalAppData%\DUSK1NG\LAN Chat\host\` 数据。
- 不能把性能、诊断或开发模式编译进正式交付；发布包必须始终在无 Node、Go、CMake 的普通 Windows 电脑上可启动。

## 7. 当前工作树注意事项

开始任何工作前先执行：

~~~powershell
# 在当前项目根目录执行
git status --short
git log -1 --oneline
~~~

未跟踪文件以本次 `git status` 为准。界面图标使用 `frontend/src/assets/lan-chat-cat.png`，Windows 打包图标使用 `assets/LANChat.ico`。

不要使用 git reset --hard、git clean 或批量删除 assets\。任何新增提交必须只包含与当前任务直接相关的文件。

## 8. 构建、测试和打包

~~~powershell
# 在当前项目根目录执行

# 完整现代构建与 CTest
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test

# 前端单独测试与构建
pnpm.cmd --dir .\frontend exec vitest run
pnpm.cmd --dir .\frontend run build

# 生成统一 ZIP 与 v1.2.1 安装器
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1 -Version 1.2.1 -SmokeTest
~~~

- 发布脚本输出：release\LANChat-Windows-x64.zip 和 release\LANChat-Setup-x64.exe。
- 若缺少 Node、Qt 或 OpenSSL，先运行 scripts\bootstrap-github.ps1；不要把依赖缺失误判为产品故障。
- Vite 产物变更后必须确认 out\modern-msvc-x64\frontend.qrc 引用了新哈希资源，否则发布版会加载旧前端。
- 打包后按 [发布基线与恢复验收清单](release-checklist.md) 复核 GUI 哈希和 ZIP 内敏感文件。

## 9. 关键文件索引

~~~text
frontend/src/app/App.tsx                    主界面、连接入口和聊天状态
frontend/src/performance/                   帧时间采样与性能策略
client-cpp/gui/src/chat_bridge.*            QWebChannel 命令/状态桥接
client-cpp/gui/src/gui_connection_worker.*  本地服务端进程、TLS 与恢复连接
client-cpp/gui/src/network_diagnostics.*    连接诊断日志
client-cpp/gui/src/lan_discovery_service.*  UDP 房主发现与公开证书固定
client-cpp/gui/src/host_path_resolver.*     包/开发目录的服务端路径解析
server-go/main.go                           Go Server 参数与 TLS 监听
server-go/lan_discovery.go                  UDP 房间公告
server-go/auto_cert.go                      首次 TLS 证书生成
scripts/build-modern.ps1                    现代构建与 CTest
scripts/package-unified-release.ps1         统一房主/成员 ZIP
scripts/package-installer.ps1               Inno Setup 安装器构建与预检
scripts/test-member-package.ps1             运行包安全/烟雾验证
docs/release-setup.md                       面向用户的连接、跨网络和卸载说明
docs/p6-performance-validation.md           P6 自动化性能门禁说明
~~~

## 10. 建议的下一步

除非用户明确改变优先级，先保持维护模式：

1. 若出现连接问题，先收集 %LocalAppData%\DUSK1NG\LAN Chat\logs\ 中对应时段日志，并区分 TCP、TLS 证书、房主审批和会话恢复阶段。
2. 若恢复 TCP 隧道工作，先完成 Phase 4B 的双机端到端验收；不要引入隧道令牌、私钥或第三方客户端打包。
3. 若恢复性能工作，按 P6 参考机基准采集真实 WebEngine 帧时间与内存，不凭父进程 FPS 数据下结论。
4. 每次发布前更新 docs/release-checklist.md 的提交、哈希、测试结果与人工验收结果，并检查发布资产不含敏感文件。
