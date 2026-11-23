# Stage 2 Length-Framed TCP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Stage 1's unframed one-shot exchange with a 4-byte big-endian length header and reusable `send_all`/`recv_all` logic for raw UTF-8 text frames.

**Architecture:** The Go server and C++ client keep the Stage 1 single-connection flow, but every payload is now sent as `uint32 big-endian length + payload bytes`. The client sends two frames back-to-back before reading two responses; the server reads exactly two frames with `io.ReadFull`, proving that application messages remain separate even when TCP combines them.

**Tech Stack:** Go 1.20-compatible standard library (`encoding/binary`, `io`, `bytes`); C++17; Windows Winsock2; MinGW-w64.

## Global Constraints

- Keep the Stage 1 scope: one client, one TCP connection, raw text only, no JSON, login, Hub, goroutines for client management, or broadcast.
- Use a 4-byte `uint32` length header in network byte order for every message.
- The header length is the UTF-8 payload byte length, not character count and not total frame length.
- `MAX_MESSAGE_SIZE` is 64 KiB; reject zero-length and oversized payloads.
- Go must use `binary.BigEndian` and `io.ReadFull`.
- C++ must use `htonl`, `ntohl`, `send_all`, and `recv_all`.
- The client sends both test frames before reading responses, so the test exercises coalesced TCP data.
- All received data is text only; do not execute remote content or use `system()`.

---

### Task 1: Add Go frame encoding and decoding

**Files:**
- Create: `server-go/protocol.go`
- Create: `server-go/protocol_test.go`

**Interfaces:**
- `writeFrame(w io.Writer, payload []byte) error`
- `readFrame(r io.Reader) ([]byte, error)`
- `maxMessageSize` constant equal to `64 * 1024`

- [ ] **Step 1: Write the failing frame tests**

Create tests using `bytes.Buffer`:

```go
func TestFrameRoundTripKeepsBackToBackMessagesSeparate(t *testing.T) {
	var stream bytes.Buffer
	if err := writeFrame(&stream, []byte("Hello")); err != nil {
		t.Fatal(err)
	}
	if err := writeFrame(&stream, []byte("World")); err != nil {
		t.Fatal(err)
	}

	first, err := readFrame(&stream)
	if err != nil || string(first) != "Hello" {
		t.Fatalf("first frame = %q, err = %v", first, err)
	}
	second, err := readFrame(&stream)
	if err != nil || string(second) != "World" {
		t.Fatalf("second frame = %q, err = %v", second, err)
	}
}
```

Add tests that `writeFrame` rejects a payload larger than `maxMessageSize`, `readFrame` rejects a zero length, and `readFrame` rejects a length greater than `maxMessageSize`.

- [ ] **Step 2: Run the tests and verify the expected failure**

Run from `server-go`:

```powershell
$env:GO111MODULE = "off"
$env:GOCACHE = "C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache"
go test ./...
```

Expected: compilation fails because `writeFrame` and `readFrame` do not yet exist.

- [ ] **Step 3: Implement `writeFrame`**

Use `encoding/binary.BigEndian` to encode a 4-byte `uint32`. Reject payloads larger than `maxMessageSize`. Write the header and payload completely; if an `io.Writer` reports a short write, continue writing the remaining bytes, and return an error for a zero-progress write.

- [ ] **Step 4: Implement `readFrame`**

Read exactly four bytes with `io.ReadFull`, decode the length with `binary.BigEndian.Uint32`, reject zero or a value above `maxMessageSize`, allocate exactly that many bytes, and read the payload with `io.ReadFull`.

- [ ] **Step 5: Run the frame tests and verify they pass**

Run:

```powershell
$env:GO111MODULE = "off"
$env:GOCACHE = "C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache"
gofmt -w protocol.go protocol_test.go
go test ./...
```

Expected: all frame tests pass, including the two back-to-back frames test.

---

### Task 2: Convert the Go server to two length-framed messages

**Files:**
- Modify: `server-go/main.go`

**Interfaces:**
- Uses `readFrame(conn)` and `writeFrame(conn, payload)` from `protocol.go`.
- Receives two frames: `Hello` and `World`.
- Sends two framed responses: `Received: Hello` and `Received: World`.

- [ ] **Step 1: Replace the raw `conn.Read` call**

Remove the Stage 1 fixed buffer read and use:

```go
payload, err := readFrame(conn)
```

Do this in a loop that runs exactly twice. Print each message with its frame number.

- [ ] **Step 2: Replace the raw `conn.Write` call**

For each received payload, create:

```go
response := []byte("Received: " + string(payload))
```

Send it using:

```go
if err := writeFrame(conn, response); err != nil {
	log.Fatalf("failed to write response: %v", err)
}
```

- [ ] **Step 3: Format and build the server**

Run:

