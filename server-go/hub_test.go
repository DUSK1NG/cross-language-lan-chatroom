package main

import (
	"errors"
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
		if got != want {
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
