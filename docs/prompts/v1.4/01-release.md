# S1 执行版：发布自动化与桌面更新

S0 通过后开始。先用现有用户可安装的产物走通更新，再收口完整发布工作流。核对 `scripts/build-desktop.ps1`、两套打包脚本、`installer/LANChat.iss` 和 Tauri 配置。

## 执行顺序

1. **安装器链路。**当前用 Cargo 构建应用、Inno Setup 打包。先验证 Tauri updater 对两版签名 Inno 安装器的参数、同版覆盖、退出与启动、数据保留及失败反馈。保留 LAN/Online 各自安装标识、路径和用户级安装范围。若现有安装器接口不匹配，提交实测结论并确定兼容的更新安装方案后再选构建路径。
2. **更新版本与交互。**首个带 updater 的版本通过同版安装器覆盖旧版。便携 ZIP 给出相应新版 ZIP 下载入口。已接入 updater 的安装版用独立 HTTPS 清单 `latest-lan.json` / `latest-online.json`，校验签名和版别。在线版启动可静默检查，LAN 版默认由设置页主动检查并维持离线可用。手动检查应反馈已最新、检查失败或有可用版本。有更新时显示版本、说明和可取得的包大小，下载显示进度，确认安装前再提示退出。开房时说明成员会断开，并提供「稍后安装」。
3. **签名。**先在隔离环境用测试签名验通两版更新。正式 Tauri 公钥固定在对应应用配置，私钥在受保护的发布环境，签名和安装前校验须能拒绝另一版产物。Android CI Debug 与发行 APK 分开，先确认旧 APK 的 application ID、签名证书和最高 versionCode，发行 APK 用可覆盖安装的签名。若无法取得匹配的签名，S5 的原位升级门禁保持阻塞，由用户决定分发方案。[Android 更新规则](https://developer.android.com/google/play/app-updates)
4. **工作流。**`v*` tag 和指定允许的 tag 的手动触发均须校验引用、版本和稳定版/RC 类型。RC 走独立演练渠道，产出 LAN/Online 各 ZIP 和安装器、APK、`SHA256SUMS` 及独立演练 manifest。稳定版先创建 draft，待 S5 验收与发布批准后公开。稳定版清单仅指向同版已签名产物，候选包的 Windows 安装版本沿用有效的数字格式。用 `scripts/make-updater-manifest.mjs` 校对清单版本与实际安装包。
5. **说明。**在现有发布手册记录产物、Secrets 名称、测试与稳定通道、首次覆盖安装/便携版升级路径，以及撤回更新后的恢复方式。使用官方 [Tauri updater 文档](https://v2.tauri.app/plugin/updater/)核对签名、HTTPS 和 Windows 安装退出行为。

## 验收

```powershell
pwsh -File scripts/package-unified-release.ps1 -Edition lan
pwsh -File scripts/package-unified-release.ps1 -Edition online
pwsh -File scripts/package-installer.ps1 -Edition lan -ValidateOnly
pwsh -File scripts/package-installer.ps1 -Edition online -ValidateOnly
pnpm --dir frontend exec vitest run
```

用同一测试 HTTPS 渠道和两个递增版本，两版分别验证「旧安装器覆盖到支持 updater 的版本 → 签名更新 → 再启动」。验证跨版或坏签名被拒、开房可延期、证书和数据仍可用。再跑一次完整 RC，记录所有产物、签名证据和通道隔离结论。正式发布调用见 S5。
