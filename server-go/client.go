package main

import (
	"errors"
	"io"
	"log"
	"net"
)

func handleConnection(conn net.Conn, hub *Hub) {
	remoteAddress := conn.RemoteAddr().String()
	log.Printf("client connected: %s", remoteAddress)

	var client *Client
	defer func() {
		if recovered := recover(); recovered != nil {
			log.Printf("client handler panic for %s: %v", remoteAddress, recovered)
		}

		if client != nil {
			client.closeConnection()
		} else {
			_ = conn.Close()
		}
		log.Printf("client disconnected: %s", remoteAddress)
	}()

	loginMessage, err := receiveMessage(conn)
	if err != nil {
		log.Printf("failed to receive login message from %s: %v", remoteAddress, err)
		_ = sendMessage(conn, Message{
			Type:    "error",
			Content: "Invalid JSON message",
		})
		return
	}
	if loginMessage.Type != "login" {
		_ = sendMessage(conn, Message{
			Type:    "login_error",
			Content: "Expected login message",
		})
		return
	}
	if err := validateMessage(loginMessage); err != nil {
		log.Printf("invalid login message from %s: %v", remoteAddress, err)
		_ = sendMessage(conn, Message{
			Type:    "login_error",
			Content: "Invalid username",
		})
		return
	}

	client = newClient(conn, loginMessage.Username)
	client.UserCode = loginMessage.UserCode
	normalizedCode, err := normalizeUserCode(loginMessage.UserCode)
	if err != nil {
		log.Printf("failed to normalize user code for %s: %v", remoteAddress, err)
		_ = sendMessage(conn, Message{
			Type:    "login_error",
			Content: "Invalid username",
		})
		return
	}
	client.NormalizedCode = normalizedCode

	registerResult := make(chan error, 1)
	hub.Register <- RegisterRequest{Client: client, Result: registerResult}
	if err := <-registerResult; err != nil {
		log.Printf("failed to register client %s: %v", client.Username, err)
		_ = sendMessage(conn, Message{
			Type:    "login_error",
			Content: "Login failed",
		})
		return
	}

	if err := sendMessage(conn, Message{
		Type:    "login_ok",
		Content: "Login successful",
	}); err != nil {
		log.Printf("failed to send login response to %s: %v", remoteAddress, err)
		return
	}

	go client.writePump()
	log.Printf("user logged in: %s", client.Username)

	client.readPump(hub)
}

func (c *Client) readPump(hub *Hub) {
	defer func() {
		hub.Unregister <- c
	}()

	for {
		message, err := receiveMessage(c.Conn)
		if err != nil {
			if !errors.Is(err, io.EOF) {
				log.Printf("failed to receive message from %s: %v", c.Username, err)
			}
			return
		}

		if message.Type != "chat" {
			if !c.enqueue(Message{
				Type:    "error",
				Content: "Expected chat message",
			}) {
				return
			}
			continue
		}
		if err := validateMessage(message); err != nil {
			log.Printf("invalid chat message from %s: %v", c.Username, err)
			if !c.enqueue(Message{
				Type:    "error",
				Content: "Invalid chat content",
			}) {
				return
			}
			continue
		}

		// 用户名始终取自当前连接，忽略客户端在 chat 消息中携带的值。
		message.Username = c.Username
		hub.Broadcast <- message
	}
}

func (c *Client) writePump() {
	for message := range c.Send {
		if err := sendMessage(c.Conn, message); err != nil {
			log.Printf("failed to send message to %s: %v", c.Username, err)
			c.closeConnection()
			return
		}
	}
}

func (c *Client) enqueue(message Message) bool {
	select {
	case c.Send <- message:
		return true
	default:
		log.Printf("send buffer is full for %s", c.Username)
		c.closeConnection()
		return false
	}
}
