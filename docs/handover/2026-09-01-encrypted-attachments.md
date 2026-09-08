# 加密附件传输交接

更新时间：2026-09-07

## 当前状态

- 阶段 1 已提交：`70c7d00 feat(attachments): add quota-backed upload metadata`，当前分支 `HEAD` 就是该提交。
- 阶段 2 仍未提交；工作树已包含服务端密文块、提交/下载、过期清理、C++ AEAD 分块和 React 下载命令接入。
- C++ 已增加 `crypto` JSON 信封和 MLS 附件 manifest：发送端在 commit 成功后用 MLS 保护 manifest，接收端在本地解密并只向 UI 暴露展示元数据，不暴露密钥。
- 端到端附件传输代码已接入：接收端保存路径选择、下载分块串联、AES-GCM 解密落盘和 UI 消费 manifest 事件均已完成；MLS++ 已在本机锁定构建并通过自动化测试。首次发送附件时，客户端为本次上传创建独立的短生命周期 MLS 群组并邀请在线成员，避免重启后丢失本地会话时重建固定房间组导致 proposal conflict。
- 当前限制：附件 MLS 群组尚未持久化；客户端进程重启后不复用旧附件组。长期方案仍需实现受保护的 MLS session 持久化和恢复，不能只保存 epoch 数字。
- 连接审批后的重连竞态已修复：旧的延迟重连回调带有连接代次校验，批准并成功登录后不会再次把已连接客户端置为“正在重新连接”。
- 上传初始化现在携带本次上传对应的唯一 MLS 群组 ID，不再按房间取第一个旧群组；连续上传时不会把 manifest 加密到错误的会话中。
- 正式包关闭“性能 / 图形信息”设置面板；性能测试目标仍只参与开发/测试构建，不随正式前端包发布。

## 阶段 1：已提交的边界（已在代码中确认）

`70c7d00` 只覆盖元数据和配额预占，不涉及真实文件加密或最终提交。当前代码已确认：

- SQLite 初始化建表：`attachments`、`attachment_uploads`、`attachment_chunks`、`room_quotas`。
- 单附件逻辑大小限制：`500 * 1024 * 1024`（500 MiB）。
- 每聊天室总配额上限：`20 * 1024 * 1024 * 1024`（20 GiB）。
- `attachment.init` 由当前频道成员发起；服务端生成 `attachment_id` 和 `upload_id`，并返回 `chunk_size` 与 24 小时到期时间。
- 入站 `attachment.init` 使用严格字段白名单，拒绝客户端伪造 `file_path`、展示名和服务端 ID。
- `InitializeAttachment()` 在事务中预占 `reserved_bytes`，并在超额时返回 `ErrRoomQuotaExceeded`。
- 相关测试覆盖：`TestAttachmentInitAcceptsMaximumSizeAndRejectsLargerFile`、`TestAttachmentInitConcurrentReservationsNeverExceedRoomQuota`、`TestHubAttachmentInitRequiresCurrentRoomMembership`。

## 阶段 2：当前工作树中已实现的服务端内容（未提交）

### 1. 分块上限与协议约束

当前未提交实现明确使用：

- `attachmentChunkSize = 47 * 1024`（47 KiB）。
- `maxAttachmentCipherChunkBytes = 47 KiB + 64 B`。
- 注释写明：此值是为了在 64 KiB JSON/TLS 帧上限下预留 Base64 扩展和外层 JSON 包装字段。

代码位置：`server-go/attachment_store.go`，常量定义与注释。

### 2. 分块校验与幂等

`StoreAttachmentChunk()` 现已实现：

