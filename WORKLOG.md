# LAN Chat 工作日志

> 作用：让任何 agent 或人随时接手。**开工先读「接手摘要」，收工在你的会话结束前把新条目加到「日志」最上面。**
> 配套材料：[交接说明](docs/CODEX_HANDOFF.md)（操作手册）· [架构说明](docs/architecture.md) · [v1.4 升级与加固合并计划](docs/plans/2026-11-23-v1.4-plus-hardening-plan.md) · [服务端加固计划 v2](docs/plans/2026-11-23-server-hardening-plan.md)

## 记录规则

1. 只追加、倒序（最新在上）；不改写旧条目，有更正就在新条目里写。
2. 每条固定写清：时间 / 执行者 / 做了什么（含文件路径）/ 验证（命令与结果，没跑就写“未验证”）/ 下一步 / 阻塞与需要人拍板的事。
3. 不写聊天内容、私钥、token、证书私钥内容；只写路径与结论。
4. 本文件纳入 Git（不要加进 .gitignore）。收工前跑 `git status --short` 并把要点写进条目。
5. 超过约 300 行时，把最早的条目移到 `docs/archive/worklog-YYYY.md`，本文件保留链接。
6. 每次完成一项项目修改后，主代理把改动范围与验证结果交给 gpt-6-luna 子代理，由其在日志顶部记录，主代理核对；工作日志写入本身无需递归触发。

## 接手摘要（更新：2026-11-23 19:18）

- **项目**：Windows 聊天程序。局域网版 = 房主本机开房（TLS/TCP 8888 + UDP 38888 发现 + SQLite）；在线版 = 独立 Rust 服务器（账号、私信、离线消息、Android APK、FCM 推送）。桌面 = Tauri 2 + WebView2 + React；服务端、协议、桌面核心、Android JNI 传输均为 Rust。
- **版本**：当前桌面源码版本为 `1.3.0`。v1.4.0 的实施与验收按 [最终执行版](docs/prompts/v1.4/README.md) 推进。
- **Git**：Rust 工作区基线为 `fcb7b19`，与远端默认分支 `origin/master` 的 `3ebc740` 合入本地 `master`。`Claude-BugHunter/` 按本地第三方目录忽略，原有 autostash 保留。
- **S0 当前状态**：Git 基线已建立。附件端到端用例同一源码下出现通过和超时，仍须定位并修复。全量 workspace 离线测试缺依赖缓存。H1 以 S0 验收为开工条件。

## 关键约束（改代码前必读）

