package main

import (
	"errors"
	"log"
	"net"
	"sync"
)

const clientSendBufferSize = 16

var ErrUserCodeAlreadyUsed = errors.New("user code already exists")

type RegisterRequest struct {
	Client *Client
	Result chan error
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

func newClient(conn net.Conn, username string) *Client {
	return &Client{
		Conn:     conn,
		Username: username,
		Send:     make(chan Message, clientSendBufferSize),
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
	Clients     map[*Client]bool
	Register    chan any
	Unregister  chan *Client
	Broadcast   chan Message
	ActiveCodes map[string]*Client
	UsedCodes   map[string]struct{}
}

func NewHub() *Hub {
	return &Hub{
		Clients:     make(map[*Client]bool),
		Register:    make(chan any),
		Unregister:  make(chan *Client),
		Broadcast:   make(chan Message),
		ActiveCodes: make(map[string]*Client),
		UsedCodes:   make(map[string]struct{}),
	}
}

// Run 持续处理客户端注册、注销和广播事件。
func (h *Hub) Run() {
	for {
		select {
		case event := <-h.Register:
			switch request := event.(type) {
			case RegisterRequest:
				h.handleRegisterRequest(request)
			case *Client:
				h.handleRegisterRequest(RegisterRequest{Client: request})
			case nil:
				continue
			default:
				log.Printf("unsupported register event type: %T", event)
			}

		case client := <-h.Unregister:
			h.unregisterClient(client, true)

		case message := <-h.Broadcast:
			for client := range h.Clients {
				select {
				case client.Send <- message:
				default:
					// 慢客户端不能阻塞整个 Hub；移除它并关闭连接。
					h.removeClient(client)
					client.closeSend()
					client.closeConnection()
					log.Printf("client removed because send buffer is full: %s", client.Username)
				}
			}
		}
	}
}

func (h *Hub) handleRegisterRequest(request RegisterRequest) {
	client := request.Client
	if client == nil {
		h.respondRegister(request.Result, nil)
		return
	}

	if client.NormalizedCode != "" {
		if _, used := h.UsedCodes[client.NormalizedCode]; used {
			h.respondRegister(request.Result, ErrUserCodeAlreadyUsed)
			return
		}

		h.UsedCodes[client.NormalizedCode] = struct{}{}
		h.ActiveCodes[client.NormalizedCode] = client
	}

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

	for client := range h.Clients {
		select {
		case client.Send <- message:
		default:
			h.removeClient(client)
			client.closeSend()
			client.closeConnection()
			log.Printf("client removed because send buffer is full: %s", client.Username)
		}
	}
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
	if client.UserCode == "" {
		return client.Username + " " + suffix
	}
	return client.Username + "#" + client.UserCode + " " + suffix
}