- 校验 Base64 可解码；
- 计算并核对 SHA-256 小写十六进制哈希；
- 校验 `upload_id` 对应 `author_code` 是否属于同一上传所有者；
- 校验调用者是否仍在当前频道成员列表中；
- 检查 `chunk_index` 是否处于合法范围；
- 限制最大密文长度：`<= 47 KiB + 64 B`；
- 对同一 `upload_id` + `chunk_index` 重复提交返回 idempotent `duplicate=true`；
- 同一索引但不同哈希/长度返回 `ErrAttachmentChunkConflict`；
- 同一索引重试时会重新读取并校验磁盘块的大小和 SHA-256，截断或篡改的已接受块不会返回 `duplicate=true`；
- 允许乱序上传，不要求按索引顺序提交。

对应测试：

- `TestAttachmentChunksAreIdempotentAndResumeInIndexOrder`
- `TestAttachmentChunkRejectsInvalidBoundsAndHash`

### 3. 持久化与断点续传

当前代码持久化了以下内容：

- `attachment_chunks` 表 `PRIMARY KEY(upload_id, chunk_index)`；
- 每个分块以 `cipher_size` 和 `cipher_sha256` 存储到 SQLite；
- 物理文件路径：`{attachmentRoot}/.partial/<upload_id>/<index>.bin`；
- 仅使用服务端生成的 UUID 与索引拼出路径，不接受客户端指定文件名或本地路径；
- `ResumeAttachmentUpload()` 返回升序索引数组，供客户端重连后继续上传。

### 4. 真实状态说明

这里的 "chunk" 仍然是“服务端接受的 opaque ciphertext + hash”；它并不等于完成的端到端加密传输：

- C++ 每附件生成随机 32 字节密钥，每块使用基于上下文派生的独立 nonce，并以 AES-256-GCM 认证；
- manifest 已由 MLS 保护并透传 `crypto` JSON 信封；
- 服务端仍只持有密文块与哈希，不持有明文密钥；
- 下载端按顺序请求密文块，逐块校验 SHA-256、AES-GCM 标签和最终块标记后写入用户选择的目标文件。

也就是说，当前工作树已经覆盖了“协议与持久化层”的结构化基础，但还没有跨出到完整的密文传输和最终提交流程。

## 阶段 2：当前工作树中已实现的 C++ 字段（未提交）

`client-cpp/include/message.hpp` 与 `client-cpp/src/message.cpp` 已补齐以下协议字段：

- `attachment_id`
- `upload_id`
- `logical_size`
- `chunk_size`
- `chunk_index`
- `ciphertext`
- `cipher_sha256`
- `expires_at`
- `received_indexes`

关键行为：

- `attachment.chunk` 仍会在 `chunk_index == 0` 时显式序列化 `chunk_index`，避免首块被服务端误判为缺失字段。
- `message.cpp` 中同样增加了附件消息的反序列化、字段检查和 round-trip 测试入口。
- `client-cpp/tests/protocol_tests.cpp` 中的附件消息测试已补齐，但本轮没有重新跑 VS 编译门禁；它是当前未提交代码的一部分，而不是已确认提交的测试结果。

## 当前未提交文件

- `server-go/attachment_store.go`
- `server-go/attachment_store_test.go`
- `server-go/auth_store.go`
- `server-go/client.go`
- `server-go/hub.go`
- `server-go/message.go`
- `client-cpp/include/message.hpp`
- `client-cpp/src/message.cpp`
- `client-cpp/tests/protocol_tests.cpp`

变更统计：以上 9 个受版本控制文件共约 `528` 行新增、`17` 行删除；本文档本身也是未跟踪文件。阶段 2 尚未形成可安全提交的端到端功能，接手时必须保留这些工作区改动，不能先执行清理、重置或选择性还原。

## 已知验证边界

- Go 门禁覆盖了服务端存储、鉴权、协议处理及分块/续传相关测试；结果见下方记录。
- 已构建的 C++ 目标是 `connection-endpoint-tests`，这只能确认协议改动没有破坏该目标的 x64 Release 编译。
- 新增的附件消息往返用例位于 `client-cpp/tests/protocol_tests.cpp`，`protocol_tests` 目标尚未在本轮实际运行；在把阶段 2 标为可交付前必须运行它。
- 尚无客户端 AEAD 分块实现、MLS 密钥/manifest 分发、GUI 命令或跨端上传恢复测试。因此“服务端可接收密文块”不等同于“附件功能可供用户使用”。

