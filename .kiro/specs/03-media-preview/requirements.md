# 03 图片 / 视频预览（≤ 100 MB）与私信附件 — 需求

目标版本：1.4.0。前置：规格 01 任务 3（下载链路加固）已合并；规格 02 任务 3.4（`animateFlip`）已合并。

## 背景与约束（来自现状）
- 附件为 AES-256-GCM 每文件随机密钥，47 KiB 分块，经同一 TLS 连接传输；manifest 经 MLS 保护后作为聊天消息发送（`gui_connection_worker.cpp:960-986`），字段无 MIME/尺寸/缩略图（`attachment_manifest.hpp:9-23`）。
- manifest 解码只校验必需字段、不拒绝未知字段（`attachment_manifest.cpp:39-67`），因此**新增可选字段对旧客户端向后兼容**（旧客户端显示为普通文件卡）。
- 聊天帧上限 64 KiB（`server-go/protocol.go:9`、`client-cpp/include/protocol.hpp:13`）。
- bridge 状态每 100 ms 整体序列化，最多 500 行（`chat_bridge.cpp:19-40`）——**不得把图片数据放进状态**。
- WebEngine 页面从 `qrc:/frontend/index.html` 加载，当前没有自定义 URL scheme（`web_ui_host.cpp:1-62`）。
- Qt 官方预编译 WebEngine **不含专有编解码器**（H.264/AAC/MP4）：<https://doc.qt.io/qt-6.7/qtwebengine-features.html>。手机拍摄的 MP4 多为 H.264。
- 现有不变量「UI 不接触路径、明文、密钥」（`MessageComposer.tsx:106-107`）。内联预览需要让解密后的像素进入渲染进程，**必须经用户批准修订该不变量**（任务 1.1 决策门）。

## R1 媒体识别与缩略图（发送端）
- R1.1 WHEN 用户选择 ≤ 100 MB 的文件 THE SYSTEM SHALL 依据文件内容（`QMimeDatabase::mimeTypeForFile(..., MatchContent)`）判定媒体类型，白名单：`image/png`、`image/jpeg`、`image/gif`、`image/webp`、`image/bmp`、`video/mp4`、`video/webm`、`video/quicktime`。
- R1.2 FOR 图片 THE SYSTEM SHALL 生成长边 ≤ 320px、≤ 16 KB 的 JPEG 缩略图（应用 EXIF 方向），并记录原始宽高。
- R1.3 FOR 视频 THE SYSTEM SHALL 尽力生成首帧缩略图与时长（依赖任务 1.2 决策）；失败时仍按视频发送，只缺缩略图。
- R1.4 THE media metadata（类型、MIME、宽高、时长、缩略图）SHALL 只存在于 MLS 保护的 manifest 中；服务端看不到。
- R1.5 THE encoded manifest SHALL 保证整条聊天消息 ≤ 64 KiB；超出时降低缩略图质量重试，仍超出则不带缩略图。

## R2 时间线内联预览（接收端）
- R2.1 WHEN 消息含图片或视频 manifest THE timeline SHALL 立即按原始宽高比显示缩略图占位（最大 360×360，CLS = 0），视频叠加播放按钮与时长。
- R2.2 WHEN 图片 ≤ 自动加载阈值（默认 10 MB）且滚动进入视口 THE SYSTEM SHALL 自动解密到媒体缓存并以清晰图替换缩略图（淡入）；更大的图片和所有视频 SHALL 点击后加载，加载中在缩略图上显示环形进度。
- R2.3 THE settings SHALL 提供「自动加载图片：≤ 10 MB / 始终 / 从不」。
- R2.4 THE 文件 > 100 MB 或非白名单类型 SHALL 保持现有文件卡，行为不变。
- R2.5 THE sender SHALL 立即看到自己发送的媒体预览（使用本地原文件生成的缓存，不重新下载）。

