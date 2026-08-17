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
	shouldUnregister := false
	defer func() {
		if recovered := recover(); recovered != nil {
			log.Printf("client handler panic for %s: %v", remoteAddress, recovered)
		}

		if shouldUnregister && client != nil {
			hub.Unregister <- client
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

	normalizedCode, err := normalizeUserCode(loginMessage.UserCode)
	if err != nil {
		log.Printf("failed to normalize user code for %s: %v", remoteAddress, err)
		_ = sendMessage(conn, Message{
			Type:    "login_error",
			Content: "Invalid username",
		})
		return
	}
	client = newClient(conn, loginMessage.Username, loginMessage.UserCode, normalizedCode)

	registerResult := make(chan error, 1)
	hub.Register <- RegisterRequest{Client: client, Result: registerResult}
	if err := <-registerResult; err != nil {
		log.Printf("failed to register client %s: %v", client.Username, err)
		content := "Login failed"
		if errors.Is(err, ErrUserCodeAlreadyUsed) {
			content = "User code already exists"
		}
		_ = sendMessage(conn, Message{
			Type:    "login_error",
			Content: content,
		})
		return
	}
	shouldUnregister = true

	if err := sendMessage(conn, Message{
		Type:     "login_ok",
		Username: client.Username,
		UserCode: client.UserCode,
		Content:  "Login successful",
	}); err != nil {
		log.Printf("failed to send login response to %s: %v", remoteAddress, err)
		return
	}

	go client.writePump(hub)
	log.Printf("user logged in: %s", client.Username)

	shouldUnregister = false
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

		switch message.Type {
		case "chat":
			if err := validateMessage(message); err != nil {
				log.Printf("invalid chat message from %s: %v", c.Username, err)
				if !c.enqueue(hub, Message{
					Type:    "error",
					Content: "Invalid chat content",
				}) {
					return
				}
				continue
			}

			message.Username = c.Username
			message.UserCode = c.UserCode
			hub.Broadcast <- message

		case "users_request":
			hub.RequestUsers <- c

		case "quit":
			hub.Unregister <- c
			return

		default:
			if !c.enqueue(hub, Message{
				Type:    "error",
				Content: "Expected chat message",
			}) {
				return
			}
		}
	}
}

func (c *Client) writePump(hub *Hub) {
	for message := range c.Send {
		if err := sendMessage(c.Conn, message); err != nil {
			log.Printf("failed to send message to %s: %v", c.Username, err)
			if hub != nil {
				hub.Unregister <- c
			}
			c.closeConnection()
			return
		}
	}
}

func (c *Client) enqueue(hub *Hub, message Message) bool {
	if hub == nil {
		log.Printf("cannot enqueue message for %s without hub", c.Username)
		return false
	}

	hub.Outbound <- OutboundMessage{
		Client:  c,
		Message: message,
	}
	return true
}