## 推荐恢复顺序

1. 从当前工作树开始，先运行 `git status --short` 与 `git diff --check`，确认只包含上述 9 个生产/测试文件及本交接文档。
2. 单独运行 `protocol_tests`，并重新运行 `go test ./... -race -count=1`；若需要重建 C++，使用下方相同的 VS x64 环境和构建目录。
3. [已完成] 客户端 AEAD 分块、MLS manifest 与密文信封分发。
4. [已完成] 接收端本地保存 manifest/key，UI 仅消费展示元数据并由用户选择目标路径。
5. [已完成] 按 chunk_index 下载，校验哈希和 AES-GCM 后落盘；剩余是双客户端跨端集成测试和取消/失败清理。

## 当前上传失败的诊断

如果界面显示“附件发送失败”，新版本会显示 C++ bridge 返回的具体原因。旧版本会丢弃该原因，只显示通用文案；若房间有在线成员，新版本会在首次发送附件时自动完成 MLS 群组初始化，不能把初始化失败误判为服务端文件存储失败。

2026-09-08 的双端日志确认了一类此前容易误判为网络故障的重连问题：Bob 收到附件 MLS welcome 后发送 `mls.group.welcome.accept`，旧实现把 `attachment_mls_group_id`（约 85 字节）直接拼入 `command_id`。Go 服务端的 `maxCommandIDSize` 为 64 字节，因此记录 `command id is too large` 后主动关闭连接，Bob 随后看到 TLS peer closed/SSL unexpected EOF。现已改为基于 group ID 与 epoch 的稳定 SHA-256 截断 ID（`wa-` + 56 个十六进制字符，长度 59），重试仍使用同一 ID 且满足服务端上限。

为诊断上传进度，网络日志现在还记录 `attachment_init_progress`、`attachment_chunk_send`、`attachment_chunk_ack` 和 `attachment_resume_progress`，其中包含 `upload_id`、`chunk_index`、`received_chunks`、`total_chunks`，初始化阶段还包含 `logical_size` 与 `chunk_size`。这些日志用于区分“分块实际没有确认”和“前端没有总块数、无法显示百分比”。

## 交接风险

- `48 KiB` 是帧预算而不是安全的明文负载；恢复实现时不得把已确认的 `47 KiB` 分块上限改回 48 KiB。
- 服务端以 `upload_id`、上传者和频道成员资格授权；客户端重连/续传时必须保持相同的服务器返回上传 ID，不得自行构造。
- 部分块目录位于数据库目录下；过期上传的清理、完整性封存和下载流程仍未实现，不能假设 `.partial` 中的文件已可展示或下载。

## 验证记录

已通过 Go race 门禁：

```powershell
Set-Location -Path 'C:\Users\Q1573\Desktop\MY_project\lan-chat\server-go'
go test ./... -race -count=1
```

结果：

```text
ok   	cross-language-lan-chat/server-go	15.397s
```

这是当前未提交 Go 代码的有效证据，覆盖服务端分块上传、校验与恢复层。

已通过 C++ x64 Release 编译：

```powershell
cmd.exe /d /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build "C:\Users\Q1573\Desktop\MY_project\lan-chat\out\modern-msvc-x64" --target connection-endpoint-tests --config Release'
```

该编译不替代 `protocol_tests` 与 Qt 全门禁；二者仍是下一次续接的首要验证项。

## 工作树卫生

本文档更新没有执行 `git add`、`git commit`、`git stash` 或重置操作。保留以上 9 个未提交源文件和此未跟踪交接文档，避免丢失阶段 2 的进行中成果。
