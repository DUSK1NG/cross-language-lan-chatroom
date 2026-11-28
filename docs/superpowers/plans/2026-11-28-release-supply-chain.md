# LAN Chat Release and Supply Chain Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 隔离 LAN/Online 更新私钥，签名并验证全部分发资产，固定供应链依赖，并为 Android、Linux 服务端和 CI 建立可重复验证门禁。

**Architecture:** 主 Release 工作流调用两个独立版别作业，分别使用 `release-lan`/`release-online` Environment；签名工具在仓库内锁定；Tauri CLI 只负责签名，Rust `minisign-verify` 工具负责验签；发布脚本拒绝覆盖已有资产；CI 增加 Cargo/pnpm/Gradle 漏洞与完整性检查。

**Tech Stack:** GitHub Actions、PowerShell、Node.js、Rust、minisign-verify、Tauri CLI、Gradle、OSV-Scanner、Dependabot。

**Spec:** `docs/superpowers/specs/2026-11-28-lan-chat-security-remediation-design.md`

## Global Constraints

- 发布版本保持 `1.4.1`。
- 每个签名作业只能接触当前版别的一份私钥和口令；禁止矩阵共用四份密钥、禁止 `secrets: inherit`。
- Tauri CLI 精确锁定 `2.12.6`；该 CLI 只有 `sign`/`generate`，没有 `verify`，验签必须使用独立工具。
- 安装器、便携 ZIP、Linux 服务端、`SHA256SUMS` 和 updater manifest 都必须有可验证签名或受签名清单保护。
- 发布前必须用对应公钥验证版本、资产名、大小、哈希和签名；错误公钥、损坏文件、缺签和改名一律失败。
- Android 发布必须比对固定证书 SHA-256，不能只验证 APK 内部签名自洽。
- GitHub Actions 固定完整 commit SHA；不使用浮动 tag。
- CI 增加 Cargo、pnpm、Gradle 漏洞/依赖门禁；Gradle 完整性校验与漏洞扫描分开。
- 不在正式 `v1.4.1` 发布前创建修改版本号的 RC tag；发布链先用测试 Environment/测试密钥验证。

---

### Task 1: 升级依赖并固定 Action

**Files:**
- Modify: `frontend/package.json`
- Modify: `frontend/pnpm-lock.yaml`
- Modify: `server-rust/Cargo.toml`
- Modify: `Cargo.lock`
- Modify: `Cargo.toml`
- Modify: `README.md`
- Modify: `android/app/build.gradle`
- Create: `android/gradle/wrapper/gradle-wrapper.properties`
- Create: `android/gradlew`
- Create: `android/gradlew.bat`
- Create: `android/gradle/wrapper/gradle-wrapper.jar`
- Modify: `.github/workflows/ci.yml`
- Modify: `.github/workflows/release.yml`
- Create: `android/gradle/verification-metadata.xml`
- Create: `android/gradle.lockfile`

**Interfaces:**
- Produces: Vite `8.4.0`、source-map-js `1.2.4`、rusqlite `0.43.0`、libsqlite3-sys `0.40.0`。
- Produces: AGP `9.6.0`、Gradle `9.8.2`、Build Tools `37.0.0`、NDK `28.2.13676358`。
- Produces: 固定 Action SHA 清单。

- [ ] **Step 1: 添加依赖门禁红灯测试**

在 CI 新增步骤前，本地运行：

```bash
pnpm.cmd --dir frontend audit --prod
cargo audit --file Cargo.lock
(cd android/transport-rust && cargo audit)
gradle -p android --no-daemon --dependency-verification strict :app:assembleDebug
```

Expected: 当前版本分别报告 Vite/source-map-js、SQLite 和 Gradle 校验/漏洞问题，作为修复前基线。

- [ ] **Step 2: 升级前端依赖**

```bash
pnpm.cmd --dir frontend add -D vite@8.4.0
pnpm.cmd --dir frontend update source-map-js@1.2.4
pnpm.cmd --dir frontend install --frozen-lockfile
pnpm.cmd --dir frontend run lint
pnpm.cmd --dir frontend exec vitest run
pnpm.cmd --dir frontend run build
```

