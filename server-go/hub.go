package main

import (
	"errors"
	"log"
	"net"
	"sort"
	"sync"
)

const clientSendBufferSize = 16

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

// Client 表示一个已经完成登录的客户端连接。
type Client struct {
	Conn           net.Conn
	Username       string
	UserCode       string
	NormalizedCode string
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
	Clients      map[*Client]bool
	Register     chan RegisterRequest
	Unregister   chan *Client
	Broadcast    chan Message
	Outbound     chan OutboundMessage
	RequestUsers chan *Client
	Private      chan PrivateMessageRequest
	RoomJoin     chan RoomRequest
	RoomLeave    chan *Client
	RequestRooms chan *Client
	ActiveCodes  map[string]*Client
	UsedCodes    map[string]struct{}
	Rooms        map[string]map[*Client]bool
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
		RoomLeave:    make(chan *Client),
		RequestRooms: make(chan *Client),
		ActiveCodes:  make(map[string]*Client),
		UsedCodes:    make(map[string]struct{}),
		Rooms:        make(map[string]map[*Client]bool),
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

		case client := <-h.RoomLeave:
			h.handleRoomLeave(client)

		case client := <-h.RequestRooms:
			h.handleRequestRooms(client)
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

	if _, used := h.UsedCodes[client.NormalizedCode]; used {
		h.respondRegister(request.Result, ErrUserCodeAlreadyUsed)
		return
	}

	h.UsedCodes[client.NormalizedCode] = struct{}{}
	h.ActiveCodes[client.NormalizedCode] = client
	h.Clients[client] = true
	h.addToRoom(client, client.Room)
	h.broadcastSystemMessageToRoom(client.Room, presenceMessage(client, "joined the chat"))
	log.Printf("client registered: %s", client.Username)
	h.respondRegister(request.Result, nil)
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
	delete(h.Clients, client)
	h.removeFromRoom(client)

	if client == nil || client.NormalizedCode == "" {
		return
	}

	if activeClient, ok := h.ActiveCodes[client.NormalizedCode]; ok && activeClient == client {
		delete(h.ActiveCodes, client.NormalizedCode)
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
		h.deliver(client, Message{Type: "system", Content: content})
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
	for client := range h.Clients {
		users = append(users, client.Username+"#"+client.UserCode+"@"+client.Room)
	}
	sort.Strings(users)

	h.deliver(requester, Message{
		Type:  "users_response",
		Users: users,
	})
}

func (h *Hub) handleRequestRooms(requester *Client) {
	if requester == nil {
		return
	}
	if _, ok := h.Clients[requester]; !ok {
		return
	}
	rooms := make([]string, 0, len(h.Rooms))
	for room := range h.Rooms {
		rooms = append(rooms, room)
	}
	sort.Strings(rooms)
	h.deliver(requester, Message{Type: "rooms_response", Rooms: rooms, Room: requester.Room})
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
		h.deliverError(sender, "Target user not found")
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
