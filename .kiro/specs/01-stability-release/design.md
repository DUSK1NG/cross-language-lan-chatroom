# 01 稳定性与发布 — 设计

## D1 下载链路（R2）
现状：`GuiChatController::downloadAttachment` 弹出保存框 → `GuiConnectionWorker::startAttachmentDownload`（`gui_connection_worker.cpp:750-783`）→ 每收到一块再请求下一块 → `AttachmentDownloadAssembler`（`attachment_download_assembler.cpp:12-76`）严格顺序写入最终路径。

改动：
1. **临时文件**：assembler 写入 `<目标路径>.lanchat-part`，全部校验通过后 `QFile::rename` 到目标路径（Windows 上先删除已存在目标，失败则报错保留原文件）。任何错误、取消或 worker 析构时删除 `.lanchat-part`。
2. **窗口化**：worker 维护 `nextRequestIndex`、`inFlight`（上限 8）与 `std::map<int, QByteArray>` 乱序缓冲（上限 = 窗口大小）；assembler 接口保持「按序 accept」，乱序块先进缓冲，轮到时再交给 assembler。服务端 `handleAttachmentDownload`（`hub.go:1796-1817`）无需修改，单个请求仍返回单块。
3. **自身 manifest**：`sendAttachmentManifest`（`gui_connection_worker.cpp:960-986`）在发送前把 manifest 写入 `receivedAttachmentManifests_`（命名改为 `knownAttachmentManifests_`）。
4. **进度事件**：worker 每 64 块或 250 ms（取先到者）发一次 `attachmentEvent("download.progress", id, received, total)`；完成发 `completed`，失败发 `error`。`GuiChatController::handleAttachmentEvent`（`gui_chat_controller.cpp:754-778`）据此更新消息行 `attachment.status` 为 `downloading`/`saved`/`failed` 与 `receivedChunks`。前端 `MessageAttachment.status` 增加 `'saved' | 'failed'`。

测试：assembler 失败清理、乱序缓冲按序写入、窗口上限、发送者可下载自己的附件、进度节流（C++ QtTest）；前端附件卡三种状态（vitest）。

## D2 CI（R3）
1. CMake 增加 `option(LAN_CHAT_BUILD_GUI "Build the Qt WebEngine GUI" ON)`。为 OFF 时只 `find_package(Qt6 COMPONENTS Core Network Test)` + OpenSSL，构建纯逻辑库与测试：`protocol`、`auth`、`command`、`connection`、`attachment-*`、`transfer-client`、`reconnect-policy`、`bridge-protocol`、`network-diagnostics`、`performance-*`。需要把这些源从 GUI 可执行目标中拆成 `lan_chat_core` 静态库（GUI 目标链接它，行为不变）。
2. 新增 CI 作业 `cpp-core`（ubuntu-latest，apt 安装 `qt6-base-dev libssl-dev cmake ninja-build`）：`cmake -S client-cpp -B build -G Ninja -DLAN_CHAT_BUILD_GUI=OFF && cmake --build build && ctest --test-dir build --output-on-failure`。
3. 新增 CI 作业 `cpp-gui-windows`（windows-latest）：用 `jurplel/install-qt-action`（Qt 6.11.2 msvc2022_64 + qtwebengine qtwebchannel，开启缓存）后执行 `scripts/build-modern.ps1 -Action Test`。MLS++ 开关保持默认 OFF（MLS 测试在本地发布前跑）。
4. 若 Windows 作业耗时 > 30 分钟，改为仅在 `master` 推送与 tag 上运行，PR 上只跑 `cpp-core`（在 PR 中记录实测时长）。

## D3 依赖固定（R4.1）
用 `pnpm-lock.yaml` 中解析出的版本替换 `frontend/package.json` 里所有 `"latest"`（精确版本，不加 `^`）。`pnpm install` 后锁文件不应变化；CI 已用 `--frozen-lockfile`。

## D4 发布工作流（R4.2–R4.5）
- 新文件 `.github/workflows/release.yml`，触发 `push: tags: ['v*']`，运行于 windows-latest：安装 Qt、Inno Setup（`choco install innosetup`）、Go、Node/pnpm → `scripts/build-modern.ps1 -Action Build` → `scripts/package-unified-release.ps1` → `scripts/package-installer.ps1 -Version <tag 去掉 v>` → `scripts/test-member-package.ps1` → 生成 `SHA256SUMS.txt` → `gh release create` 上传，正文 `docs/release-notes/v<version>.md`。
- `scripts/check-version.mjs`（Node，无依赖）：读取三处版本号，不一致则退出码 1；在 CI 的 frontend 作业与 release 作业中运行；tag 版本必须与之相等。
- MLS++：发布包是否启用 MLS++ 由现有打包脚本决定；工作流需复用 `scripts/test-mlspp-supply-chain.ps1` 的校验步骤（若打包需要 MLS++ 源，见任务中的决策门）。
- 签名：若仓库 secrets 存在 `WINDOWS_CERT_PFX`/`WINDOWS_CERT_PASSWORD`，用 `signtool` 签名 exe 与安装包；否则跳过并在发行说明末尾追加「未签名」段落。

## D5 前端健壮性（R5）
- `frontend/src/app/ErrorBoundary.tsx`：类组件，捕获渲染错误，显示与 `BridgeUnavailablePage` 同风格的错误页（重试 = `location.reload()`，返回 = 清空页面状态回到模式选择），并通过 bridge 发送 `diagnostics.frontendError`（仅错误名与组件栈前 2 KB，不含消息内容）。C++ 侧仅当连接日志开启时写入日志。
- `main.tsx` 注册 `unhandledrejection` 监听，同样上报并 `preventDefault()`。

## 风险
- Windows CI 安装 Qt WebEngine 体积大：用缓存；必要时按 D2.4 降级。
- 拆分 `lan_chat_core` 可能牵动 include 路径：只移动目标定义，不移动源文件位置。
