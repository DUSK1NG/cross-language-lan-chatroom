package main

import (
	"fmt"
	"log"
	"net"
)

func handleConnection(conn net.Conn, registry *ClientRegistry) {
	remoteAddress := conn.RemoteAddr().String()
	log.Printf("Client connected: %s", remoteAddress)

	defer func() {
		if recovered := recover(); recovered != nil {
			log.Printf("client handler panic for %s: %v", remoteAddress, recovered)
		}
		_ = conn.Close()
		log.Printf("Client disconnected: %s", remoteAddress)
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

	username := loginMessage.Username
	registry.Add(conn, username)
	defer func() {
		registry.Remove(conn)
		log.Printf("Active clients: %d", registry.Count())
	}()

	fmt.Printf("Login username: %s\n", username)
	log.Printf("Active clients: %d", registry.Count())
	if err := sendMessage(conn, Message{
		Type:    "login_ok",
		Content: "Login successful",
	}); err != nil {
		log.Printf("failed to send login response to %s: %v", remoteAddress, err)
		return
	}

	chatMessage, err := receiveMessage(conn)
	if err != nil {
		log.Printf("failed to receive chat message from %s: %v", remoteAddress, err)
		_ = sendMessage(conn, Message{
			Type:    "error",
			Content: "Invalid JSON message",
		})
		return
	}
	if chatMessage.Type != "chat" {
		_ = sendMessage(conn, Message{
			Type:    "error",
			Content: "Expected chat message",
		})
		return
	}
	if err := validateMessage(chatMessage); err != nil {
		log.Printf("invalid chat message from %s: %v", remoteAddress, err)
		_ = sendMessage(conn, Message{
			Type:    "error",
			Content: "Invalid chat content",
		})
		return
	}

	fmt.Printf("Chat from %s: %s\n", username, chatMessage.Content)
	if err := sendMessage(conn, Message{
		Type:     "chat",
		Username: username,
		Content:  chatMessage.Content,
	}); err != nil {
		log.Printf("failed to send chat response to %s: %v", remoteAddress, err)
	}
}
