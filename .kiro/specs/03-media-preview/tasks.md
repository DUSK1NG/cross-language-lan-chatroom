# 03 媒体预览与私信附件 — 任务

前置：规格 01 任务 3 与规格 02 任务 3.4 已合并。执行规则见 `.kiro/steering/engineering-methodology.md`。

- [ ] 1. 决策（不写代码）
  - [x] 1.1 ⛔ 决策门：向用户陈述 design.md D1 的 (a)/(b)/(c) 与磁盘缓存风险，确认不变量修订文字，以及「退出时清除媒体缓存」是否默认开启。确认后把修订后的不变量写入 `.kiro/steering/engineering-methodology.md` §3 与 `docs/handover/2026-09-01-encrypted-attachments.md`。_R4.5_
    > 决定（用户 2026-10-21 采纳推荐）：方案 (a) 媒体缓存 + `lanchat-media://` 不透明令牌；不变量按 design.md D1 修订；「退出时清除媒体缓存」默认**开启**，设置中可关闭。
  - [x] 1.2 ⛔ 决策门：视频方案——(i) 引入 Qt Multimedia 生成首帧与时长（推荐）；(ii) 不引入，视频无缩略图；另问是否评估自行编译带专有编解码器的 Qt WebEngine（许可与专利风险由用户判断，默认不做）。_R1.3 R3.3_
    > 决定（用户 2026-10-21 采纳推荐）：(i) 引入 Qt Multimedia 生成首帧与时长；WebEngine 能播放的格式（WebM 等）内联播放，H.264 MP4 提供「用系统播放器打开」；不自行编译带专有编解码器的 WebEngine。

- [ ] 2. 技术探针（分支 `spike/03-media-scheme`，不合并，只产出结论）
  - [ ] 2.1 在 Qt 6.11.2 WebEngine 中注册 `lanchat-media` scheme，验证：qrc 页面中 `<img>`/`<video>` 可加载；CSP 生效；对 80 MB WebM 拖动进度时 handler 是否收到 `Range`，以及 `requestHeaders()`/`setAdditionalResponseHeaders` 是否可用。结论写入本规格 `design.md` D5 末尾。_R4.3_
  - [ ] 2.2 在同一环境记录 `canPlayType` 对 `video/mp4; codecs="avc1.42E01E"`、`video/webm; codecs="vp9"`、`video/mp4; codecs="av01.0.05M.08"` 的结果，写入 D7。_R3.3_

- [ ] 3. manifest 与发送端（分支 `feat/03-media-sender`）
  - [ ] 3.1 失败测试 → manifest `media` 可选字段编解码、严格校验、校验失败仅丢弃 media；旧解码逻辑夹具接受新 manifest。_D2 R6.1_
  - [ ] 3.2 失败测试 → 消息大小预算与缩略图降级（构造接近 64 KiB 的情形）。_R1.5_
  - [ ] 3.3 失败测试 → `MediaProbe` 图片路径（PNG、带 EXIF 方向的 JPEG、GIF 首帧、101 MB 文件返回空、`.jpg` 扩展名的文本文件返回空）。_R1.1 R1.2_
  - [ ] 3.4 `MediaProbe` 视频路径（按 1.2 决策；若选 (ii) 则只做类型识别）。_R1.3_
  - [ ] 3.5 上传流程接入 MediaProbe（worker 线程），manifest 携带 media；`chat_bridge_tests` 中 MLS 开启时的附件管线测试扩展断言。_R1.4_

- [ ] 4. 缓存、令牌与 scheme（分支 `feat/03-media-cache`）
  - [ ] 4.1 失败测试 → `MediaCache`：随机文件名、LRU 淘汰、`remove`、`clear`、索引持久化与损坏恢复。_R4.1 R4.4_
  - [ ] 4.2 失败测试 → `MediaRegistry`：令牌长度与随机性、作废、缩略图内存上限。_R4.2_
  - [ ] 4.3 `MediaSchemeHandler` + 注册（`main.cpp`、`web_ui_host.cpp`）；测试覆盖拒绝未登记令牌、拒绝非 qrc 发起者、正确 MIME、Range（按 2.1 结论）。_R4.3_
  - [ ] 4.4 `attachment.preview`：下载到缓存、登记令牌、更新消息行 `previewUrl`/`status`；发送端上传后复制原文件进缓存。_R2.2 R2.5_
  - [ ] 4.5 撤回/登出/断开时删除缓存与作废令牌；`settings.clearMediaCache`、`settings.setMediaAutoLoad`；`index.html` 增加 CSP 并确认现有界面无回归。_R4.4 R2.3_

- [ ] 5. 前端预览与灯箱（分支 `feat/03-media-ui`）
  - [ ] 5.1 `types.ts` 契约扩展与 `bridge_protocol.cpp` 新命令白名单（含 C++ 校验测试）。_D6_
  - [ ] 5.2 失败测试 → `MediaAttachment`：宽高比占位、视频播放按钮与时长、> 100 MB 或非媒体保持文件卡。_R2.1 R2.4_
  - [ ] 5.3 失败测试 → 自动加载策略（≤10MB/始终/从不，使用 mock `IntersectionObserver`）；设置页新增选项与「清除媒体缓存」。_R2.2 R2.3_
  - [ ] 5.4 `MediaViewer`：FLIP 打开/关闭、缩放 1–4×、平移、`←`/`→`、`Esc`、保存/系统播放器打开；视频不可播放时降级。遵守 design-language 的时长/缓动令牌与门控。_R3_
  - [ ] 5.5 截图与关键帧证据：时间线（浅/深/窄屏）、灯箱打开过程 3 帧、reduced-motion 下无动画。_design-language §6_

- [ ] 6. 私信附件（分支 `feat/03-dm-attachments`）
  - [ ] 6.1 失败测试（Go）→ 作用域迁移（旧行加 `room:` 前缀）与 dm 成员检查（第三人被拒）、dm 配额 2 GiB、init 白名单仍拒绝 `file_path`。_R5.2 R5.5_
  - [ ] 6.2 失败测试（Go）→ 离线私信保留 `crypto` 字段；如不保留则修复。_R5.4_
  - [ ] 6.3 C++：`chooseAndUploadAttachment` 支持 `peerUserCode`、作用域键进入 group id 与 AAD、manifest 走 `private_chat`；接收端作用域一致性校验；旧附件仍可解密的回归测试。_R5.3 D8_
  - [ ] 6.4 前端：私信会话启用附件按钮与媒体预览；更新 `MessageComposer.test.tsx` 中「仅频道」的断言为新行为。_R5.1_

- [ ] 7. 验收门
  - [ ] 7.1 按 `requirements.md` 验收清单 1–7 在两台 Windows 电脑执行并记录到 `docs/release-checklist.md`。
  - [ ] 7.2 版本号三处同步为 1.4.0（`scripts/check-version.mjs` 通过）；更新 `frontend/src/changelog/releaseNotes.ts` 与 `docs/release-notes/v1.4.0.md`（含发行版 SVG 卡片 `assets/release-card-v1.4.0.svg`）；用户确认后打 `v1.4.0` tag。

## 待评估
