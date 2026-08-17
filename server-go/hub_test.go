package main

import (
	"errors"
	"net"
	"reflect"
	"testing"
	"time"
)

func TestHubRejectsDuplicateUserCodesCaseInsensitively(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	first := newTestClient(t, "Alice", "Alex2026")
	if err := registerForTest(t, hub, first); err != nil {
		t.Fatalf("register first client: %v", err)
	}

	second := newTestClient(t, "Bob", "alex2026")
	err := registerForTest(t, hub, second)
	if !errors.Is(err, ErrUserCodeAlreadyUsed) {
		t.Fatalf("register second client error = %v, want %v", err, ErrUserCodeAlreadyUsed)
	}
}

func TestHubRejectsReusedUserCodeAfterUnregister(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	first := newTestClient(t, "Alice", "Alex2026")
	if err := registerForTest(t, hub, first); err != nil {
		t.Fatalf("register first client: %v", err)
	}
	assertMessageReceived(t, first.Send, Message{
		Type:    "system",
		Content: "Alice#Alex2026 joined the chat",
	})

	hub.Unregister <- first
	assertChannelClosed(t, first.Send)

	second := newTestClient(t, "Bob", "Alex2026")
	err := registerForTest(t, hub, second)
	if !errors.Is(err, ErrUserCodeAlreadyUsed) {
		t.Fatalf("register second client error = %v, want %v", err, ErrUserCodeAlreadyUsed)
	}
}

func TestHubBroadcastsJoinMessageOnFirstRegistration(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	client := newTestClient(t, "Alice", "Alex2026")
	if err := registerForTest(t, hub, client); err != nil {
		t.Fatalf("register client: %v", err)
	}

	assertMessageReceived(t, client.Send, Message{
		Type:    "system",
		Content: "Alice#Alex2026 joined the chat",
	})
}

func TestHubRejectsRegistrationWithoutNormalizedCode(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	client := &Client{
		Username: "Alice",
		UserCode: "Alex2026",
		Send:     make(chan Message, 8),
	}

	err := registerForTest(t, hub, client)
	if err == nil {
		t.Fatal("expected registration without normalized code to fail")
	}
}

func TestHubBroadcastsLeaveMessageToRemainingClients(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	first := newTestClient(t, "Alice", "Alex2026")
	second := newTestClient(t, "Bob", "Bob2026")
	if err := registerForTest(t, hub, first); err != nil {
		t.Fatalf("register first client: %v", err)
	}
	if err := registerForTest(t, hub, second); err != nil {
		t.Fatalf("register second client: %v", err)
	}

	assertMessageReceived(t, first.Send, Message{
		Type:    "system",
		Content: "Alice#Alex2026 joined the chat",
	})
	assertMessageReceived(t, first.Send, Message{
		Type:    "system",
		Content: "Bob#Bob2026 joined the chat",
	})
	assertMessageReceived(t, second.Send, Message{
		Type:    "system",
		Content: "Bob#Bob2026 joined the chat",
	})

	hub.Unregister <- first
	assertChannelClosed(t, first.Send)

	assertMessageReceived(t, second.Send, Message{
		Type:    "system",
		Content: "Alice#Alex2026 left the chat",
	})
}

func TestHubBroadcastsToAllRegisteredClients(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	first := newTestClient(t, "Alice", "Alice01")
	second := newTestClient(t, "Bob", "Bob2026")
	if err := registerForTest(t, hub, first); err != nil {
		t.Fatalf("register first client: %v", err)
	}
	if err := registerForTest(t, hub, second); err != nil {
		t.Fatalf("register second client: %v", err)
	}

	assertMessageReceived(t, first.Send, Message{
		Type:    "system",
		Content: "Alice#Alice01 joined the chat",
	})
	assertMessageReceived(t, first.Send, Message{
		Type:    "system",
		Content: "Bob#Bob2026 joined the chat",
	})
	assertMessageReceived(t, second.Send, Message{
		Type:    "system",
		Content: "Bob#Bob2026 joined the chat",
	})

	want := Message{
		Type:     "chat",
		Username: "Alice",
		Content:  "你好，大家好",
	}
	hub.Broadcast <- want

	assertMessageReceived(t, first.Send, want)
	assertMessageReceived(t, second.Send, want)
}

func TestHubRespondsWithSortedOnlineUsers(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	first := newTestClient(t, "Zoe", "Z001")
	second := newTestClient(t, "Alex", "A001")
	if err := registerForTest(t, hub, first); err != nil {
		t.Fatalf("register first client: %v", err)
	}
	if err := registerForTest(t, hub, second); err != nil {
		t.Fatalf("register second client: %v", err)
	}

	assertMessageReceived(t, first.Send, Message{
		Type:    "system",
		Content: "Zoe#Z001 joined the chat",
	})
	assertMessageReceived(t, first.Send, Message{
		Type:    "system",
		Content: "Alex#A001 joined the chat",
	})
	assertMessageReceived(t, second.Send, Message{
		Type:    "system",
		Content: "Alex#A001 joined the chat",
	})

	hub.RequestUsers <- first

	assertMessageReceived(t, first.Send, Message{
		Type:  "users_response",
		Users: []string{"Alex#A001", "Zoe#Z001"},
	})
	assertNoMessageReceived(t, second.Send)
}

