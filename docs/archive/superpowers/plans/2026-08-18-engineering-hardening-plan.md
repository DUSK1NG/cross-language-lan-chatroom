# 工程化完善实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为跨语言聊天室补充可复现的 CMake 构建、C++ 协议自动化测试、Windows/Ubuntu GitHub Actions CI 和准确的发布文档。

**Architecture:** CMake 只负责组织现有 C++ 源文件和测试目标，不改变生产协议接口。C++ 测试通过 Windows TCP loopback 验证 framing 和 JSON；GitHub Actions 在 Windows 验证完整项目，在 Ubuntu 只验证 Go Server。

**Tech Stack:** CMake 3.20+、C++17、Winsock2、CTest、Go 1.20+、GitHub Actions、MinGW-w64。

**Spec:** `docs/superpowers/specs/2026-08-18-engineering-hardening-design.md`

## Global Constraints

- C++ 客户端保持 Windows 专用，继续使用 Winsock2 和 `ws2_32`。
- Go Server 必须继续通过 `go test ./...`、`go test -race ./...`、`go vet ./...` 和 `go build`。
- TCP framing 继续使用 4-byte big-endian length + UTF-8 JSON，最大 payload 为 64 KiB。
- 保留现有 MinGW 和 MSVC 构建命令，不用 CMake 替换它们。
- C++ 测试使用 loopback TCP，不依赖 Unix `socketpair` 或第三方测试框架。
- 不在本轮增加自动重连、TLS、数据库、GUI、私聊、房间或文件传输。

---

### Task 1: 添加 CMake 构建和 CTest 入口

**Files:**
- Create: `client-cpp/CMakeLists.txt`
- Modify: none
- Test: configure and build commands below

**Interfaces:**
- Produces `chat-client` from `src/main.cpp`, `src/message.cpp`, `src/protocol.cpp`.
- Task 2 will extend the file with the `protocol-tests` target and CTest registration after `tests/protocol_tests.cpp` exists.

- [ ] **Step 1: Create the CMake target definitions**

  The file must set `CMAKE_CXX_STANDARD 17`, add `include/` and `third_party/`, create `chat-client`, and link `ws2_32`. Do not reference the not-yet-created test source or add Linux-only libraries in this task.

- [ ] **Step 2: Configure and build the production target**

  Run from the repository root:

  ```powershell
  cmake -S client-cpp -B client-cpp/build -G "MinGW Makefiles"
  cmake --build client-cpp/build --config Release --target chat-client
  ```

  Expected: CMake configuration and `chat-client.exe` build succeed without requiring the future test source.

- [ ] **Step 3: Verify the existing direct build still works**

  Run from `client-cpp`:

  ```powershell
  g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client-direct.exe -municode -lws2_32
  ```

  Expected: no compiler warnings and a successful executable. Remove this temporary executable after verification.

- [ ] **Step 4: Commit the build entry point**

  ```powershell
  git add client-cpp/CMakeLists.txt
  git commit -m "build: add cmake configuration for cpp client"
  ```

### Task 2: Add Windows loopback protocol tests

**Files:**
- Create: `client-cpp/tests/protocol_tests.cpp`
- Modify: `client-cpp/CMakeLists.txt`
- Test: `client-cpp/tests/protocol_tests.cpp`

**Interfaces:**
- Test executable returns `0` only when every test passes.
- Test helpers create a loopback listener, connect a client socket, and close both sockets on every path.
- Tests call production functions from `protocol` and `message`; production APIs remain unchanged.
- `client-cpp/CMakeLists.txt` gains the `protocol-tests` target, `enable_testing()`, and `add_test(NAME protocol-tests COMMAND protocol-tests)` only after this test source is created.

- [ ] **Step 1: Add a failing test for frame boundary and limits**

  Add cases that assert `send_frame(socket, "") == false`, `send_frame(socket, std::string(kMaxMessageSize + 1, 'x')) == false`, and that a raw big-endian zero or `kMaxMessageSize + 1` header makes `recv_frame` return `false`.

- [ ] **Step 1a: Add the test target after the source exists**

  Extend `client-cpp/CMakeLists.txt` with `add_executable(protocol-tests tests/protocol_tests.cpp src/message.cpp src/protocol.cpp)`, the same include directories and `ws2_32` link, then add `enable_testing()` and `add_test(NAME protocol-tests COMMAND protocol-tests)`.

- [ ] **Step 2: Add a failing test for truncation and malformed JSON**

  Send fewer than four header bytes, a complete header followed by a truncated payload, malformed JSON, JSON without string `type`, and a numeric `content`; assert `receive_message` or `recv_frame` returns `false`.

- [ ] **Step 3: Add a passing UTF-8 message round-trip test**

  Send a `message::Message` with `type = "chat"`, `username = "Alice"`, `user_code = "A001"`, and UTF-8 content `你好，这是测试消息。`; receive it and assert all fields are equal.

- [ ] **Step 4: Add ordered multi-frame coverage**

  Send three valid frames sequentially on one loopback connection and receive them three times. Assert the payloads remain in order, proving the test does not rely on one `recv` call equaling one message.

