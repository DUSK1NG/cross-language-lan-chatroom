package main

import (
	"errors"
	"io"
	"log"
	"net"
)

func handleConnection(conn net.Conn, hub *Hub) {
	handleConnectionWithStore(conn, hub, nil)
}

func handleConnectionWithStore(conn net.Conn, hub *Hub, store *AuthStore) {
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

	for {
		loginMessage, err := receiveMessage(conn)
		if err != nil {
			log.Printf("failed to receive authentication message from %s: %v", remoteAddress, err)
			_ = sendMessage(conn, Message{Type: "error", Content: "Invalid JSON message"})
			return
		}

		switch loginMessage.Type {
		case "register":
			if store == nil {
				_ = sendMessage(conn, Message{Type: "register_error", Content: "Account storage is unavailable"})
				continue
			}
			if err := validateMessage(loginMessage); err != nil {
				_ = sendMessage(conn, Message{Type: "register_error", Content: "Invalid registration data"})
				continue
			}
			if err := store.Register(loginMessage.Username, loginMessage.UserCode, loginMessage.Password); err != nil {
				content := "Registration failed"
				if errors.Is(err, ErrAccountAlreadyExists) {
					content = "Username or user code already exists"
				}
				_ = sendMessage(conn, Message{Type: "register_error", Content: content})
				continue
			}
			if err := sendMessage(conn, Message{Type: "register_ok", Content: "Registration successful"}); err != nil {
				return
			}
			continue

		case "login_auth":
			if store == nil {
				_ = sendMessage(conn, Message{Type: "login_error", Content: "Account storage is unavailable"})
				return
			}
			if err := validateMessage(loginMessage); err != nil {
				_ = sendMessage(conn, Message{Type: "login_error", Content: "Invalid credentials"})
				return
			}
			account, err := store.Authenticate(loginMessage.Username, loginMessage.Password)
			if err != nil {
				_ = sendMessage(conn, Message{Type: "login_error", Content: "Invalid username or password"})
				return
			}
			normalizedCode, _ := normalizeUserCode(account.UserCode)
			client = newClient(conn, account.Username, account.UserCode, normalizedCode)
			client.AccountBacked = true

		case "login":
			if err := validateMessage(loginMessage); err != nil {
				_ = sendMessage(conn, Message{Type: "login_error", Content: "Invalid username"})
				return
			}
			if store != nil {
				hasIdentity, err := store.HasIdentity(loginMessage.Username, loginMessage.UserCode)
				if err != nil {
					_ = sendMessage(conn, Message{Type: "login_error", Content: "Account storage error"})
					return
				}
				if hasIdentity {
					_ = sendMessage(conn, Message{Type: "login_error", Content: "Password login required"})
					return
				}
			}
			normalizedCode, _ := normalizeUserCode(loginMessage.UserCode)
			client = newClient(conn, loginMessage.Username, loginMessage.UserCode, normalizedCode)

		default:
			_ = sendMessage(conn, Message{Type: "login_error", Content: "Expected register, login_auth, or login message"})
			return
		}
		break
	}

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
		IsAdmin:  client.IsAdmin,
		Content:  "Login successful",
	}); err != nil {
		log.Printf("failed to send login response to %s: %v", remoteAddress, err)
		return
	}

	go client.writePump(hub)
	if store != nil {
		offlineMessages, err := store.TakeOfflineMessages(client.UserCode)
		if err != nil {
			log.Printf("failed to load offline messages for %s: %v", client.Username, err)
		} else {
			for _, offlineMessage := range offlineMessages {
				client.Send <- offlineMessage
			}
		}
	}
	log.Printf("user logged in: %s (admin=%t)", client.Username, client.IsAdmin)

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
			if c.Muted {
				if !c.enqueue(hub, Message{Type: "error", Content: "You are muted"}) {
					return
				}
				continue
			}
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

		case "private_chat":
			if _, err := normalizeUserCode(message.TargetUserCode); err != nil {
				log.Printf("invalid private chat target from %s: %v", c.Username, err)
				if !c.enqueue(hub, Message{
					Type:    "error",
					Content: "Invalid target user code",
				}) {
					return
				}
				continue
			}
			if err := validateTextContent("private chat", message.Content); err != nil {
				log.Printf("invalid private chat message from %s: %v", c.Username, err)
				if !c.enqueue(hub, Message{
					Type:    "error",
					Content: "Invalid private chat content",
				}) {
					return
				}
				continue
			}

			targetCode, _ := normalizeUserCode(message.TargetUserCode)

			hub.Private <- PrivateMessageRequest{
				Sender:     c,
				TargetCode: targetCode,
				Content:    message.Content,
			}

		case "users_request":
			hub.RequestUsers <- c

		case "room_join":
			if err := validateMessage(message); err != nil {
				if !c.enqueue(hub, Message{Type: "error", Content: "Invalid room name"}) {
					return
				}
				continue
			}
			hub.RoomJoin <- RoomRequest{Client: c, Room: message.Room}

		case "room_leave":
			hub.RoomLeave <- c

		case "rooms_request":
			hub.RequestRooms <- c

		case "history_request":
			if err := validateMessage(message); err != nil {
				if !c.enqueue(hub, Message{Type: "error", Content: "Invalid history request"}) {
					return
				}
				continue
			}
			hub.RequestHistory <- HistoryRequest{Client: c, Limit: message.HistoryLimit}

		case "admin_action":
			if err := validateMessage(message); err != nil {
				if !c.enqueue(hub, Message{Type: "error", Content: "Invalid administrator action"}) {
					return
				}
				continue
			}
			hub.AdminAction <- AdminActionRequest{Sender: c, Action: message.Content, TargetCode: message.TargetUserCode}

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
