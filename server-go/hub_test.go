package main

import (
	"testing"
	"time"
)

func TestHubBroadcastsToAllRegisteredClients(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	first := &Client{Username: "Alice", Send: make(chan Message, 1)}
	second := &Client{Username: "Bob", Send: make(chan Message, 1)}
	hub.Register <- first
	hub.Register <- second

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

	client := &Client{Username: "Alice", Send: make(chan Message, 1)}
	hub.Register <- client
	hub.Unregister <- client

	select {
	case _, ok := <-client.Send:
		if ok {
			t.Fatal("expected client Send channel to be closed")
		}
	case <-time.After(time.Second):
		t.Fatal("timed out waiting for client Send channel to close")
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
		t.Fatal("timed out waiting for broadcast message")
	}
}
