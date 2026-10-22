# 03 媒体预览与私信附件 — 设计

## 总览
```text
发送端 C++                                   接收端 C++                                     React
QFileDialog → MediaProbe(类型/宽高/时长/缩略图)  manifest(MLS) → knownManifests_                 MessageAttachment.media{kind,w,h,dur,thumbUrl}
  → 上传(不变) → manifest + media 字段 ─────────→ 缩略图 → MediaRegistry(内存) → token ──────→ <img src="lanchat-media://t/<token>">
  → 原文件复制进 MediaCache（发送者即时预览）       attachment.preview → 下载到 MediaCache → token → <img/video src="lanchat-media://m/<token>">
                                               MediaSchemeHandler: token → QFile(Range)        灯箱：animateFlip + 缩放/平移/切换
```

## D1 不变量修订（决策门 1.1 的推荐方案）
原文：「UI 不接触路径、明文、密钥」。修订为：
> UI 不接触**文件路径与密钥**；UI 可以通过会话内有效、可作废的不透明令牌，让渲染进程显示已解密的媒体。明文只存在于当前用户的应用缓存目录，文件名随机。

备选（供决策门陈述）：
- (a) 推荐：上述缓存 + 自定义 scheme。
- (b) 只在内存中解密并通过 scheme 流式返回，不落盘：100 MB 视频需常驻内存或每次重新下载解密，拖动进度需重新解密前缀，复杂度高；适合只做图片预览时采用。
- (c) 不做内联预览，只「用系统查看器打开」：最安全，但不满足需求。

缓存在磁盘的风险：同一 Windows 账户下其他进程可读。缓解：随机文件名、LRU 上限、撤回即删、「清除缓存」按钮、可选「退出时清除」（决策门确定默认值）。

## D2 manifest 扩展（向后兼容）
在 `AttachmentManifest` 中新增可选字段（`v` 仍为 1）：
```json
"media": { "kind": "image|video", "mime": "image/jpeg", "w": 3024, "h": 4032,
           "dur_ms": 0, "thumb": "<base64 JPEG>", "thumb_mime": "image/jpeg" }
```
- `encode()` 仅在有媒体信息时写入；`decode()` 对 `media` 做独立的严格校验（kind 枚举、mime 在白名单、w/h 1–20000、dur_ms 0–86 400 000、thumb ≤ 16 384 字节解码后），校验失败时**丢弃 media 但保留附件**。
- 大小预算：先 `encode()` 再估算 MLS 密文 + base64 + 外层 JSON；若整条消息 > 60 KiB（留 4 KiB 余量），依次以 JPEG 质量 60/45/30 重做缩略图，仍超出则去掉 `thumb`。
- 测试：v1 无 media 的旧 manifest 仍可解码；带 media 的新 manifest 被旧解码逻辑接受（直接用当前 `decode` 的拷贝作为「旧版」测试夹具）。

## D3 MediaProbe（发送端，`client-cpp/gui/src/attachments/media_probe.{hpp,cpp}`）
- 输入文件路径，输出 `std::optional<MediaInfo>`；文件 > 100 MB 或 MIME 不在白名单返回空。
- 图片：`QImageReader` + `setAutoTransform(true)` 读尺寸；`setScaledSize` 解码缩略图，避免全图解码；GIF 取首帧。
- 视频：⛔ 决策门 1.2。推荐引入 **Qt Multimedia**（Windows 上 Qt 6 默认使用 FFmpeg 后端，可读取 H.264 元数据与首帧）：`QMediaPlayer` + `QVideoSink` 取首帧、`duration()` 取时长，3 秒超时。若不引入，则视频只有类型与大小，无缩略图与时长。
- 在 worker 线程执行，不阻塞 GUI 线程；结果随 `startAttachmentUpload` 传入。

## D4 MediaCache 与 MediaRegistry（C++，`client-cpp/gui/src/media/`）
- `MediaCache`：根目录 `QStandardPaths::writableLocation(CacheLocation) + "/media"`；文件名 `QUuid::createUuid().toString(WithoutBraces)`；索引 `attachmentId → {fileName, size, lastAccess}` 存于同目录 `index.json`；LRU 上限 2 GB；`remove(attachmentId)`；`clear()`。
- 下载到缓存：复用规格 01 加固后的下载流程，目标路径由 `MediaCache::reserve()` 给出（不弹保存框）。
- 发送端：上传成功后把原文件**复制**进缓存（不移动用户文件）。
- `MediaRegistry`（仅内存）：`token(128 位随机 hex) → {kind: thumb|media, attachmentId, mime}`；缩略图字节保存在内存（≤ 16 KB/条，上限 2000 条 LRU）。撤回、登出、断开时作废相关令牌。