Expected: 全部 PASS，lockfile 中 Vite 为 8.4.0，source-map-js 不低于 1.2.2。

- [ ] **Step 3: 升级 SQLite 与 Rust 版本**

```bash
cargo update -p rusqlite --precise 0.43.0
cargo update -p libsqlite3-sys --precise 0.40.0
cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
```

将根 `rust-version` 从 1.85 更新为 1.91，与 `client-core` 和 Tauri crate 一致；README 同步。

- [ ] **Step 4: 升级 Android 工具链**

依据官方 AGP 9.6 兼容表，设置：

```groovy
id 'com.android.application' version '9.6.0' apply false
compileSdk 37
buildToolsVersion '37.0.0'
ndkVersion '28.2.13676358'
targetSdk 35
```

Gradle wrapper 通过以下命令生成并设置为 `9.8.2`，CI 的 SDK 包安装同步为 `platforms;android-37`、`build-tools;37.0.0`、`ndk;28.2.13676358`。保留 JDK 17。

```bash
(cd android && gradle wrapper --gradle-version 9.8.2)
```

- [ ] **Step 5: 生成 Gradle 锁与验证元数据**

```bash
gradle -p android --no-daemon --write-verification-metadata sha256 help
gradle -p android --no-daemon --dependency-verification strict :app:assembleDebug
```

在 `android/build.gradle` 启用 `dependencyLocking { lockAllConfigurations() }`，生成并提交 `gradle.lockfile` 与 `gradle/verification-metadata.xml`。

- [ ] **Step 6: 固定 Action SHA**

使用以下已核对提交：

| Action | 固定 SHA |
| --- | --- |
| `actions/checkout` | `11d5960a326750d5838078e36cf38b85af677262` |
| `Swatinem/rust-cache` | `d1527c8f67ef9af357290475d4af4b0fab8f12e0` |
| `pnpm/action-setup` | `b906affcce14559ad1aafd4ab0e942779e9f58b1` |
| `actions/setup-node` | `49933ea5288caeca8642d1e84afbd3f7d6820020` |
| `actions/setup-java` | `cf277c60eb25467037889841efdb72551f06f6c3` |
| `gradle/actions/setup-gradle` | `8da46b539cd8fe6ab27ca75128919a18d757604c` |
| `android-actions/setup-android` | `9fc6c4e9069bf8d3d10b2204b1fb8f6ef7065407` |
| `actions/upload-artifact` | `ea165f8d65b6e75b540449e92b4886f43607fa02` |
| `actions/download-artifact` | `d3f86a106a0bac45b974a628896c90dbdf5c8093` |
| `google/osv-scanner-action/osv-scanner-action` | `32d3cd482bdadfb38b186c0ad1b031902d55d932` |

- [ ] **Step 7: 运行升级门禁**

```bash
cargo test --workspace --locked
pnpm.cmd --dir frontend audit --prod
cargo audit --file Cargo.lock
(cd android/transport-rust && cargo audit)
gradle -p android --no-daemon --dependency-verification strict :app:assembleDebug
```

Expected: 全部 PASS，无未处理高危告警。

- [ ] **Step 8: 提交**

```bash
git add frontend/package.json frontend/pnpm-lock.yaml server-rust/Cargo.toml Cargo.toml Cargo.lock README.md android .github/workflows
git commit -m "build: upgrade vulnerable dependencies and pin actions"
```

---

### Task 2: 锁定签名工具并实现独立验签

**Files:**
- Modify: `Cargo.toml`
- Create: `tools/release-signing/package.json`
- Create: `tools/release-signing/package-lock.json`
- Create: `tools/release-signing/sign.mjs`
- Create: `tools/release-signing/Cargo.toml`
- Create: `tools/release-signing/src/main.rs`
- Create: `tools/release-signing/test/fixtures`
- Test: `tools/release-signing/test/sign.test.mjs`
- Test: `tools/release-signing/src/main.rs`

