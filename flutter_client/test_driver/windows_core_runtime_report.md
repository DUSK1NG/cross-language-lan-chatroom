# Windows Core Runtime Report

## RED

在全新 Flutter test 进程中，以 `LAN_CHAT_CORE_DLL` 指向
`out\\modern-msvc-x64\\lan_chat_core.dll` 执行
`flutter test test\\lan_chat_core_test.dart`，`DynamicLibrary.open` 稳定失败：
Windows error code 127。

## 根因

`dumpbin /EXPORTS` 确认 Core 有六个 C ABI 导出：create、destroy、dispatch、
current-state、take-event 和 free-string。`dumpbin /DEPENDENTS` 确认它依赖 Qt 6
和 OpenSSL 运行时，而不是缺少导出。

Core 直接导入 Qt6Core、Qt6Gui、Qt6Network、Qt6Quick、libssl-3-x64 和
libcrypto-3-x64；Qt6Quick 的传递依赖还包括 Qt6OpenGL、Qt6Qml、
Qt6QmlModels、Qt6QmlMeta 与 Qt6QmlWorkerScript。实际来源已核对为 Qt
6.10.3.0 和 OpenSSL 3.6.3。

Flutter test 宿主的 PATH 预加载了不匹配的 MSYS Qt 6.11.2；Windows 按模块名
复用该 DLL，故该宿主的真实 FFI 分支仍会报 127，不能作为 runner 打包结果的证据。

## GREEN

`windows/CMakeLists.txt` 在配置期验证全部运行时 DLL 存在，runner 的
`POST_BUILD` 将它们和 Core 复制到 Debug runner。Core 加载失败时，应用只显示
不可操作的启动错误页，不再回退到 Idle ChatCore。

新 PowerShell 进程运行 Debug runner 后，模块清单显示 Core、全部 Qt 6.10.3
运行时和 OpenSSL 3.6.3 均从 runner Debug 目录加载；进程保持运行。

## 验证命令

```powershell
$env:LAN_CHAT_CORE_DLL = 'C:\\Users\\Q1573\\Desktop\\MY_project\\lan-chat\\.worktrees\\flutter-ui-prototype\\out\\modern-msvc-x64\\lan_chat_core.dll'
$env:LAN_CHAT_QT_PREFIX = 'C:\\Users\\Q1573\\Desktop\\MY_project\\lan-chat\\.tools\\qt\\6.10.3\\msvc2022_64'
$env:LAN_CHAT_OPENSSL_ROOT = 'C:\\Users\\Q1573\\Desktop\\MY_project\\lan-chat\\.tools\\vcpkg\\installed\\x64-windows'
flutter build windows --debug
flutter test
flutter analyze
git diff --check
```
