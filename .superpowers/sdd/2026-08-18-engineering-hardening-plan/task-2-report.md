# Task 2 report

## Changed files

- `client-cpp/tests/protocol_tests.cpp`
- `client-cpp/CMakeLists.txt`

## Test cases

1. `protocol::send_frame(socket, "")` returns `false`.
2. `protocol::send_frame(socket, std::string(protocol::kMaxMessageSize + 1, 'x'))` returns `false`.
3. `protocol::recv_frame` rejects a raw big-endian length header of `0`.
4. `protocol::recv_frame` rejects a raw big-endian length header of `protocol::kMaxMessageSize + 1`.
5. `protocol::recv_frame` rejects fewer than 4 header bytes.
6. `protocol::recv_frame` rejects a complete header followed by truncated payload.
7. `protocol::recv_all` reads a known complete byte sequence into the provided buffer.
8. `message::receive_message` rejects malformed JSON.
9. `message::receive_message` rejects JSON without a string `type`.
10. `message::receive_message` rejects numeric `content`.
11. A valid `message::Message` with `type = "chat"`, `username = "Alice"`, `user_code = "A001"`, and UTF-8 content `u8"你好，这是测试消息。"` round-trips with equal fields.
12. Three valid frames sent sequentially on one connection are received in the same order.

## Commands

```powershell
cmake -S client-cpp -B client-cpp/build -G "MinGW Makefiles"
cmake --build client-cpp/build --config Release
ctest --test-dir client-cpp/build --output-on-failure
C:\msys64\ucrt64\bin\g++.exe -std=c++17 -Wall -Wextra -Iclient-cpp/include -Iclient-cpp/third_party client-cpp/tests/protocol_tests.cpp client-cpp/src/message.cpp client-cpp/src/protocol.cpp -lws2_32 -o client-cpp/build-direct/protocol-tests.exe
.\client-cpp\build-direct\protocol-tests.exe
```

## Outputs

```text
cmake : 无法将“cmake”项识别为 cmdlet、函数、脚本文件或可运行程序的名称。
All 12 protocol tests passed
```

## Commit hash

- Initial Task 2 commit: `9b1ba1b`
- Fix round 1 implementation commit: `6e4d15a`

## Concerns

- `cmake` and `ctest` were unavailable in this environment, so I verified the test target by compiling it directly with `C:\msys64\ucrt64\bin\g++.exe` and running the resulting executable.

## Fix round 1

### Review items addressed

1. Replaced scenario 10 content with the exact UTF-8 string literal `u8"你好，这是测试消息。"` in `client-cpp/tests/protocol_tests.cpp`.
2. Added a direct `protocol::recv_all` coverage case that sends a known byte sequence and verifies the full buffer is received intact.
3. Updated this report to record actual commit hashes instead of the previous summary-only wording.

### Fix round 1 commands

```powershell
cmake -S client-cpp -B client-cpp/build -G "MinGW Makefiles"
cmake --build client-cpp/build --config Release
ctest --test-dir client-cpp/build --output-on-failure
C:\msys64\ucrt64\bin\g++.exe -std=c++17 -Wall -Wextra -Iclient-cpp/include -Iclient-cpp/third_party client-cpp/tests/protocol_tests.cpp client-cpp/src/message.cpp client-cpp/src/protocol.cpp -lws2_32 -o client-cpp/build-direct/protocol-tests.exe
.\client-cpp\build-direct\protocol-tests.exe
```

### Fix round 1 outputs

```text
cmake : 无法将“cmake”项识别为 cmdlet、函数、脚本文件或可运行程序的名称。
All 12 protocol tests passed
```

### Fix round 1 concerns

- `cmake` and `ctest` were still unavailable in this environment during fix round 1, so I could not claim CTest passed.
