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
