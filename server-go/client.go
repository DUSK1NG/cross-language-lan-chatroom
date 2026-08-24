package main

import (
	"errors"
	"io"
	"log"
	"net"
	"strings"
	"time"
)

const connectionApprovalTimeout = 60 * time.Second

func isLoopbackRemote(address net.Addr) bool {
	if address == nil {
		return false
	}
	host, _, err := net.SplitHostPort(address.String())
	if err != nil {
		return false
	}
	return net.ParseIP(host).IsLoopback()
}

func handleConnection(conn net.Conn, hub *Hub) {
	handleConnectionWithStore(conn, hub, nil)
}

func handleConnectionWithStore(conn net.Conn, hub *Hub, store *AuthStore) {
	remoteAddress := conn.RemoteAddr().String()
	log.Printf("client connected: %s", remoteAddress)
	setLoginDeadline(conn)

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
			_ = sendMessage(conn, Message{Type: "login_error", Content: "Password registration has been removed; use username and user code"})
			return

		case "login_auth":
			_ = sendMessage(conn, Message{Type: "login_error", Content: "Password login has been removed; use username and user code"})
			return

		case "login":
			if err := validateMessage(loginMessage); err != nil {
				_ = sendMessage(conn, Message{Type: "login_error", Content: "Invalid username"})
				return
			}
			if store != nil {
				bootstrapAdmin := hub != nil && hub.AdminCode != "" &&
					strings.EqualFold(loginMessage.UserCode, hub.AdminCode) && isLoopbackRemote(conn.RemoteAddr())
				account, err := store.ResolveConnectionIdentity(loginMessage.Username, loginMessage.UserCode)
				if err != nil {
					log.Printf("connection identity rejected for %s: %v", remoteAddress, err)
					_ = sendMessage(conn, Message{Type: "login_error", Content: "Connection identity failed"})
					return
				}
				if !bootstrapAdmin {
					if hub == nil {
						_ = sendMessage(conn, Message{Type: "login_error", Content: "Connection approval is unavailable"})
						return
					}
					created := make(chan string, 1)
					decision := make(chan ConnectionApprovalDecision, 1)
					hub.ConnectionApproval <- ConnectionApprovalRequest{
						Username: account.Username, UserCode: account.UserCode,
						NormalizedCode: strings.ToLower(account.UserCode), RemoteAddress: remoteAddress,
						Created: created, Decision: decision,
					}
					approvalID := <-created
					if approvalID == "" {
						_ = sendMessage(conn, Message{Type: "login_error", Content: "Connection approval could not be created"})
						return
					}
					_ = sendMessage(conn, Message{Type: "login_pending",
						MessageID: approvalID,
						Content:   "Waiting for the room owner to approve this connection"})
					_ = conn.SetDeadline(time.Now().Add(connectionApprovalTimeout))
					approved := false
					reason := "The room owner did not approve the connection in time"
					select {
					case resolution := <-decision:
						approved = resolution.Approved
						if resolution.Reason != "" {
							reason = resolution.Reason
						}
					case <-time.After(connectionApprovalTimeout):
					}
					select {
					case hub.CancelConnectionApproval <- approvalID:
					default:
					}
					if !approved {
						_ = sendMessage(conn, Message{Type: "login_error", Content: reason})
						return
					}
				}
				account, err = store.EnsureIdentity(account.Username, account.UserCode)
				if err != nil {
					log.Printf("failed to persist approved identity for %s: %v", remoteAddress, err)
					_ = sendMessage(conn, Message{Type: "login_error", Content: "Approved connection could not be completed"})
					return
				}
				normalizedCode, _ := normalizeUserCode(account.UserCode)
				client = newClient(conn, account.Username, account.UserCode, normalizedCode)
				client.AccountBacked = true
			} else {
				normalizedCode, _ := normalizeUserCode(loginMessage.UserCode)
				client = newClient(conn, loginMessage.Username, loginMessage.UserCode, normalizedCode)
			}

		default:
			_ = sendMessage(conn, Message{Type: "login_error", Content: "Expected login message"})
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
	_ = conn.SetDeadline(time.Time{})

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
		setReadDeadline(c.Conn)
		message, err := receiveMessage(c.Conn)
		if err != nil {
			if !errors.Is(err, io.EOF) {
				log.Printf("failed to receive message from %s: %v", c.Username, err)
			}
			return
		}
		if message.Type != "quit" && !c.allowInboundMessage(time.Now()) {
			if !c.enqueue(hub, Message{Type: "error", Content: "Rate limit exceeded; please slow down"}) {
				return
			}
			continue
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

		case "room_create":
			if err := validateMessage(message); err != nil {
				if !c.enqueue(hub, Message{Type: "error", Content: "Invalid channel name"}) {
					return
				}
				continue
			}
			hub.RoomCreate <- RoomCreateRequest{Client: c, Room: message.Room, Private: message.Private}

		case "room_action":
			if err := validateMessage(message); err != nil {
				if !c.enqueue(hub, Message{Type: "error", Content: "Invalid channel action"}) {
					return
				}
				continue
			}
			hub.RoomAction <- RoomActionRequest{Sender: c, Action: message.Content, Room: message.Room, TargetCode: message.TargetUserCode}

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
			hub.History <- HistoryRequest{Client: c, Room: message.Room,
				TargetCode: message.TargetUserCode, Private: message.Private,
				BeforeMessageID: message.BeforeMessageID, Limit: message.Limit}

		case "admin_action":
			if err := validateMessage(message); err != nil {
				if !c.enqueue(hub, Message{Type: "error", Content: "Invalid administrator action"}) {
					return
				}
				continue
			}
			hub.AdminAction <- AdminActionRequest{Sender: c, Action: message.Content, TargetCode: message.TargetUserCode, MessageID: message.MessageID, CommandID: message.CommandID}

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
		setWriteDeadline(c.Conn)
		if err := sendMessage(c.Conn, message); err != nil {
			log.Printf("failed to send message to %s: %v", c.Username, err)
			if hub != nil {
				hub.Unregister <- c
			}
			c.closeConnection()
			return
		}
		if c.disconnectAfterFlush.Load() &&
			message.Type == "system" &&
			message.Content == "You were kicked by the administrator" {
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
