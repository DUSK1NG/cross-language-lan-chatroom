# 历史文档归档

## 为什么在这里

2026-11-22 Rust 迁移完成后，仓库只保留描述当前实现的文档。Go + Wails + C++ 时期的计划、规格、交接记录与执行 Prompt 移到这里：保留当时的推理与决策，同时避免与现行文档混淆。

归档范围：`docs/superpowers/` 全量、`docs/project/specs/` 全量、`docs/handover/`、`docs/prompts/`、`docs/stage9-lan-test.md`、`docs/developer-transfer.md`。

## 规则

- **只读**：归档文档不再更新；与 `docs/` 下现行文档冲突时，以现行文档为准。
- **不删除**：确需废弃时也只加新横幅，不物理删除，以便回溯。
- **引用方式**：引用归档内容时写 `docs/archive/...` 完整路径；归档文件内部的相对链接已按新位置修正。
- **正文旧路径保持原样**：例如 `server-go/hub.go`、`client-cpp/`、`docs/superpowers/...`。它们是历史记录的一部分，按下方映射表换算，不要批量改写。

## 路径映射（旧 → 新）

| 旧路径 | 现路径 |
| --- | --- |
| `docs/superpowers/plans/…` | `docs/archive/superpowers/plans/…` |
| `docs/superpowers/specs/…` | `docs/archive/superpowers/specs/…` |
| `docs/project/specs/…` | `docs/archive/project-specs/…` |
| `docs/handover/…` | `docs/archive/handover/…` |
| `docs/prompts/…` | `docs/archive/prompts/…` |
| `docs/stage9-lan-test.md` | `docs/archive/stage9-lan-test.md` |
| `docs/developer-transfer.md` | `docs/archive/developer-transfer.md` |

## 目录结构

```text
docs/archive/
  README.md
  superpowers/{plans,specs}/
  project-specs/{01-stability-release,02-design-system,03-media-preview,04-desktop-notifications}/
  handover/
  prompts/
  stage9-lan-test.md
  developer-transfer.md
```

## 索引

| 分组 | 数量 | 年代 | 内容 | 现行替代 |
| --- | --- | --- | --- | --- |
| [superpowers/plans](superpowers/plans/) | 19 | 2026-08-17 – 09-08 | TDD 实现计划：TCP 分帧、登录、Hub 广播、命令交互、私信、管理员权限、加密附件、MLS E2EE、前端改版 | [architecture.md](../architecture.md)、[protocol.md](../protocol.md) |
| [superpowers/specs](superpowers/specs/) | 11 | 2026-08-17 – 09-02 | 配套设计稿，含 sol/luna 委派与 T3 改版设计 | [ui-design-spec.md](../ui-design-spec.md) |
| [project-specs](project-specs/README.md) | 13 | 2026-10 – 11 | 规格 01–04（稳定性、设计系统、媒体预览、桌面通知）的 requirements/design/tasks 与目录说明 | [architecture.md](../architecture.md)、[testing.md](../testing.md) |
| [handover](handover/) | 1 | 2026-09-01 | 加密附件交接记录，索引 server-go 与 client-cpp 代码位置 | [protocol.md](../protocol.md)、[architecture.md](../architecture.md) |
| [prompts](prompts/) | 1 | 2026-11-22 | Rust 迁移执行 Prompt（迁移已完成，保留供回溯） | [rust-migration.md](../rust-migration.md)、[ADR-0001](../decisions/0001-rust-only.md) |
| 单篇 | 2 | 2026-09 – 10 | stage9 局域网测试记录、developer-transfer 打包说明（引用已删除的 client-cpp 路径） | [testing.md](../testing.md)、[installer.md](../installer.md) |

## 已知局限

- 归档计划与规格里的命令、路径、依赖版本均停留在当时状态（Go、Qt/WebEngine、CMake、MLS++），照抄会失败。
- 部分归档文档的验收门未全部勾选；勾选状态即当时真实进度，不是当前产品状态。
- 归档文档的正文相对链接已修正；但指向 `server-go/`、`client-cpp/` 等已删除目录的代码路径无法修复，只能作为历史位置线索。
