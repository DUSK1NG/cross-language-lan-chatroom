# GitHub 发布清单

当前公开仓库已上线：<https://github.com/DUSK1NG/cross-language-lan-chatroom>
当前仓库事实：`origin` 指向 `https://github.com/DUSK1NG/cross-language-lan-chatroom.git`；默认发布分支 / 公开仓库主线为 `master`。

## 发布前检查

- [ ] README、协议文档、架构图和测试记录已更新
- [ ] 本地 Go 检查通过：`go test ./...`、`go test -race ./...`、`go vet ./...`
- [ ] 本地构建通过：Go Server、C++ Client（至少完成 MinGW 或 MSVC 构建；如环境允许再跑 CMake / CTest）
- [ ] GitHub Actions 最近一次 Windows job 与 Ubuntu job 均通过
- [ ] 不提交 `*.exe`、构建缓存、日志、访问令牌、密钥文件或私人截图
- [ ] 真实局域网测试截图已脱敏
- [ ] release 说明、版本号和里程碑（当前为 `v1.0.0`）已核对

## 当前仓库建议流程

先确认远程地址和分支状态：

```powershell
git remote -v
git branch -vv
```

本仓库发布前，建议至少完成以下本地检查：

```powershell
cd server-go
go test ./...
go test -race ./...
go vet ./...
go build -o chat-server.exe .

cd ..\client-cpp
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client.exe -municode -lws2_32
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

如果当前环境不具备 MinGW / CMake / Windows GUI 条件，也至少要在可用 Windows 环境中补跑这些检查，再准备发布。

## 推送与发布

确认本地检查完成后，再推送 `master`：

```powershell
git push origin master
```

推送后立刻到 GitHub 查看 Actions：

- Windows：完整验证 Go + C++（含 CMake / CTest）
- Ubuntu：只验证 Go Server

未来所有功能变更、文档更新或 release 候选版本，都应先完成本地检查，再等待 GitHub Actions 通过后再发布。不要跳过远程 CI，也不要把未验证的构建产物标记为 release-ready。

## README 首页建议突出内容

README 首页应持续突出：

1. Go Server + C++ Client 的跨语言结构
2. 4-byte big-endian length + UTF-8 JSON 协议
3. `goroutine/channel` 与 `Winsock2/std::thread`
4. Wi-Fi 与 Ethernet 跨设备测试结果
5. 粘包拆包、异常断线和资源清理测试