- 协议帧：4 字节大端长度 + UTF-8 JSON，单帧 1 ≤ length ≤ 65536；新行为通过 capability 协商，旧客户端行为逐字不变。
- 数据目录：`%LocalAppData%\DUSK1NG\LAN Chat\host\`（certs\server-lan.crt、certs\server-lan.key、chat.db）；在线版数据在 `%LocalAppData%\DUSK1NG\LAN Chat Online\`。表结构迁移走 `PRAGMA user_version`，幂等、可回滚，测试只用临时目录。
- 安全：不关闭或放宽 TLS 证书/主机名校验；不新增绕过房主实时审批的路径；附件权限校验不得削弱；私钥、chat.db、日志、聊天内容不进 Git / Release / ZIP / 安装器 / 日志输出。
- 前端边界：React 不碰 socket、TLS、证书、私钥或 SQLite；桥接集中在 `frontend/src/bridge/chatBridge.ts` 与 `frontend/src/app/runtimeBridge.ts`。
- 动画只允许短时 transform / opacity；性能面板与基准门禁只用于测试，不进发布 UI。
- 局域网版零服务器、离线可开房；不新增重量级依赖。

## 常用验证命令（在项目根执行）

~~~powershell
git status --short
git log -1 --oneline

cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo build --workspace --locked --release

pnpm.cmd --dir .\frontend exec vitest run
pnpm.cmd --dir .\frontend run build

powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-desktop.ps1
~~~

打包产物：`release\LANChat-Desktop-Windows-x64.zip`（package-unified-release.ps1）、`release\LANChat-Setup-x64.exe`（package-installer.ps1 -SmokeTest）。

## 环境坑（本机 / agent 会话）

- DSH `run_code` 里 OPS 工具要先 `ops_load_sdk`，且加载只在**下一个程序**生效；`tools.glob` 的 `"*"` 是递归按 basename 匹配（会命中 .tools 里 vendored 源码），列顶层要用 `ops_glob`（`kind:"all", pattern:["*"]`）。
- 早前会话里 shell `cd` 到本项目曾报 `Permission denied`；当前 file policy 为 danger-full-access，`ops_bash` 在项目根可直接跑 git/cargo（本文件里的 Git 状态即由此取得）。
- 文档写的诊断日志目录 `%LocalAppData%\DUSK1NG\LAN Chat\logs\` 在本机**尚不存在**：2026-11-23 检查 DUSK1NG 下没有任何 *.log / *.jsonl，只有证书、附件与 known-hosts。
- 机器时区 Asia/Taipei，日志时间用本地时间。

## 日志

### 2026-11-26 13:45 — 发布桌面双版 1.4.0 正式版（更新签名密钥、公告、v1.4.0 标签与 CI）

- 执行者：lead（DSH）。
- 改动：
  - 生成桌面在线版自动更新签名密钥对（Tauri minisign），公钥/私钥/口令保存在 `C:\Users\Q1573\.lan-chat-keys\`（`online-updater.key`、`.key.pub`、`.password` 等）；生成 Android 正式发布 keystore `android-release.jks`（PKCS12，alias `lanchat`，有效期 10950 天，SHA-256 指纹 `0B:6D:D7:A8:31:18:B4:9B:59:41:4F:BA:22:00:39:D7:88:69:8C:EB:6F:35:61:13:63:E3:61:E2:E2:A6:4C:18`）。
  - GitHub 配置：仓库变量新增 `LANCHAT_ONLINE_UPDATER_PUBLIC_KEY`；`release` 环境机密新增 `LANCHAT_ONLINE_UPDATER_PRIVATE_KEY` / `_PASSWORD`、`LANCHAT_ANDROID_KEYSTORE_BASE64`、`LANCHAT_ANDROID_KEY_ALIAS`、`LANCHAT_ANDROID_STORE_PASSWORD`、`LANCHAT_ANDROID_KEY_PASSWORD`、`GOOGLE_SERVICES_JSON`。
  - 发布公告：`docs/release-notes/v1.4.0.md` 重写为「新增 / 改进 / 修复」三段；`frontend/src/changelog/releaseNotes.ts` 的 `currentRelease` 更新为 1.4.0 正式版卡片；`docs/release-runbook.md` 新增「### 更新签名密钥管理」（生成命令、变量与机密映射、换钥须卸载重装）。
  - 打标签 `v1.4.0`（指向 `68b65f4`）并推送，触发 Release workflow `45761851648`；两次批准 `environment: release` 审批门（构建 job 与 draft job 各一次）。
- 验证：
  - `node scripts/check-version.mjs` → 发布版本一致：1.4.0；`frontend` 的 `npm run typecheck` 通过、`npx vitest run src/changelog` 2/2 通过。
  - CI 全部通过：validate、desktop (lan)、desktop (online)、android、draft 均 success。Draft release `v1.4.0` 产出 10 个资产：`LANChat-Desktop-Windows-x64.zip` 14,086,603、`LANChat-Online-Windows-x64.zip` 10,769,724、`LANChat-Setup-x64.exe` 12,618,862（`.sig` 428）、`LANChat-Online-Setup-x64.exe` 10,319,890（`.sig` 436）、`LANChat-Android-arm64.apk` 11,863,163、`latest-lan.json` 2,661、`latest-online.json` 2,679、`SHA256SUMS` 837。
  - 两份 manifest 的 `version` 为 1.4.0、`signature` 解码含 `version:1.4.0` 且 `size` 与安装器一致；安装器 SHA256：LAN `e84496e418cf001f78224d261cdcfe120af625bde7373b22c7dd582314f24797`，Online `1b4b237e2d1d75e19281f6caf2004c677e73dbaaed219572837ba3afca88c3ef`。
- 下一步：draft 需要人工发布并设为 latest（更新检查走 `releases/latest/download`），发布后安装版才能收到 1.4.0 更新；本机已装的 Online 是 11:40 构建、未嵌入新公钥，须先手工换成 1.4.0 安装包才能走后续自动更新；Android 新签名与旧调试签名 APK 不兼容，安装新版需先卸载。
- 阻塞与需拍板：是否发布该 draft release、是否删除 2026-11-24 遗留的 draft release `lan-v1.4.0`。

### 2026-11-26 12:26 — 推送 master 与 stage/1.4-candidate

- 执行者：lead（DSH）。
- 改动：把 `1e0d3a8` 推到 origin：`refs/heads/master` 3ebc740→1e0d3a8，`refs/heads/stage/1.4-candidate` 1c3ca6d→1e0d3a8。本机 git 没有凭据配置，使用 `git -c credential.helper='!gh auth git-credential' push`（gh 已登录 DUSK1NG，scope 含 repo）。
- 验证：`git fetch` 后 `git ls-remote origin` 两个分支均指向 `1e0d3a8581ad3a0ba25cf19c7e80e0a3069d2ca6`，本地与上游同步、工作区干净。顺带清掉仓库根部一个误建的 `CON` 设备名文件（35 字节，未跟踪、未提交）。
- 下一步：桌面在线版自动更新验签公钥仍未配置；需要发版时按 runbook 打标签。

### 2026-11-26 12:23 — LAN 版安装替换

- 执行者：lead（DSH）。
- 改动：用 `release/local-desktop-20261126-motion-1140/lan/` 覆盖 `%LocalAppData%\Programs\LAN Chat`：`LANChat.exe` 21,727,232→21,771,264 字节、`lan-chat-server.exe` 8,145,408→8,206,336 字节（均 11:41 构建），保留 `unins000.exe`、`unins000.dat`、`lan-chat.install`。替换前该目录有 LANChat 进程（pid 6036）在运行，已先停止；旧文件备份到 `%LocalAppData%\Temp\LANChat-lan-backup-20261126-122304`。
- 验证：两个 exe 的 SHA256 与 release 包一致、与旧版不同（LANChat `995DE026…` vs 旧 `9915F791…`；server `19620F71…` vs 旧 `574E63CE…`）；启动 8 秒后窗口标题 `LAN Chat`、`msedgewebview2.exe` 子进程存在，退出无残留。用户数据目录 `%LocalAppData%\DUSK1NG\LAN Chat\` 未改动。
- 下一步：无；远端仍未推送（origin/master 落后 7 个提交，origin/stage/1.4-candidate 落后 3 个）。

### 2026-11-26 12:17 — 桌面 Online 版本替换、1.4 合并与开发区清理

- 执行者：lead（DSH）。
- 改动：
  - 用 `release/local-desktop-20261126-motion-1140/online/` 的 `LANChat.exe`（22,579,200 字节，FileVersion 1.4.0，11:41 构建）覆盖安装目录 `%LocalAppData%\Programs\LAN Chat Online`，保留 `unins000.exe`、`unins000.dat`、`lan-chat.install`；旧文件备份到 `%LocalAppData%\Temp\LANChat-online-backup-20261126-121559`。桌面快捷方式 `LAN Chat Online.lnk` 仍指向该目录，无需修改。
  - 工作区 70 项改动（桌面动效与交互、AstrBot 桌面/Android 桥接、协议与在线服务端、预览工装、文档）提交为 `fc7e46e Capture the 1.4 desktop motion, interaction and AstrBot work`；`stage/1.4-candidate` 快进合并进 `master`，两条分支均指向 `fc7e46e`，工作区恢复干净。
  - `.gitignore` 增加 `/.cargo-target/`。
  - 清理开发区：删除 `out/`（1.7G）、`.cargo-target/`（873M）、`release/local-bot-20261125090210`、`release/rust-migration`、`release/v1.4-s0`、`release/v1.4-candidate`、`release/v1.4-full-acceptance`、`release/v1.4-local-acceptance`、`.tools/tmp` 与两个 Temp 冒烟目录，仓库占用 15G→12G。保留 `target/`（4.3G Cargo 缓存）、`.tools/`（6.9G Android SDK/JDK）、`release/local-desktop-20261126`（上一版）与 `release/local-desktop-20261126-motion-1140`。
- 验证：安装后 `LANChat.exe` SHA256 `29870F26320E964B68B5CED64F9A57235A592BE08784427745CE43BE4FFF92C5` 与 release 包一致、与旧版 `344EE0F2…` 不同；启动安装版 8 秒后窗口标题 `LAN Chat Online`、`msedgewebview2.exe` 子进程存在，强制退出无残留。`git status --short` 为空；`git log -1 master` 与 `git log -1 stage/1.4-candidate` 均为 fc7e46e。未重跑 cargo test / vitest：源码树就是 11:40 出包时的状态，11:35 记录已通过 191 项测试、Lint、前端构建与 Windows Release 隔离启动。
- 下一步：桌面在线版自动更新验签公钥仍未配置；LAN 版安装目录仍是 09:36 构建（同为 1.4.0），需要时可从 `release/local-desktop-20261126-motion-1140/lan/` 同法替换。未推送远端（origin/master 落后 2 个提交，origin/stage/1.4-candidate 落后 1 个）。
- 阻塞与需拍板：是否把 `master` 与 `stage/1.4-candidate` 推送到 origin，以及是否同时替换 LAN 版安装。

### 2026-11-26 11:35 — 桌面动效播放

- 执行者：Codex。
- 改动：`frontend/src/styles/app.css`、`App.tsx`、`MessageList.tsx` 和 `EmojiPopover.tsx` 按应用内动效与性能设置播放过渡和滚动。
- 验证：Edge 实测搜索、菜单、表情和消息的动画时长。Lint、191 项测试、前端构建和 Windows Release 隔离启动与退出通过。
- 后续：本地工作区 `stage/1.4-candidate` 保留此次改动。本轮无待拍板事项。

### 2026-11-26 11:29 — 聊天搜索、气泡与表情分类交互

- 执行者：Codex。
- 改动：`frontend/src/components/chat/ChatHeader.tsx` 为搜索框和按钮添加过渡。窄窗展开搜索时让出标题区域。`MessageBubble.tsx` 将悬停和文字选择起点收进气泡。`frontend/src/components/composer/EmojiPopover.tsx` 为分类指示条和网格切换应用动效。
- 验证：Lint、191 项测试、前端构建与 Windows Release 隔离启动退出通过。Edge 在宽窗和 375 px 窄窗验证搜索与焦点，检查气泡拖选、操作栏、右键选中、表情的鼠标与键盘切换以及减少动态效果。
- 后续：本地工作区 `stage/1.4-candidate` 保留此次改动。本轮无待拍板事项。

### 2026-11-26 10:51 — 桌面聊天界面排版与动效

- 执行者：Codex。
- 改动：`frontend/src/app/WorkspacePage.tsx` 与 `frontend/src/styles/` 对齐聊天内容列和输入框，调整字体与过渡。会话行采用 48 px 高度，输入框随窗口宽度重新计算草稿行数。
- 验证：前端构建、Lint、191 项测试通过。Edge 六档宽度和 Chrome 两档宽度验证内容列对齐。Edge 复核菜单键盘操作、减少动态效果、消息淡入、模拟发送和草稿重排。局域网版离线 Release 构建与隔离 WebView2 启动、退出通过。
- 后续：本地工作区 `stage/1.4-candidate` 保留此次改动和原有修改。原生聊天交互验收待安装时执行。本轮无待拍板事项。

### 2026-11-26 09:37 — 桌面双版 1.4.0 更新

- 执行者：Codex。
- 改动：复核桌面主题、动效和 IME；`frontend/src/components/chat/MessageList.tsx` 修正减少动态效果与省电模式下的平滑滚动，补对应测试；`frontend/src/preview.tsx` 修正 lint 误报。
- 验证：前端 lint、191 项测试、版本一致性与 4 项版本测试通过；Rust server/client-core 回归（含 heartbeat、`lan_desktop`）通过。LAN 与 Online 1.4.0 安装包均通过 WebView2 冒烟和 Inno 原位更新；LAN 从 1.3.0 升级（含房主程序），Online 同版更新。原快捷方式启动确认 LAN 房主入口及 Online 注册账号按钮。
- 范围与后续：仅本机。安装程序备份 `%LOCALAPPDATA%/Temp/LANChat-desktop-backup-20261126`。真实双机收发未验证；Online 正式自动更新验签公钥未配置。阻塞/拍板：无。

### 2026-11-25 23:28 — Android 会话打开体验

- 执行者：Codex。Android 的推送注册成功回执在后台处理。
- 验证：离线构建通过，使用原签名覆盖安装。真机冷启动并打开 AstrBot 会话，历史消息和输入框正常显示。

### 2026-11-25 23:20 — 桌面机器人设置候选菜单

- 执行者：Codex。
- 改动：`frontend/src/app/BotSettingsSection.tsx` 和 `frontend/src/components/ui/menu.tsx` 接入应用内菜单。读取时显示所有候选，并标记当前编号。菜单优先向上展开，长列表使用应用的滚动条样式。
- 验证：Windows 隔离构建及 189 项前端测试通过。已覆盖更新本机在线版并从原快捷方式启动。实测三个人格全量显示、可切换，窗口底部通过 Esc 和方向键重开也能完整显示。

### 2026-11-25 22:56 — AstrBot OpenAPI Key 同账号同步

- 执行者：Codex。
- 改动：在线服协商 `online-bot-key-sync-v1`，SQLite 迁移到 5 并用 AES-256-GCM 保存 OpenAPI Key。密文绑定账号和 AstrBot 地址。Windows 在线版及 Android 打开机器人设置时同步密钥。
- 验证：`cargo test --offline --locked -p lan-chat-server -p lan-chat-client-core` 通过。隔离 Windows 构建及 189 项前端测试通过。Android 使用原签名覆盖成功。Android 和 Windows 同账号自动载入密钥并读取 `deepseek-flash`。生产副本 TLS 联调覆盖旧版兼容、账号隔离、并发冲突和地址变更。生产服务更新后 TLS 与 SQLite 完整性通过。
- 范围：已更新本机 Windows 在线版、Android 真机和生产在线服。生产回滚备份 `/var/backups/lan-chat-online/bot-key-sync.R3vnfIIr/`。

### 2026-11-25 17:12 — 补充升级清单验收与日志记录规则

- 执行者：记录子代理。
- 改动：在 `docs/plans/2026-11-23-v1.4-plus-hardening-plan.md` 增加六项后续升级方向及逐项验收条件；在本文件记录规则中明确项目修改完成后由主代理交接改动范围和验证结果、gpt-6-luna 记入日志顶部并由主代理核对，日志写入不递归触发。
- 验证：核对计划文档差异，六个方向均列出操作和验收条件；`git diff --check` 通过。
- 下一步：主代理核对本条记录。
- 阻塞/拍板：无。

### 2026-11-25 17:06 — 本机安装机器人设置候选

- Windows 在线版从 1.3.0 覆盖到 1.4.0。Inno 安装成功，原桌面快捷方式启动新版并显示在线登录页。
- Android 0.4.0 使用手机上原包的调试签名覆盖。账号登录状态保留，真机已打开「设置 → 机器人设置」并确认 AstrBot 地址和 OpenAPI Key 字段。
- 原 Windows 程序文件及 Android APK 备份保存在 `%TEMP%\LANChat-local-update-20261125090210`。

### 2026-11-25 — Windows 与 Android 机器人设置

- 执行者：Codex。
- 改动：Windows 在线版和 Android 增加机器人设置，通过 AstrBot OpenAPI 读取、新建和编辑模型源、模型及人格。在线 SQLite `user_version=4` 保存每个账号的 AstrBot 地址与配置编号，版本检查防止旧设置覆盖更新。模型 API Key 直接提交给 AstrBot，OpenAPI Key 在各端单独填写。使用步骤见 [AstrBot 手动接入](docs/release-runbook.md#astrbot-手动接入)。
- 验证：Rust 工作区构建和测试、桌面前端构建和回归通过。在线 TLS 回环验证同账号同步、旧版本保存冲突和账号隔离，AstrBot 4.29.4 HTTPS 接口模拟覆盖读写、密钥保留和拒绝重定向。Android 调试 APK 复用现有 JNI、资源和字体压缩产物打包，已核对 APK 中包含新代码和 ARM64 JNI。
- 范围：`stage/1.4-candidate` 本地工作区。真实 AstrBot 实例、Android 真机和生产环境待联调。

### 2026-11-25 — AstrBot 用户配置模型和内容

- 执行者：Codex。
- 改动：`integrations/astrbot_plugin_lan_chat/main.py` 负责消息接入。使用者在 AstrBot WebUI 配置模型服务、API 信息、人设、提示词和工具，入口见 [手动接入说明](docs/release-runbook.md#astrbot-手动接入)。
- 验证：源码语法检查通过。线上 AstrBot 运行环境成功导入新适配器并读取现有接入配置。原适配器备份在 `/var/backups/lan-chat-online/v1.4.0-H8K06wDp/astrbot-before-user-settings.py`。

### 2026-11-25 — 线上服务端 1.4.0 和 AstrBot 手动启用

- 执行者：Codex。
- 改动：服务端继承工作区发布版本。AstrBot 适配器由用户填写连接信息和账号后手动启用。线上 AstrBot 服务已停止并取消开机启动，原配置保留。
- 验证：Windows 和 Linux 服务端均显示 `1.4.0`。Linux 单元、心跳和隔离网络 LAN 集成测试通过。生产副本迁移、旧版恢复、候选 TLS 群聊与私信通过。已验证线上服务版本、Windows 到生产的 TLS 登录响应及 SQLite 完整性。已用现有 AstrBot 运行环境导入适配器并验证保存配置可用。
- 备份保存在服务器 `/var/backups/lan-chat-online/v1.4.0-H8K06wDp/`。Wi-Fi 与移动网络实切结果按用户确认记录，详见 [验收记录](docs/release-acceptance.md#线上服务端-140)。

### 2026-11-25 — Android 真实 FCM 送达

- 执行者：Codex。
- 验证：小米 14 Pro 的隔离包完成 FCM 设备注册。候选服务端发送的后台私信和锁屏群消息均进入 Android 通知列表。点击两条通知分别打开对应私信和群聊，隔离包的 ADB 崩溃日志为 0 条。
- 测试服务、USB 转发、手机隔离包和测试设备的推送登记已清理。Wi-Fi 与移动网络实切待验证。详见 [发布前验收](docs/release-acceptance.md#android-真机补充验收)。

### 2026-11-25 — Firebase 隔离测试配置

- 执行者：Codex。
- 验证：通过 Google Cloud CLI 创建独立 Firebase 测试项目，注册 Android 隔离包并配置测试发信服务账号。隔离包通过构建、包名及签名校验。候选服务端成功读取测试凭据并在本机回环地址启动，FCM HTTP v1 接受仅校验请求。
- 下一步：手机重新接入 ADB 后，验证后台和锁屏收件。结果记录在 [发布前验收](docs/release-acceptance.md#android-真机补充验收)。

### 2026-11-25 — LAN 更新签名配置

- 执行者：Codex。
- 验证：使用 Tauri 2.12.6 生成 LAN 更新签名，完成本地试签。GitHub 的 `release` 环境设置仓库所有者审批，已配置 LAN 更新公钥变量和两项签名 Secrets。通过 GitHub API 核对审批人、Secret 名称和公钥。
- Android：旧 APK 的证书与本机默认 Android 调试 keystore 一致。正式签名方案待定，FCM 继续使用隔离测试项目。

### 2026-11-24 16:26 — Android 真机补充验收

- 执行者：Codex。
- 验证：小米 14 Pro 的同签名调试版通过 `adb install -r` 从 0.3.3-rust 原位升级至 0.4.0。通过 `dumpsys package` 核对安装版本、通知权限和原用户数据目录。隔离服务下复核私信、群消息、图片和文件双向收发，分块文件与源文件一致。群历史翻页、邀请、管理员任免、群主转让、退群、重连以及通知跳转和权限开关均通过。候选包及隔离包的 ADB 崩溃日志均为 0 条。
- 结果记录在 [发布前验收](docs/release-acceptance.md#android-真机补充验收)。临时测试包、手机测试文件和 USB 转发已清理。分支仍为 `stage/1.4-candidate` 的 `1c3ca6d`，验收记录保存在本地。
- 下一步：Wi-Fi 与移动网络实切仍待验证。后台和锁屏实际 FCM 投递需隔离测试项目凭据。

### 2026-11-24 — 1.4 候选验收与 LAN 草稿

- 执行者：Codex。
- 验证：`stage/1.4-candidate` 的 `1c3ca6d` 通过五项干净 CI。用实际 1.3 在线库验证迁移、旧附件读取和备份恢复。两版隔离 Windows 环境完成 1.4.0 到 1.4.1 签名升级、跨版拒绝及卸载设置保留。Android 0.4.0 ARM64 调试包与旧 0.3.3 调试包签名证书一致。
- 验收范围：用户批准跳过 LAN 双机，Windows 采用隔离安装验收，Android 采用本机 APK 验收。在线附件上限为图片 4 MiB、文件 32 MiB。
- 交付：草稿仅含 `LANChat-Desktop-Windows-x64.zip`，实际 ZIP 内为客户端、房主服务端、图标与说明。[候选验收记录](docs/release-acceptance.md#140-候选验收范围)

### 2026-11-24 — 1.4 完整候选的本机验证

- 执行者：Codex。
- 改动：`server-rust/src/lan` 按职责拆分，并将附件文件 I/O 移出 Hub 锁。`server-rust/src/online` 增加 FTS5、磁盘附件和旧库迁移、群管理及群附件。桌面和 Android 接入群功能。发布工作流配置两版独立签名和 Android 构建。
- 验证：Rust 工作区、Clippy、前端 188 项、版本校验 4 项及 LAN TLS 11 项通过。群权限、旧附件迁移和 LAN 附件并发用例通过。两版 ZIP 及安装器已构建。Android 0.4.0 ARM64 APK 构建和 v2 签名通过。
- 产物：`release/v1.4-candidate/`。双机按用户要求跳过，Android 以本机 APK 验收。候选干净 CI 使用独立测试分支，正式签名和标准安装权限待确认。

### 2026-11-23 — 1.4 局域网候选与本机验收

- 执行者：Codex。
- 改动：S0 提交 `aa04d78` 推送测试分支。`server-rust` 完成心跳回归、FTS5 历史搜索和房间权限持久化，`client-core` 与 `frontend` 接入搜索分页及房间权限。Tauri、Inno Setup 和 LAN 发布工作流接入签名更新。
- 验证：S0 干净 CI 四项通过。本机工作区测试和 Clippy、前端 lint/188 项测试/构建、版本检查 4 项、TLS 11 项通过。完成 1.3 与 1.4 双向互通、旧库迁移、隔离签名更新及卸载保留数据。详见[局域网验收记录](docs/release-acceptance.md#140-局域网验收记录)。
- 工作区与产物：`stage/0-baseline` 上保留后续本地源码改动。`release/v1.4-local-acceptance/LANChat-Desktop-Windows-x64.zip` 仅含客户端、房主服务程序、图标和说明。
- 待验收：候选干净 CI、正式签名演练、可写注册表和快捷方式的 Windows 安装环境、真实双机发现与重连。完成后交付无数据 LAN 便携 ZIP。

### 2026-11-23 19:43 — S0 本机基线与局域网便携包

- 执行者：Codex。
- 改动：`server-rust/src/lan/mod.rs` 让附件控制帧和加密附件消息使用原命令限额排队，修复附件落盘后消息被限流的问题。`lan_desktop.rs` 同时断言发送方和接收方都能看到附件。桌面 `1.4.0` 版本从工作区 Cargo 配置同步，CI 增加 LAN 安装器预检及 TLS 验收。
- 验证：工作区离线测试及 Clippy 通过，附件连续 12 次通过，前端 188 项测试与构建通过，版本校验 4 项通过。Windows LAN 便携包启动、安装器预检和 TLS 11 项通过，ZIP 检查仅含客户端、房主服务程序、图标和说明。
- 产物：`release/v1.4-s0/LANChat-Desktop-Windows-x64.zip`。
- 后续：干净 CI、后续阶段和完整 1.4 验收待完成。

### 2026-11-23 19:18 — 合并 Rust 工作区与 v1.4 执行文档

- 执行者：Codex。
- 改动：将 Rust 服务端、桌面、Android、前端及 v1.4 文档提交为 `fcb7b19`，先同步远端 `master` 的 PR #25 合并提交，再合入本地 `master`。
- 验证：所有已获取的项目分支均在合并历史内。合并前检查通过，无冲突。前端 188 项测试、lint、生产构建及版本检查 4 项测试通过。Rust 协议 5 项通过，`lan_desktop` 首次通过、再次运行在附件步骤超时。全量 workspace 离线测试缺少 `alloc-no-stdlib v3.0.0`。
- 后续：按 S0 验收条件处理附件间歇失败及干净环境构建。当前 Git 操作为本地合并。

### 2026-11-23 18:40 — 补齐 H1–H4 执行 Prompt
- 执行者：lead（DSH agent）
- 做了什么：新建 `docs/prompts/v1.4/H1-heartbeat.md`（91 行）、`H2-fts-search.md`（89 行）、`H3-hub-split.md`（75 行）、`H4-attachment-io.md`（84 行），均含前置条件、任务分解、设计红线、验收命令、报告与禁止项；`docs/prompts/v1.4/README.md` 表格与引言改为合并计划口径并加入 H1–H4 行；合并计划 §13 交付物改为 10 份 prompt 实际链接。
- 验证：文档级改动，未跑测试；已确认 4 个新文件存在且无占位符。
- 下一步：等 G0 拍板 → S0 开工。
- 阻塞/拍板：G0（基线提交、Claude-BugHunter/）、H4（WAL）、H5（rename）；产品 P1–P7。

### 2026-11-23 18:20 — 修订加固计划并合并 v1.4 计划
- 执行者：lead（DSH agent）
- 做了什么：把 [服务端加固计划](docs/plans/2026-11-23-server-hardening-plan.md) 修订为 v2（修正编译/Git/环境事实；补 FTS 自愈与 MATCH 回退、C2 WAL/测试钩子/权限对照/rename、门禁分级）；新建 [v1.4 升级与加固合并计划](docs/plans/2026-11-23-v1.4-plus-hardening-plan.md)（8 周主路径 + 6 周保底、S0→H1–H4→S1–S5、统一决策编号）；两份原计划加状态说明；docs/README.md 索引已更新。
- 验证：文档级改动，未跑测试。引用基线事实均来自 2026-11-23 实测：D8050 环境变量绕过可编译；lan_desktop e2e 失败于附件步骤；git 分支 feat/01-stability-downloads，HEAD 78552a7，306 项未提交。
- 下一步：等用户拍板 G0 后执行 S0。
- 阻塞/拍板：G0（基线提交、Claude-BugHunter 去留）、H1/H4/H5、P1–P7。

### 2026-11-23 13:20 — 建立工作日志机制（本文件）

- 执行者：lead（DeepSeek Harness）
- 做了什么：用户要求“记录工作日志以保证其他 agent 可随时接手，先检查本机有没有日志”。检查结论：**此前没有任何工作日志**——仓库根无 WORKLOG.md / AGENTS.md，无 worklog 目录，全仓库无 *.log / *.jsonl（.tools 依赖缓存除外）；`%LocalAppData%\DUSK1NG` 下也无日志文件；最接近的交接材料是 `docs/CODEX_HANDOFF.md`（2026-11-22 更新）与 `docs/rust-migration.md`。新建本文件，并在 `README.md` 与 `docs/README.md` 增加入口。
- 验证：写入后用 `ops_read_files` 回读；README / docs 索引中的入口指向本文件。
- 下一步：按服务端加固计划 P0 做环境检查（cargo / link.exe 可读写），再进阶段 A。
- 阻塞：G0（305 项改动是否提交）、Claude-BugHunter/ 归属。

### 2026-11-23 — 服务端加固计划制定（待执行）

- 执行者：用户 / 其他 agent（本会话只读取，未参与制定）
- 做了什么：新增 `docs/plans/2026-11-23-server-hardening-plan.md`：A 应用层心跳 heartbeat-v1（20s ping / 60s 超时）、B FTS5 历史搜索、C1 拆分局域网 Hub、C2 附件 IO 移出 Hub 锁；要求在该计划完成后再做 v1.4 的 S2。
- 验证：**未验证**——计划自述“待执行”，阶段 A 代码已写入但从未编译。
- 下一步：P0 环境检查 → 基线 `cargo test --workspace --locked`。
- 阻塞：cargo / link.exe 的历史 ACL 故障（CodexSandboxUsers；cargo 报 `failed to create directory` / `failed to open: Cargo.lock`，link.exe 报 `LNK1104`）是否已修复未知。

### 2026-11-22 — v1.4 升级计划与阶段执行 Prompt 交付

- 执行者：lead（DeepSeek Harness），依据用户 4 轮问答
- 做了什么：产出 `docs/plans/2026-11-22-v1.4-upgrade-plan.md` 与 `docs/prompts/v1.4/`（00-baseline、01-release、02-lan-persistence、03-online-storage、04-online-groups、05-android-acceptance、README）；`docs/README.md` 增加“计划与执行 Prompt”段；48 条相对链接校验 0 失效。
- 决策：6 周 S0–S5；LAN 1.4.0 + Online 1.4.0 同版；优先 G1 发布与自更新 / G2 局域网权限持久化 / G3 在线存储重构 / G4 在线群 / G5 底座；超期先砍局域网管理员/禁言持久化；桌面 1.4.0、Android 0.4.x；单一版本源。
- 验证：链接已校验；计划本身未评审、未开工。
- 下一步：S0（Git 基线、版本同步脚本、CI 补齐）——但按服务端加固计划，先做该计划。

### 2026-11-22 — 文档去 Go 化、迁移完成报告与历史归档

- 执行者：lead（DeepSeek Harness）
- 做了什么：`docs/rust-migration.md` 重写为“迁移完成报告”；`docs/decisions/0001-rust-only.md` 标记“已接受并已执行”；`docs/project/specs/01–04`、`docs/handover`、`docs/prompts` 加归档横幅；`engineering-methodology.md` 命令表改为 Rust；Go + Wails + C++ 时代文档随后归入 `docs/archive/`（现含 README.md、developer-transfer.md、handover/、project-specs/、prompts/、stage9-lan-test.md、superpowers/），只读保留。
- 验证：当时仓库没有 .git，无法用 diff 核对；文档链接在后续索引更新中检查。
- 下一步：无（历史工作，已归档）。