**Interfaces:**
- Produces: `node tools/release-signing/sign.mjs --file PATH --version 1.4.1 --edition lan|online`。
- Produces: `lan-chat-release-verify verify --public-key BASE64 --signature PATH --file PATH --expected-version 1.4.1`。
- Produces: `lan-chat-release-verify embedded-key --asset PATH --public-key-base64 BASE64`。

- [ ] **Step 1: 锁定依赖并确认实际接口**

```bash
mkdir -p tools/release-signing
cd tools/release-signing
npm init -y
npm install --save-exact @tauri-apps/cli@2.12.6
npm ci
npx --no-install tauri signer sign --help
npx --no-install tauri signer generate --help
cargo init --bin --name lan-chat-release-verify
```

将根 `Cargo.toml` 的 workspace members 增加 `tools/release-signing`，然后继续：

```bash
cargo add minisign-verify@0.3.0 clap@4 serde@1 serde_json@1 ring@0.17
cargo info minisign-verify@0.3.0
```

记录 `sign` 的实际参数、环境变量和 `.sig` 输出格式；阅读 `minisign-verify` 文档/源码，记录公钥、签名、trusted comment 的精确类型和方法。若接口与计划不一致，先更新计划再写代码。

- [ ] **Step 2: 添加验签红灯测试**

```rust
#[test]
fn wrong_key_version_and_corrupt_file_are_rejected() -> Result<()> {
    let dir = temp_root("release-verify")?;
    let file = dir.join("asset.zip");
    std::fs::write(&file, b"trusted asset")?;
    let signature = sign_fixture(&file, "1.4.1", &key_a())?;
    assert!(verify(&file, &signature, &key_a(), "1.4.1").is_ok());
    assert!(verify(&file, &signature, &key_b(), "1.4.1").is_err());
    assert!(verify(&file, &signature, &key_a(), "1.4.2").is_err());
    std::fs::write(&file, b"corrupt")?;
    assert!(verify(&file, &signature, &key_a(), "1.4.1").is_err());
    Ok(())
}
```

`sign_fixture` 使用测试私钥调用锁定的 Tauri CLI，测试私钥只存在临时目录。测试辅助模块提供 `make_release_fixture() -> (tempdir, key_a, key_b)`、`sign_fixture(&Path, &str, &str) -> Result<Vec<u8>>` 和 `verify(&Path, &[u8], &str, &str) -> Result<()>`；`key_a`/`key_b` 由 `tauri signer generate --ci` 在临时目录生成，不读取真实密钥。

- [ ] **Step 3: 运行测试确认失败**

Run: `cargo test --locked -p lan-chat-release-verify wrong_key_version_and_corrupt_file_are_rejected`
Expected: FAIL，crate 尚不存在。

- [ ] **Step 4: 实现签名脚本**

`sign.mjs` 只设置当前版别的 `TAURI_SIGNING_PRIVATE_KEY`/`TAURI_SIGNING_PRIVATE_KEY_PASSWORD`，调用锁定 CLI：

```js
execFileSync('npx', ['--no-install', 'tauri', 'signer', 'sign', '--app-version', version, file], {
  cwd: toolsRoot,
  env: { ...process.env, TAURI_SIGNING_PRIVATE_KEY: privateKey, TAURI_SIGNING_PRIVATE_KEY_PASSWORD: password },
  stdio: 'inherit',
});
```

签名后检查 `<file>.sig` 非空且为 Base64；不输出私钥和口令。

- [ ] **Step 5: 实现 Rust 验签器**

使用 `minisign-verify 0.3.0` 的实际 API 实现：

1. 解析 base64 公钥和 `.sig`。
2. 验证文件内容和 trusted comment 中的 `version:1.4.1`。
3. `embedded-key` 读取 EXE 字节，确认编译期 base64 公钥字符串存在；结合签名验证判断内置公钥与签名公钥一致。
4. 所有错误返回非零退出，不打印秘密。

