# ADR-0001：Rust 作为唯一实现语言，退役 Go 与 C++

- 状态：已接受并已执行（2026-11-22 完成迁移；Go 与 C++ 代码已从仓库删除）
- 日期：2026-11-22
- 决策：项目所有者
- 影响范围：桌面客户端、局域网服务端、在线服务端、协议层、Android 传输层、构建与发布脚本
- 相关文档：[Rust 迁移完成报告](../rust-migration.md)、[架构说明](../architecture.md)、[交接说明](../CODEX_HANDOFF.md)

## 背景

决策时（2026-11-22 之前）仓库同时使用三套语言，同一协议存在多份互不引用的实现：

| 层 | 迁移前实现 | 规模 |
| --- | --- | --- |
| 桌面客户端 | Go + Wails + WebView2；TLS 交给 Rust 子进程 `lan-chat-transport.exe`；附件 MLS 交给 C++ 子进程 `lan-chat-mls.exe` | Go 约 2400 行 + C++ 约 200 行 |
| 局域网服务端 | Go（`server-go/`） | 约 7500 行 |
| 在线服务端 | Go（`server-go/online*.go`）与 Rust（`server-rust/`）两套功能对等实现 | Go 约 1400 行，Rust 约 8000 行 |
| 协议 | Go `server-go/wire`、Rust `protocol-rust`、Android 两份 `TransportCodec`、C++ MLS 封装 | 4–6 份 |
| 构建链 | Go + Cargo + MSVC + CMake + OpenSSL 3 + `vendor/mls` 锁定的 MLS++ 源码 | — |

由此产生的已知代价：

1. **协议漂移**：帧上限、capability 字符串（`online-files-v1`、`room-password-v1` 等）、消息类型在 Go / Rust / Java 中各写一遍，只能靠人工同步。
2. **双份服务端**：`server-go` 与 `server-rust` 都实现了在线版的账号、私信、群组、文件、FCM 推送；同一缺陷需要修两遍，新功能只进其中一边。
3. **四进程客户端**：`LANChat.exe` → `lan-chat-transport.exe` → `lan-chat-mls.exe`，房主模式再加 `chat-server.exe`。每帧消息需要两次管道 JSON 编解码，启动存在三次握手的竞态，错误跨进程传播困难。
4. **构建门槛**：局域网版构建依赖 Go、Cargo、MSVC、CMake、OpenSSL 3 和 MLS++ 源码；新环境搭建成本高，CI 难以覆盖，发布只能手工在 Windows 上完成。
5. **人员与注意力分裂**：同一功能学习两套代码，评审与测试无法复用。

## 决策

1. 项目的目标实现语言**只有 Rust**。
2. 桌面客户端由 Wails（Go）迁移到 **Tauri 2**：Rust 后端 + WebView2，前端继续使用现有 React/Vite 代码，不重写界面。
3. 服务端只保留 Rust。同一 crate 提供两种运行模式：局域网房主模式与在线服务器模式，不再保留第二套服务端。
4. `protocol-rust`（crate `lan-chat-protocol`）成为协议的**唯一权威实现**；Go / Java 侧实现一律收缩为参考或删除。
5. 用 Rust MLS 库（最终选型 `mls-rs` 0.58）替换 C++ MLS++，删除 CMake、OpenSSL、vcpkg、`vendor/mls` 与 `.tools/mlspp` 依赖。
6. Go 代码（`server-go/`、`desktop/`、`go.mod`）冻结并**已删除**（2026-11-22 完成迁移）。
7. Android 只保留 Rust 传输层（`android/transport-rust`），删除 `javaTransport` 变体。

## 理由

- Rust 已经是 TLS 传输层与在线服务端的实现语言，再加桌面后端即可端到端同栈，删掉两处跨语言桥接。
- 单一协议 crate 可以生成 TS/Java 绑定并做跨实现 golden 帧测试，漂移问题从流程约束变成编译期约束。
- 去掉 C++/CMake/OpenSSL/MSVC 后，构建链只剩 Cargo 与 Node/pnpm，CI 与贡献门槛显著下降。
- 四进程收敛为单进程，删除 stdio 管道协议、`transport_ready` 握手状态机与 `errTransportLost` 一类跨进程错误分类。
- Rust 内存安全与错误类型系统更适合处理不可信网络输入；现有 `rustls`、`rusqlite`、`tokio`、FCM 推送代码可直接复用。

## 后果

### 正面

- 协议与消息类型一个来源，跨语言一致性由测试保证。
- 桌面端单进程、无 IPC 编解码，消息路径更短；发布包不再包含三个辅助可执行文件。
- 构建环境只需 Cargo + Node/pnpm，可在 GitHub Actions 的 Windows Linux 运行器上完成。
- 在线服务端保留现有 Rust 实现即可继续发布，迁移期间产品不断供。

### 代价与风险

- 桌面业务逻辑（会话、状态机、命令分发约 2400 行 Go）需要重写为 Rust，迁移期存在双轨维护。
- Wails 的 `window.go.main.Desktop.*` 与 `window.runtime` 事件 API 必须替换为 Tauri `invoke` / `listen`；桥接面目前集中在 `frontend/src/bridge/chatBridge.ts` 与 `frontend/src/app/runtimeBridge.ts`。
- MLS 实现更换是最高风险项：现有 `keyPackage`、`commit`、`welcome` 及附件密文格式必须保持可解密，只能换实现、不能换格式。
- 历史数据必须可直接打开：`%LocalAppData%\DUSK1NG\LAN Chat\host\` 下的 `certs/`、`chat.db` 与 12 张表结构不得破坏。
- 迁移期开发效率短期下降。

### 不变量（迁移不得放宽）

- 私钥、`chat.db`、聊天记录与访问令牌不得进入 Git、同步盘或 Release。
- TLS 不得降级：最低 TLS 1.2、禁止明文、禁止跳过证书或主机名校验。
- React 不直接访问 socket、TLS、私钥或 SQLite；边界保持现有 ChatBridge 形态。
- 发布包不得要求用户安装 Node、Go、CMake 或 MSVC 运行时。
- 帧格式语义不变：4 字节大端长度 + UTF-8 JSON，`1 <= length <= 65536`；capability 字符串不变。
- 消息 ID 规则与回执兼容逻辑（`isLegacyGeneratedMessageID` 对纯数字 ID 的判定）必须逐字复刻。
- 两道准入检查不得合并：成员核对公开证书指纹，房主单独批准连接。

## 执行结果（2026-11-22）

- 桌面客户端：Tauri 2（apps/desktop/src-tauri）+ crates/client-core，Go / Wails 版已删除。
- 服务端：单一 lan-chat-server，--mode lan/online；Go 双份实现已删除。
- 协议：protocol-rust 为唯一权威；Go wire 与 C++ MLS 封装已删除。
- MLS：mls-rs 0.58；C++ MLS++、CMake、OpenSSL 已退出构建链。
- Android：transport-rust 转正；javaTransport 已不在源码中。
- 构建：scripts/*.ps1 全部改为 cargo；新增 tools/bootstrap 源码启动器。

遗留工作见 [Rust 迁移完成报告](../rust-migration.md) 的「验证状态」一节。

## 回滚策略

Go 实现已在迁移完成后删除（2026-11-22）。回滚依赖 Git 历史（origin 仓库与 backup bundle remote）与上一份已发布安装包；用户数据目录布局未变，因此回滚不需要迁移用户数据。
