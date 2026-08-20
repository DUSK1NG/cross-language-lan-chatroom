package main

import (
	"errors"
	"log"
	"net"
	"sort"
	"strconv"
	"sync"
)

// 上线提示和用户列表可能在登录后连续到达，缓冲区不能过小，
// 否则正常的短时消息突发会被误判为慢客户端并强制断开。
const clientSendBufferSize = 256

var ErrUserCodeAlreadyUsed = errors.New("user code already exists")
var errRegisterRequestMissingIdentity = errors.New("register request requires user code identity")

const defaultRoomName = "lobby"

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
}

type RoomRequest struct {
	Client *Client
	Room   string
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

	closeOnce     sync.Once
	closeSendOnce sync.Once
}

func newClient(conn net.Conn, username, userCode, normalizedCode string) *Client {
	return &Client{
		Conn:           conn,
		Username:       username,
		UserCode:       userCode,
		NormalizedCode: normalizedCode,
		Room:           defaultRoomName,
		Send:           make(chan Message, clientSendBufferSize),
	}
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
	Clients         map[*Client]bool
	Register        chan RegisterRequest
	Unregister      chan *Client
	Broadcast       chan Message
	Outbound        chan OutboundMessage
	RequestUsers    chan *Client
	Private         chan PrivateMessageRequest
	RoomJoin        chan RoomRequest
	RoomCreate      chan RoomCreateRequest
	RoomAction      chan RoomActionRequest
	RoomLeave       chan *Client
	RequestRooms    chan *Client
	AdminAction     chan AdminActionRequest
	ActiveCodes     map[string]*Client
	UsedCodes       map[string]struct{}
	Rooms           map[string]map[*Client]bool
	RoomNames       map[string]struct{}
	RoomDefinitions map[string]*RoomDefinition
	OfflineStore    *AuthStore
	AdminCode       string
	NextMessageID   uint64
}

func NewHub() *Hub {
	return &Hub{
		Clients:      make(map[*Client]bool),
		Register:     make(chan RegisterRequest),
		Unregister:   make(chan *Client),
		Broadcast:    make(chan Message),
		Outbound:     make(chan OutboundMessage),
		RequestUsers: make(chan *Client),
		Private:      make(chan PrivateMessageRequest),
		RoomJoin:     make(chan RoomRequest),
		RoomCreate:   make(chan RoomCreateRequest),
		RoomAction:   make(chan RoomActionRequest),
		RoomLeave:    make(chan *Client),
		RequestRooms: make(chan *Client),
		AdminAction:  make(chan AdminActionRequest),
		ActiveCodes:  make(map[string]*Client),
		UsedCodes:    make(map[string]struct{}),
		Rooms:        make(map[string]map[*Client]bool),
		RoomNames:    map[string]struct{}{defaultRoomName: {}},
		RoomDefinitions: map[string]*RoomDefinition{
			defaultRoomName: {Name: defaultRoomName, Allowed: make(map[string]bool)},
		},
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

		case request := <-h.AdminAction:
			h.handleAdminAction(request)
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

	if _, active := h.ActiveCodes[client.NormalizedCode]; active {
		h.respondRegister(request.Result, ErrUserCodeAlreadyUsed)
		return
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
	log.Printf("client registered: %s", client.Username)
	h.respondRegister(request.Result, nil)
}

func (h *Hub) handleAdminAction(request AdminActionRequest) {
	sender := request.Sender
	if sender == nil || !sender.IsAdmin {
		if sender != nil {
			h.deliverError(sender, "Administrator permission required")
		}
		return
	}
	if request.Action == "recall" {
		if request.MessageID == "" {
			h.deliverError(sender, "Message not found")
			return
		}
		h.broadcastMessage(Message{Type: "message_recalled", MessageID: request.MessageID})
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
		h.unregisterClient(target, true)
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

func (h *Hub) broadcastMessage(message Message) {
	if message.MessageID == "" && message.Type != "message_recalled" {
		h.NextMessageID++
		message.MessageID = strconv.FormatUint(h.NextMessageID, 10)
	}
	room := ""
	if message.UserCode != "" {
		if normalized, err := normalizeUserCode(message.UserCode); err == nil {
			if sender, ok := h.ActiveCodes[normalized]; ok {
				room = sender.Room
			}
		}
	}
	for client := range h.roomClients(room) {
		h.deliver(client, message)
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
		message := Message{Type: "private_chat", Username: sender.Username,
			UserCode: sender.UserCode, TargetUserCode: request.TargetCode, Content: request.Content}
		h.NextMessageID++
		message.MessageID = strconv.FormatUint(h.NextMessageID, 10)
		if err := h.OfflineStore.SaveOfflineMessage(targetCode, message); err != nil {
			h.deliverError(sender, "Failed to save offline message")
			return
		}
		h.deliver(sender, message)
		h.deliver(sender, Message{Type: "system", Content: "Private message saved for offline user"})
		return
	}
	if target == sender {
		h.deliverError(sender, "Cannot send private message to yourself")
		return
	}

	message := Message{
		Type:           "private_chat",
		Username:       sender.Username,
		UserCode:       sender.UserCode,
		TargetUserCode: target.UserCode,
		Content:        request.Content,
	}
	h.NextMessageID++
	message.MessageID = strconv.FormatUint(h.NextMessageID, 10)
	if !h.deliver(sender, message) {
		return
	}
	h.deliver(target, message)
}

func (h *Hub) deliverError(client *Client, content string) {
	if content == "" {
		return
	}
	h.deliver(client, Message{
		Type:    "error",
		Content: content,
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