- [ ] **Step 6: 运行签名与验签测试**

```bash
cargo test --locked -p lan-chat-release-verify
node --test tools/release-signing/test/*.test.mjs
```

Expected: PASS。

- [ ] **Step 7: 提交**

```bash
git add tools/release-signing
git commit -m "feat(release): lock signer and add independent verification"
```

---

### Task 3: 隔离 LAN/Online Environment 与签名密钥

**Files:**
- Modify: `.github/workflows/release.yml`
- Modify: `docs/release-runbook.md`

**Interfaces:**
- Produces: 动态 `release-${{ matrix.edition }}` Environment，每个矩阵作业只解析本版别 Environment 的通用私钥。
- Produces: `release-lan`、`release-online`、`release-android`、`release-publish` Environment。

- [ ] **Step 1: 添加工作流静态红灯测试**

新增 `scripts/tests/release-workflow.test.mjs`，读取 YAML 文本并断言：

1. desktop job 的 `environment` 为 `release-${{ matrix.edition }}`。
2. 工作流不再出现 `LANCHAT_UPDATER_PRIVATE_KEY`、`LANCHAT_ONLINE_UPDATER_PRIVATE_KEY` 或它们的口令变量。
3. 签名 step 只出现 `secrets.UPDATER_PRIVATE_KEY` 和 `secrets.UPDATER_PRIVATE_KEY_PASSWORD`。
4. 没有 `secrets: inherit`。
5. 汇总/发布 job 不引用任何私钥 secret。

- [ ] **Step 2: 运行测试确认失败**

Run: `node --test scripts/tests/release-workflow.test.mjs`
Expected: FAIL，当前矩阵作业同时注入四份密钥。

- [ ] **Step 3: 修改 desktop job 的 Environment**

```yaml
desktop:
  needs: validate
  runs-on: windows-latest
  environment: release-${{ matrix.edition }}
  strategy:
    matrix:
      edition: [lan, online]
```

保留矩阵为每个版别创建独立 job；Environment 名按 matrix 动态解析，GitHub 只向当前 job 注入该 Environment 的 secret。

- [ ] **Step 4: 将四份私钥改为两份通用 secret**

```yaml
- name: Sign installer and create edition manifest
  env:
    TAURI_SIGNING_PRIVATE_KEY: ${{ secrets.UPDATER_PRIVATE_KEY }}
    TAURI_SIGNING_PRIVATE_KEY_PASSWORD: ${{ secrets.UPDATER_PRIVATE_KEY_PASSWORD }}
  run: |
    node tools/release-signing/sign.mjs --file "release/$env:INSTALLER" --version $env:RELEASE_VERSION --edition $env:EDITION
    node tools/release-signing/sign.mjs --file "release/$env:ZIP" --version $env:RELEASE_VERSION --edition $env:EDITION
    node scripts/make-updater-manifest.mjs $env:RELEASE_TAG release "docs/release-notes/v$env:RELEASE_VERSION.md" $env:EDITION
```

`INSTALLER` 和 `ZIP` 按 `EDITION` 设置为当前版别文件名；两个签名调用只使用当前 Environment 的通用 secret。删除 `LAN_SIGNING_KEY`、`LAN_SIGNING_PASSWORD`、`ONLINE_SIGNING_KEY`、`ONLINE_SIGNING_PASSWORD` 以及选择密钥的 PowerShell 分支。公钥变量不是秘密，可继续同时注入用于构建。

- [ ] **Step 5: 配置外部 Environment**

1. `release-lan`：通用 `UPDATER_PRIVATE_KEY`、`UPDATER_PRIVATE_KEY_PASSWORD`，值为 LAN 密钥；配置 LAN 公钥变量和必需审批人。
2. `release-online`：同名通用 secret，值为 Online 密钥；配置 Online 公钥变量和必需审批人。
3. `release-android`：keystore、FCM 配置、预期证书 SHA-256。
4. `release-publish`：发布审批，无签名私钥。
5. 迁移完成后从旧 `release` 环境移除全部签名机密。

