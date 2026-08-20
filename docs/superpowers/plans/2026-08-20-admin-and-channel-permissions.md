# 管理员菜单与频道权限 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让房主/管理员能够在 GUI 中可靠执行禁言、踢出和撤回，并让频道创建者管理公开或私密频道的加入权限。

**Architecture:** Go Hub 继续作为用户、频道定义与在线成员集合的唯一拥有者。协议增加结构化的在线成员和频道描述；Qt Worker 只传递服务器字段，Controller 保存模型，QML 根据服务端给出的权限显示菜单。所有实际授权都在 Hub 中再次判断。

**Tech Stack:** Go 1.25、TLS/TCP、4-byte big-endian frame、UTF-8 JSON、C++20、Qt 6/QML、CMake/CTest。

**Spec:** `docs/superpowers/specs/2026-08-20-admin-and-channel-permissions-design.md`

## Global Constraints

- 保持 `0.0.0.0:8888`、TLS 和 4-byte 大端长度头协议不变。
- 用户代码必须继续忽略大小写且全局唯一；Hub 仍是共享状态的唯一写入者。
- 私钥、证书、数据库和聊天记录不得进入公开发布包。
- 不增加文件传输、远程命令执行或公网 NAT 穿透。
- Windows 11 + Qt 6.11 MinGW 64-bit 是 GUI 验收环境。

---

### Task 1: 定义结构化成员与频道协议

**Files:**
- Modify: `server-go/message.go`
- Modify: `server-go/message_test.go`
- Modify: `client-cpp/include/protocol.hpp`
- Modify: `client-cpp/src/protocol.cpp`
- Modify: `client-cpp/gui/src/gui_connection_worker.cpp`

**Interfaces:**
- Produces: Go `OnlineUser`、`RoomInfo`，以及 `Message.UserDetails []OnlineUser`、`Message.RoomDetails []RoomInfo`。
- Produces: C++ `protocol::OnlineUser`、`protocol::RoomInfo`，供 Worker 转为 Qt `QStringList`/角色数据。

- [ ] **Step 1: 写 Go 协议失败测试**

在 `server-go/message_test.go` 增加 JSON round-trip：

```go
message := Message{Type: "users_response", UserDetails: []OnlineUser{{
    Username: "Alice", UserCode: "A001", Room: "lobby", IsAdmin: true,
}}}
```

断言解码后 `UserDetails[0].IsAdmin` 为 `true`；再对 `rooms_response` 断言 `RoomDetails` 的 `OwnerCode`、`Private` 与 `CanManage`。

- [ ] **Step 2: 运行失败测试**

Run: `cd server-go; go test -run 'Test.*(UserDetails|RoomDetails)' ./...`

Expected: FAIL，因为类型和 JSON 字段尚不存在。

- [ ] **Step 3: 在 Go Message 中加入类型与校验**

在 `message.go` 添加：

```go
type OnlineUser struct {
    Username string `json:"username"`
    UserCode string `json:"user_code"`
    Room     string `json:"room"`
    IsAdmin  bool   `json:"is_admin"`
}

type RoomInfo struct {
    Name      string `json:"name"`
    OwnerCode string `json:"owner_code,omitempty"`
    Private   bool   `json:"private"`
    CanManage bool   `json:"can_manage"`
}
```

为 `users_response`、`rooms_response` 验证每个名称、代码和频道名；保留旧的 `Users` 与 `Rooms` 字段，以避免旧客户端立即失效。

- [ ] **Step 4: 在 C++ 协议层对齐字段**

在 `protocol.hpp/.cpp` 为消息 JSON 读写增加对应结构；未知字段保持忽略。Worker 从 `Message` 读取结构化成员和频道并传给 Controller，不通过 `Name#Code@Room` 字符串解析管理员状态。

- [ ] **Step 5: 运行协议测试**

Run: `cd server-go; go test ./...`

Expected: PASS；旧登录、聊天和长度帧测试不回归。

- [ ] **Step 6: 提交**

```powershell
git add -- server-go/message.go server-go/message_test.go client-cpp/include/protocol.hpp client-cpp/src/protocol.cpp client-cpp/gui/src/gui_connection_worker.cpp
git commit -m "feat: add member and room permission metadata"
```

### Task 2: 完成 Hub 的管理员与频道数据模型

**Files:**
- Modify: `server-go/hub.go`
- Modify: `server-go/client.go`
- Modify: `server-go/hub_test.go`

**Interfaces:**
- Consumes: `OnlineUser`、`RoomInfo`。
- Produces: `RoomDefinition`、`RoomDefinitions`、`RoomMembers` 和 `RoomActionRequest`。

- [ ] **Step 1: 写 Hub 失败测试**

在 `hub_test.go` 增加以下场景：

