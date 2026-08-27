# Flutter Release 缩放性能设计

## 问题

当前交付程序是 Flutter Debug 构建。Debug 模式包含断言、诊断和 JIT 开销，连续调整 Windows 窗口大小时会出现明显卡顿。

## 方案

保持 Flutter UI、原生 Core、通信协议和全部现有功能不变，仅使用现有 Release Core 构建 Flutter Windows Release 程序，并打包匹配的 Qt 与 OpenSSL 运行库。

本轮不进行布局重构。只有 Release 版手动缩放仍不流畅时，才使用 Profile 构建采集帧耗时并定位具体布局热点。

## 验收标准

- Release 程序成功启动，不显示 DEBUG 标记。
- 核心、Qt、OpenSSL 运行库全部从 Release 程序目录加载。
- Flutter 测试、静态分析和 Core 测试继续通过。
- 连接方式、局域网发现、聊天、私聊、房间管理、成员管理、连接审批和设置功能保持不变。
- 用户手动连续拖动窗口边缘验证缩放流畅度。

## 回退

Release 构建不修改源码；如运行失败，保留现有 Debug 构建并检查运行库打包配置。
