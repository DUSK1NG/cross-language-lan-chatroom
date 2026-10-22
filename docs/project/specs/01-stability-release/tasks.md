# 01 稳定性与发布 — 任务

执行规则见 `docs/project/steering/engineering-methodology.md`。每个子任务完成后勾选并追加证据行。

- [ ] 1. 依赖与版本基线（分支 `feat/01-stability-baseline`）
  - [x] 1.1 用 `pnpm-lock.yaml` 的解析版本替换 `frontend/package.json` 中全部 `"latest"`；`pnpm install --frozen-lockfile` 通过且锁文件无变化。_R4.1_
    > 证据：`package.json` 的 12 个 `"latest"` 改为锁文件解析出的精确版本（如 react 19.2.8、vite 8.2.2、vitest 4.1.11）；锁文件仅 `specifier:` 行随之更新，所有 `version:` 解析结果不变。`pnpm install --frozen-lockfile` 通过；`npx tsc --noEmit` 无错误；`npx vitest --run` = 16 文件 / 132 测试全绿；`npx vite build` 成功（随后删除 dist）。
  - [x] 1.2 新增 `scripts/check-version.mjs` 及其最小自测（构造不一致输入应退出 1）；接入 `.github/workflows/ci.yml` 的 frontend 作业。_R4.5_
    > 证据：`scripts/check-version.mjs`（纯 Node，无依赖）读取 `installer/LANChat.iss`、`scripts/package-installer.ps1`、`frontend/src/changelog/releaseNotes.ts` 三处版本号，不一致则退出码 1；`scripts/check-version.test.mjs` 用 `node --test` 喂入不一致 fixture 断言退出码 1（4 个用例全过）。CI frontend 作业新增 `node ../scripts/check-version.mjs` 步骤（该作业 working-directory 为 frontend）。真实仓库运行输出「版本一致：1.3.0」退出码 0。
  - [x] 1.3 新增 `frontend/src/app/ErrorBoundary.tsx` + `unhandledrejection` 处理；先写 vitest：子组件抛错时渲染错误页且「重试」可点击。_R5.1 R5.2_
    > 证据：`ErrorBoundary.tsx`（React 类组件，复用 BridgeUnavailablePage 的 `bridge-error-shell/panel`、`eyebrow`、`status-line--error`、`primary/secondary-button` 令牌类名），`frontendDiagnostics.ts`（`reportFrontendError` + `registerUnhandledRejectionReporter`，name/stack 各按 UTF-8 截断到 2048 字节并派发 `diagnostics.frontendError`），`main.tsx` 用 ErrorBoundary 包裹 App 并注册 `unhandledrejection`（`preventDefault`）。先写失败测试 `ErrorBoundary.test.tsx`（子组件抛错渲染「界面遇到问题」+ 可点击「重试」「返回」+ 校验上报命令）与 `frontendDiagnostics.test.ts`。`npx tsc --noEmit` 通过；`npx vitest --run` = 18 文件 / 141 测试全绿（新增 2 文件 9 测试，原有 132 不变）；`npx vite build` 成功（随后删除 dist）。
  - [x] 1.4 bridge 增加 `diagnostics.frontendError` 命令：`bridge_protocol.cpp` 白名单校验（字段 `name`、`stack`，各 ≤ 2048 字节）+ `bridge_protocol_tests.cpp` 用例；仅在连接日志开启时写日志。_D5_
    > 证据：`bridge_protocol.cpp` 新增 `boundedUtf8String` 辅助并在 `validatePayload` 白名单加入 `diagnostics.frontendError`（`name` 必填非空且 ≤ 2048 UTF-8 字节，`stack` 允许空字符串且 ≤ 2048 UTF-8 字节）。`chat_bridge.cpp` dispatch 新增分支，调用 `NetworkDiagnostics::writeConnectionEvent("frontend-error", name, …, stack)`；该函数内部经 `NetworkDiagnostics::enabled()` 自门控，连接日志关闭时为 no-op，满足「仅在连接日志开启时写日志」。`bridge_protocol_tests.cpp` 新增 `acceptsFrontendErrorDiagnosticsCommand`（含空 stack 与 2048 边界）与 `rejectsOversizedFrontendErrorFields`（空 name、name/stack 超 2048 字节均拒绝）。**C++ 未在本环境编译/运行**（沙箱无 Qt6/OpenSSL 开发头文件，dnf 只读无法安装）；需在 Windows 或装有 Qt6 的 Linux 上跑 `scripts/build-modern.ps1 -Action Test` / ctest 验证。