```go
func TestPrivateRoomRejectsUninvitedMember(t *testing.T) { /* Alice 创建私密频道，Bob join 得到 error */ }
func TestRoomOwnerCanInviteMember(t *testing.T) { /* invite 后 Bob join 成功 */ }
func TestRegularMemberCannotManageRoom(t *testing.T) { /* Bob room_action 得到 Administrator permission required */ }
func TestAdminActionRejectsAnotherAdmin(t *testing.T) { /* Alice 不能 kick 第二位管理员 */ }
```

测试使用 `hub.handle...` 或 channel 驱动 Hub，并断言目标以外的客户端没有收到私密频道系统消息。

- [ ] **Step 2: 运行失败测试**

Run: `cd server-go; go test -run 'Test(PrivateRoom|RoomOwner|RegularMemberCannotManageRoom|AdminActionRejectsAnotherAdmin)' ./...`

Expected: FAIL，因为频道定义、邀请和权限检查尚不存在。

- [ ] **Step 3: 引入独立频道状态**

在 `hub.go` 添加：

```go
type RoomDefinition struct {
    Name       string
    OwnerCode  string
    Private    bool
    Allowed    map[string]struct{}
}
```

将现有 `Rooms` 重命名为 `RoomMembers`；新增 `RoomDefinitions map[string]*RoomDefinition`，在 `NewHub` 初始化公开的 `lobby`。频道只由 Hub goroutine 修改。

- [ ] **Step 4: 增加频道操作与服务端授权**

增加 `room_create` 和 `room_action` 消息类型。`room_action` 的 `Content` 只能是 `invite`、`remove_member`、`set_private` 或 `set_public`；调用者必须是频道所有者或全局管理员。`room_join` 在移动用户前调用 `canJoinRoom`；拒绝时仅向请求者投递 `error`。

- [ ] **Step 5: 补齐现有全局管理员保护**

在 `handleAdminAction` 中拒绝对另一位 `IsAdmin` 客户端执行 `kick`/`mute`；继续允许管理员撤回任意带 `MessageID` 的消息。所有结果发给操作者与必要目标，不向无关频道广播。

- [ ] **Step 6: 发送结构化刷新响应**

`handleRequestUsers` 填充 `UserDetails`；`handleRequestRooms` 按请求者身份填充 `RoomDetails.CanManage`。同时保留旧字符串数组字段。

- [ ] **Step 7: 运行 Go 全量测试**

Run: `cd server-go; gofmt -w message.go hub.go client.go hub_test.go; go test ./...`

Expected: PASS。

- [ ] **Step 8: 提交**

```powershell
git add -- server-go/hub.go server-go/client.go server-go/hub_test.go server-go/message.go
git commit -m "feat: enforce channel ownership and access rules"
```

### Task 3: 在 Qt Worker 与 Controller 暴露频道权限

**Files:**
- Modify: `client-cpp/gui/src/gui_connection_worker.hpp`
- Modify: `client-cpp/gui/src/gui_connection_worker.cpp`
- Modify: `client-cpp/gui/src/gui_chat_controller.hpp`
- Modify: `client-cpp/gui/src/gui_chat_controller.cpp`

**Interfaces:**
- Consumes: C++ `OnlineUser`、`RoomInfo`。
- Produces: QML `memberModel` 的 `admin` 角色与 `roomModel` 的 `private`、`canManage`、`ownerCode` 角色。
- Produces: `Q_INVOKABLE createRoom(QString,bool)`、`roomAction(QString,QString,QString)`。

- [ ] **Step 1: 写 Controller 模型失败测试或最小编译检查**

若当前 GUI 没有 Qt 单元测试目标，新增纯 C++ helper 测试，输入两个 `OnlineUser` 与两个 `RoomInfo`，断言映射角色包含 `admin=true`、`private=true` 与 `canManage=true`。

- [ ] **Step 2: 运行失败检查**

Run: `cmake --build client-cpp/gui/build --parallel 2`

Expected: 编译失败或新 helper 测试失败，因为新 Worker 信号和 Controller 方法尚不存在。

- [ ] **Step 3: 扩展 Worker 信号和请求**

新增 `createRoom(room, isPrivate)`、`sendRoomAction(action, room, targetCode)` 槽，并用现有线程安全发送路径写入 `room_create` / `room_action`。`messageReceived` 传递结构化成员与频道数据，保持网络线程不直接修改 QML 模型。

- [ ] **Step 4: 映射服务端权限到模型**

Controller 更新 `kMemberRoles`、`kRoomRoles`，并在 `handleMessage` 的 `users_response`/`rooms_response` 分支用结构化字段构造模型。删除对管理员一律写死 `false` 的逻辑。

- [ ] **Step 5: 构建与测试**