- [ ] **Step 6: 运行静态测试**

Run: `node --test scripts/tests/release-workflow.test.mjs`
Expected: PASS。

- [ ] **Step 7: 提交**

```bash
git add .github/workflows/release.yml scripts/tests/release-workflow.test.mjs docs/release-runbook.md
git commit -m "security(release): isolate edition signing environments"
```

---

### Task 4: 签名全部资产、验签与幂等发布

**Files:**
- Create: `scripts/verify-release-assets.mjs`
- Create: `scripts/create-release.mjs`
- Modify: `.github/workflows/release.yml`
- Modify: `scripts/make-updater-manifest.mjs`
- Test: `scripts/tests/release-assets.test.mjs`
- Test: `scripts/tests/create-release.test.mjs`

**Interfaces:**
- Produces: `node scripts/verify-release-assets.mjs --release-dir DIR --edition lan|online`。
- Produces: `node scripts/create-release.mjs --tag v1.4.1 --release-dir DIR --notes docs/release-notes/v1.4.1.md`。

- [ ] **Step 1: 添加资产验签红灯测试**

```js
test('rejects unsigned zip, wrong key, renamed asset and tampered sums', async () => {
  const fixture = await makeReleaseFixture();
  await expect(verifyReleaseAssets(fixture.dir, 'lan')).rejects.toThrow(/unsigned|signature|hash|name/i);
  fixture.files.get('LANChat-Desktop-Windows-x64.zip').write('tampered');
  await expect(verifyReleaseAssets(fixture.dir, 'lan')).rejects.toThrow(/hash|signature/i);
});
```

夹具包含微型 EXE、ZIP、manifest、`SHA256SUMS` 和测试签名，由 `makeReleaseFixture()` 调用 Rust 测试辅助模块生成的两套临时密钥。同步扩展 `release-workflow.test.mjs`：`checksums` 无 secret，两个 sign-sums job 只引用通用私钥，`draft.needs` 包含它们。

- [ ] **Step 2: 运行测试确认失败**

Run: `node --test scripts/tests/release-assets.test.mjs`
Expected: FAIL，脚本尚不存在。

- [ ] **Step 3: 实现签名资产清单**

每个桌面 job 使用两次 `sign.mjs` 分别签名安装器和 ZIP，生成：

```text
LANChat-Setup-x64.exe
LANChat-Setup-x64.exe.sig
LANChat-Desktop-Windows-x64.zip
LANChat-Desktop-Windows-x64.zip.sig
latest-lan.json
```

Online 对应 `LANChat-Online-*`。全部 desktop/android/linux artifact 汇总后，新增无密钥 `checksums` job 生成定序 `SHA256SUMS`；随后新增 `sign-sums-lan` 和 `sign-sums-online` 两个短作业，分别使用 `release-lan`/`release-online` Environment 和通用 secret，对同一 `SHA256SUMS` 生成 `SHA256SUMS.lan.sig`、`SHA256SUMS.online.sig`。`draft` job 的 `needs` 增加 `checksums` 和两个 sign-sums job，下载全部签名后再验签和创建草稿。任何资产变化都重新生成清单和两份签名。

- [ ] **Step 4: 实现发布前验签**

`verify-release-assets.mjs` 调用 Rust verifier：

1. 验证安装器、ZIP、Linux 服务端和 `SHA256SUMS` 签名。
2. 验证 `SHA256SUMS` 中每个文件的大小和 SHA-256。
3. 验证 updater manifest 的 version、url 资产名和 size。
4. 验证 EXE 内置公钥与当前版别公钥一致。
5. 验证 Android APK 证书指纹。

- [ ] **Step 5: 实现幂等发布脚本**

`create-release.mjs` 通过 `gh release view` 查询：

- 无 Release：`gh release create --draft --verify-tag`。
- 草稿且 tag commit/资产哈希一致：复用。
- 草稿不一致或已公开：非零退出，不覆盖。