## D5 MediaSchemeHandler（`client-cpp/gui/src/media/media_scheme_handler.{hpp,cpp}`）
- `main.cpp` 在创建 `QApplication` **之前** `QWebEngineUrlScheme::registerScheme`：名称 `lanchat-media`，`SyntaxHostAndPort` 不需要，使用 `QWebEngineUrlScheme::Syntax::Path`，标志 `SecureScheme | CorsEnabled`（不设 `LocalAccessAllowed`）。
- `WebUiHost` 在 profile 上 `installUrlSchemeHandler("lanchat-media", handler)`。
- 请求校验：`job->initiator()` 必须是 `qrc:` 或开发模式下的回环地址；路径形如 `/t/<token>` 或 `/m/<token>`；令牌未登记返回 `RequestDenied`。
- 响应：缩略图用 `QBuffer`；媒体用 `QFile`。**Range 支持需实测**（任务 2.1 探针）：若 WebEngine 不对自定义 scheme 处理 Range，则在 handler 中读取请求头 `Range`（`job->requestHeaders()`，Qt ≥ 6.6），返回 206 + `Content-Range`（`setAdditionalResponseHeaders`）；若接口不可用则退化为整文件返回（视频可播放但拖动受限，记录在 PR）。
- 内容安全：`index.html` 增加 CSP `img-src 'self' qrc: lanchat-media: data:; media-src lanchat-media:;`，并确认现有页面在 CSP 下工作。

## D6 bridge 契约
`MessageAttachment` 增加：
```ts
media?: { kind: 'image' | 'video'; width: number; height: number; durationMs?: number; thumbUrl?: string };
previewUrl?: string;          // 已在缓存中时由 C++ 填入 lanchat-media://m/<token>
status?: 'available' | 'downloading' | 'cached' | 'saved' | 'verified-failed' | 'expired' | 'failed';
```
新增命令（全部在 `bridge_protocol.cpp` 白名单校验）：`attachment.preview {attachmentId}`、`attachment.save {attachmentId}`、`attachment.openExternal {attachmentId}`、`settings.setMediaAutoLoad {mode: 'limit'|'always'|'never'}`、`settings.clearMediaCache {}`。
URL 只出现在消息行字段中（每条 < 80 字节），不会显著增大状态 JSON。

## D7 前端组件
- `MediaAttachment.tsx`：按宽高比的容器（`aspect-ratio`，最大 360×360）；缩略图 `<img>` 带轻微模糊，清晰图加载完成后交叉淡化；`IntersectionObserver` 进入视口且满足自动加载策略时发 `attachment.preview`；视频叠加播放按钮与时长；加载中 SVG 环形进度（`stroke-dashoffset` 不允许动画——改用 `transform: rotate` 的分段环或 `scaleX` 进度条，遵守只动 transform/opacity 的规则）。
- `MediaViewer.tsx`：全屏层；打开时用 `animateFlip(缩略图 rect, 查看器图片)`；缩放用 CSS `transform: translate() scale()`；键盘与滚轮处理；`usePresence` 退出动画；视频 `canPlayType`/`error` 降级为海报 + 「用系统播放器打开」。
- 媒体列表：从 `state.activeMessages` 过滤出有 `media` 的消息，供 `←`/`→` 切换。

## D8 私信附件（服务端与客户端）
- Go：`AttachmentInitRequest` 增加 `ScopeKind`（`room|dm`）与 `PeerCode`；`attachments`/`room_quotas` 的 `room` 列存作用域键（`room:<name>` / `dm:<a>:<b>`，a < b）。迁移：已有行加 `room:` 前缀（SQLite 迁移语句 + 测试）。`handleAttachmentInit`/`handleAttachmentDownload`（`hub.go:1656-1686`、`1796-1817`）按作用域做成员检查：room 走现有逻辑，dm 要求 sender ∈ {a, b}。dm 配额 2 GiB。
- C++：`chooseAndUploadAttachment` 接受 `room` 或 `peerUserCode`；目标用户为对端一人；`attachment_mls_group_id(scopeKey, commandId)`；AAD 中 `room=` 字段改为写入作用域键（新上传生效，旧附件仍按旧 AAD 解密——旧 manifest 的 `room` 字段即旧作用域值，保持兼容）。manifest 以 `private_chat` + `crypto` 发送。
- 离线：验证离线私信存储是否保留 `crypto` 字段（`server-go` 的 offline_message 路径）；不保留则补齐并加测试。

## 测试矩阵
| 层 | 用例 |
|---|---|
| C++ | manifest media 编解码与严格校验、旧解码兼容、大小预算降级；MediaProbe（PNG/JPEG+EXIF/GIF/超限/伪造扩展名）；MediaCache LRU 与撤回删除；MediaRegistry 令牌作废；scheme handler 拒绝未登记令牌、拒绝非 qrc 发起者、Range 响应 |
| Go | 作用域迁移、dm 成员检查（第三人被拒）、dm 配额、离线私信保留 crypto、init 白名单仍拒绝 `file_path` |
| 前端 | MediaAttachment 宽高比占位、自动加载策略三种模式、视频降级、灯箱键盘/关闭、私信启用附件按钮 |
| 人工 | `requirements.md` 验收清单 1–7 |
