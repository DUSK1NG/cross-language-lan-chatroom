# Stage 9：Ethernet 服务端与 Wi-Fi 客户端局域网测试

## 服务端启动

在 Ethernet 服务端电脑执行：

~~~powershell
cd server-go
go run .
~~~

服务端应显示并监听：

~~~text
listening on 0.0.0.0:8888
~~~

查看监听：

~~~powershell
Get-NetTCPConnection -LocalPort 8888 -State Listen
~~~

## Wi-Fi 客户端测试

在与服务端连接同一个路由器的 Wi-Fi 电脑上，先执行：

~~~powershell
ping 192.168.0.3
Test-NetConnection 192.168.0.3 -Port 8888
~~~

然后运行：

~~~powershell
cd client-cpp
.\chat-client.exe 192.168.0.3 8888 Alice WIFI001
~~~

第二台 Wi-Fi 客户端使用不同的代码：

~~~powershell
.\chat-client.exe 192.168.0.3 8888 Bob WIFI002
~~~

## 验收矩阵

| 编号 | 测试 | 预期结果 |
|---|---|---|
| LAN-1 | Wi-Fi 客户端 Ping `192.168.0.3` | 能到达时通过；Ping 被禁时继续测试 TCP |
| LAN-2 | TCP 8888 测试 | `TcpTestSucceeded: True` |
| LAN-3 | 单客户端登录 | 显示 `username#user_code` |
| LAN-4 | 两个 Wi-Fi 客户端同时登录 | 互相收到上线提示 |
| LAN-5 | Ethernet 服务端发送中文 | Wi-Fi 客户端正确显示 UTF-8 |
| LAN-6 | Wi-Fi 客户端发送中文 | 服务端和其他客户端正确广播 |
| LAN-7 | `/users` | 返回当前在线代码列表 |
| LAN-8 | 一个客户端断开 | 其他客户端收到离线提示，Server 继续运行 |
| LAN-9 | 服务端重启后重新连接 | 新连接可正常登录 |

## Wi-Fi 与 Ethernet 不能通信时

按顺序检查：

1. 客户端连接的是 `192.168.0.3`，不是 Radmin VPN 或 WSL 地址；
2. 两台设备是否在同一网段，例如都为 `192.168.0.x/24`；
3. Wi-Fi 是否为访客网络；
4. 路由器是否开启 AP Isolation / Client Isolation；
5. Windows 网络是否为 Private Network；
6. Go Server 是否允许通过 Private Network；
7. 是否存在 TCP 8888 入站规则；
8. 是否存在 VLAN 或校园网设备隔离。

不要关闭整个 Windows Defender Firewall。应只允许 Go Server 使用 Private Network，或增加限定 TCP 8888 的入站规则。

## Stage 9 验收标准

- 至少一台 Wi-Fi 客户端通过 TCP 连接 Ethernet 服务端；
- 两台客户端可以同时登录并互发中文消息；
- `/users`、上线和离线提示正常；
- 客户端断开后 Server 继续运行；
- 测试记录客户端网卡类型、IPv4、端口检查结果和截图；