工作流增加：

```yaml
concurrency:
  group: release-${{ github.ref }}
  cancel-in-progress: false
```

- [ ] **Step 6: 运行测试确认通过**

```bash
node --test scripts/tests/release-assets.test.mjs scripts/tests/create-release.test.mjs
node scripts/verify-release-assets.mjs --release-dir release --edition lan
node scripts/verify-release-assets.mjs --release-dir release --edition online
```

Expected: PASS。

- [ ] **Step 7: 提交**

```bash
git add scripts/verify-release-assets.mjs scripts/create-release.mjs scripts/make-updater-manifest.mjs scripts/tests .github/workflows/release.yml
git commit -m "security(release): sign and verify every published asset"
```

---

### Task 5: Android 证书指纹与 Linux 服务端产物

**Files:**
- Modify: `.github/workflows/release.yml`
- Create: `scripts/verify-android-certificate.sh`
- Test: `scripts/tests/android-certificate.test.sh`

**Interfaces:**
- Produces: `LANCHAT_ANDROID_CERT_SHA256` Environment secret/variable。
- Produces: `lan-chat-server-linux-x86_64` Release 资产及签名。

- [ ] **Step 1: 添加指纹红灯测试**

```bash
#!/usr/bin/env bash
set -euo pipefail
apk="$1"
expected="$2"
actual="$(apksigner verify --print-certs "$apk" | awk -F'SHA-256 digest: ' '/SHA-256 digest:/ {gsub(/ /,"",$2); print $2; exit}')"
[[ "$actual" == "$expected" ]]
```

测试构造匹配/不匹配 SHA-256 输入；实现时避免用 `grep` 工具替代项目搜索要求，脚本内使用 `awk` 仅解析 apksigner 输出。

- [ ] **Step 2: 运行测试确认失败**

Run: `bash scripts/tests/android-certificate.test.sh`
Expected: FAIL，脚本尚不存在。

- [ ] **Step 3: 实现 Android 指纹校验**

Release workflow 在 `apksigner verify` 后调用脚本，并从 `release-android` Environment 读取预期 SHA-256；不匹配立即失败，不上传 artifact。

- [ ] **Step 4: 增加 Linux 服务端构建 job**

新增 Ubuntu job：

```bash
cargo build --locked --release -p lan-chat-server
sha256sum target/release/lan-chat-server
```

上传 `lan-chat-server-linux-x86_64`，纳入 `SHA256SUMS` 并由两个桌面 Environment 签名。生产部署使用该 artifact，不在服务器临时编译。

- [ ] **Step 5: 运行验证**

```bash
bash scripts/tests/android-certificate.test.sh
apksigner verify --print-certs android/app/build/outputs/apk/release/app-release.apk
cargo build --locked --release -p lan-chat-server
```

Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add .github/workflows/release.yml scripts/verify-android-certificate.sh scripts/tests/android-certificate.test.sh
git commit -m "security(release): pin Android certificate and publish Linux server"
```

---

### Task 6: CI 漏洞门禁与 Dependabot

**Files:**
- Modify: `.github/workflows/ci.yml`
- Create: `.github/dependabot.yml`
- Create: `scripts/tests/dependency-gates.test.mjs`

**Interfaces:**
- Produces: 独立的 `Supply chain` CI job（job id 为 `supply-chain`）。
- Produces: Cargo、pnpm、Gradle、GitHub Actions Dependabot 配置。

- [ ] **Step 1: 添加 CI 静态红灯测试**

断言 `ci.yml` 包含 `cargo audit --file Cargo.lock`、`android/transport-rust` 审计、`pnpm audit --prod`、`--dependency-verification strict` 和固定 SHA 的 OSV-Scanner Action。

- [ ] **Step 2: 运行测试确认失败**

Run: `node --test scripts/tests/dependency-gates.test.mjs`
Expected: FAIL，当前 CI 无漏洞门禁。

- [ ] **Step 3: 实现 supply-chain job**

```yaml
supply-chain:
  name: Supply chain
  runs-on: ubuntu-latest
  permissions:
    contents: read
    security-events: write
    actions: read