Run: `cmake -S client-cpp/gui -B client-cpp/gui/build -G Ninja; cmake --build client-cpp/gui/build --parallel 2; ctest --test-dir client-cpp/gui/build --output-on-failure`

Expected: GUI 构建成功，已有测试全部通过。

- [ ] **Step 6: 提交**

```powershell
git add -- client-cpp/gui/src/gui_connection_worker.hpp client-cpp/gui/src/gui_connection_worker.cpp client-cpp/gui/src/gui_chat_controller.hpp client-cpp/gui/src/gui_chat_controller.cpp
git commit -m "feat: expose channel permissions to Qt models"
```

### Task 4: 完善成员和频道管理界面

**Files:**
- Modify: `client-cpp/gui/qml/components/UserProfilePopup.qml`
- Create: `client-cpp/gui/qml/components/ChannelSettingsPopup.qml`
- Modify: `client-cpp/gui/qml/pages/ChatPage.qml`
- Modify: `client-cpp/gui/qml/components/RoomItem.qml`

**Interfaces:**
- Consumes: `chatController.admin`、`roomModel.canManage`、`roomModel.private`、`chatController.roomAction()`。
- Produces: 中文确认弹窗、创建频道访问级别选择和频道设置入口。

- [ ] **Step 1: 写 QML 交互验收清单**

在 `docs/testing.md` 记录：普通成员、房主、频道所有者分别点击成员和频道设置时应显示的按钮；所有取消操作不发送帧。

- [ ] **Step 2: 改造成员资料弹窗**

在 `UserProfilePopup.qml`：

```qml
visible: root.canAdmin && !root.admin && !root.selfUser
onClicked: confirmAction.open()
```

确认后才调用 `chatController.sendAdminAction("mute", root.userCode)` 或 `"kick"`；对其他管理员和自己隐藏全局管理按钮。

- [ ] **Step 3: 创建频道设置弹窗**

`ChannelSettingsPopup.qml` 接收 `roomName`、`isPrivate`、`canManage` 与在线成员模型。频道所有者/全局管理员可点“邀请成员”“移除成员”“设为私密”“设为公开”；普通成员只读显示频道类型。

- [ ] **Step 4: 扩展新建频道对话框和侧边栏**

在 `ChatPage.qml` 新建频道时增加“公开频道/私密频道”选择，确认时调用 `chatController.createRoom(roomName, privateCheck.checked)`。频道标题设置按钮仅在当前 `roomModel.canManage` 为真时可用；`RoomItem.qml` 用锁形图标或“私密”文本标记私密频道。

- [ ] **Step 5: 本地 GUI 构建**

Run: `cmake --build client-cpp/gui/build --parallel 2`

Expected: QML 无模块、属性或语法错误；`lan-chat-gui.exe` 生成成功。

- [ ] **Step 6: 提交**

```powershell
git add -- client-cpp/gui/qml/components/UserProfilePopup.qml client-cpp/gui/qml/components/ChannelSettingsPopup.qml client-cpp/gui/qml/pages/ChatPage.qml client-cpp/gui/qml/components/RoomItem.qml docs/testing.md
git commit -m "feat: add channel permission controls to GUI"
```

### Task 5: 端到端验收与文档更新

**Files:**
- Modify: `README.md`
- Modify: `docs/administrator.md`
- Modify: `docs/protocol.md`
- Modify: `docs/testing.md`

**Interfaces:**
- Consumes: 已完成的 Go 协议、Hub 权限与 Qt GUI。
- Produces: 面向房主和成员的中文权限说明与可复现验收步骤。

- [ ] **Step 1: 运行自动化检查**

Run:

```powershell
cd server-go
go test ./...
cd ..\client-cpp\gui
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Expected: 所有命令成功。

- [ ] **Step 2: 运行双客户端 localhost 验收**

房主以 A001 创建本地聊天室；Bob 以 B001 加入。依次验证：禁言/解禁、踢出、公开频道加入、私密频道拒绝、邀请后加入、频道成员移除、普通成员权限拒绝、管理员撤回。

- [ ] **Step 3: 更新中文文档**

README 功能列表注明“房主全局管理、频道所有者权限”。`docs/administrator.md` 说明管理员代码和频道所有者区别；`docs/protocol.md` 记录新消息类型和服务端授权规则；`docs/testing.md` 记录上述验收步骤与预期结果。

- [ ] **Step 4: 最终差异检查**

Run: `git diff --check; git status --short`

Expected: 无空白错误；只出现本功能的源码、测试与文档改动。

- [ ] **Step 5: 提交**

```powershell
git add -- README.md docs/administrator.md docs/protocol.md docs/testing.md
git commit -m "docs: document moderator and channel permissions"
```
