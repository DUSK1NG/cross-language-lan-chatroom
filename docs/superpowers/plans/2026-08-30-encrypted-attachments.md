# 加密附件传输实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 以端到端密文方式支持单文件 500MB、每聊天室 20GiB、断点续传、取消与垃圾回收。

**Architecture:** 先在既有 64KiB JSON/TLS 通道实现 48KiB 密文块，保证兼容与最小部署；metadata 由 MLS 加密，服务端只验证大小、块哈希与成员授权。对象写入临时目录，`commit` 校验后原子可见并更新配额。

**Tech Stack:** Go、SQLite、C++17/Qt、React、MLS 密钥封装、AEAD。

## Global Constraints

- chunk base64 后的 JSON frame 必须小于 64KiB；初始 `chunk_size` 固定 48KiB。
- 单附件 logical bytes ≤ 524288000；聊天室预占/已用总量 ≤ 21474836480。
- 服务端保存密文块，文件系统路径只能由服务端 `attachmentId` 推导。
- 未完成上传 24 小时过期；完成对象仅在引用计数为 0 后 GC。

---

### Task 1: 附件元数据、迁移与配额预占

**Files:**
- Modify: `server-go/auth_store.go`, `server-go/message.go`, `server-go/message_test.go`, `server-go/auth_store_test.go`
- Create: `server-go/attachment_store.go`, `server-go/attachment_store_test.go`

- [ ] 写失败测试：500MB 接受、500MB+1 拒绝；20GiB 精确上限与并发预占不得超额。
- [ ] 创建 `attachments`, `attachment_uploads`, `attachment_chunks`, `room_quotas` 迁移和事务 API。
- [ ] `attachment.init` 生成 UUID，校验成员权限与配额，并返回 `upload_id`；不接受客户端文件路径。
- [ ] 运行 `go test ./... -race`；提交：`feat(attachments): add quota-backed upload metadata`。

### Task 2: 密文块上传与断点续传

**Files:**
- Modify: `server-go/message.go`, `server-go/hub.go`, `server-go/message_test.go`
- Modify: `client-cpp/gui/src/gui_connection_worker.*`, `client-cpp/gui/src/chat_bridge.*`
- Create: `client-cpp/gui/src/attachments/transfer_client.*`, `client-cpp/gui/tests/transfer_client_tests.cpp`

- [ ] 写失败测试：乱序/重复块幂等；越界 index、错误 hash、超过 48KiB 明文或超帧 payload 均拒绝。
- [ ] 实现 `attachment.chunk`、`attachment.resume` 与 ranges 响应；块写入 `{data_dir}/attachments/.partial/<upload_id>/<index>.bin`。
- [ ] 客户端每块以独立 nonce AEAD 加密，发送 ciphertext 和 SHA-256；密钥由 MLS 加密 metadata 分发，服务端不持有。
- [ ] 运行 Go/C++ 测试；提交：`feat(attachments): stream encrypted chunks with resume`。

### Task 3: 原子提交、下载与消息引用

**Files:**
- Modify: `server-go/attachment_store.go`, `server-go/message.go`, `server-go/auth_store.go`
- Modify: `client-cpp/gui/src/chat_model.*`, `frontend/src/*`

- [ ] 写失败测试：whole hash 不一致不提交；成功 commit 后引用消息可下载；中断下载从确认 index 恢复。
- [ ] `attachment.commit` 重算大小/hash，原子 rename 到 `attachments/<attachment_id>/`，扣除预占并增加已用量。
- [ ] MLS 消息携带密文附件 manifest；UI 只显示客户端解密后的展示名和进度。
- [ ] 运行 Go race、C++、前端测试；提交：`feat(attachments): commit and download encrypted files`。

### Task 4: 撤回、GC 与压力门禁

**Files:**
- Modify: `server-go/attachment_store.go`, `server-go/main.go`, `server-go/attachment_store_test.go`
- Create: `scripts/test-attachment-storage.ps1`

- [ ] 写失败测试：撤回最后引用后 GC 回收配额与磁盘；仍有引用的对象不删除；24 小时未完成上传清理。
- [ ] 实现启动恢复、周期 GC、孤儿临时块清理及配额重算。
- [ ] 自动化验证：内存不一次性读取 500MB，慢客户端/磁盘满/权限越权/篡改块均失败。
- [ ] 运行完整门禁；提交：`feat(attachments): reclaim expired encrypted uploads`。

## Self-Review

- 覆盖：500MB、20GiB、密文块、断点续传、权限、原子性、GC 与 UI。
- 依赖：MLS 计划 Task 2 先完成，附件不自行发明密钥交换。
