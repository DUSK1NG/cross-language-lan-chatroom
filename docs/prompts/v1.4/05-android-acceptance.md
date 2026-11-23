# S5 执行版：Android、跨端验收与正式发布

以 [执行索引](README.md) 确认纳入本版的阶段均已验收合并。用与正式版同源的候选构建验收。群能力在 S4 完成，本阶段聚焦回归、现网前演练和正式分发。

## 候选验收矩阵

| 场景 | 通过条件 |
| --- | --- |
| Windows LAN 双机 | 安装及覆盖升级后证书、历史与房间规则仍可用。两机发现、连接审批、休眠或切网后重连均有明确状态。上传下载可完成 |
| Windows Online | 与旧服务端互通。与新服务端验证私信、离线历史、群消息、图和文件。LAN/Online 各走一次签名更新、独立安装和卸载 |
| Android 旧 APK 覆盖升级 | 旧已分发 APK 原位升级到 `0.4.0`，核对 application ID、签名和递增的 versionCode，账号和所需的本地状态仍可用 |
| Android 真机 | 前台验证登录、私信、群消息、群图片与文件收发。切到后台或锁屏后验证私信与群推送，点击进入目标会话。覆盖通知权限开关、网络切换和进程被收回后重开。FCM 以测试项目或经授权服务实际到达为依据 |
| 在线测试服务器 | 用旧库副本演练停写、备份、迁移、中断恢复和整套回滚。迁移前后旧图与旧文件可取，群权限及旧客户端可达 |
| 发布产物 | 实际候选的桌面 ZIP、两版安装器与 APK 对应同一已验收提交。校验清单、签名、版别和安装器目标正确，候选资源与稳定更新渠道隔离 |

每项记录设备、产物版本与提交、步骤、结果和失败证据。仅复现后才修复 FCM 或收发缺陷。Android 继续从 Release 下载后由系统安装。

## 自动化与发布顺序

```powershell
cargo test --locked -p lan-chat-server -p lan-chat-client-core -p lan-chat-protocol
node scripts/check-version.mjs
pnpm --dir frontend run lint
pnpm --dir frontend exec vitest run
pnpm --dir frontend run build
```

1. CI 检查 workspace、两版打包与 Android JNI。基于同一候选提交补齐上述人工验收和现有 `docs/release-acceptance.md`。关闭失败和未验证的必需项，再冻结版本、发布说明与延期项。
2. 经授权执行目标环境备份并确认部署和回滚窗口。从已验收提交创建 `v1.4.0` 标签，工作流先生成 draft。核对签名、产物、更新清单与候选验收结果。若重新构建，实际正式产物至少复核覆盖安装和更新路径。
3. 经正式发布授权再公开 Release 和稳定更新清单。检查 LAN/Online 下载指向正确且公钥验证通过，Android APK 可下载并可覆盖安装。保留上版产物及已演练的数据恢复方法。Android 已覆盖升级的设备通过同签名、更高 versionCode 的修复包恢复。记录部署、发布和回滚状态。