```powershell
$env:Path = "C:\Users\jking1\go-sdk\go\bin;$env:Path"
$env:GO111MODULE = "off"
$env:GOCACHE = "C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache"
gofmt -w main.go protocol.go protocol_test.go
go build -o chat-server.exe .
go vet ./...
```

Expected: formatting, build, vet, and tests succeed.

---

### Task 3: Add C++ frame protocol functions

**Files:**
- Create: `client-cpp/include/protocol.hpp`
- Create: `client-cpp/src/protocol.cpp`

**Interfaces:**
- `bool send_all(SOCKET socket_handle, const char* data, std::size_t length);`
- `bool recv_all(SOCKET socket_handle, char* data, std::size_t length);`
- `bool send_frame(SOCKET socket_handle, const std::string& payload);`
- `bool recv_frame(SOCKET socket_handle, std::string& payload);`
- `kMaxMessageSize` equal to `64 * 1024`.

- [ ] **Step 1: Define the protocol interface**

`protocol.hpp` must include Winsock2, `<cstddef>`, `<cstdint>`, and `<string>`. Expose only the four functions and the maximum payload constant.

- [ ] **Step 2: Implement `send_all`**

Loop until all bytes are sent. For every call to `send`, handle `SOCKET_ERROR` as failure and advance by the returned byte count. Use a bounded `int` chunk size when converting `std::size_t` to the Winsock length parameter.

- [ ] **Step 3: Implement `recv_all`**

Loop until the requested byte count is received. Return false on `SOCKET_ERROR` or `recv == 0`. Do not treat one `recv` call as one application message.

- [ ] **Step 4: Implement `send_frame` and `recv_frame`**

`send_frame` must reject an empty or oversized payload, compute its byte length from `std::string::size()`, convert the `uint32_t` length with `htonl`, and call `send_all` for the 4-byte header and payload.

`recv_frame` must call `recv_all` for the 4-byte header, copy it into a `uint32_t`, call `ntohl`, reject zero or a value over `kMaxMessageSize`, resize the output string, and call `recv_all` for the payload.

---

### Task 4: Convert the C++ client and run integration acceptance

**Files:**
- Modify: `client-cpp/src/main.cpp`

**Interfaces:**
- Uses `send_frame` and `recv_frame` from `protocol.hpp`.
- Sends two frames back-to-back: `Hello`, then `World`.
- Receives two framed responses and prints both.

- [ ] **Step 1: Replace the Stage 1 raw send**

Include `protocol.hpp` and send these two payloads before calling `recv_frame`:

```cpp
if (!send_frame(socket_handle, "Hello") ||
    !send_frame(socket_handle, "World")) {
    std::cerr << "Failed to send framed message.\n";
    closesocket(socket_handle);
    WSACleanup();
    return 1;
}
```

- [ ] **Step 2: Replace the Stage 1 raw recv**

Receive two strings with `recv_frame` and print them:

```cpp
for (int i = 0; i < 2; ++i) {
    std::string response;
    if (!recv_frame(socket_handle, response)) {
        std::cerr << "Failed to receive framed response.\n";
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }
    std::cout << "Server replied: " << response << '\n';
}
```

- [ ] **Step 3: Compile the client**

Run from `client-cpp`:

```powershell
g++ -std=c++17 src\main.cpp src\protocol.cpp -Iinclude -o chat-client.exe -lws2_32
```

Expected: the client compiles and links without errors.

- [ ] **Step 4: Run localhost integration**

Start `server-go\chat-server.exe`, then run `client-cpp\chat-client.exe`.

Expected client output:

```text
Connected to 127.0.0.1:8888
Server replied: Received: Hello
Server replied: Received: World
```

Expected server output includes both independent payloads:

```text
Client sent frame 1: Hello
Client sent frame 2: World
```

- [ ] **Step 5: Verify invalid frame lengths**

Use the Go unit tests for zero and oversized lengths. Confirm that the server exits with a protocol error instead of allocating unbounded memory.

- [ ] **Step 6: Commit Stage 2**

After all checks pass:

```powershell
git add .gitignore server-go client-cpp/include/protocol.hpp client-cpp/src/protocol.cpp client-cpp/src/main.cpp docs/superpowers/plans/2026-08-17-stage2-length-framing-plan.md
git commit -m "feat: add stage2 length-framed tcp messages"
```

## Stage 2 Acceptance Checklist

- [ ] The Go frame unit tests pass.
- [ ] The C++ client builds with `-lws2_32`.
- [ ] The header is exactly 4 bytes.
- [ ] Go uses big-endian encoding and `io.ReadFull`.
- [ ] C++ uses `htonl`, `ntohl`, `send_all`, and `recv_all`.
- [ ] Two frames sent back-to-back are recovered as `Hello` and `World`.
- [ ] Responses are also framed and recovered separately.
- [ ] Empty and oversized lengths are rejected.
- [ ] No JSON, login, Hub, broadcast, or multi-client code has been added.

