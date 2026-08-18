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

// Client 表示一个已经完成登录的客户端连接。
type Client struct {
	Conn           net.Conn
	Username       string
	UserCode       string
	NormalizedCode string
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
	ActiveCodes  map[string]*Client
	UsedCodes    map[string]struct{}
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
		ActiveCodes:  make(map[string]*Client),
		UsedCodes:    make(map[string]struct{}),
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

	if _, used := h.UsedCodes[client.NormalizedCode]; used {
		h.respondRegister(request.Result, ErrUserCodeAlreadyUsed)
		return
	}

	h.UsedCodes[client.NormalizedCode] = struct{}{}
	h.ActiveCodes[client.NormalizedCode] = client
	h.Clients[client] = true
	h.broadcastSystemMessage(presenceMessage(client, "joined the chat"))
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
		h.broadcastSystemMessage(presenceMessage(client, "left the chat"))
	}
	client.closeSend()
	client.closeConnection()
	log.Printf("client unregistered: %s", client.Username)
}

func (h *Hub) broadcastMessage(message Message) {
	for client := range h.Clients {
		h.deliver(client, message)
	}
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

func (h *Hub) handleRequestUsers(requester *Client) {
	if requester == nil {
		return
	}
	if _, ok := h.Clients[requester]; !ok {
		return
	}

	users := make([]string, 0, len(h.Clients))
	for client := range h.Clients {
		users = append(users, client.Username+"#"+client.UserCode)
	}
	sort.Strings(users)

	h.deliver(requester, Message{
		Type:  "users_response",
		Users: users,
	})
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
