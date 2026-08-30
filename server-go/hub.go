package main

import (
	"database/sql"
	"errors"
	"log"
	"net"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

// 上线提示和用户列表可能在登录后连续到达，缓冲区不能过小，
// 否则正常的短时消息突发会被误判为慢客户端并强制断开。
const clientSendBufferSize = 256

var ErrUserCodeAlreadyUsed = errors.New("user code already exists")
var errRegisterRequestMissingIdentity = errors.New("register request requires user code identity")

const defaultRoomName = "lobby"

func isLegacyGeneratedMessageID(messageID string) bool {
	if messageID == "" {
		return false
	}
	_, err := strconv.ParseUint(messageID, 10, 64)
	return err == nil
}

type RegisterRequest struct {
	Client *Client
	Result chan error
}

type OutboundMessage struct {
	Client  *Client
	Message Message
}

// PrivateMessageRequest 是客户端提交给 Hub 的私聊请求。
// Sender 由当前 TCP 连接绑定，不能由客户端消息中的身份字段替代。
type PrivateMessageRequest struct {
	Sender     *Client
	TargetCode string
	Content    string
	MessageID  string
}

type MLSKeyPackagePublishRequest struct {
	Sender     *Client
	KeyPackage string
	CommandID  string
}

type MLSKeyPackageFetchRequest struct {
	Sender     *Client
	TargetCode string
	Room       string
	CommandID  string
}

type MLSGroupCommitRequest struct {
	Sender    *Client
	GroupID   string
	Room      string
	Epoch     uint64
	Commit    string
	CommandID string
}

type MLSGroupWelcomeRequest struct {
	Sender     *Client
	GroupID    string
	Room       string
	Epoch      uint64
	TargetCode string
	Welcome    string
	CommandID  string
}

type RoomRequest struct {
	Client *Client
	Room   string
}

type HistoryRequest struct {
	Client          *Client
	Room            string
	TargetCode      string
	Private         bool
	BeforeMessageID string
	SearchQuery     string
	Limit           int
}

// historyJob contains only immutable request data needed by the SQLite worker.
// The worker must not read or mutate Hub state directly.
type historyJob struct {
	Store           *AuthStore
	Client          *Client
	Room            string
	TargetCode      string
	Private         bool
	BeforeMessageID string
	SearchQuery     string
	Limit           int
	Query           HistoryQuery
}

type historyResult struct {
	Job  historyJob
	Page HistoryPage
	Err  error
}

// RoomDefinition 保存频道的服务端权限状态。只有 Hub goroutine 可以修改它。
type RoomDefinition struct {
	Name      string
	OwnerCode string
	Private   bool
	Allowed   map[string]bool
}

type RoomCreateRequest struct {
	Client  *Client
	Room    string
	Private bool
}

type RoomActionRequest struct {
	Sender     *Client
	Action     string
	Room       string
	TargetCode string
}

type AdminActionRequest struct {
	Sender     *Client
	Action     string
	TargetCode string
	MessageID  string
	CommandID  string
}

// ConnectionApprovalRequest represents one live member connection waiting for
// a room owner decision. It is intentionally in-memory only: approving a
// connection must never grant a device a permanent pass.
type ConnectionApprovalRequest struct {
	Username       string
	UserCode       string
	NormalizedCode string
	RemoteAddress  string
	Created        chan string
	Decision       chan ConnectionApprovalDecision
}

type ConnectionApprovalDecision struct {
	Approved bool
	Reason   string
}

type pendingConnectionApproval struct {
	ID             string
	Username       string
	UserCode       string
	NormalizedCode string
	RemoteAddress  string
	CreatedAt      time.Time
	Decision       chan ConnectionApprovalDecision
}

// MessageRecord is the minimum server-side state needed to authorize a recall.
// It is accessed only by the Hub goroutine.
type MessageRecord struct {
	AuthorCode       string
	Recipients       map[string]bool
	OfflineMessageID string
	Recalled         bool
}

// Client 表示一个已经完成登录的客户端连接。
type Client struct {
	Conn           net.Conn
	Username       string
	UserCode       string
	NormalizedCode string
	AccountBacked  bool
	IsAdmin        bool
	Muted          bool
	Room           string
	Send           chan Message
	inboundLimiter inboundRateLimiter

	closeOnce            sync.Once
	closeSendOnce        sync.Once
	disconnectAfterFlush atomic.Bool
}

func newClient(conn net.Conn, username, userCode, normalizedCode string) *Client {
	return &Client{
		Conn:           conn,
		Username:       username,
		UserCode:       userCode,
		NormalizedCode: normalizedCode,
		Room:           defaultRoomName,
		Send:           make(chan Message, clientSendBufferSize),
		inboundLimiter: newInboundRateLimiter(time.Now()),
	}
}

func (c *Client) allowInboundMessage(now time.Time) bool {
	return c != nil && c.inboundLimiter.allow(now)
}

func (c *Client) closeConnection() {
	if c == nil || c.Conn == nil {
		return
	}

	c.closeOnce.Do(func() {
		_ = c.Conn.Close()
	})
}

func (c *Client) closeSend() {
	if c == nil || c.Send == nil {
		return
	}

	c.closeSendOnce.Do(func() {
		close(c.Send)
	})
}

// Hub 是聊天室中客户端集合和广播消息的唯一管理者。
// Clients、ActiveCodes 和 UsedCodes 只能由 Run goroutine 访问。
type Hub struct {
	Clients                    map[*Client]bool
	Register                   chan RegisterRequest
	Unregister                 chan *Client
	Broadcast                  chan Message
	Outbound                   chan OutboundMessage
	RequestUsers               chan *Client
	Private                    chan PrivateMessageRequest
	MLSKeyPackagePublish       chan MLSKeyPackagePublishRequest
	MLSKeyPackageFetch         chan MLSKeyPackageFetchRequest
	MLSGroupCommit             chan MLSGroupCommitRequest
	MLSGroupWelcome            chan MLSGroupWelcomeRequest
	RoomJoin                   chan RoomRequest
	RoomCreate                 chan RoomCreateRequest
	RoomAction                 chan RoomActionRequest
	RoomLeave                  chan *Client
	RequestRooms               chan *Client
	History                    chan HistoryRequest
	HistoryJobs                chan historyJob
	HistoryResults             chan historyResult
	AdminAction                chan AdminActionRequest
	ConnectionApproval         chan ConnectionApprovalRequest
	CancelConnectionApproval   chan string
	ActiveCodes                map[string]*Client
	UsedCodes                  map[string]struct{}
	Rooms                      map[string]map[*Client]bool
	RoomNames                  map[string]struct{}
	RoomDefinitions            map[string]*RoomDefinition
	OfflineStore               *AuthStore
	AdminCode                  string
	NextMessageID              uint64
	MessageRecords             map[string]MessageRecord
	PendingConnectionApprovals map[string]pendingConnectionApproval
	NextConnectionApprovalID   uint64
}

func NewHub() *Hub {
	hub := &Hub{
		Clients:                  make(map[*Client]bool),
		Register:                 make(chan RegisterRequest),
		Unregister:               make(chan *Client),
		Broadcast:                make(chan Message),
		Outbound:                 make(chan OutboundMessage),
		RequestUsers:             make(chan *Client),
		Private:                  make(chan PrivateMessageRequest),
		MLSKeyPackagePublish:     make(chan MLSKeyPackagePublishRequest),
		MLSKeyPackageFetch:       make(chan MLSKeyPackageFetchRequest),
		MLSGroupCommit:           make(chan MLSGroupCommitRequest),
		MLSGroupWelcome:          make(chan MLSGroupWelcomeRequest),
		RoomJoin:                 make(chan RoomRequest),
		RoomCreate:               make(chan RoomCreateRequest),
		RoomAction:               make(chan RoomActionRequest),
		RoomLeave:                make(chan *Client),
		RequestRooms:             make(chan *Client),
		History:                  make(chan HistoryRequest),
		HistoryJobs:              make(chan historyJob, 32),
		HistoryResults:           make(chan historyResult, 32),
		AdminAction:              make(chan AdminActionRequest),
		ConnectionApproval:       make(chan ConnectionApprovalRequest),
		CancelConnectionApproval: make(chan string, 128),
		ActiveCodes:              make(map[string]*Client),
		UsedCodes:                make(map[string]struct{}),
		Rooms:                    make(map[string]map[*Client]bool),
		RoomNames:                map[string]struct{}{defaultRoomName: {}},
		RoomDefinitions: map[string]*RoomDefinition{
			defaultRoomName: {Name: defaultRoomName, Allowed: make(map[string]bool)},
		},
		MessageRecords:             make(map[string]MessageRecord),
		PendingConnectionApprovals: make(map[string]pendingConnectionApproval),
	}
	go hub.historyWorker()
	return hub
}

// historyWorker keeps SQLite I/O out of the Hub event loop. A single bounded
// worker deliberately limits database pressure while still allowing broadcasts,
// login, and room operations to continue during a slow history query.
func (h *Hub) historyWorker() {
	for job := range h.HistoryJobs {
		page, err := job.Store.LoadHistory(job.Query)
		h.HistoryResults <- historyResult{Job: job, Page: page, Err: err}
	}
}

// Run 持续处理客户端注册、注销和广播事件。
func (h *Hub) Run() {
	for {
		select {
		case request := <-h.Register:
			h.handleRegisterRequest(request)

		case client := <-h.Unregister:
			h.unregisterClient(client, true)

		case message := <-h.Broadcast:
			h.broadcastMessage(message)

		case outbound := <-h.Outbound:
			h.deliver(outbound.Client, outbound.Message)

		case client := <-h.RequestUsers:
			h.handleRequestUsers(client)

		case request := <-h.Private:
			h.handlePrivateMessage(request)

		case request := <-h.MLSKeyPackagePublish:
			h.handleMLSKeyPackagePublish(request)

		case request := <-h.MLSKeyPackageFetch:
			h.handleMLSKeyPackageFetch(request)

		case request := <-h.MLSGroupCommit:
			h.handleMLSGroupCommit(request)

		case request := <-h.MLSGroupWelcome:
			h.handleMLSGroupWelcome(request)

		case request := <-h.RoomJoin:
			h.handleRoomJoin(request)

		case request := <-h.RoomCreate:
			h.handleRoomCreate(request)

		case request := <-h.RoomAction:
			h.handleRoomAction(request)

		case client := <-h.RoomLeave:
			h.handleRoomLeave(client)

		case client := <-h.RequestRooms:
			h.handleRequestRooms(client)

		case request := <-h.History:
			h.handleHistoryRequest(request)

		case result := <-h.HistoryResults:
			h.handleHistoryResult(result)

		case request := <-h.AdminAction:
			h.handleAdminAction(request)

		case request := <-h.ConnectionApproval:
			h.handleConnectionApproval(request)

		case approvalID := <-h.CancelConnectionApproval:
			h.cancelConnectionApproval(approvalID)
		}
	}
}

func (h *Hub) handleRegisterRequest(request RegisterRequest) {
	client := request.Client
	if client == nil {
		h.respondRegister(request.Result, errRegisterRequestMissingIdentity)
		return
	}

	if client.UserCode == "" || client.NormalizedCode == "" {
		h.respondRegister(request.Result, errRegisterRequestMissingIdentity)
		return
	}
	if client.Room == "" {
		client.Room = defaultRoomName
	}
	if err := validateRoomName(client.Room); err != nil {
		h.respondRegister(request.Result, err)
		return
	}
	if _, exists := h.RoomDefinitions[defaultRoomName]; !exists {
		h.RoomDefinitions[defaultRoomName] = &RoomDefinition{Name: defaultRoomName, Allowed: make(map[string]bool)}
		h.RoomNames[defaultRoomName] = struct{}{}
	}

	if active, exists := h.ActiveCodes[client.NormalizedCode]; exists {
		// Account-backed clients have already passed the room owner's approval
		// for this connection. A network interruption can leave their prior TCP
		// session alive until its read deadline expires; let the approved session
		// take over instead of rejecting it as a duplicate user code.
		if !client.AccountBacked {
			h.respondRegister(request.Result, ErrUserCodeAlreadyUsed)
			return
		}
		h.unregisterClient(active, true)
	}
	if !client.AccountBacked {
		if _, used := h.UsedCodes[client.NormalizedCode]; used {
			h.respondRegister(request.Result, ErrUserCodeAlreadyUsed)
			return
		}
	}

	h.UsedCodes[client.NormalizedCode] = struct{}{}
	h.ActiveCodes[client.NormalizedCode] = client
	if h.AdminCode != "" && client.NormalizedCode == h.AdminCode {
		client.IsAdmin = true
	}
	h.Clients[client] = true
	h.addToRoom(client, client.Room)
	h.broadcastSystemMessageToRoom(client.Room, presenceMessage(client, "joined the chat"))
	if client.IsAdmin {
		for _, pending := range h.PendingConnectionApprovals {
			h.deliverPendingConnectionApproval(client, pending)
		}
	}
	log.Printf("client registered: %s", client.Username)
	h.respondRegister(request.Result, nil)
}

func (h *Hub) handleAdminAction(request AdminActionRequest) {
	sender := request.Sender
	if sender == nil || !h.Clients[sender] {
		return
	}
	if request.Action == "recall" {
		record, exists := h.MessageRecords[request.MessageID]
		if !exists && h.OfflineStore != nil && request.MessageID != "" {
			if stored, err := h.OfflineStore.GetStoredMessage(request.MessageID); err == nil {
				record = h.recordForStoredMessage(stored)
				exists = true
			}
		}
		if request.MessageID == "" || !exists {
			h.deliverError(sender, "Message not found", request.MessageID, request.CommandID)
			return
		}
		if record.Recalled {
			h.deliverError(sender, "Message already recalled", request.MessageID, request.CommandID)
			return
		}
		if !sender.IsAdmin && sender.NormalizedCode != record.AuthorCode {
			h.deliverError(sender, "Only the message author or an administrator can recall this message", request.MessageID, request.CommandID)
			return
		}
		if h.OfflineStore != nil {
			if err := h.OfflineStore.MarkMessageRecalled(request.MessageID); err != nil && !errors.Is(err, sql.ErrNoRows) {
				h.deliverError(sender, "Failed to persist recalled message", request.MessageID, request.CommandID)
				return
			}
		}
		if record.OfflineMessageID != "" && h.OfflineStore != nil {
			if err := h.OfflineStore.DeleteOfflineMessage(record.OfflineMessageID); err != nil {
				h.deliverError(sender, "Failed to remove recalled offline message", request.MessageID, request.CommandID)
				return
			}
		}
		record.Recalled = true
		h.MessageRecords[request.MessageID] = record
		h.deliverRecall(record, Message{Type: "message_recalled", MessageID: request.MessageID, CommandID: request.CommandID})
		return
	}
	if !sender.IsAdmin {
		h.deliverError(sender, "Administrator permission required")
		return
	}
	if request.Action == "approve_connection" || request.Action == "deny_connection" {
		pending, found := h.PendingConnectionApprovals[request.MessageID]
		if !found {
			h.deliverError(sender, "Connection request is no longer pending", request.MessageID, request.CommandID)
			return
		}
		delete(h.PendingConnectionApprovals, request.MessageID)
		status := "denied"
		decision := ConnectionApprovalDecision{Approved: false, Reason: "The room owner declined this connection"}
		if request.Action == "approve_connection" {
			status = "approved"
			decision = ConnectionApprovalDecision{Approved: true}
		}
		result := Message{Type: "connection_approval_result", MessageID: request.MessageID,
			Username: pending.Username, UserCode: pending.UserCode, Content: status,
			CommandID: request.CommandID}
		for client := range h.Clients {
			if client.IsAdmin {
				h.deliver(client, result)
			}
		}
		select {
		case pending.Decision <- decision:
		default:
		}
		return
	}
	targetCode, err := normalizeUserCode(request.TargetCode)
	if err != nil {
		h.deliverError(sender, "Invalid target user code")
		return
	}
	target, ok := h.ActiveCodes[targetCode]
	if !ok || target == sender {
		h.deliverError(sender, "Target user not found")
		return
	}
	switch request.Action {
	case "kick":
		h.deliver(target, Message{Type: "system", Content: "You were kicked by the administrator"})
		h.unregisterClientAfterFlush(target)
		h.deliver(sender, Message{Type: "system", Content: target.Username + "#" + target.UserCode + " was kicked"})
	case "mute":
		target.Muted = !target.Muted
		status := "muted"
		if !target.Muted {
			status = "unmuted"
		}
		h.deliver(target, Message{Type: "system", Content: "You were " + status + " by the administrator"})
		h.deliver(sender, Message{Type: "system", Content: target.Username + "#" + target.UserCode + " is " + status})
	default:
		h.deliverError(sender, "Unsupported administrator action")
	}
}

func (h *Hub) handleConnectionApproval(request ConnectionApprovalRequest) {
	if request.Username == "" || request.UserCode == "" || request.NormalizedCode == "" || request.Decision == nil {
		if request.Created != nil {
			request.Created <- ""
		}
		return
	}
	h.NextConnectionApprovalID++
	id := "connection-" + strconv.FormatUint(h.NextConnectionApprovalID, 10)
	pending := pendingConnectionApproval{
		ID:             id,
		Username:       request.Username,
		UserCode:       request.UserCode,
		NormalizedCode: request.NormalizedCode,
		RemoteAddress:  request.RemoteAddress,
		CreatedAt:      time.Now().UTC(),
		Decision:       request.Decision,
	}
	h.PendingConnectionApprovals[id] = pending
	for client := range h.Clients {
		if client.IsAdmin {
			h.deliverPendingConnectionApproval(client, pending)
		}
	}
	if request.Created != nil {
		request.Created <- id
	}
}

func (h *Hub) cancelConnectionApproval(approvalID string) {
	pending, found := h.PendingConnectionApprovals[approvalID]
	if !found {
		return
	}
	delete(h.PendingConnectionApprovals, approvalID)
	result := Message{Type: "connection_approval_result", MessageID: approvalID,
		Username: pending.Username, UserCode: pending.UserCode, Content: "expired"}
	for client := range h.Clients {
		if client.IsAdmin {
			h.deliver(client, result)
		}
	}
}

func (h *Hub) deliverPendingConnectionApproval(client *Client, request pendingConnectionApproval) {
	if client == nil || request.ID == "" {
		return
	}
	h.deliver(client, Message{Type: "connection_approval_request", MessageID: request.ID,
		Username: request.Username, UserCode: request.UserCode,
		Content: request.CreatedAt.UTC().Format(time.RFC3339Nano)})
}

func (h *Hub) unregisterClient(client *Client, broadcastLeave bool) {
	if client == nil {
		return
	}
	if _, ok := h.Clients[client]; !ok {
		return
	}

	h.removeClient(client)
	if broadcastLeave {
		h.broadcastSystemMessageToRoom(client.Room, presenceMessage(client, "left the chat"))
	}
	client.closeSend()
	client.closeConnection()
	log.Printf("client unregistered: %s", client.Username)
}

// unregisterClientAfterFlush removes a kicked client from the Hub while
// allowing its queued kick notification to reach the socket first.
func (h *Hub) unregisterClientAfterFlush(client *Client) {
	if client == nil {
		return
	}
	if _, ok := h.Clients[client]; !ok {
		return
	}

	room := client.Room
	client.disconnectAfterFlush.Store(true)
	h.removeClient(client)
	h.broadcastSystemMessageToRoom(room, presenceMessage(client, "left the chat"))
	client.closeSend()
	log.Printf("client unregistered after notification: %s", client.Username)
}

func (h *Hub) broadcastMessage(message Message) {
	clientSuppliedMessageID := message.MessageID != ""
	deliveryTracking := clientSuppliedMessageID && !isLegacyGeneratedMessageID(message.MessageID)
	if message.MessageID == "" && message.Type != "message_recalled" {
		h.NextMessageID++
		message.MessageID = strconv.FormatUint(h.NextMessageID, 10)
	}
	room := ""
	var sender *Client
	if message.UserCode != "" {
		if normalized, err := normalizeUserCode(message.UserCode); err == nil {
			if activeSender, ok := h.ActiveCodes[normalized]; ok {
				sender = activeSender
				room = sender.Room
			}
		}
	}
	if message.Type == "chat" {
		if h.OfflineStore != nil {
			persisted := message
			persisted.Room = room
			persisted.CreatedAt = time.Now().UTC().Format(time.RFC3339Nano)
			persisted.DeliveryState = "sent"
			inserted, err := h.OfflineStore.SaveChatMessageIfNew(persisted)
			if err != nil {
				if sender, ok := h.ActiveCodes[strings.ToLower(message.UserCode)]; ok {
					h.deliverError(sender, "Failed to save message history")
				}
				return
			}
			if !inserted {
				if sender != nil && deliveryTracking {
					stored, err := h.OfflineStore.GetStoredMessageForMessage(persisted)
					if err != nil {
						h.deliverError(sender, "Failed to load delivery receipt")
						return
					}
					h.deliver(sender, Message{Type: "delivery_receipt", MessageID: message.MessageID,
						Content: stored.Message.DeliveryState, DeliveryState: stored.Message.DeliveryState})
				}
				return
			}
			message = persisted
		}
	}
	if message.Type != "message_recalled" && message.MessageID != "" {
		h.recordMessage(message, h.recipientCodes(h.roomClients(room)), "")
	}
	deliveredToPeer := false
	wireMessage := message
	if !deliveryTracking {
		wireMessage.DeliveryState = ""
		wireMessage.CreatedAt = ""
	}
	for client := range h.roomClients(room) {
		if h.deliver(client, wireMessage) && client != sender {
			deliveredToPeer = true
		}
	}
	if message.Type == "chat" && sender != nil && deliveryTracking {
		state := "sent"
		if deliveredToPeer {
			state = "delivered"
			if h.OfflineStore != nil {
				if err := h.OfflineStore.UpdateChatMessageDeliveryStateForMessage(message, state); err != nil {
					h.deliverError(sender, "Failed to update delivery state")
					return
				}
			}
		}
		h.deliver(sender, Message{Type: "delivery_receipt", MessageID: message.MessageID,
			Content: state, DeliveryState: state})
	}
}

func (h *Hub) roomClients(room string) map[*Client]bool {
	if room == "" {
		return h.Clients
	}
	return h.Rooms[room]
}

func (h *Hub) deliver(client *Client, message Message) bool {
	if client == nil {
		return false
	}
	if _, ok := h.Clients[client]; !ok {
		return false
	}

	select {
	case client.Send <- message:
		return true
	default:
		h.removeSlowClient(client)
		return false
	}
}

func (h *Hub) removeSlowClient(client *Client) {
	h.removeClient(client)
	client.closeSend()
	client.closeConnection()
	log.Printf("client removed because send buffer is full: %s", client.Username)
}

func (h *Hub) removeClient(client *Client) {
	if client == nil {
		return
	}
	delete(h.Clients, client)
	h.removeFromRoom(client)

	if client.NormalizedCode == "" {
		return
	}

	if activeClient, ok := h.ActiveCodes[client.NormalizedCode]; ok && activeClient == client {
		delete(h.ActiveCodes, client.NormalizedCode)
	}
	// 临时用户代码只在在线期间占用；账号用户的代码由账号数据库持久管理。
	if !client.AccountBacked {
		delete(h.UsedCodes, client.NormalizedCode)
	}
}

func (h *Hub) broadcastSystemMessage(content string) {
	if content == "" {
		return
	}

	message := Message{
		Type:    "system",
		Content: content,
	}

	h.broadcastMessage(message)
}

func (h *Hub) broadcastSystemMessageToRoom(room, content string) {
	if content == "" {
		return
	}
	for client := range h.Rooms[room] {
		h.deliver(client, Message{Type: "system", Room: room, Content: content})
	}
}

func (h *Hub) handleRequestUsers(requester *Client) {
	if requester == nil {
		return
	}
	if _, ok := h.Clients[requester]; !ok {
		return
	}

	users := make([]string, 0, len(h.Clients))
	userDetails := make([]OnlineUser, 0, len(h.Clients))
	for client := range h.Clients {
		users = append(users, client.Username+"#"+client.UserCode+"@"+client.Room)
		userDetails = append(userDetails, OnlineUser{
			Username: client.Username,
			UserCode: client.UserCode,
			Room:     client.Room,
			IsAdmin:  client.IsAdmin,
		})
	}
	sort.Strings(users)
	sort.Slice(userDetails, func(i, j int) bool {
		return userDetails[i].Username+"#"+userDetails[i].UserCode < userDetails[j].Username+"#"+userDetails[j].UserCode
	})

	h.deliver(requester, Message{
		Type:        "users_response",
		Users:       users,
		UserDetails: userDetails,
	})
}

func (h *Hub) handleRequestRooms(requester *Client) {
	if requester == nil {
		return
	}
	if _, ok := h.Clients[requester]; !ok {
		return
	}
	rooms := make([]string, 0, len(h.RoomDefinitions))
	roomDetails := make([]RoomInfo, 0, len(h.RoomDefinitions))
	for room, definition := range h.RoomDefinitions {
		if !h.canViewRoom(requester, definition) {
			continue
		}
		rooms = append(rooms, room)
		roomDetails = append(roomDetails, RoomInfo{
			Name: room, OwnerCode: definition.OwnerCode, Private: definition.Private,
			CanManage: h.canManageRoom(requester, definition),
		})
	}
	sort.Strings(rooms)
	sort.Slice(roomDetails, func(i, j int) bool { return roomDetails[i].Name < roomDetails[j].Name })
	h.deliver(requester, Message{
		Type:        "rooms_response",
		Rooms:       rooms,
		Room:        requester.Room,
		RoomDetails: roomDetails,
	})
}

func (h *Hub) handleRoomJoin(request RoomRequest) {
	client := request.Client
	if client == nil {
		return
	}
	if _, ok := h.Clients[client]; !ok {
		return
	}
	if err := validateRoomName(request.Room); err != nil {
		h.deliverError(client, "Invalid room name")
		return
	}
	definition, exists := h.RoomDefinitions[request.Room]
	if !exists {
		h.deliverError(client, "Room not found. Create it first.")
		return
	}
	if !h.canJoinRoom(client, definition) {
		h.deliverError(client, "This is a private channel. Ask the owner for an invitation.")
		return
	}
	if request.Room == client.Room {
		h.deliver(client, Message{Type: "system", Content: "Already in room " + client.Room})
		return
	}
	oldRoom := client.Room
	h.broadcastSystemMessageToRoom(oldRoom, presenceMessage(client, "left room "+oldRoom))
	h.removeFromRoom(client)
	client.Room = request.Room
	h.addToRoom(client, client.Room)
	h.broadcastSystemMessageToRoom(client.Room, presenceMessage(client, "joined room "+client.Room))
}

func (h *Hub) handleRoomCreate(request RoomCreateRequest) {
	client := request.Client
	if client == nil || !h.Clients[client] {
		return
	}
	if err := validateRoomName(request.Room); err != nil {
		h.deliverError(client, "Invalid room name")
		return
	}
	if _, exists := h.RoomDefinitions[request.Room]; exists {
		h.deliverError(client, "A channel with this name already exists")
		return
	}
	definition := &RoomDefinition{Name: request.Room, OwnerCode: client.NormalizedCode, Private: request.Private,
		Allowed: map[string]bool{client.NormalizedCode: true}}
	h.RoomDefinitions[request.Room] = definition
	h.RoomNames[request.Room] = struct{}{}
	h.deliver(client, Message{Type: "system", Content: "Channel #" + request.Room + " created"})
	h.handleRoomJoin(RoomRequest{Client: client, Room: request.Room})
}

func (h *Hub) handleRoomAction(request RoomActionRequest) {
	sender := request.Sender
	if sender == nil || !h.Clients[sender] {
		return
	}
	definition, exists := h.RoomDefinitions[request.Room]
	if !exists || request.Room == defaultRoomName {
		h.deliverError(sender, "Channel not found or cannot be changed")
		return
	}
	if !h.canManageRoom(sender, definition) {
		h.deliverError(sender, "Channel owner or administrator permission required")
		return
	}
	if request.Action == "delete" {
		for member := range h.Rooms[request.Room] {
			h.removeFromRoom(member)
			member.Room = defaultRoomName
			h.addToRoom(member, defaultRoomName)
			h.deliver(member, Message{Type: "system", Content: "Channel #" + request.Room + " was deleted"})
		}
		delete(h.Rooms, request.Room)
		delete(h.RoomDefinitions, request.Room)
		delete(h.RoomNames, request.Room)
		return
	}
	targetCode, err := normalizeUserCode(request.TargetCode)
	if err != nil {
		h.deliverError(sender, "Invalid target user code")
		return
	}
	if request.Action == "invite" {
		definition.Allowed[targetCode] = true
		h.deliver(sender, Message{Type: "system", Content: "Member invited to #" + request.Room})
		return
	}
	if request.Action == "remove_member" {
		delete(definition.Allowed, targetCode)
		if target, online := h.ActiveCodes[targetCode]; online && target.Room == request.Room {
			h.removeFromRoom(target)
			target.Room = defaultRoomName
			h.addToRoom(target, defaultRoomName)
			h.deliver(target, Message{Type: "system", Content: "You were removed from #" + request.Room})
		}
		h.deliver(sender, Message{Type: "system", Content: "Member removed from #" + request.Room})
	}
}

func (h *Hub) handleRoomLeave(client *Client) {
	if client == nil {
		return
	}
	if _, ok := h.Clients[client]; !ok {
		return
	}
	if client.Room == defaultRoomName {
		h.deliver(client, Message{Type: "system", Content: "Already in room " + defaultRoomName})
		return
	}
	oldRoom := client.Room
	h.broadcastSystemMessageToRoom(oldRoom, presenceMessage(client, "left room "+oldRoom))
	h.removeFromRoom(client)
	client.Room = defaultRoomName
	h.addToRoom(client, client.Room)
	h.broadcastSystemMessageToRoom(client.Room, presenceMessage(client, "joined room "+defaultRoomName))
}

func (h *Hub) addToRoom(client *Client, room string) {
	if h.Rooms[room] == nil {
		h.Rooms[room] = make(map[*Client]bool)
	}
	h.Rooms[room][client] = true
}

func (h *Hub) canJoinRoom(client *Client, room *RoomDefinition) bool {
	return room != nil && (!room.Private || client.IsAdmin || room.Allowed[client.NormalizedCode])
}

func (h *Hub) canViewRoom(client *Client, room *RoomDefinition) bool {
	return h.canJoinRoom(client, room)
}

func (h *Hub) canManageRoom(client *Client, room *RoomDefinition) bool {
	return client != nil && room != nil && (client.IsAdmin || (room.OwnerCode != "" && room.OwnerCode == client.NormalizedCode))
}

func (h *Hub) removeFromRoom(client *Client) {
	if client == nil || client.Room == "" {
		return
	}
	if members, ok := h.Rooms[client.Room]; ok {
		delete(members, client)
		if len(members) == 0 && client.Room != defaultRoomName {
			delete(h.Rooms, client.Room)
		}
	}
}

func (h *Hub) handlePrivateMessage(request PrivateMessageRequest) {
	sender := request.Sender
	if sender == nil {
		return
	}
	if _, ok := h.Clients[sender]; !ok {
		return
	}

	targetCode, err := normalizeUserCode(request.TargetCode)
	if err != nil {
		h.deliverError(sender, "Invalid target user code")
		return
	}
	if err := validateTextContent("private chat", request.Content); err != nil {
		h.deliverError(sender, "Invalid private chat content")
		return
	}
	if targetCode == sender.NormalizedCode {
		h.deliverError(sender, "Cannot send private message to yourself")
		return
	}

	clientSuppliedMessageID := request.MessageID != ""
	deliveryTracking := clientSuppliedMessageID && !isLegacyGeneratedMessageID(request.MessageID)
	messageID := request.MessageID
	if messageID == "" {
		h.NextMessageID++
		messageID = strconv.FormatUint(h.NextMessageID, 10)
	}
	message := Message{Type: "private_chat", MessageID: messageID, Username: sender.Username,
		UserCode: sender.UserCode, TargetUserCode: request.TargetCode, Content: request.Content,
		DeliveryState: "sent", CreatedAt: time.Now().UTC().Format(time.RFC3339Nano)}
	if h.OfflineStore != nil {
		persisted := message
		persisted.Private = true
		inserted, err := h.OfflineStore.SaveChatMessageIfNew(persisted)
		if err != nil {
			h.deliverError(sender, "Failed to save message history")
			return
		}
		if !inserted {
			if !deliveryTracking {
				return
			}
			stored, err := h.OfflineStore.GetStoredMessageForMessage(persisted)
			if err != nil {
				h.deliverError(sender, "Failed to load delivery receipt")
				return
			}
			h.deliver(sender, Message{Type: "delivery_receipt", MessageID: message.MessageID,
				Content: stored.Message.DeliveryState, DeliveryState: stored.Message.DeliveryState})
			return
		}
		message.CreatedAt = persisted.CreatedAt
		message.DeliveryState = persisted.DeliveryState
	}

	target, ok := h.ActiveCodes[targetCode]
	if !ok {
		if h.OfflineStore == nil {
			h.deliverError(sender, "Target user not found")
			return
		}
		exists, err := h.OfflineStore.HasUserCode(targetCode)
		if err != nil || !exists {
			h.deliverError(sender, "Target user not found")
			return
		}
		h.recordMessage(message, map[string]bool{sender.NormalizedCode: true, targetCode: true}, message.MessageID)
		if err := h.OfflineStore.SaveOfflineMessage(targetCode, message); err != nil {
			h.deliverError(sender, "Failed to save offline message")
			return
		}
		wireMessage := message
		if !deliveryTracking {
			wireMessage.DeliveryState = ""
			wireMessage.CreatedAt = ""
		}
		h.deliver(sender, wireMessage)
		h.deliver(sender, Message{Type: "system", Content: "Private message saved for offline user"})
		if deliveryTracking {
			h.deliver(sender, Message{Type: "delivery_receipt", MessageID: message.MessageID,
				Content: "sent", DeliveryState: "sent"})
		}
		return
	}
	message.TargetUserCode = target.UserCode
	h.recordMessage(message, map[string]bool{sender.NormalizedCode: true, targetCode: true}, "")
	wireMessage := message
	if !deliveryTracking {
		wireMessage.DeliveryState = ""
		wireMessage.CreatedAt = ""
	}
	if !h.deliver(sender, wireMessage) {
		return
	}
	state := "sent"
	if h.deliver(target, wireMessage) {
		state = "delivered"
		if h.OfflineStore != nil {
			storedMessage := message
			storedMessage.Private = true
			if err := h.OfflineStore.UpdateChatMessageDeliveryStateForMessage(storedMessage, state); err != nil {
				h.deliverError(sender, "Failed to update delivery state")
				return
			}
		}
	}
	if deliveryTracking {
		h.deliver(sender, Message{Type: "delivery_receipt", MessageID: message.MessageID,
			Content: state, DeliveryState: state})
	}
}

func (h *Hub) handleHistoryRequest(request HistoryRequest) {
	client := request.Client
	if client == nil || !h.Clients[client] || h.OfflineStore == nil {
		return
	}
	if request.Private {
		peerCode, err := normalizeUserCode(request.TargetCode)
		if err != nil || peerCode == client.NormalizedCode {
			h.deliverError(client, "Invalid private conversation")
			return
		}
		query := HistoryQuery{
			UserCode: client.UserCode, PeerCode: peerCode, Private: true,
			BeforeMessageID: request.BeforeMessageID, SearchQuery: request.SearchQuery, Limit: request.Limit,
		}
		h.enqueueHistory(historyJob{
			Store: h.OfflineStore, Client: client, TargetCode: peerCode, Private: true, Query: query,
		})
		return
	}
	if err := validateRoomName(request.Room); err != nil {
		h.deliverError(client, "Invalid room name")
		return
	}
	definition, exists := h.RoomDefinitions[request.Room]
	if !exists || !h.canViewRoom(client, definition) {
		h.deliverError(client, "You do not have access to this channel")
		return
	}
	query := HistoryQuery{
		UserCode: client.UserCode, Room: request.Room,
		BeforeMessageID: request.BeforeMessageID, SearchQuery: request.SearchQuery, Limit: request.Limit,
	}
	h.enqueueHistory(historyJob{
		Store: h.OfflineStore, Client: client, Room: request.Room, Query: query,
	})
}

func (h *Hub) enqueueHistory(job historyJob) {
	select {
	case h.HistoryJobs <- job:
	default:
		if job.Client != nil {
			h.deliverError(job.Client, "History service is busy")
		}
	}
}

func (h *Hub) handleHistoryResult(result historyResult) {
	client := result.Job.Client
	if client == nil || !h.Clients[client] {
		return
	}
	if result.Err != nil {
		h.deliverError(client, "Failed to load message history")
		return
	}

	message := Message{
		Type:           "history_response",
		Room:           result.Job.Room,
		Private:        result.Job.Private,
		Messages:       result.Page.Messages,
		HasMore:        result.Page.HasMore,
		TargetUserCode: result.Job.TargetCode,
		SearchQuery:    result.Job.Query.SearchQuery,
	}
	h.deliver(client, message)
}

func (h *Hub) recordForStoredMessage(stored StoredMessage) MessageRecord {
	recipients := make(map[string]bool)
	if stored.Kind == "private" {
		recipients[stored.AuthorCode] = true
		recipients[stored.TargetCode] = true
	} else {
		recipients = h.recipientCodes(h.roomClients(stored.Message.Room))
	}
	return MessageRecord{AuthorCode: stored.AuthorCode, Recipients: recipients}
}

func (h *Hub) recipientCodes(clients map[*Client]bool) map[string]bool {
	recipients := make(map[string]bool, len(clients))
	for client := range clients {
		if client != nil && client.NormalizedCode != "" {
			recipients[client.NormalizedCode] = true
		}
	}
	return recipients
}

func (h *Hub) recordMessage(message Message, recipients map[string]bool, offlineMessageID string) {
	if message.MessageID == "" {
		return
	}
	authorCode := ""
	if normalized, err := normalizeUserCode(message.UserCode); err == nil {
		authorCode = normalized
	}
	h.MessageRecords[message.MessageID] = MessageRecord{AuthorCode: authorCode, Recipients: recipients, OfflineMessageID: offlineMessageID}
}

func (h *Hub) canAccessMLSRoom(sender *Client, targetCode, room string) bool {
	if sender == nil || !h.Clients[sender] {
		return false
	}
	if targetCode == sender.NormalizedCode {
		return true
	}
	if room == "" {
		return false
	}
	definition, exists := h.RoomDefinitions[room]
	if !exists || !h.Rooms[room][sender] {
		return false
	}
	if definition.Allowed[targetCode] {
		return true
	}
	target, online := h.ActiveCodes[targetCode]
	return online && target.Room == room && h.Rooms[room][target]
}

func (h *Hub) handleMLSKeyPackagePublish(request MLSKeyPackagePublishRequest) {
	sender := request.Sender
	if sender == nil || !h.Clients[sender] || h.OfflineStore == nil {
		if sender != nil {
			h.deliverError(sender, "MLS key package service unavailable", "", request.CommandID)
		}
		return
	}
	if err := h.OfflineStore.PublishMLSKeyPackage(sender.NormalizedCode, request.KeyPackage); err != nil {
		h.deliverError(sender, "Invalid MLS key package", "", request.CommandID)
		return
	}
	h.deliver(sender, Message{Type: "mls.key_package.publish", UserCode: sender.UserCode,
		Content: "stored", CommandID: request.CommandID})
}

func (h *Hub) handleMLSKeyPackageFetch(request MLSKeyPackageFetchRequest) {
	sender := request.Sender
	if sender == nil || !h.Clients[sender] || h.OfflineStore == nil {
		if sender != nil {
			h.deliverError(sender, "MLS key package service unavailable", "", request.CommandID)
		}
		return
	}
	targetCode, err := normalizeUserCode(request.TargetCode)
	if err != nil {
		h.deliverError(sender, "Invalid MLS key package target", "", request.CommandID)
		return
	}
	room := request.Room
	if room == "" && targetCode != sender.NormalizedCode {
		room = sender.Room
	}
	if !h.canAccessMLSRoom(sender, targetCode, room) {
		h.deliverError(sender, "MLS key package access denied", "", request.CommandID)
		return
	}
	keyPackage, err := h.OfflineStore.FetchMLSKeyPackage(targetCode)
	if err != nil {
		h.deliverError(sender, "MLS key package not found", "", request.CommandID)
		return
	}
	h.deliver(sender, Message{Type: "mls.key_package.fetch", TargetUserCode: targetCode,
		KeyPackage: keyPackage, Room: room, CommandID: request.CommandID})
}

func (h *Hub) handleMLSGroupCommit(request MLSGroupCommitRequest) {
	sender := request.Sender
	if sender == nil || !h.Clients[sender] || h.OfflineStore == nil {
		if sender != nil {
			h.deliverError(sender, "MLS group service unavailable", "", request.CommandID)
		}
		return
	}
	room := request.Room
	if room == "" {
		room = sender.Room
	}
	if !h.canAccessMLSRoom(sender, sender.NormalizedCode, room) || !h.Rooms[room][sender] {
		h.deliverError(sender, "MLS group commit access denied", "", request.CommandID)
		return
	}
	inserted, err := h.OfflineStore.SaveMLSCommit(request.GroupID, room, request.Epoch, request.Commit)
	if err != nil {
		content := "Invalid MLS group commit"
		if errors.Is(err, ErrMLSEpochRollback) {
			content = "MLS group epoch rollback"
		} else if errors.Is(err, ErrMLSCommitConflict) {
			content = "MLS group commit conflict"
		}
		h.deliverError(sender, content, "", request.CommandID)
		return
	}
	content := "stored"
	if !inserted {
		content = "duplicate"
	}
	h.deliver(sender, Message{Type: "mls.group.commit", GroupID: request.GroupID,
		Room: room, Epoch: request.Epoch, Content: content, CommandID: request.CommandID})
}

func (h *Hub) handleMLSGroupWelcome(request MLSGroupWelcomeRequest) {
	sender := request.Sender
	if sender == nil || !h.Clients[sender] || h.OfflineStore == nil {
		if sender != nil {
			h.deliverError(sender, "MLS group service unavailable", "", request.CommandID)
		}
		return
	}
	targetCode, err := normalizeUserCode(request.TargetCode)
	if err != nil {
		h.deliverError(sender, "Invalid MLS welcome target", "", request.CommandID)
		return
	}
	room := request.Room
	if room == "" {
		room = sender.Room
	}
	if !h.canAccessMLSRoom(sender, targetCode, room) || !h.Rooms[room][sender] {
		h.deliverError(sender, "MLS group welcome access denied", "", request.CommandID)
		return
	}
	inserted, err := h.OfflineStore.SaveMLSWelcome(request.GroupID, room, request.Epoch, targetCode, request.Welcome)
	if err != nil {
		content := "Invalid MLS group welcome"
		if errors.Is(err, ErrMLSWelcomeConflict) {
			content = "MLS group welcome conflict"
		}
		h.deliverError(sender, content, "", request.CommandID)
		return
	}
	if target := h.ActiveCodes[targetCode]; target != nil {
		h.deliver(target, Message{Type: "mls.group.welcome", GroupID: request.GroupID,
			Room: room, Epoch: request.Epoch, TargetUserCode: targetCode, Welcome: request.Welcome})
	}
	content := "stored"
	if !inserted {
		content = "duplicate"
	}
	h.deliver(sender, Message{Type: "mls.group.welcome", GroupID: request.GroupID,
		Room: room, Epoch: request.Epoch, TargetUserCode: targetCode, Content: content, CommandID: request.CommandID})
}

func (h *Hub) deliverRecall(record MessageRecord, message Message) {
	for code := range record.Recipients {
		if client, online := h.ActiveCodes[code]; online {
			h.deliver(client, message)
		}
	}
}

func (h *Hub) deliverError(client *Client, content string, identifiers ...string) {
	if content == "" {
		return
	}
	id := ""
	if len(identifiers) > 0 {
		id = identifiers[0]
	}
	commandID := ""
	if len(identifiers) > 1 {
		commandID = identifiers[1]
	}
	h.deliver(client, Message{
		Type:      "error",
		MessageID: id,
		CommandID: commandID,
		Content:   content,
	})
}

func (h *Hub) respondRegister(result chan error, err error) {
	if result == nil {
		return
	}
	result <- err
}

func presenceMessage(client *Client, suffix string) string {
	if client == nil {
		return ""
	}
	return client.Username + "#" + client.UserCode + " " + suffix
}
