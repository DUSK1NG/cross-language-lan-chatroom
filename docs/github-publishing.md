# GitHub 发布清单

当前本地仓库已经完成 Stage 1–10 的代码和文档准备，但尚未配置 GitHub origin remote。

## 发布前检查

- [ ] README、协议文档、架构图和测试记录已更新；
- [ ] go test ./...、go test -race ./...、go vet ./... 通过；
- [ ] Go Server 和 C++ Client 构建通过；
- [ ] 不提交 *.exe、构建缓存、日志和私人截图；
- [ ] 真实局域网测试截图已脱敏；
- [ ] GitHub 仓库名和可见性已确认。

## 配置远程仓库

创建 GitHub 仓库后，在项目根目录执行：

~~~powershell
git remote add origin https://github.com/DUSK1NG/<repository-name>.git
git push -u origin master
~~~

如果远程仓库已经存在，先使用 git remote -v 检查地址，避免推送到错误仓库。

## 简历展示建议

README 首页应突出：

1. Go Server + C++ Client 的跨语言架构；
2. 4-byte big-endian length + UTF-8 JSON 协议；
3. goroutine/channel 与 Winsock2/std::thread；
4. Wi-Fi 与 Ethernet 跨设备测试结果；
5. 粘包拆包、异常断线和资源清理测试。