func TestHubUnregisterClosesClientSendChannel(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	client := newTestClient(t, "Alice", "Alice01")
	if err := registerForTest(t, hub, client); err != nil {
		t.Fatalf("register client: %v", err)
	}
	assertMessageReceived(t, client.Send, Message{
		Type:    "system",
		Content: "Alice#Alice01 joined the chat",
	})

	hub.Unregister <- client
	assertChannelClosed(t, client.Send)
}

func TestHubIgnoresOutboundMessageForUnregisteredClient(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	client := newTestClient(t, "Alice", "Alice01")
	if err := registerForTest(t, hub, client); err != nil {
		t.Fatalf("register client: %v", err)
	}
	assertMessageReceived(t, client.Send, Message{
		Type:    "system",
		Content: "Alice#Alice01 joined the chat",
	})

	hub.Unregister <- client
	assertChannelClosed(t, client.Send)

	hub.Outbound <- OutboundMessage{
		Client: client,
		Message: Message{
			Type:    "error",
			Content: "Expected chat message",
		},
	}

	next := newTestClient(t, "Bob", "Bob01")
	if err := registerForTest(t, hub, next); err != nil {
		t.Fatalf("register next client after ignored outbound: %v", err)
	}
	assertMessageReceived(t, next.Send, Message{
		Type:    "system",
		Content: "Bob#Bob01 joined the chat",
	})
}

func TestHandleConnectionRegistersLoginUserCodeAndBroadcastsJoin(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	serverConn, clientConn := net.Pipe()
	defer clientConn.Close()

	done := make(chan struct{})
	go func() {
		handleConnection(serverConn, hub)
		close(done)
	}()

	if err := sendMessage(clientConn, Message{
		Type:     "login",
		Username: "Alice",
		UserCode: "Alex2026",
	}); err != nil {
		t.Fatalf("send login message: %v", err)
	}

	assertMessageFromConn(t, clientConn, Message{
		Type:     "login_ok",
		Username: "Alice",
		UserCode: "Alex2026",
		Content:  "Login successful",
	})
	assertMessageFromConn(t, clientConn, Message{
		Type:    "system",
		Content: "Alice#Alex2026 joined the chat",
	})

	_ = clientConn.Close()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("timed out waiting for handleConnection to exit")
	}
}

func TestHandleConnectionRejectsDuplicateCodeBeforeLoginOK(t *testing.T) {
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
		Username: "Alice",
		UserCode: "Alex2026",
	}); err != nil {
		t.Fatalf("send first login message: %v", err)
	}

	assertMessageFromConn(t, firstClient, Message{
		Type:     "login_ok",
		Username: "Alice",
		UserCode: "Alex2026",
		Content:  "Login successful",
	})
	assertMessageFromConn(t, firstClient, Message{
		Type:    "system",
		Content: "Alice#Alex2026 joined the chat",
	})

	secondServer, secondClient := net.Pipe()
	defer secondClient.Close()

	secondDone := make(chan struct{})
	go func() {
		handleConnection(secondServer, hub)
		close(secondDone)
	}()

	if err := sendMessage(secondClient, Message{
		Type:     "login",
		Username: "Bob",
		UserCode: "alex2026",
	}); err != nil {
		t.Fatalf("send second login message: %v", err)
	}

	assertMessageFromConn(t, secondClient, Message{
		Type:    "login_error",
		Content: "User code already exists",
	})

	_ = firstClient.Close()
	select {
	case <-firstDone:
	case <-time.After(time.Second):
		t.Fatal("timed out waiting for first handleConnection to exit")
	}

	select {
	case <-secondDone:
	case <-time.After(time.Second):
		t.Fatal("timed out waiting for second handleConnection to exit")
	}
}

func registerForTest(t *testing.T, hub *Hub, client *Client) error {
	t.Helper()
	request := RegisterRequest{Client: client, Result: make(chan error, 1)}
	hub.Register <- request
	return <-request.Result
}

func newTestClient(t *testing.T, username string, userCode string) *Client {
	t.Helper()

	normalized, err := normalizeUserCode(userCode)
	if err != nil {
		t.Fatalf("normalizeUserCode(%q): %v", userCode, err)
	}

	return &Client{
		Username:       username,
		UserCode:       userCode,
		NormalizedCode: normalized,
		Send:           make(chan Message, 8),
	}
}

func assertMessageReceived(t *testing.T, messages <-chan Message, want Message) {
	t.Helper()

	select {
	case got := <-messages:
		if !reflect.DeepEqual(got, want) {
			t.Fatalf("received message %+v, want %+v", got, want)
		}
	case <-time.After(time.Second):
		t.Fatal("timed out waiting for message")
	}
}

func assertChannelClosed(t *testing.T, messages <-chan Message) {
	t.Helper()

	select {
	case _, ok := <-messages:
		if ok {
			t.Fatal("expected channel to be closed")
		}
	case <-time.After(time.Second):
		t.Fatal("timed out waiting for channel to close")
	}
}

func assertNoMessageReceived(t *testing.T, messages <-chan Message) {
	t.Helper()

	select {
	case got := <-messages:
		t.Fatalf("received unexpected message %+v", got)
	case <-time.After(100 * time.Millisecond):
	}
}

func assertMessageFromConn(t *testing.T, conn net.Conn, want Message) {
	t.Helper()

	if err := conn.SetReadDeadline(time.Now().Add(time.Second)); err != nil {
		t.Fatalf("set read deadline: %v", err)
	}
	got, err := receiveMessage(conn)
	if err != nil {
		t.Fatalf("receive message: %v", err)
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("received message %+v, want %+v", got, want)
	}
}