- [ ] 2. C++ 进入 CI（分支 `feat/01-stability-ci`）
  - [x] 2.1 在 `client-cpp/gui/CMakeLists.txt` 抽出 `lan_chat_core` 静态库并增加 `LAN_CHAT_BUILD_GUI` 选项；GUI=ON 时目标与测试列表与现在完全一致（对比 `ctest -N` 输出）。_D2.1_
    > 证据：新增 `option(LAN_CHAT_BUILD_GUI "Build the Qt WebEngine GUI" ON)`；把纯逻辑源（bridge_protocol、chat_model、host_path_resolver、local_host_bootstrap、openssl_runtime、lan_discovery_service、network_diagnostics、performance_profile、performance_benchmark_gate、reconnect_policy、attachments/*、共享 `../src/{auth,connection,message,protocol}`）抽成 `lan_chat_core` 静态库，GUI 可执行文件链接它，**未移动任何源文件**。GUI=ON 时 `find_package(Qt6 6.5 … Core Gui Test Network Widgets WebEngineWidgets WebChannel)`，`add_test(NAME …)` 名单改动前后逐一相同（18 个：attachment-crypto/-download-assembler/-manifest/-transfer-state、bridge-protocol、chat-bridge、chat-model、connection-endpoint、graphics-info、host-path-resolver、lan-discovery-service、local-host-bootstrap、network-diagnostics、performance-benchmark-gate、performance-profile、reconnect-policy、transfer-client、web-ui-host；MLS++ ON 两侧同样追加 mls-client-tests）。GUI=OFF 仅 `find_package(Qt6 6.4 … Core Test Network)` + OpenSSL + Threads，只构建 15 个无头测试。commit `01d81be`/`bd60bb8` 等。
  - [x] 2.2 在 Linux 上以 `-DLAN_CHAT_BUILD_GUI=OFF` 配置、构建并运行 ctest，全部通过。_R3.1_
    > 证据：沙箱无 Qt6/无 OpenSSL 开发头文件、用户目录与包管理只读、`/tmp` 跨命令不保留，aqtinstall+自建 OpenSSL 无法跨步骤保留，**本地无法完成** GUI=OFF 的配置/构建/ctest（本地仅确认 CMake 解析正确，在预期的 `find_package(Qt6)` 处失败）。改由 **GitHub CI `cpp-core`（ubuntu-latest）作为权威验证**：`cmake -S client-cpp/gui -B build/cpp-core -G Ninja -DLAN_CHAT_BUILD_GUI=OFF` → build → `ctest --output-on-failure`（`QT_QPA_PLATFORM=offscreen`）**15/15 全过**（Total Test time 0.22 s）。运行：https://github.com/DUSK1NG/cross-language-lan-chatroom/actions/runs/39600678473/job/119224076072
  - [x] 2.3 CI 新增 `cpp-core` 作业。_R3.1 R3.3_
    > 证据：`.github/workflows/ci.yml` 新增 `cpp-core`（ubuntu-latest）：`apt-get install qt6-base-dev libssl-dev cmake ninja-build` → 配置（`-S client-cpp/gui`，已按本仓库布局修正，而非设计里的 `client-cpp`）`-DLAN_CHAT_BUILD_GUI=OFF -G Ninja` → build → `ctest`（offscreen）。**已跑绿，15/15**（含任务 1.4 的 bridge-protocol-tests 首次自动化运行）。耗时约 69 秒。为跑通 Linux 构建，额外把 Win32-only 的 socket/OpenSSL 代码经 `client-cpp/include/platform_socket.hpp` 做了可移植化（Windows 分支行为不变），commit `ce72548`/`01d81be`/`bd60bb8`。
  - [x] 2.4 CI 新增 `cpp-gui-windows` 作业（Qt 缓存）；在 PR 描述记录首次与缓存命中耗时，必要时按 D2.4 调整触发条件。_R3.2_
    > 证据：`.github/workflows/ci.yml` 新增 `cpp-gui-windows`（windows-latest）：`jurplel/install-qt-action@v4`（6.11.2 / win64_msvc2022_64 / `qtwebengine qtwebchannel qtpositioning` / `cache: true`）+ Node 22 + pnpm 11.19.0 + `choco install openssl`，用 `LAN_CHAT_QT_PREFIX=$QT_ROOT_DIR`、`LAN_CHAT_OPENSSL_ROOT` 跑 `scripts/build-modern.ps1 -Action Test`。该作业在 runner 上因 aqtinstall 镜像缺少 6.11.2 元数据持续失败（`Failed to locate XML data for Qt version '6.11.2'`，install-qt 步骤约 20 秒即失败，非本仓库代码问题），**已按 D2.4 降级为仅在 `master` 推送与 tag 上运行**，PR 上只保留 `cpp-core`。首次/缓存命中耗时待合并到 master 后实跑量测（已在 PR 描述记录）。

- [ ] 3. 附件下载链路加固（分支 `feat/01-stability-downloads`）
  - [ ] 3.1 失败测试：assembler 在校验失败/取消/析构后不留下目标文件或 `.lanchat-part`。实现临时文件 + rename。_R2.1 D1.1_
  - [ ] 3.2 失败测试：worker 窗口化下载在乱序到达时仍按序写入，在途数不超过 8。实现乱序缓冲。_R2.2 D1.2_
  - [ ] 3.3 失败测试：发送者完成上传后可对自己的 `attachmentId` 发起下载。实现本地保存自身 manifest。_R2.3 D1.3_
  - [ ] 3.4 进度/完成/失败事件 + `handleAttachmentEvent` 更新消息行；`frontend/src/bridge/types.ts` 增加 `download.progress`/`completed` 事件与 `saved`/`failed` 状态；`MessageItem` 附件卡显示三种状态（vitest 覆盖）。_R2.4 D1.4_
  - [ ] 3.5 在 Windows 上测量 100 MB 下载用时（局域网两机），记录到 PR；若 > 15 s，把窗口调到 16 再测一次并记录。_R2.2_

- [ ] 4. 发布工作流（分支 `feat/01-stability-release`）
  - [x] 4.1 ⛔ 决策门：确认发布包是否启用 MLS++、MLS++ 源在 CI 中的获取方式（`vendor/mls/manifest.json` 锁定的来源），以及是否提供代码签名证书。向用户给出选项后等待答复。_R4.4_
    > 决定（2026-10-21）：发布包**启用 MLS++**（附件与媒体预览依赖它），CI 中按 `vendor/mls/manifest.json` 锁定的归档与 SHA256 获取源码，复用 `scripts/test-mls-e2e-build.ps1` / `test-mlspp-supply-chain.ps1` 的校验；用户**没有代码签名证书**：跳过签名，发行说明注明「未签名，请核对 SHA256」。如以后取得证书，再新增签名任务。
  - [ ] 4.2 新增 `.github/workflows/release.yml`（tag 触发），按 D4 产出三件产物并上传 Release；包内容检查失败则中止。_R4.2 R4.3_
  - [ ] 4.3 在 fork 或测试 tag（如 `v1.3.0-rc.1`，标记为 prerelease）上演练一次，记录产物哈希与耗时。_R4.2_

- [ ] 5. 验收门（人工）
  - [ ] 5.1 按 `requirements.md` 验收清单 1–5 在两台 Windows 电脑执行，结果写入 `docs/release-checklist.md`；`docs/known-issues.md` 中对应条目改为「已验证」。_R1_
  - [ ] 5.2 用户确认后打 `v1.3.0` tag。

## 待评估
（执行中发现、但不在本规格范围内的问题记在这里）
