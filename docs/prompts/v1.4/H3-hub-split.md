# H3 执行版：拆分 LAN Hub

本阶段可延期。若纳入 v1.4，在 S2 合并并通过后实施。目标是在保留协议、错误文本、权限与锁时序的前提下，按职责移动现有实现。

## 执行顺序

1. **记录行为基线。**查看 `server-rust/src/lan/mod.rs` 及已分出的子模块。复用 `lan_desktop`、H1 心跳、H2 搜索和 S2 持久化用例，核对本机房主、访客审批通过与拒绝、房间与私信、历史分页、离线移除、重启后的权限和附件。只补实际缺少的关键用例。改前先跑并记录相关用例结果。
2. **按职责移动。**`mod.rs` 保留模块声明、核心类型、服务入口及共享校验。连接与心跳放入 `connection.rs`，会话注册、移除、投递和在线状态放入 `presence.rs`，审批放入 `approval.rs`，历史任务和结果分发放入 `history.rs`。如果基线在 S2 已把函数移入合理位置，沿用该边界。权限判断仍由原入口负责，按模块尽量使用最小可见性。
3. **复查行为。**对照函数移动前后，关注审批、房间代次、历史结果与在线连接的关联，以及错误文本、响应顺序和锁范围。行数仅用于说明职责分离。测试行为与可观察结果作为合并条件。
4. **交接。**更新现有架构文档中的模块职责，记录移动清单、可见性变化和前后用例结果。附件并发和文件发布行为由 H4 处理。

## 验收

```powershell
cargo test --locked -p lan-chat-server
cargo clippy --locked -p lan-chat-server --all-targets -- -D warnings
(Get-Content server-rust/src/lan/mod.rs | Measure-Object -Line).Lines
git diff --stat
```

用同一套基线用例验证常规聊天、审批、重连和文件互传。测试和锁语义保持一致后才交给 H4。
