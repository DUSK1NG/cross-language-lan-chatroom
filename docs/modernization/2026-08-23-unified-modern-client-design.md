# 统一现代客户端与 GitHub 自构建启动器设计

## 目标

将仓库收口为一个面向用户的正式客户端路径：React + Qt WebEngine + QWebChannel
桌面客户端。旧的命令行客户端和 QML 页面不再出现在默认启动、默认构建、发布包或
README 中。

GitHub 面向开发者的下载包提供 `LANChat-Launcher.exe`：双击后检查源码和构建产物，
必要时构建前端、现代 GUI 与本地 Go Server，然后启动现代 GUI。局域网测试时发给
另一台电脑的成员包保持预编译、仅运行，不引入自动编译或开发工具链。

## 已审计到的冲突

| 位置 | 当前行为 | 统一后的行为 |
| --- | --- | --- |
| `client-cpp/src/main.cpp` / `chat-client.exe` | 旧命令行客户端仍可构建 | 不再作为正式客户端构建或发布；网络代码继续供 GUI 复用 |
| `client-cpp/gui` | WebEngine 为可选构建，QML 可成为默认界面 | WebEngine 成为默认且唯一正式 UI；QML 仅保留为内部故障回退，不打包、不写入用户文档 |
| `scripts/start-gui.ps1` | 指向旧的 `client-cpp/gui/build` | 改为调用统一构建模块并启动现代 GUI |
| `scripts/package-release.ps1` | 依赖旧构建目录、单一角色包假设 | 按“GitHub 源码启动包 / 测试成员包”分别生成并验证 |
| `README.md`、发布文档 | 同时描述旧 GUI、单一包与新成员包 | 只描述现代客户端、两个发布物及安全边界 |

## 发布物与职责

### 1. GitHub 源码启动包

GitHub Release 提供 `LANChat-Source-Launcher-windows-x64.zip`，包含完整源码和一个预编译、
静态链接的 `LANChat-Launcher.exe`。用户从 Release 下载该包并双击 EXE，而不是直接使用
GitHub 的 “Download ZIP” 源码快照。

启动器是一个很小的启动模块，接口只有“启动”。其实现负责调用仓库内的
`scripts/bootstrap-github.ps1`，并显示可读错误。PowerShell 脚本负责：

1. 检查 Node、CMake、Ninja、MSVC Build Tools、Qt 6 MSVC WebEngine/WebChannel 和 OpenSSL；
2. 缺少依赖时，先明确展示将安装的内容并要求一次确认；允许时调用受控安装流程；
3. 比较源码时间戳和构建清单；有变化时构建 `frontend/dist` 与现代 GUI；
4. 构建或复用 `server-go/chat-server.exe`；
5. 启动现代 GUI。

它绝不静默提权、不下载私钥、不写入仓库受保护的证书目录。首次工具链安装可能需要
管理员授权和数 GB 磁盘空间；后续双击通常只做增量构建。

### 2. 局域网测试成员包

`LANChat-member-modern-x64.zip` 保持为已部署的预编译客户端。它只运行现代 GUI，携带
公开 CA 证书，不含服务端、私钥、数据库、Node、CMake、Qt SDK 或自动编译入口。

### 3. 主机端

GitHub 源码启动包在本机开发/房主场景可构建 Go 服务端。私钥在第一次创建本地聊天室时
于房主机器生成；它不是仓库文件，也不是发布资产。

## 统一构建模块

新增 `scripts/build-modern.ps1` 作为深模块。调用者只需选择构建模式；该模块内部完成：

- 现代前端构建；
- CMake 配置，始终启用 `LAN_CHAT_ENABLE_WEB_UI=ON`；
- MSVC x64 环境与 Qt 6 MSVC WebEngine 的定位；
- OpenSSL 定位；
- 现代 GUI、Go 服务端和测试的构建；
- 构建清单与可读诊断输出。

标准构建目录固定为 `out/modern-msvc-x64`，不再使用含义含混的 `build`、`build-bridge`
或 `build-webengine-*` 作为用户入口。该目录和所有生成物保持被 Git 忽略。

## 迁移策略

1. 新增统一构建/启动模块与 GitHub 启动器源代码；先不删除任何旧文件。
2. 将脚本、CMake 预设、CI 与发布脚本全部改为调用统一构建模块。
3. 将 README 和用户文档改写为现代客户端说明；过期阶段文档移入 `docs/archive/`，保留
   历史而不再影响用户路径。
4. 在“现代 GUI 本机启动、双客户端 TLS、成员包独立启动、GitHub 启动器增量构建”均验证后，
   停止构建旧 CLI 目标，并将 QML 回退列为内部诊断选项。

## 安全边界

`server-lan.key` 是服务端身份凭据。泄露后，攻击者可以在局域网中伪装成受信任的房主，
对成员建立看似有效的 TLS 连接。因此：

- 可以上传 GitHub：源码、构建脚本、文档、测试、公开证书使用说明；
- 不可以上传 GitHub：私钥、数据库、聊天记录、个人访问令牌和本机构建输出；
- 可以向成员分享：房主 IPv4、端口和 `.crt` 公开证书；
- 不可以向成员分享：`.key` 私钥。

这不是禁止上传 GitHub，而是限制上传任何可被滥用的秘密材料。

## 验收标准

- GitHub 源码启动包双击 `LANChat-Launcher.exe` 后，首次完成明确的依赖检查，后续可增量构建并启动现代 GUI；
- 所有用户入口都启动 React/WebEngine 现代 UI，不再误入旧 QML 或 CLI；
- 成员测试包不发生自动编译，且仍可独立启动；
- `server-lan.key`、`chat.db` 和生成物仍被 Git 忽略，发布包检查也拒绝它们；
- README、发布指南、CI 和脚本均只引用统一现代构建路径；
- 前端、C++/Qt、Go 测试与独立包启动烟测通过。
