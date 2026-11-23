# v1.4 最终执行版

本目录是 v1.4 的执行入口。阶段依赖、实施要求和验收以这里为准。[合并计划](../../plans/2026-11-23-v1.4-plus-hardening-plan.md)提供范围和排期背景，[服务端加固计划 v2](../../plans/2026-11-23-server-hardening-plan.md)提供技术背景。交接时把本索引与当期阶段文件一起交给执行者。

## 阶段与依赖

| 阶段 | 执行文档 | 分支 | 开工条件 |
| --- | --- | --- | --- |
| S0 | [基线与 CI](00-baseline.md) | stage/0-baseline | G0 前可盘点和复跑基线。建立提交、分支和标签须先获授权 |
| H1 | [应用层心跳](H1-heartbeat.md) | stage/h1-heartbeat | S0 通过 |
| H2 | [历史搜索](H2-fts-search.md) | stage/h2-fts-search | H1 通过 |
| S1 | [发布与桌面更新](01-release.md) | stage/1-release | S0 通过 |
| S2 | [局域网权限持久化](02-lan-persistence.md) | stage/2-lan-persistence | H2 通过 |
| S3 | [在线附件存储](03-online-storage.md) | stage/3-online-storage | H2 通过 |
| S4 | [在线群聊](04-online-groups.md) | stage/4-online-groups | S3 的存储与兼容性验收通过 |
| H3 | [拆分 LAN Hub](H3-hub-split.md) | stage/h3-hub-split | 可延期。若纳入本版，在 S2 合并后执行 |
| H4 | [LAN 附件 IO 出锁](H4-attachment-io.md) | stage/h4-attachment-io | 可随 H3 延期。纳入本版须先通过 H3 并关闭 H4/H5 决策 |
| S5 | [跨端验收与发版](05-android-acceptance.md) | stage/5-android-acceptance | S0、H1、H2、S1–S4 已合并。纳入本版的 H3/H4 也须通过 |

S1 可以与 H1–H2 独立推进。有多个执行者时，S2、S3 可在 H2 后分别实施，合并前复核共享的数据库初始化代码和两库迁移测试。单人优先走 S0 → H1 → H2 → S1 → S2 → S3 → S4 → S5。每阶段从已验收的集成分支起步，合并前基于最新集成结果重跑相关门禁。

按原阶段估算，完整路径约 43.5 人日，延后 H3/H4 后约 37.5 人日。八周作为排期参考。六周交付须提前落实独立并行和验收资源。延期顺序为 H3/H4 → 在线提额 → 群头像等可独立发布的设置。将实际选择同步到 S5 验收矩阵和发布说明。

## 共用规则

- 先读 `WORKLOG.md`、`docs/CODEX_HANDOFF.md` 和本阶段涉及的实现。现有工作区改动按路径审阅，保护原有文件，基线按 G0 执行。
- 保持 4 字节大端长度、UTF-8 JSON 和 65,536 字节帧上限。新增消息或行为按能力协商。每个影响终端的阶段验证旧客户端与新服务端，以及新客户端与旧服务端。
- 使用已有迁移机制，按数据库分别记录 `PRAGMA user_version`。H2 到 1，S2 的 LAN 库到 2，S3 的在线库到 2，S4 的在线库到 3。修复派生索引时保留已完成的迁移版本。涉及文件的备份和恢复以数据库与文件目录为同一份快照。
- 本机先跑阶段命令，干净 CI 跑相关全量门禁。工具未在 PATH 时优先复用 `.tools/pnpm-11.19.0` 和 `.tools/android` 下的现有工具链，可在当前 PowerShell 进程配置路径。若遇 MSVC D8050，设 `$env:CARGO_TARGET_DIR = 'C:/lc-target'` 和 `$env:CARGO_PROFILE_DEV_DEBUG = '0'` 后复跑，记录两次结果。前端测试用 `pnpm --dir frontend exec vitest run`。
- 优先扩展现有测试，新增测试超过五个前先确认。业务验收所需的旧版客户端、真机、双机和测试服务端用实际产物验证。记录每项通过、失败或未验证，必需项失败或未验证时保持阶段待验收。
- 自动化可以生成候选产物。基线提交、推送标签、线上迁移及公开发布按各自授权进行。候选包仅供演练，正式更新源只引用经批准公开的稳定版。

## 每阶段交付

列出实际改动、涉及的协议和数据变更、通过与失败的命令、手工验收的版本与设备、未完成项和需决定的问题。迁移阶段补行数、耗时、磁盘占用及恢复证据。执行阶段结束后按工作日志约定记录结论。发布记录以 S5 验收矩阵为准。
