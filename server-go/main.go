package main

import (
	"fmt"
	"log"
	"net"
)

const listenAddress = "0.0.0.0:8888"

func main() {
	listener, err := net.Listen("tcp", listenAddress)
	if err != nil {
		log.Fatalf("failed to listen on %s: %v", listenAddress, err)
	}
	defer listener.Close()

	log.Printf("listening on %s", listenAddress)

	conn, err := listener.Accept()
	if err != nil {
		log.Fatalf("failed to accept client: %v", err)
	}
	defer conn.Close()

	loginMessage, err := receiveMessage(conn)
	if err != nil {
		log.Printf("failed to receive login message: %v", err)
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
		log.Printf("invalid login message: %v", err)
		_ = sendMessage(conn, Message{
			Type:    "login_error",
			Content: "Invalid username",
		})
		return
	}

	username := loginMessage.Username
	fmt.Printf("Login username: %s\n", username)
	if err := sendMessage(conn, Message{
		Type:    "login_ok",
		Content: "Login successful",
	}); err != nil {
		log.Printf("failed to send login response: %v", err)
		return
	}

	chatMessage, err := receiveMessage(conn)
	if err != nil {
		log.Printf("failed to receive chat message: %v", err)
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
		log.Printf("invalid chat message: %v", err)
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
		log.Printf("failed to send chat response: %v", err)
	}
}