- [ ] **Step 5: Build and run CTest**

  ```powershell
  cmake -S client-cpp -B client-cpp/build -G "MinGW Makefiles"
  cmake --build client-cpp/build --config Release
  ctest --test-dir client-cpp/build --output-on-failure
  ```

  Expected: `protocol-tests` passes and malformed input is rejected without process crash.

- [ ] **Step 6: Commit the protocol tests**

  ```powershell
  git add client-cpp/tests/protocol_tests.cpp client-cpp/CMakeLists.txt
  git commit -m "test: add cpp protocol loopback coverage"
  ```

### Task 3: Add Windows and Ubuntu GitHub Actions

**Files:**
- Create: `.github/workflows/ci.yml`
- Modify: `.gitignore` only if the workflow creates build directories that should be ignored

**Interfaces:**
- Workflow triggers on `push` and `pull_request`.
- Windows job verifies Go and C++.
- Ubuntu job verifies Go only.

- [ ] **Step 1: Define the Windows job**

  Use `windows-latest`, `actions/checkout`, `actions/setup-go` with Go `1.20.x`, then run:

  ```text
  cd server-go && go test ./...
  cd server-go && go test -race ./...
  cd server-go && go vet ./...
  cd server-go && go build -o chat-server.exe .
  cmake -S client-cpp -B client-cpp/build -G "MinGW Makefiles"
  cmake --build client-cpp/build --config Release
  ctest --test-dir client-cpp/build --output-on-failure
  ```

  Use PowerShell syntax where required by the Windows runner and fail on nonzero exit codes.

- [ ] **Step 2: Define the Ubuntu Go job**

  Use `ubuntu-latest`, `actions/checkout`, `actions/setup-go` with Go `1.20.x`, and run the same four Go commands. Do not attempt to compile `client-cpp` on Ubuntu.

- [ ] **Step 3: Validate the workflow locally as far as possible**

  Run the exact Go and CMake commands locally on Windows, inspect YAML indentation, and verify that all referenced files exist. The remote Actions run is the final CI validation.

- [ ] **Step 4: Commit the workflow**

  ```powershell
  git add .github/workflows/ci.yml .gitignore
  git commit -m "ci: test go server and cpp client"
  ```

### Task 4: Update release documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/github-publishing.md`
- Modify: `docs/testing.md`

**Interfaces:**
- README build commands must match the repository files and CI commands.
- GitHub documentation must link to `https://github.com/DUSK1NG/cross-language-lan-chatroom`.

- [ ] **Step 1: Correct the current repository state**

  Replace the stale statement that the local repository has no `origin` with the current public repository link and a note that `master` tracks `origin/master`.

- [ ] **Step 2: Document CMake and CTest**

  Add Windows commands for `cmake -S client-cpp -B client-cpp/build`, `cmake --build`, and `ctest`; retain the direct MinGW and MSVC commands.

- [ ] **Step 3: Document CI and version scope**

  Add a CI section describing Windows/Ubuntu coverage and mark the current milestone as `v1.0.0`. State that C++ remains Windows-only and Ubuntu validates Go only.

- [ ] **Step 4: Verify Markdown references**

  Run:

  ```powershell
  git diff --check
  rg -n "CMake|CTest|GitHub Actions|cross-language-lan-chatroom|v1\.0\.0" README.md docs\github-publishing.md docs\testing.md
  ```

  Expected: every referenced command and link points to an existing file or repository.

- [ ] **Step 5: Commit the documentation**

  ```powershell
  git add README.md docs/github-publishing.md docs/testing.md
  git commit -m "docs: document ci and cmake builds"
  ```

### Task 5: Full verification and GitHub handoff

**Files:**
- Modify: none unless verification exposes a defect

- [ ] **Step 1: Run the complete local verification**

  ```powershell
  cd server-go
  go test ./...
  go test -race ./...
  go vet ./...
  go build -o chat-server.exe .
  cd ..\client-cpp
  g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp src\message.cpp src\protocol.cpp -Iinclude -Ithird_party -o chat-client-direct.exe -municode -lws2_32
  cmake -S . -B build -G "MinGW Makefiles"
  cmake --build build --config Release
  ctest --test-dir build --output-on-failure
  ```

- [ ] **Step 2: Check repository hygiene**

  ```powershell
  git status --short
  git diff --check
  git log --oneline -5
  ```

  Expected: only intentionally tracked source, test, workflow, and documentation files are present; generated executables and build directories are ignored.

- [ ] **Step 3: Push and inspect GitHub Actions**

  ```powershell
  git push
  gh run list --limit 5
  ```

  Expected: the latest Windows and Ubuntu workflow runs finish successfully. If a run fails, fix the workflow or code before declaring this plan complete.

- [ ] **Step 4: Record completion**

  Add the successful CI run and local verification commands to `docs/testing.md`, then commit the final record with:

  ```powershell
  git add docs/testing.md
  git commit -m "docs: record engineering verification"
  git push
  ```
