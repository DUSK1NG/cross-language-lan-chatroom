# 附件发布、数据隔离与 GitHub 整理计划

## 目标

修复附件在“校验完成”后没有作为聊天内容发送、上传限流错误反复出现在时间线、搜索后消息顺序异常的问题；保证测试不会读取或污染正式聊天数据库；将本次修复构建为可发布版本并同步到 GitHub；清理 GitHub 上带 `codex/` 前缀或 `glass-ui` 表述的分支名。

## 范围与约束

- 单附件上限保持 5 GiB，协议帧保持 64 KiB，逻辑分块保持 47 KiB。
- 附件内容仍使用 MLS 保护的 manifest；服务端不能获得明文文件名和密钥。
- 上传分块使用独立字节限流，普通聊天限流继续保留，且任何拒绝都必须关联原始命令。
- 测试只能使用临时目录或明确的测试数据根；发布包和正式运行路径不得携带或复用测试数据库、证书、日志或附件数据。
- 不重写 `master` 历史；分支改名采用“创建新远程分支、验证、删除旧远程分支”的可恢复顺序。

## 当前证据

- 截图中的 `Rate limit exceeded; please slow down` 是旧版本把附件分块计入每秒 8 条的聊天限流后产生的错误；这些错误被当作系统消息加入时间线。
- 附件 manifest 由提交成功后的 MLS 聊天消息传递，但其消息标识与本地待附加元数据的关联不稳定，导致已校验的附件没有渲染为聊天卡片。
- 搜索结果通过历史消息路径回填模型；系统/传输错误混入同一模型后，会在回填时出现到顶部的现象。
- GitHub 远程当前包含：
  - `codex/admin-channel-permissions`
  - `codex/glass-ui`
  - `codex/gui-polish`
  - `codex/v1.0.1-release`

## 实施步骤

### 1. 修复附件发布、错误路由与时间线排序

**涉及文件：**

- `client-cpp/gui/src/gui_connection_worker.cpp`
- `client-cpp/gui/src/gui_chat_controller.cpp`
- `client-cpp/gui/src/chat_bridge.cpp`
- `client-cpp/gui/tests/chat_bridge_tests.cpp`
- `frontend/src/bridge/*`
- `frontend/src/components/MessageComposer.*`

**实施：**

1. 为 manifest 聊天消息生成并保留稳定的 `message_id`，使发送端和接收端的 manifest 事件能与对应聊天行关联。
2. 将附件 commit、manifest 和普通聊天的完成顺序覆盖到集成测试：上传完成后，发送端与接收端都出现可下载的附件聊天卡片。
3. 只把用户可理解的系统事件放进时间线；传输、限流、协议和附件错误改为命令结果/附件错误卡片，按上传命令去重，绝不作为聊天消息持久化或广播。
4. 让历史分页和搜索结果按服务端时间线顺序合并，不使用搜索回填覆盖实时消息；搜索结束后保留实时消息的相对位置。
5. 前端上传卡保留百分比、已传大小、速度及“正在校验”状态，commit 成功后消失并显示实际附件消息。

**验收：**

- 129 块附件可完成上传、commit、manifest 发送和双方附件卡片展示。
- 对 900 MiB 文件的上传过程中没有 `Rate limit exceeded` 气泡；若确有操作错误，仅显示一条关联的错误卡片。
- 搜索后，历史结果与实时消息按时间显示，错误提示不会移动到顶部或进入聊天历史。

### 2. 隔离测试、开发和正式运行数据

**涉及文件：**

- `client-cpp/gui/src/host_path_resolver.*`
- `client-cpp/gui/src/local_host_bootstrap.*`
- `client-cpp/gui/tests/host_path_resolver_tests.cpp`
- `client-cpp/gui/tests/local_host_bootstrap_tests.cpp`
- `scripts/build-modern.ps1`
- `scripts/package-unified-release.ps1`
- `scripts/package-release.ps1`
- `.gitignore`

**实施：**

1. 审计启动路径，明确正式宿主仅使用 `%LOCALAPPDATA%/DUSK1NG/LAN Chat/host` 下的私钥、证书和数据库。
2. 为测试执行设置独立的临时数据根，并在测试结束后自动清理；测试不得回退到正式 `AppLocalDataLocation` 或仓库根的 `server-go/chat.db`。
3. 在打包前校验发布目录：拒绝任何 `.db`、私钥、证书、日志、测试输出或附件缓存进入发行包。
4. 添加回归测试，分别验证正式路径、显式测试根、打包清单和旧路径迁移行为。

**验收：**

- 从 GitHub 发布包首次启动时看不到测试消息或旧测试附件。
- 运行 C++/Go/前端测试后，正式数据库的哈希和内容不变。
- 发布打包检查能明确拒绝数据库、日志和测试产物。

### 3. 统一修复构建和验证

**涉及文件：**

- `server-go/client.go`
- `server-go/connection_limits.go`
- `server-go/*_test.go`
- `client-cpp/gui/tests/*`
- `frontend/src/**/*.test.*`
- `scripts/build-modern.ps1`

**实施：**

1. 运行 Go 全量测试、前端全量测试和现代 C++ 客户端测试。
2. 执行真实双客户端附件上传测试，断言附件卡片在双方出现、下载内容哈希一致，并记录吞吐量。
3. 构建发布所需的 GUI、房主服务端和安装/便携包；检查包内容与数据隔离要求一致。

**验收：**

- 所有相关测试通过。
- 安装包和便携包都可启动，且不包含用户数据或测试数据。
- 新构建的版本号、提交号和校验信息可追溯。

### 4. GitHub 分支整理与发布

**涉及对象：**

- 当前 `master` 的已验证提交
- GitHub 远程分支
- GitHub release 资产（仅在包验证通过后更新）

**分支映射：**

| 旧远程分支 | 新远程分支 |
| --- | --- |
| `codex/admin-channel-permissions` | `admin-channel-permissions` |
| `codex/glass-ui` | `desktop-ui-refresh` |
| `codex/gui-polish` | `desktop-ui-polish` |
| `codex/v1.0.1-release` | `v1.0.1-release` |

**实施：**

1. 检查未提交变更，确保本次提交只包含已验证的产品、测试、打包和文档修改；不覆盖已有工作。
2. 创建清晰的提交并推送 `master`。
3. 先推送新名称的远程分支并验证提交 SHA，再删除对应的旧 `codex/*` 远程分支；保留 `master` 和现有标签不变。
4. 验证发布包后上传/更新 GitHub 发行版资产，并在发布说明中注明数据隔离与附件修复。

**验收：**

- GitHub 默认分支为最新已验证代码。
- GitHub 分支列表不再显示上述 `codex/*` 名称，也没有 `glass-ui` 分支名。
- 发布资产不包含聊天数据库、测试消息、证书、私钥、日志或开发输出。

## 风险与处理

- 远程分支删除是可见的外部变更：在新分支 SHA 已验证后才删除旧分支；本地工作树和标签保持不变。
- 当前工作树已有大量未提交的现代化改动：在提交前按文件和测试归属审阅，保留无关用户修改，不使用重置或强制覆盖。
- 真实 900 MiB 上传耗时较长：使用较小文件覆盖端到端逻辑与吞吐量，另执行可取消的大文件冒烟测试，不在测试中制造 5 GiB 实体文件。
