# Flutter 全功能 UI 保留设计

## 目标

以 Flutter Windows 替换当前 QML 前端界面，但完整复用既有 C++ ChatBridge、GuiChatController、TLS、局域网发现和 Go 服务端逻辑。Flutter 不重写网络协议或服务端，也不改变现有 QML/React 客户端。

## 当前缺口

当前 Flutter 原型仅有远程连接、局域网发现、基础会话和消息发送，缺少原有模式选择页以及本地主机创建等既有入口，因此不能作为完整替代品。

截图中的 DLL 错误不是缺少 C ABI 导出：`lan_chat_core.dll` 已导出创建、销毁、命令、状态、事件与字符串释放六个接口。后续先在干净 Debug 运行目录核对 Qt 6.10.3、OpenSSL 3.6.3 与 MSVC 运行库的实际加载来源，排除旧进程和 DLL 依赖版本不匹配；不得用吞掉错误的降级逻辑掩盖它。

## 功能映射

| 原有入口/功能 | Flutter 目标 | 既有后端命令/状态 |
|---|---|---|
| 模式选择 | 首页面三张入口卡：远程服务器、创建本地聊天室、加入局域网聊天室 | 无命令，仅导航 |
| 远程连接 | 保留手动服务器表单与公共 CA 路径 | `session.connectRemote` |
| 本地主机 | 显式选择服务端可执行文件、公开证书、私钥与数据库路径；仅将路径传给 C++，不读取文件内容 | `session.connectLocalHost` |
| 局域网加入 | 搜索、选择主机并连接；C++ 发现模块管理该主机 CA | `session.discoverLanHosts`、`session.connectDiscoveredHost` |
| 会话与聊天 | 房间、私聊、消息、发送、断线和错误状态 | 现有 state JSON 与聊天命令 |
| 设置与频道管理 | 迁移现有可用 Bridge 命令和状态，不新增协议 | 现有 ChatBridge 协议 |

## 架构与安全

- Flutter 控制器只构造 JSON 命令并渲染非敏感 state JSON；C++ Core 保持线程、TLS、证书持久化和网络生命周期的唯一所有者。
- UI 仅显示路径文本；不读取、打印、复制、哈希或上传私钥、证书内容、数据库、聊天或日志。
- 每个界面功能先用可注入 `ChatCore` 写失败 Flutter 测试，再以最小 UI/控制器代码转绿。
- 每个迁移模块都要包含 Windows Debug 构建与手动验收；旧客户端不被修改。

## 交付顺序

1. 修复与回归验证 C ABI DLL 运行时依赖加载。
2. 增加模式选择页和三条连接流程。
3. 迁移聊天、会话和频道/用户管理入口。
4. 迁移设置与剩余 Bridge 能力，执行全量回归和双客户端人工验收。
