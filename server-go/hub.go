package main

import (
	"log"
	"net"
	"sync"
)

const clientSendBufferSize = 16

// Client 表示一个已经完成登录的客户端连接。
type Client struct {
	Conn     net.Conn
	Username string
	Send     chan Message

	closeOnce sync.Once
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

// Hub 是聊天室中客户端集合和广播消息的唯一管理者。
// Clients 只能由 Run goroutine 访问，其他 goroutine 通过 channel 提交事件。
type Hub struct {
	Clients    map[*Client]bool
	Register   chan *Client
	Unregister chan *Client
	Broadcast  chan Message
}

func NewHub() *Hub {
	return &Hub{
		Clients:    make(map[*Client]bool),
		Register:   make(chan *Client),
		Unregister: make(chan *Client),
		Broadcast:  make(chan Message),
	}
}

// Run 持续处理客户端注册、注销和广播事件。
func (h *Hub) Run() {
	for {
		select {
		case client := <-h.Register:
			if client == nil {
				continue
			}
			h.Clients[client] = true
			log.Printf("client registered: %s", client.Username)

		case client := <-h.Unregister:
			if client == nil {
				continue
			}
			if _, ok := h.Clients[client]; !ok {
				continue
			}

			delete(h.Clients, client)
			close(client.Send)
			client.closeConnection()
			log.Printf("client unregistered: %s", client.Username)

		case message := <-h.Broadcast:
			for client := range h.Clients {
				select {
				case client.Send <- message:
				default:
					// 慢客户端不能阻塞整个 Hub；移除它并关闭连接。
					delete(h.Clients, client)
					close(client.Send)
					client.closeConnection()
					log.Printf("client removed because send buffer is full: %s", client.Username)
				}
			}
		}
	}
}
