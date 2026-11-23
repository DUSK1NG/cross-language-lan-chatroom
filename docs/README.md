# LAN Chat 文档索引

本文件是 `docs/` 的唯一入口。仓库根 `README.md` 面向使用者，这里面向开发、发布与接手维护。

## 先读这几篇

- [工作日志](../WORKLOG.md)：最新完成事项、当前阻塞与下一步；接手前必读，收工前追加。
- [交接说明](CODEX_HANDOFF.md)：新接手者第一站，含交付状态、架构边界、构建命令与已知问题。
- [架构说明](architecture.md)：运行时链路、协议与桥接边界、安全与资源限制。
- [协议规范](protocol.md)：帧格式、消息类型与兼容规则。

## 开发与测试

- [测试指南](testing.md)
- [工程方法论](project/steering/engineering-methodology.md)
- [设计语言](project/steering/design-language.md)
- [界面规格](ui-design-spec.md)

## 功能说明

- [离线消息](offline-messages.md)
- [管理员与权限](administrator.md)
- [跨网络：Radmin VPN](cross-network-radmin.md)
- [跨网络：Sakura Frp（已搁置）](cross-network-sakura-frp.md)

## 发布

- [发布检查清单](release-checklist.md)
- [发布验收](release-acceptance.md)
- [发布运行手册](release-runbook.md)
- [发布环境配置](release-setup.md)
- [统一运行包](unified-package.md)
- [安装器](installer.md)
- [GitHub 发布](github-publishing.md)
- [版本说明 v1.3.0](release-notes/v1.3.0.md)

## 决策与迁移

- [ADR-0001：只用 Rust 实现](decisions/0001-rust-only.md)
- [Rust 迁移完成报告](rust-migration.md)

## 计划与执行 Prompt

- [v1.4 升级与加固合并计划（执行依据）](plans/2026-11-23-v1.4-plus-hardening-plan.md)
- [服务端加固技术明细 v2](plans/2026-11-23-server-hardening-plan.md)
- [v1.4 升级计划（已被合并计划取代）](plans/2026-11-22-v1.4-upgrade-plan.md)
- [v1.4 阶段执行 Prompt](prompts/v1.4/README.md)

## 已知问题

- [已知问题](known-issues.md)

## 历史文档

Go + Wails + C++ 时代的计划、规格、交接记录与迁移执行 Prompt 已归档到 [docs/archive/](archive/README.md)，只读保留，供回溯。

## 维护约定

- 新增文档放在 `docs/` 顶层，文件名用小写连字符；主题成组时建子目录（如 `decisions/`、`release-notes/`）。
- 新增、移动或删除文档后同步更新本索引。
- 描述"当时怎么做"的文档一旦过期，移动到 `docs/archive/` 并在文首加归档横幅，不要直接删除。
- 架构决策写成 `decisions/NNNN-<topic>.md`，编号递增；被取代的决策保留原文，只追加状态与替代链接。
- 每次会话结束前在仓库根 `WORKLOG.md` 顶部追加一条工作日志（格式见该文件）；只记路径、结论与验证命令，不写聊天内容或密钥。