```

后续步骤添加到该 job。job 开始时安装锁定版本的审计工具：

```bash
cargo install cargo-audit --locked
```

```yaml
- run: cargo audit --file Cargo.lock
- run: cargo audit --manifest-path android/transport-rust/Cargo.toml --file android/transport-rust/Cargo.lock
- run: pnpm --dir frontend audit --prod
- run: gradle -p android --no-daemon --dependency-verification strict :app:assembleDebug
- uses: google/osv-scanner-action/osv-scanner-action@32d3cd482bdadfb38b186c0ad1b031902d55d932
```

固定版本 `action.yml` 没有自定义输入，会自动扫描仓库支持的 lockfile；不要添加未定义的 `scan-args`。实施时仍需读取该提交的 `action.yml` 和 README 确认扫描范围，并确保 `Cargo.lock`、`android/transport-rust/Cargo.lock`、`frontend/pnpm-lock.yaml`、`android/gradle.lockfile` 都被纳入。

- [ ] **Step 4: 添加 Dependabot**

```yaml
version: 2
updates:
  - package-ecosystem: cargo
    directory: /
    schedule: { interval: weekly }
  - package-ecosystem: cargo
    directory: /android/transport-rust
    schedule: { interval: weekly }
  - package-ecosystem: npm
    directory: /frontend
    schedule: { interval: weekly }
  - package-ecosystem: npm
    directory: /tools/release-signing
    schedule: { interval: weekly }
  - package-ecosystem: gradle
    directory: /android
    schedule: { interval: weekly }
  - package-ecosystem: github-actions
    directory: /
    schedule: { interval: weekly }
```

- [ ] **Step 5: 运行门禁**

```bash
node --test scripts/tests/dependency-gates.test.mjs
cargo audit --file Cargo.lock
cargo audit --manifest-path android/transport-rust/Cargo.toml --file android/transport-rust/Cargo.lock
pnpm.cmd --dir frontend audit --prod
gradle -p android --no-daemon --dependency-verification strict :app:assembleDebug
```

Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add .github/workflows/ci.yml .github/dependabot.yml scripts/tests/dependency-gates.test.mjs
git commit -m "ci: enforce dependency and action supply-chain gates"
```

---

### Task 7: 发布链文档与测试环境验收

**Files:**
- Modify: `README.md`
- Modify: `docs/release-runbook.md`
- Modify: `docs/release-checklist.md`
- Modify: `docs/release-acceptance.md`

- [ ] **Step 1: 更新文档**

1. 记录四个 Environment、密钥作用域和审批要求。
2. 记录全部签名资产与 `verify-release-assets.mjs` 命令。
3. 记录 Android 证书指纹和 Linux 服务端 artifact。
4. 明确 Tauri CLI 不提供 verify，验签由 Rust 工具完成。
5. 记录 Gradle/AGP 版本和依赖验证命令。

- [ ] **Step 2: 使用测试密钥跑完整发布链**

在测试 Environment 中构建 LAN/Online 微型资产，执行签名、验签、坏钥/损坏/改名测试和幂等发布测试；不得触碰正式私钥或正式 Release。

- [ ] **Step 3: 运行发布链全量门禁**

```bash
cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
node --test tools/release-signing/test/*.test.mjs scripts/tests/release-*.test.mjs scripts/tests/dependency-gates.test.mjs
cargo audit --file Cargo.lock
pnpm.cmd --dir frontend audit --prod
gradle -p android --no-daemon --dependency-verification strict :app:assembleDebug
node scripts/check-version.mjs
```

Expected: 全部 PASS，版本保持 1.4.1。

- [ ] **Step 4: 提交**

```bash
git add README.md docs/release-runbook.md docs/release-checklist.md docs/release-acceptance.md
git commit -m "docs: document verified 1.4.1 release chain"
```