## R3 灯箱查看器
- R3.1 WHEN 点击已加载的图片 THE SYSTEM SHALL 以共享元素转场（FLIP，从缩略图放大）打开全屏查看器，关闭时反向回到原位。
- R3.2 THE image viewer SHALL 支持滚轮/双击缩放（1×–4×）、拖拽平移、`←`/`→` 切换当前会话中的媒体、`Esc` 关闭。
- R3.3 THE video viewer SHALL 使用 `<video controls>` 播放；WHEN WebEngine 不支持该编码（`canPlayType` 为空或播放出错）THE SYSTEM SHALL 显示首帧缩略图与「用系统播放器打开」按钮。
- R3.4 THE viewer SHALL 提供「保存到…」（从缓存复制，不重新下载）与「在文件夹中显示」（仅已保存时）。

## R4 媒体缓存与访问通道
- R4.1 THE decrypted media SHALL 只写入当前用户的应用缓存目录 `QStandardPaths::CacheLocation/media/`，文件名为不可预测的随机名，不含原文件名。
- R4.2 THE page SHALL 只通过 `lanchat-media://` 自定义 scheme 的不透明令牌（128 位随机，会话内有效）访问媒体；页面永不获知文件路径或密钥。
- R4.3 THE scheme handler SHALL 只响应来自 `qrc:` 主页面的请求，只返回已登记令牌对应的文件，设置正确的 `Content-Type`，并在可行时支持 `Range` 以便视频拖动。
- R4.4 THE cache SHALL 有总量上限（默认 2 GB，LRU 淘汰）；WHEN 消息被撤回 THE SYSTEM SHALL 删除对应缓存并作废令牌；设置中提供「清除媒体缓存」。
- R4.5 WHEN 应用退出 THE SYSTEM SHALL 默认清除媒体缓存；设置中提供「退出时清除媒体缓存」开关（已决定，见 tasks 1.1）。

## R5 私信附件（含媒体）
- R5.1 THE composer SHALL 在私信会话中启用附件按钮（移除「附件暂仅支持频道会话」限制）。
- R5.2 THE server SHALL 以作用域（`room:<name>` 或 `dm:<小代码>:<大代码>`）存储附件，私信附件只允许两位参与者上传与下载；私信作用域配额 2 GiB。
- R5.3 THE manifest SHALL 经私聊 MLS 保护发送；接收端校验 manifest 作用域与消息作用域一致。
- R5.4 WHEN 对方离线且为已注册账号 THE SYSTEM SHALL 在其上线后送达 manifest（复用离线私信机制，需验证 `crypto` 字段在离线存储中完整保留）。
- R5.5 THE inbound init request SHALL 继续使用严格字段白名单；新增字段仅 `scope_kind`、`peer_code`。

## R6 兼容与降级
- R6.1 THE old clients（1.3.x）SHALL 仍能把新 manifest 显示为普通文件卡并下载（向后兼容测试）。
- R6.2 WHEN 未启用 MLS++ THE SYSTEM SHALL 维持现有行为（附件不可用并提示原因），不得以明文路径绕过。

## 验收清单（两台 Windows 电脑）
1. A 在频道发送 3 MB JPEG（竖拍，含 EXIF 方向）：B 立即看到正确方向的缩略图，进入视口后自动变清晰；点击打开灯箱，缩放/平移/切换/关闭转场流畅。
2. A 发送 80 MB WebM：B 点击加载，环形进度，加载后可播放并拖动进度。
3. A 发送 60 MB H.264 MP4：B 看到首帧（若任务 1.2 选择了生成方案）与「用系统播放器打开」，或可直接播放（若启用了专有编解码器）。
4. A 发送 120 MB 视频：显示为普通文件卡。
5. A 与 B 私信互发图片；第三人 C 无法下载（服务端拒绝，记录日志）。
6. A 撤回图片：B 端缓存文件被删除，灯箱中无法再打开。
7. 1.3.0 客户端 D 收到新 manifest：显示为文件卡且可下载。
