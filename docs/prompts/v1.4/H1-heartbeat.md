# H1 执行版：应用层心跳

S0 附件基线及测试通过后开始。现有 `heartbeat-v1` 已覆盖 LAN、在线服务端与桌面 Rust 客户端。本阶段验证协商、超时与重连，生产使用 20 秒 ping、60 秒读超时，未协商仍走原空闲策略。

## 实施要求

1. **复用连接测试入口。**在 `server-rust/tests/heartbeat.rs` 复用现有 TLS、临时目录与登录夹具，服务端监听 `127.0.0.1:0`。LAN 房主使用既有本机凭据路径，访客按真实审批流程登录。在线使用临时账号和测试数据库。
2. **缩短测试耗时。**优先使用可控时钟或注入测试超时。若需真实二进制的缩短参数，仅在显式 `test-hooks` feature 构建中暴露。生产构建和打包保持默认时序。
3. **连接行为。**仅在双方声明心跳能力后回 pong。心跳绕过业务令牌桶并使用现有弱发送器。弱发送器失效时结束本次 ping 处理，由既有会话接管流程关闭旧连接。复测未协商客户端仍可发送普通消息。
4. **覆盖两种模式。**验证正常 ping/pong 且不消耗业务配额、协商后静默超时移除、未协商仍可继续收发、旧连接失效后同码重连不会被旧会话或迟到响应干扰。LAN 重连复走审批，在线同账号按既有接管策略验收。根据实际行为验证终端收到 pong 时的连接状态。
5. **用户验收。**桌面分别在在线和 LAN 做一次网络切换或待机唤醒。在线恢复后可继续发送，LAN 需要审批时展示等待状态，断线期间已有会话和发送反馈可理解。Android 若未声明该能力，按旧协议连接并回归切网与前后台行为。

## 验收

```powershell
cargo test --locked -p lan-chat-server --test lan_desktop
cargo test --locked -p lan-chat-server -p lan-chat-client-core -p lan-chat-protocol
cargo clippy --locked -p lan-chat-server -p lan-chat-client-core --all-targets -- -D warnings
```

若采用测试 feature，再执行 `cargo test --locked -p lan-chat-server --features test-hooks --test heartbeat`，在 CI 同样运行。记录双方能力、测试时序和 LAN/在线重连结果，更新现有协议、架构和测试文档。
