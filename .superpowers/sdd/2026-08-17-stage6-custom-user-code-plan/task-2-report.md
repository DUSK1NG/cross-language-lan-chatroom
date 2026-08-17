# Stage 6 Task 2 Report

Date: 2026-08-17

## Modified files

- `server-go/hub.go`
- `server-go/hub_test.go`

## Summary

Implemented hub-owned user-code reservation and case-insensitive uniqueness checks with persistent used-code tracking, active-code tracking, join/leave system messages, and idempotent send-channel closure. Added tests first, verified they failed, then implemented the minimal hub changes needed to pass.

## TDD evidence

### Failing test run before implementation

Command:

```powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
go test ./...
```

Output:

```text
# _/C_/Users/jking1/Desktop/my-project/chat_X/server-go [_/C_/Users/jking1/Desktop/my-project/chat_X/server-go.test]
.\hub_test.go:20:21: undefined: ErrUserCodeAlreadyUsed
.\hub_test.go:21:63: undefined: ErrUserCodeAlreadyUsed
.\hub_test.go:43:21: undefined: ErrUserCodeAlreadyUsed
.\hub_test.go:44:63: undefined: ErrUserCodeAlreadyUsed
.\hub_test.go:154:13: undefined: RegisterRequest
.\hub_test.go:169:3: unknown field UserCode in struct literal of type Client
.\hub_test.go:170:3: unknown field NormalizedCode in struct literal of type Client
FAIL	_/C_/Users/jking1/Desktop/my-project/chat_X/server-go [build failed]
FAIL
```

## Verification

### Formatting and tests

Command:

```powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
gofmt -w hub.go hub_test.go
go test ./...
```

Output:

```text
ok  	_/C_/Users/jking1/Desktop/my-project/chat_X/server-go	0.270s
```

### Race check and vet

Command:

```powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
go test -race ./...
go vet ./...
```

Output:

```text
ok  	_/C_/Users/jking1/Desktop/my-project/chat_X/server-go	1.283s
```

## Notes / concerns

- Brief requested `Hub.Register` to become `chan RegisterRequest`, but `client.go` was explicitly out of scope and still sends `*Client` directly. To keep the package building without modifying `client.go`, `Hub.Register` was implemented as a compatibility channel that accepts both `RegisterRequest` and legacy `*Client` events, while the new tests and uniqueness logic use `RegisterRequest`.
- Because `client.go` still constructs clients with `newClient(conn, username)` and does not populate `UserCode`/`NormalizedCode` yet, the new uniqueness path is fully exercised by tests now and is ready for Task 3 to wire login data into registration.

## Commit

- `35f7c8e` — `feat: reserve user codes in hub`

---

## Fix round 1 (2026-08-17)

### Additional modified files

- `server-go/client.go`

### Summary

Tightened `Hub.Register` to the required `chan RegisterRequest`, removed the legacy `*Client` bypass and `chan any` type switch, required user-code identity on registration, removed the no-code presence-message fallback, and minimally adapted `client.go` to send `RegisterRequest` and wait for the registration result.

### TDD evidence for fix round 1

Command:

```powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
go test ./...
```

Output:

```text
2026/08/17 17:13:55 client registered: Alice
2026/08/17 17:13:55 client registered: Alice
2026/08/17 17:13:55 client unregistered: Alice
2026/08/17 17:13:55 client registered: Alice
2026/08/17 17:13:55 client registered: Alice
--- FAIL: TestHubRejectsRegistrationWithoutNormalizedCode (0.00s)
    hub_test.go:75: expected registration without normalized code to fail
2026/08/17 17:13:55 client registered: Alice
2026/08/17 17:13:55 client registered: Bob
2026/08/17 17:13:55 client unregistered: Alice
2026/08/17 17:13:55 client registered: Alice
2026/08/17 17:13:55 client registered: Bob
2026/08/17 17:13:55 client registered: Alice
2026/08/17 17:13:55 client unregistered: Alice
FAIL
FAIL	_/C_/Users/jking1/Desktop/my-project/chat_X/server-go	0.286s
FAIL
```

