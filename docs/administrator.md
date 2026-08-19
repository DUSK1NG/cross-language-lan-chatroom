# 管理员权限

## 配置远程服务端管理员

启动 Go 服务端时指定管理员用户代码：

```powershell
.\chat-server.exe `
  -cert .\certs\server.crt `
  -key .\certs\server.key `
  -admin-code ALICE001
```

代码比较不区分大小写。管理员必须使用该代码对应的账号登录。

## 管理命令

```text
/kick Bob#BOB001
/mute Bob#BOB001
```

- `/kick`：立即断开目标用户。
- `/mute`：切换禁言状态；再次执行可以解除禁言。
- 被禁言用户仍可接收消息，但发送群聊时会收到错误提示。
- 普通用户执行管理命令会收到 `Administrator permission required`。

## Host 模式

使用 `--host` 启动时，客户端会把自己的临时代码自动作为管理员代码传给本机 Go 服务端，因此房主可以管理其他访客：

```powershell
.\chat-client.exe --host Alice --ca-file ..\certs\server.crt
```

管理员权限只保存在当前服务端进程内，不会把权限写入账号数据库。
