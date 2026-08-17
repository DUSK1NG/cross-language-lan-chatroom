package main

import (
	"net"
	"reflect"
	"testing"
	"time"
)

func TestHandleConnectionUsesBoundIdentity(t *testing.T) {
	serverConn, clientConn := net.Pipe()
	hub := NewHub()
	go hub.Run()

	done := make(chan struct{})
	go func() {
		handleConnection(serverConn, hub)
		close(done)
	}()

	if err := sendMessage(clientConn, Message{
		Type:     "login",
		Username: "Alex",
		UserCode: "A001",
	}); err != nil {
		t.Fatal(err)
	}

	loginOK := receiveClientTestMessage(t, clientConn)
	if loginOK.Type != "login_ok" || loginOK.Username != "Alex" || loginOK.UserCode != "A001" || loginOK.Content != "Login successful" {
		t.Fatalf("login response = %+v", loginOK)
	}

	if err := sendMessage(clientConn, Message{
		Type:     "chat",
		Username: "Fake",
		UserCode: "FAKE01",
		Content:  "真实消息",
	}); err != nil {
		t.Fatal(err)
	}

	for {
		message := receiveClientTestMessage(t, clientConn)
		if message.Type != "chat" {
			continue
		}
		if message.Username != "Alex" || message.UserCode != "A001" {
			t.Fatalf("broadcast identity = %+v", message)
		}
		if message.Content != "真实消息" {
			t.Fatalf("broadcast content = %+v", message)
		}
		break
	}

	_ = clientConn.Close()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("connection handler did not stop")
	}
}

func TestHandleConnectionReturnsDuplicateCodeError(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	firstServer, firstClient := net.Pipe()
	firstDone := make(chan struct{})
	go func() {
		handleConnection(firstServer, hub)
		close(firstDone)
	}()

	if err := sendMessage(firstClient, Message{
		Type:     "login",
		Username: "Alex",
		UserCode: "Alex2026",
	}); err != nil {
		t.Fatal(err)
	}

	firstLogin := receiveClientTestMessage(t, firstClient)
	if firstLogin.Type != "login_ok" {
		t.Fatalf("first login response = %+v", firstLogin)
	}

	secondServer, secondClient := net.Pipe()
	secondDone := make(chan struct{})
	go func() {
		handleConnection(secondServer, hub)
		close(secondDone)
	}()

	if err := sendMessage(secondClient, Message{
		Type:     "login",
		Username: "Blake",
		UserCode: "alex2026",
	}); err != nil {
		t.Fatal(err)
	}

	loginError := receiveClientTestMessage(t, secondClient)
	if !reflect.DeepEqual(loginError, Message{Type: "login_error", Content: "User code already exists"}) {
		t.Fatalf("duplicate login response = %+v", loginError)
	}

	_ = firstClient.Close()
	_ = secondClient.Close()

	select {
	case <-firstDone:
	case <-time.After(time.Second):
		t.Fatal("first connection handler did not stop")
	}

	select {
	case <-secondDone:
	case <-time.After(time.Second):
		t.Fatal("second connection handler did not stop")
	}
}

func receiveClientTestMessage(t *testing.T, conn net.Conn) Message {
	t.Helper()

	if err := conn.SetReadDeadline(time.Now().Add(time.Second)); err != nil {
		t.Fatalf("set read deadline: %v", err)
	}

	message, err := receiveMessage(conn)
	if err != nil {
		t.Fatalf("receive message: %v", err)
	}

	return message
}
