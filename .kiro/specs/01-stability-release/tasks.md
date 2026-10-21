# 01 稳定性与发布 — 任务

执行规则见 `.kiro/steering/engineering-methodology.md`。每个子任务完成后勾选并追加证据行。

- [ ] 1. 依赖与版本基线（分支 `feat/01-stability-baseline`）
  - [x] 1.1 用 `pnpm-lock.yaml` 的解析版本替换 `frontend/package.json` 中全部 `"latest"`；`pnpm install --frozen-lockfile` 通过且锁文件无变化。_R4.1_
    > 证据：`package.json` 的 12 个 `"latest"` 改为锁文件解析出的精确版本（如 react 19.2.8、vite 8.2.2、vitest 4.1.11）；锁文件仅 `specifier:` 行随之更新，所有 `version:` 解析结果不变。`pnpm install --frozen-lockfile` 通过；`npx tsc --noEmit` 无错误；`npx vitest --run` = 16 文件 / 132 测试全绿；`npx vite build` 成功（随后删除 dist）。
  - [ ] 1.2 新增 `scripts/check-version.mjs` 及其最小自测（构造不一致输入应退出 1）；接入 `.github/workflows/ci.yml` 的 frontend 作业。_R4.5_
  - [ ] 1.3 新增 `frontend/src/app/ErrorBoundary.tsx` + `unhandledrejection` 处理；先写 vitest：子组件抛错时渲染错误页且「重试」可点击。_R5.1 R5.2_
  - [ ] 1.4 bridge 增加 `diagnostics.frontendError` 命令：`bridge_protocol.cpp` 白名单校验（字段 `name`、`stack`，各 ≤ 2048 字节）+ `bridge_protocol_tests.cpp` 用例；仅在连接日志开启时写日志。_D5_

- [ ] 2. C++ 进入 CI（分支 `feat/01-stability-ci`）
  - [ ] 2.1 在 `client-cpp/gui/CMakeLists.txt` 抽出 `lan_chat_core` 静态库并增加 `LAN_CHAT_BUILD_GUI` 选项；GUI=ON 时目标与测试列表与现在完全一致（对比 `ctest -N` 输出）。_D2.1_
  - [ ] 2.2 在 Linux 上以 `-DLAN_CHAT_BUILD_GUI=OFF` 配置、构建并运行 ctest，全部通过。_R3.1_
  - [ ] 2.3 CI 新增 `cpp-core` 作业。_R3.1 R3.3_
  - [ ] 2.4 CI 新增 `cpp-gui-windows` 作业（Qt 缓存）；在 PR 描述记录首次与缓存命中耗时，必要时按 D2.4 调整触发条件。_R3.2_

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
