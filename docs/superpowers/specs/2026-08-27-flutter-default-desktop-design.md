# Flutter 默认桌面程序设计

## 目标

将 Flutter Windows 客户端作为 LAN Chat 的默认发布程序，提供可直接双击运行的发布目录，同时保留现有 Qt/React 客户端作为独立回退实现。

## 范围

- 合并现有 `codex/flutter-ui-prototype` 分支的 Flutter 客户端和 FFI 核心桥接。
- 构建 `lan_chat_core.dll`，供 Flutter 通过 FFI 调用既有聊天核心。
- 新增 Flutter Windows 发布脚本，发布根目录固定为 `out/flutter-windows-release/`。
- 发布入口命名为 `LANChat-Launcher.exe`。
- 发布目录包含 Flutter 可执行文件、`data/`、`flutter_windows.dll`、`lan_chat_core.dll`、Qt 6.10.3 运行库和 OpenSSL 运行库。

## 不在范围内

- 不删除或重写 Qt/React 客户端。
- 不修改现有聊天协议、TLS、局域网发现或服务端实现。
- 不把 Windows 发布包压缩为单个可执行文件；Windows Flutter 发布需要相邻 DLL 和 `data/` 资源目录。

## 架构

Flutter 是默认 UI。它通过相邻的 `lan_chat_core.dll` 调用已有 C++ 聊天核心，因此保留 Qt Core、网络、TLS 和本地服务端能力，但不使用 Qt WebEngine/React 作为默认界面。

发布脚本按以下顺序工作：

1. 以项目固定的 Qt 6.10.3 和 OpenSSL 工具链配置、构建 `lan-chat-core`。
2. 执行 `flutter build windows --release`。
3. 创建干净的 `out/flutter-windows-release/` 发布目录并复制 Flutter release bundle。
4. 将 `lan_chat_core.dll`、其 Qt 运行库依赖和 OpenSSL DLL 放入发布目录。
5. 将 Flutter 可执行文件重命名为 `LANChat-Launcher.exe`，保留所有配套 DLL 和 `data/`。

现有 `scripts/build-modern.ps1` 继续单独构建 Qt/React 回退客户端；它不再定义默认桌面入口。

## 运行与失败处理

- 构建脚本显式定位 Flutter SDK、Qt 6.10.3、OpenSSL 和核心 DLL；不依赖系统 `PATH` 中的 Qt DLL。
- 任一依赖、构建步骤或复制步骤失败时，脚本立即终止并显示具体缺失项。
- 打包完成后，脚本验证 `LANChat-Launcher.exe`、`flutter_windows.dll`、`lan_chat_core.dll`、`data/`、Qt 和 OpenSSL DLL 均存在。
- 发布目录的启动不需要用户配置 Qt 或 OpenSSL 环境变量，从而避免错误加载机器上不兼容的 Qt DLL。

## 验收

- `flutter analyze` 和 Flutter 测试通过。
- 既有 C++ CTest 通过，包含 Flutter FFI 核心测试。
- 发布目录完整性检查通过。
- 从 `out/flutter-windows-release/` 直接启动 `LANChat-Launcher.exe` 的冒烟测试通过。
- `scripts/build-modern.ps1` 的 Qt/React 回退构建继续可用。
