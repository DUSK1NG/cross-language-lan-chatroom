# LAN Chat 项目交接说明

更新：2026-08-25。此文档供新的 Codex 对话直接接手；以仓库实际代码、git status 与 GitHub Release 为准。

## 1. 当前交付状态

- 仓库：https://github.com/DUSK1NG/cross-language-lan-chatroom
- 主工作目录：C:\Users\jking1\Desktop\my-project\chat_X
- 主分支：master；最新已推送提交：fe5181c docs(release): prepare v1.2.0。
- 最新正式发布：[LAN Chat v1.2.0](https://github.com/DUSK1NG/cross-language-lan-chatroom/releases/tag/v1.2.0)。
- 用户入口：React + TypeScript + Vite + Qt 6 WebEngine + QWebChannel + C++20 的现代客户端。
- 服务端：Go TLS/TCP + SQLite，默认 TCP 8888；局域网发现使用 UDP 38888。
- 旧 CLI 不是用户入口；旧 QML 仅用于诊断回退，可用 lan-chat-gui.exe --legacy-qml 显式进入。

### v1.2.0 发布资产

| 资产 | 用途 | SHA-256 |
| --- | --- | --- |
| LANChat-Setup-x64.exe | 推荐；每用户安装、开始菜单/桌面快捷方式、Windows 卸载项 | E1B9A13A04C105F894A0CD19D70325D88D6EF9A631F1B84B10895680F34E2C95 |
| LANChat-Windows-x64.zip | 便携运行包；完整解压后运行 LANChat.exe | ECEFA0D5DC8345150C2FFD53812248933E7EA5F57BBBFA062378FFBBC5E408EC |

- 自动验证：Vitest 13 个文件、79 项通过；CTest 15/15 通过；go test ./... 通过。
- 包安全检查：ZIP 内禁止文件 0；包内 lan-chat-gui.exe 与构建目录的 SHA-256 一致。
- 用户已验收：安装包安装、桌面/开始菜单快捷方式，以及从安装目录直接运行卸载 EXE。

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
- 不要添加文件、图片、音频或视频传输；不要添加自动更新、P2P/NAT 穿透、管理员角色/黑名单/禁言管理或专门的无障碍工作流，除非用户明确重新立项。

## 3. 已实现功能

- TLS/TCP 连接、4-byte big-endian 长度帧、UTF-8 JSON 协议。
- 大厅、公开/私有频道、一对一私信、离线私信、历史消息、复制、引用、撤回与表情。
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

已完成验收的 Phase 1（会话恢复）、Phase 2（投递状态/搜索）和 Phase 4A（虚拟局域网）若出现回归，应重新打开对应 Phase 并补充复现证据。P2P、自动更新和管理员权限明确不做。

## 6. 当前工作树注意事项

开始任何工作前先执行：

~~~powershell
cd C:\Users\jking1\Desktop\my-project\chat_X
git status --short
git log -1 --oneline
~~~

当前工作树保留 14 个未跟踪的 assets\lan-chat-desktop-icon-*.png 图标草稿。它们不是当前发布图标，不能顺手提交、删除或覆盖；当前正式图标由已跟踪的 frontend\src\assets\lan-chat-cat.png 使用。

不要使用 git reset --hard、git clean 或批量删除 assets\。任何新增提交必须只包含与当前任务直接相关的文件。

## 7. 构建、测试和打包

~~~powershell
cd C:\Users\jking1\Desktop\my-project\chat_X

# 完整现代构建与 CTest
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-modern.ps1 -Action Test

# 前端单独测试；若系统找不到 Node，使用仓库工具链
$env:PATH = "$PWD\.tools\node-v24.19.0-win-x64;$env:PATH"
pnpm.cmd --dir .\frontend test -- --run

# 生成统一 ZIP 与 v1.2.0 安装器
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-unified-release.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-installer.ps1 -Version 1.2.0 -SmokeTest
~~~

- 发布脚本输出：release\LANChat-Windows-x64.zip 和 release\LANChat-Setup-x64.exe。
- 若缺少 Node、Qt 或 OpenSSL，先运行 scripts\bootstrap-github.ps1；不要把依赖缺失误判为产品故障。
- Vite 产物变更后必须确认 client-cpp\gui\resources\frontend.qrc 引用了新哈希资源，否则发布版会加载旧前端。
- 打包后按 [发布基线与恢复验收清单](release-checklist.md) 复核 GUI 哈希和 ZIP 内敏感文件。

## 8. 关键文件索引

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

## 9. 建议的下一步

除非用户明确改变优先级，先保持维护模式：

1. 若出现连接问题，先收集 %LocalAppData%\DUSK1NG\LAN Chat\logs\ 中对应时段日志，并区分 TCP、TLS 证书、房主审批和会话恢复阶段。
2. 若恢复 TCP 隧道工作，先完成 Phase 4B 的双机端到端验收；不要引入隧道令牌、私钥或第三方客户端打包。
3. 若恢复性能工作，按 P6 参考机基准采集真实 WebEngine 帧时间与内存，不凭父进程 FPS 数据下结论。
4. 每次发布前更新 docs/release-checklist.md 的提交、哈希、测试结果与人工验收结果，并检查发布资产不含敏感文件。