### Verification for fix round 1

Command:

```powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
gofmt -w hub.go hub_test.go client.go
go test ./...
go test -race ./...
go vet ./...
```

Output:

```text
ok  	_/C_/Users/jking1/Desktop/my-project/chat_X/server-go	0.259s
ok  	_/C_/Users/jking1/Desktop/my-project/chat_X/server-go	1.291s
```

### Notes / concerns for fix round 1

- `client.go` is now only minimally adapted to the strict registration request flow. Full wiring of login `UserCode` / `NormalizedCode` into `newClient` remains intentionally deferred to Task 3.
- Until Task 3 lands, live login attempts through `handleConnection` will fail hub registration because the client created by `newClient(conn, username)` still lacks user-code identity. This round keeps the package correct against the tightened Hub contract without broadening scope beyond the allowed minimal adaptation.

### Commit for fix round 1

- `420e837` — `fix: tighten hub register contract`

---

## Fix round 2 (2026-08-17)

### Summary

Wired `loginMessage.UserCode` and its normalized form into `Client` before hub registration, moved registration ahead of writer startup, and kept `login_ok` behind successful registration so real protocol clients with `user_code` can complete login without leaving a blocked writer goroutine on registration failure.

### TDD evidence for fix round 2

Added real login-flow regression coverage in `server-go/hub_test.go`:

- `TestHandleConnectionRegistersLoginUserCodeAndBroadcastsJoin`
- `TestHandleConnectionRejectsDuplicateCodeBeforeLoginOK`

Command:

```powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
go test ./...
```

Output:

```text
2026/08/17 17:26:29 client registered: Alice
2026/08/17 17:26:29 client registered: Alice
2026/08/17 17:26:29 client unregistered: Alice
2026/08/17 17:26:29 client registered: Alice
2026/08/17 17:26:29 client registered: Alice
2026/08/17 17:26:29 client registered: Bob
2026/08/17 17:26:29 client unregistered: Alice
2026/08/17 17:26:29 client registered: Alice
2026/08/17 17:26:29 client registered: Bob
2026/08/17 17:26:29 client registered: Alice
2026/08/17 17:26:29 client unregistered: Alice
2026/08/17 17:26:29 client connected: pipe
2026/08/17 17:26:29 failed to register client Alice: register request requires user code identity
2026/08/17 17:26:29 client disconnected: pipe
--- FAIL: TestHandleConnectionRegistersLoginUserCodeAndBroadcastsJoin (0.00s)
    hub_test.go:194: received message {Type:login_error Username: UserCode: Content:Login failed}, want {Type:system Username: UserCode: Content:Alice#Alex2026 joined the chat}
2026/08/17 17:26:29 client connected: pipe
2026/08/17 17:26:29 failed to register client Alice: register request requires user code identity
2026/08/17 17:26:29 client disconnected: pipe
--- FAIL: TestHandleConnectionRejectsDuplicateCodeBeforeLoginOK (0.00s)
    hub_test.go:230: received message {Type:login_error Username: UserCode: Content:Login failed}, want {Type:system Username: UserCode: Content:Alice#Alex2026 joined the chat}
FAIL
FAIL	_/C_/Users/jking1/Desktop/my-project/chat_X/server-go	0.274s
FAIL
```

### Verification for fix round 2

Command:

```powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
gofmt -w hub.go hub_test.go client.go
go test ./...
go test -race ./...
go vet ./...
```

Output:

```text
ok  	_/C_/Users/jking1/Desktop/my-project/chat_X/server-go	0.272s
ok  	_/C_/Users/jking1/Desktop/my-project/chat_X/server-go	1.275s
```

### Notes / concerns for fix round 2

- `newClient(conn, username)` remains unchanged as required; identity wiring now happens immediately after construction in `handleConnection`.
- This round fixes the real Go protocol login path for valid `user_code` clients and prevents the pre-registration writer goroutine leak by only starting `writePump` after registration succeeds.

### Commit for fix round 2

- `22b45b4` — `fix: wire login user codes before hub register`
