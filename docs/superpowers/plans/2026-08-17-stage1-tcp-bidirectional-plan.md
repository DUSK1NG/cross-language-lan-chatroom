# Stage 1 TCP Bidirectional Communication Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the smallest runnable Go TCP server and Windows C++ Winsock2 client so the client sends `Hello` and receives `Received` over localhost.

**Architecture:** The Go server listens on `0.0.0.0:8888`, accepts one TCP connection, reads one plain-text message, and sends one plain-text response. The C++ client initializes Winsock2, connects to a server IP and port, sends `Hello`, receives the response, prints it, and exits. Stage 1 intentionally uses one `send`/`recv` exchange and does not include JSON, length framing, username login, goroutines for client management, or broadcast.

**Tech Stack:** Go standard library `net`; C++17; Windows Winsock2; MinGW-w64 or MSVC; PowerShell for local verification.

## Global Constraints

- The server must listen on `0.0.0.0:8888` so later LAN tests can use the server's real IPv4 address.
- The client must use `SOCKET`, `WSAStartup`, `connect`, `send`, `recv`, `closesocket`, and `WSACleanup`.
- The client must link against Winsock2 with `-lws2_32` under MinGW or `Ws2_32.lib` under MSVC.
- Stage 1 payloads are plain UTF-8 text: client sends `Hello`, server replies `Received`.
- Do not add JSON, a 4-byte length header, username management, Hub, broadcast, or multiple-client behavior in this stage.
- The first acceptance test uses `127.0.0.1:8888`; LAN testing is only a follow-up smoke test after localhost passes.
- Any remote data is treated as text only; do not use `system()` or execute commands.

---

### Task 1: Create the Go server module and one-message TCP handler

**Files:**
- Create: `server-go/go.mod`
- Create: `server-go/main.go`

**Interfaces:**
- Produces a TCP listener on `0.0.0.0:8888`.
- Accepts one connection at a time for Stage 1.
- Reads up to 1024 bytes, trims surrounding whitespace for display, and writes the literal response `Received`.

- [ ] **Step 1: Create the Go module declaration**

Create `server-go/go.mod` with module path `cross-language-lan-chat/server-go` and a current Go language directive supported by the installed Go toolchain.

- [ ] **Step 2: Write the minimal server entry point**

`server-go/main.go` must:

1. call `net.Listen("tcp", "0.0.0.0:8888")`;
2. fail clearly if the port cannot be opened;
3. defer listener closure;
4. call `Accept()`;
5. defer connection closure;
6. read one request into a bounded buffer;
7. print the received text;
8. write the bytes of `Received`;
9. return after the first client exchange.

The code must not use JSON, framing, shared maps, or command execution.

- [ ] **Step 3: Format and compile the server**

Run from `server-go`:

```powershell
gofmt -w main.go
go build -o chat-server.exe .
```

Expected: `chat-server.exe` is created without compiler errors.

- [ ] **Step 4: Run the server manually**

Run:

```powershell
./chat-server.exe
```

Expected: the process waits for a TCP client and does not immediately exit.

---

### Task 2: Create the C++17 Winsock2 client

**Files:**
- Create: `client-cpp/src/main.cpp`

**Interfaces:**
- Starts with `WSAStartup(MAKEWORD(2, 2), ...)`.
- Connects to `127.0.0.1` on port `8888` by default.
- Sends the literal UTF-8 text `Hello`.
- Receives the server response into a bounded buffer.
- Prints the response and closes all resources.

- [ ] **Step 1: Write the client startup and socket initialization**

`client-cpp/src/main.cpp` must include:

```cpp
#include <winsock2.h>
#include <ws2tcpip.h>
```

The program must:

1. call `WSAStartup`;
2. create a `SOCKET` with `socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)`;
3. convert the server IP with `inet_pton` or an equivalent Winsock-safe API;
4. convert port `8888` using `htons`;
5. call `connect`;
6. report `WSAGetLastError()` when a Winsock operation fails.

- [ ] **Step 2: Implement the one-message exchange**

After `connect` succeeds, send exactly:

```text
Hello
```

Use `send` and check its return value. Then call `recv` once into a fixed-size buffer, append a null terminator only within the valid received range, and print the response. Stage 1 intentionally does not implement `send_all`, `recv_all`, or a length header; those belong to Stage 2.

- [ ] **Step 3: Implement cleanup on every exit path**

The client must close the socket with `closesocket`, call `WSACleanup`, and return a nonzero exit code for startup, socket, connect, send, or receive failures. The successful path must print a clear result such as:

```text
Connected to 127.0.0.1:8888
Server replied: Received
```

- [ ] **Step 4: Compile the client with MinGW-w64**

Run from `client-cpp`:

```powershell
g++ -std=c++17 src\main.cpp -o chat-client.exe -lws2_32
```

Expected: `chat-client.exe` is created without linker errors.

For MSVC Developer Command Prompt, use:

```text
cl /std:c++17 /EHsc src\main.cpp Ws2_32.lib
```

---

### Task 3: Run the localhost acceptance test

**Files:**
- Test only: `server-go/chat-server.exe`, `client-cpp/chat-client.exe`

**Interfaces:**
- Server address: `127.0.0.1`.
- Server port: `8888`.
- Request: `Hello`.
- Response: `Received`.

- [ ] **Step 1: Start the server in Terminal A**

Run:

```powershell
cd server-go
./chat-server.exe
```

Expected: the server waits for one client.

- [ ] **Step 2: Start the client in Terminal B**

Run:

```powershell
cd client-cpp
./chat-client.exe
```

Expected client output contains:

```text
Connected to 127.0.0.1:8888
Server replied: Received
```

Expected server output contains the received text `Hello` and then the server exits because Stage 1 handles one exchange.

- [ ] **Step 3: Verify failure behavior**

Stop the server, run the client again, and verify it reports a connection failure instead of crashing. Start another unrelated listener on port 8888 if needed and verify the Go server reports that the port is unavailable.

- [ ] **Step 4: Verify source formatting and repository state**

Run:

```powershell
cd server-go
gofmt -d main.go
go vet ./...
cd ..
git status --short
```

Expected: `gofmt -d` prints no changes, `go vet` succeeds, and only Stage 1 files plus this plan are new or modified.

- [ ] **Step 5: Commit the Stage 1 implementation**

After the localhost test passes:

```powershell
git add server-go client-cpp/src/main.cpp docs/superpowers/plans/2026-08-17-stage1-tcp-bidirectional-plan.md
git commit -m "feat: add stage1 tcp hello client and server"
```

## Stage 1 Acceptance Checklist

- [ ] `go build` succeeds in `server-go`.
- [ ] MinGW-w64 or MSVC builds the C++ client and links Winsock2 successfully.
- [ ] Server listens on `0.0.0.0:8888`.
- [ ] Client connects to `127.0.0.1:8888`.
- [ ] Client sends `Hello`.
- [ ] Server prints `Hello`.
- [ ] Server replies `Received`.
- [ ] Client prints `Server replied: Received`.
- [ ] Client handles connection failure without crashing.
- [ ] Socket and Winsock resources are released.
- [ ] No JSON, length header, Hub, broadcast, or remote command execution has been added.

