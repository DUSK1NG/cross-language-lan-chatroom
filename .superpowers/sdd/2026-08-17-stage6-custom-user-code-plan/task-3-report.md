# Task 3 Report

## Summary

Implemented the Go login-flow identity binding updates in `server-go/client.go` and added focused regression coverage in `server-go/client_test.go`.

Behavior now implemented:

- `login_ok` includes `username` and `user_code`
- duplicate user-code registration returns `login_error` with `User code already exists`
- chat messages are rebound to the authenticated connection identity before broadcast
- if `login_ok` fails to send after registration, the handler triggers hub unregister cleanup
- writer send failures now trigger hub unregister before closing the connection

## Files changed

- `server-go/client.go`
- `server-go/client_test.go`

## TDD notes

Added failing tests first:

- `TestHandleConnectionUsesBoundIdentity`
- `TestHandleConnectionReturnsDuplicateCodeError`

Verified they failed before implementation for the expected reasons:

- missing identity fields in `login_ok`
- duplicate user-code error returned `Login failed`

After the implementation changes, both targeted tests pass.

## Verification results

Environment used:

```powershell
$env:Path = 'C:\Users\jking1\go-sdk\go\bin;' + $env:Path
$env:GO111MODULE = 'off'
$env:GOCACHE = 'C:\Users\jking1\Desktop\my-project\chat_X\server-go\.go-build-cache'
```

### `gofmt -w client.go client_test.go`

- PASS

### `go test -run 'TestHandleConnectionUsesBoundIdentity|TestHandleConnectionReturnsDuplicateCodeError'`

- PASS

### `go test ./...`

- FAIL

Failure source:

- existing `server-go/hub_test.go`
  - `TestHandleConnectionRegistersLoginUserCodeAndBroadcastsJoin`
  - `TestHandleConnectionRejectsDuplicateCodeBeforeLoginOK`

Observed mismatch:

- those older tests still expect the pre-Task-3 `login_ok` payload without `username`/`user_code`
- Task 3 brief requires:

```json
{
  "type": "login_ok",
  "username": "Alex",
  "user_code": "Alex2026",
  "content": "Login successful"
}
```

### `go test -race ./...`

- FAIL

Same protocol-expectation failures from `server-go/hub_test.go`.

### `go vet ./...`

- PASS

## Constraints / concerns

1. The brief says `newClient` must become:

```go
func newClient(conn net.Conn, username, userCode, normalizedCode string) *Client
```

but `newClient` currently lives in `server-go/hub.go`, and this task explicitly restricted edits to `server-go/client.go` and `server-go/client_test.go`. I therefore implemented the required behavior without changing `hub.go`.

2. Full verification is currently blocked by stale expectations in `server-go/hub_test.go`, which was outside the allowed edit set.

## Commit status

Local changes are ready, but full test/race verification is not clean because of the outdated `hub_test.go` expectations above.

---

## Follow-up after brief update

The Task 3 brief was updated to allow:

- `server-go/hub.go` updates for `newClient`
- `server-go/hub_test.go` updates for the two existing connection tests

I completed those follow-up changes:

- changed `newClient` to `func newClient(conn net.Conn, username, userCode, normalizedCode string) *Client`
- updated `handleConnection` to construct the fully populated client via `newClient(...)`
- updated the two existing connection tests in `hub_test.go` so `login_ok` expects `Username` and `UserCode`
- updated the duplicate-code connection test to expect `User code already exists`

## Final verification

### `gofmt -w client.go client_test.go hub.go hub_test.go`

- PASS

### `go test -run 'TestHandleConnectionRegistersLoginUserCodeAndBroadcastsJoin|TestHandleConnectionRejectsDuplicateCodeBeforeLoginOK'`

- PASS

### `go test ./...`

- PASS

Output:

```text
ok  	_/C_/Users/jking1/Desktop/my-project/chat_X/server-go	0.273s
```

### `go test -race ./...`

- PASS

Output:

```text
ok  	_/C_/Users/jking1/Desktop/my-project/chat_X/server-go	1.277s
```

### `go vet ./...`

- PASS

## Final concerns

- none
