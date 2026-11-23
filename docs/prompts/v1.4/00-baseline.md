# S0 执行版：Rust 基线、版本单源与 CI

完成条件为 Rust 基线可追溯、局域网附件端到端测试通过、两版桌面及 Android 可在干净 CI 构建。

## 执行顺序

1. **盘点与 G0。**在仓库根运行 `git status --short --branch`、`git log -1 --oneline`、`git branch --list`。审阅 Rust 迁移的待提交范围，包括未跟踪目录。按路径核对敏感文件和输出目录，排除本地数据库及其旁文件、证书私钥、日志、工具缓存和未批准的第三方目录。`Claude-BugHunter/` 只在 G0 作范围决定，项目实际使用的依赖保留。G0 确认提交范围、基线标签和集成分支后记录迁移快照，已有仓库直接沿用。
2. **修绿附件基线。**复跑 `lan_desktop`。审查时已复现发送后访客等待附件消息超时，根因待定位。用临时目录沿客户端发送、服务端确认、双方可见、接收方下载的正常入口定位并最小修复。该用例通过才进入后续阶段。与附件问题相关的业务代码可在本阶段修复。
3. **同步版本。**用根 `Cargo.toml` 的 `[workspace.package].version` 表示桌面发布版本。同步或派生 Tauri 的 Cargo 与配置版本、前端包、安装器和发布脚本的版本，并在 CI 核对发布说明的当前条目。历史说明按各自版本保留。Android 本次映射为 `0.4.0`、目标 `versionCode 16`，先核对已分发 APK 的最高版本号，保证本次严格递增。版本映射与 Android 签名分别验收。
4. **补齐 CI。**保留现有服务端、前端、Windows 桌面和源码启动器作业。Windows 在生成两版运行包后各跑安装器预检，并接入当前 TLS 验收脚本。Android 固定 Gradle 8.13、JDK 17、SDK 35，使用 `android/app/build.gradle` 声明的 Build Tools 和 NDK，安装 Rust ARM64 target。入口为 `gradle -p android :app:assembleDebug`，Gradle 任务负责构建 JNI，检查 APK 内含 arm64 库。Debug 产物供 CI 验收，发布签名见 S1。[AGP 兼容表](https://developer.android.com/build/releases/agp-8-13-0-release-notes)
5. **确认干净构建。**记录阶段测试结果和本机环境差异。基线路径、工具版本、版本同步命令和已通过的门禁写入现有开发测试文档。

## 本机验收

在仓库根运行。全量 workspace 测试和 clippy 在 Windows CI 独立执行。

```powershell
cargo test --locked -p lan-chat-server --test lan_desktop
cargo test --locked -p lan-chat-server -p lan-chat-client-core -p lan-chat-protocol
node scripts/check-version.mjs
pnpm --dir frontend run lint
pnpm --dir frontend exec vitest run
pnpm --dir frontend run build
pwsh -File scripts/build-desktop.ps1 -Edition lan
node scripts/p10-tls-acceptance.mjs out/desktop/lan-chat-server.exe
```

依赖已在本机缓存时可用 `--offline` 复现。基线报告另附附件根因与回归记录、版本来源、CI 作业结果和获批准的 Git 基线引用。
