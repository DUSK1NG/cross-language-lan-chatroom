package main

import (
	"errors"
	"net"
	"reflect"
	"sync"
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

func TestHubRoutesPrivateMessageOnlyToSenderAndTarget(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	sender := newTestClient(t, "Alice", "A001")
	target := newTestClient(t, "Bob", "Bob01")
	third := newTestClient(t, "Charlie", "C003")
	if err := registerForTest(t, hub, sender); err != nil {
		t.Fatalf("register %s: %v", sender.Username, err)
	}
	assertMessageReceived(t, sender.Send, Message{Type: "system", Content: "Alice#A001 joined the chat"})

	if err := registerForTest(t, hub, target); err != nil {
		t.Fatalf("register %s: %v", target.Username, err)
	}
	assertMessageReceived(t, sender.Send, Message{Type: "system", Content: "Bob#Bob01 joined the chat"})
	assertMessageReceived(t, target.Send, Message{Type: "system", Content: "Bob#Bob01 joined the chat"})

	if err := registerForTest(t, hub, third); err != nil {
		t.Fatalf("register %s: %v", third.Username, err)
	}
	assertMessageReceived(t, sender.Send, Message{Type: "system", Content: "Charlie#C003 joined the chat"})
	assertMessageReceived(t, target.Send, Message{Type: "system", Content: "Charlie#C003 joined the chat"})
	assertMessageReceived(t, third.Send, Message{Type: "system", Content: "Charlie#C003 joined the chat"})

	hub.Private <- PrivateMessageRequest{
		Sender:     sender,
		TargetCode: "bOB01",
		Content:    "你好，这是私聊。",
	}

	want := Message{
		Type:           "private_chat",
		Username:       "Alice",
		UserCode:       "A001",
		TargetUserCode: "Bob01",
		Content:        "你好，这是私聊。",
	}
	assertMessageReceived(t, sender.Send, want)
	assertMessageReceived(t, target.Send, want)
	assertNoMessageReceived(t, third.Send)
}

func TestHubPrivateMessageErrorsOnlyGoToSender(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	sender := newTestClient(t, "Alice", "A001")
	target := newTestClient(t, "Bob", "Bob01")
	if err := registerForTest(t, hub, sender); err != nil {
		t.Fatalf("register %s: %v", sender.Username, err)
	}
	assertMessageReceived(t, sender.Send, Message{Type: "system", Content: "Alice#A001 joined the chat"})

	if err := registerForTest(t, hub, target); err != nil {
		t.Fatalf("register %s: %v", target.Username, err)
	}
	assertMessageReceived(t, sender.Send, Message{Type: "system", Content: "Bob#Bob01 joined the chat"})
	assertMessageReceived(t, target.Send, Message{Type: "system", Content: "Bob#Bob01 joined the chat"})

	tests := []struct {
		name       string
		targetCode string
		content    string
		wantError  string
	}{
		{name: "unknown target", targetCode: "Missing01", content: "hello", wantError: "Target user not found"},
		{name: "self target", targetCode: "a001", content: "hello", wantError: "Cannot send private message to yourself"},
		{name: "invalid target", targetCode: "bad-code", content: "hello", wantError: "Invalid target user code"},
		{name: "empty content", targetCode: "Bob01", content: "", wantError: "Invalid private chat content"},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			hub.Private <- PrivateMessageRequest{
				Sender:     sender,
				TargetCode: test.targetCode,
				Content:    test.content,
			}
			assertMessageReceived(t, sender.Send, Message{Type: "error", Content: test.wantError})
			assertNoMessageReceived(t, target.Send)
		})
	}
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

func TestHubUnregisterIsIdempotent(t *testing.T) {
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

	sendHubUnregister(t, hub, client)
	assertChannelClosed(t, client.Send)

	// A second unregister must be ignored rather than panic or stop the Hub.
	sendHubUnregister(t, hub, client)
	assertChannelClosed(t, client.Send)

	next := newTestClient(t, "Bob", "Bob01")
	if err := registerForTest(t, hub, next); err != nil {
		t.Fatalf("register client after repeated unregister: %v", err)
	}
	assertMessageReceived(t, next.Send, Message{
		Type:    "system",
		Content: "Bob#Bob01 joined the chat",
	})
}

func TestHubIgnoresConcurrentLateOutboundAfterUnregister(t *testing.T) {
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

	const senderCount = 4
	const messagesPerSender = 32
	var senders sync.WaitGroup
	senders.Add(senderCount)
	finished := make(chan struct{})
	go func() {
		senders.Wait()
		close(finished)
	}()

	for sender := 0; sender < senderCount; sender++ {
		go func(sender int) {
			defer senders.Done()
			for messageIndex := 0; messageIndex < messagesPerSender; messageIndex++ {
				hub.Outbound <- OutboundMessage{
					Client: client,
					Message: Message{
						Type:    "error",
						Content: "late outbound",
					},
				}
			}
		}(sender)
	}

	select {
	case <-finished:
	case <-time.After(2 * time.Second):
		t.Fatal("timed out sending late outbound messages")
	}

	next := newTestClient(t, "Bob", "Bob01")
	if err := registerForTest(t, hub, next); err != nil {
		t.Fatalf("register next client after late outbound: %v", err)
	}
	assertMessageReceived(t, next.Send, Message{
		Type:    "system",
		Content: "Bob#Bob01 joined the chat",
	})
}

func TestHubRemovesSlowClientAndKeepsHealthyClient(t *testing.T) {
	hub := NewHub()

	slow := newTestClient(t, "Slow", "Slow01")
	registerResult := make(chan error, 1)
	hub.handleRegisterRequest(RegisterRequest{Client: slow, Result: registerResult})
	if err := <-registerResult; err != nil {
		t.Fatalf("register slow client: %v", err)
	}
	assertMessageReceived(t, slow.Send, Message{
		Type:    "system",
		Content: "Slow#Slow01 joined the chat",
	})

	pressureMessage := Message{
		Type:     "chat",
		Username: "Server",
		Content:  "buffer pressure",
	}
	for index := 0; index < cap(slow.Send)+1; index++ {
		hub.deliver(slow, pressureMessage)
	}
	assertChannelClosedAfterDraining(t, slow.Send)

	healthy := newTestClient(t, "Healthy", "Healthy01")
	registerResult = make(chan error, 1)
	hub.handleRegisterRequest(RegisterRequest{Client: healthy, Result: registerResult})
	if err := <-registerResult; err != nil {
		t.Fatalf("register healthy client after slow removal: %v", err)
	}
	assertMessageReceived(t, healthy.Send, Message{
		Type:    "system",
		Content: "Healthy#Healthy01 joined the chat",
	})

	want := Message{Type: "chat", Username: "Healthy", Content: "server still works"}
	if !hub.deliver(healthy, want) {
		t.Fatal("healthy client did not accept broadcast")
	}
	assertMessageReceived(t, healthy.Send, want)
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

func sendHubUnregister(t *testing.T, hub *Hub, client *Client) {
	t.Helper()

	select {
	case hub.Unregister <- client:
	case <-time.After(time.Second):
		t.Fatal("timed out sending unregister request")
	}
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

func assertChannelClosedAfterDraining(t *testing.T, messages <-chan Message) {
	t.Helper()

	deadline := time.NewTimer(time.Second)
	defer deadline.Stop()

	for {
		select {
		case _, ok := <-messages:
			if !ok {
				return
			}
		case <-deadline.C:
			t.Fatal("timed out waiting for channel to close after draining")
		}
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
