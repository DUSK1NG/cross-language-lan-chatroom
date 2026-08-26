# Flutter UI Prototype

此目录是 LAN Chat 的独立 Flutter Windows UI 原型壳，不修改现有 QML、React、Go、C++、安装器或发布包。

## 本地验证

需要 Flutter stable SDK、Windows desktop 支持和 Visual Studio Windows 工具链：

```powershell
flutter test test/app_smoke_test.dart
flutter run -d windows
```

当前壳显示应用标题 `LAN Chat` 和连接状态 `未连接`；后续任务在本目录内扩展 UI 和核心接入。
