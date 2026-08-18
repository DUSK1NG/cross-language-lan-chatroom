# System Architecture

## Overall architecture

~~~mermaid
flowchart LR
    A["C++ Client A<br/>Winsock2 + std::thread"] -->|"TCP 8888<br/>4-byte big-endian length + JSON"| S["Go Server<br/>net + goroutine + channel"]
    B["C++ Client B<br/>Winsock2 + std::thread"] -->|"TCP 8888"| S
    C["C++ Client C<br/>Winsock2 + std::thread"] -->|"TCP 8888"| S
    S --> H["Hub goroutine<br/>register / unregister / broadcast"]
    H --> W["Per-client writer<br/>Client.Send channel"]
~~~

## Go server concurrency

~~~mermaid
flowchart TD
    L["net.Listen 0.0.0.0:8888"] --> A["Accept loop"]
    A --> C["One connection goroutine"]
    C --> R["Login + readPump"]
    R --> H["Hub channel events"]
    H --> B["Broadcast / users response / unregister"]
    B --> Q["Client.Send"]
    Q --> P["writePump"]
~~~

Hub 是客户端 map、Send channel 和 user_code 集合的唯一所有者，避免多个 goroutine 随意修改共享 map 或向已关闭 channel 写入。

## C++ client concurrency

~~~mermaid
flowchart LR
    M["Main thread<br/>input"] -->|"thread-safe send"| CS["ConnectionState<br/>socket + login state"]
    CS --> N["Current Winsock TCP socket"]
    N --> R["Receive thread<br/>recv frame + parse JSON"]
    R --> RC["Reconnect loop<br/>1/2/4/8/16/30s backoff"]
    RC -->|"login + login_ok"| CS
    R --> O["UTF-8 console output"]
    M --> X["running + reconnect_enabled"]
    R --> X
    X --> J["stop + join + one socket close + WSACleanup"]
~~~

## Network topology

~~~mermaid
flowchart TB
    Router["Router / LAN<br/>192.168.0.1"]
    Ethernet["Ethernet Server<br/>192.168.0.3:8888"]
    WifiA["Wi-Fi Client<br/>192.168.0.108"]
    WifiB["Wi-Fi Client B"]
    Router --- Ethernet
    Router --- WifiA
    Router --- WifiB
~~~

Stage 9 已验证 Ethernet 服务端 192.168.0.3 与 Wi-Fi 客户端 192.168.0.108 可以建立 TCP 连接并完成聊天。
