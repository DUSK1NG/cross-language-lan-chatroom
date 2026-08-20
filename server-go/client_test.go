package main

import (
	"encoding/binary"
	"errors"
	"net"
	"reflect"
	"testing"
	"time"
)

func TestHandleConnectionStopsAfterLoginEOF(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	firstServer, firstClient := net.Pipe()
	firstDone := make(chan struct{})
	go func() {
		handleConnection(firstServer, hub)
		close(firstDone)
	}()

	secondServer, secondClient := net.Pipe()
	secondDone := make(chan struct{})
	go func() {
		handleConnection(secondServer, hub)
		close(secondDone)
	}()

	loginAndDrainJoinMessage(t, firstClient, "Alice", "Alice01")
	loginAndDrainJoinJoinerView(t, secondClient, "Bob", "Bob01")
	drainExpectedSystemMessage(t, firstClient, "Bob#Bob01 joined the chat")

	if err := firstClient.Close(); err != nil {
		t.Fatalf("close first client: %v", err)
	}
	waitForHandler(t, firstDone, "logged-in EOF handler")

	drainExpectedSystemMessage(t, secondClient, "Alice#Alice01 left the chat")
	if err := sendMessage(secondClient, Message{Type: "chat", Content: "Bob remains online"}); err != nil {
		t.Fatalf("send chat after peer EOF: %v", err)
	}
	chat := receiveClientTestMessage(t, secondClient)
	if chat.Type != "chat" || chat.Username != "Bob" || chat.UserCode != "Bob01" || chat.Content != "Bob remains online" {
		t.Fatalf("chat after peer EOF = %+v", chat)
	}

	if err := secondClient.Close(); err != nil {
		t.Fatalf("close second client: %v", err)
	}
	waitForHandler(t, secondDone, "remaining client handler")
}

func TestHandleConnectionStopsAfterInvalidFrame(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	firstServer, firstClient := net.Pipe()
	firstDone := make(chan struct{})
	go func() {
		handleConnection(firstServer, hub)
		close(firstDone)
	}()

	secondServer, secondClient := net.Pipe()
	secondDone := make(chan struct{})
	go func() {
		handleConnection(secondServer, hub)
		close(secondDone)
	}()

	loginAndDrainJoinMessage(t, firstClient, "Alice", "Alice01")
	loginAndDrainJoinJoinerView(t, secondClient, "Bob", "Bob01")
	drainExpectedSystemMessage(t, firstClient, "Bob#Bob01 joined the chat")

	if err := writeRawFrame(firstClient, nil); err != nil {
		t.Fatalf("send invalid zero-length frame: %v", err)
	}
	waitForHandler(t, firstDone, "invalid-frame handler")

	drainExpectedSystemMessage(t, secondClient, "Alice#Alice01 left the chat")
	if err := sendMessage(secondClient, Message{Type: "chat", Content: "server remains available"}); err != nil {
		t.Fatalf("send chat after invalid frame: %v", err)
	}
	chat := receiveClientTestMessage(t, secondClient)
	if chat.Type != "chat" || chat.Username != "Bob" || chat.UserCode != "Bob01" {
		t.Fatalf("chat after invalid frame = %+v", chat)
	}

	_ = firstClient.Close()
	_ = secondClient.Close()
	waitForHandler(t, secondDone, "remaining client handler")
}

func TestHandleConnectionStopsAfterInvalidJSON(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	firstServer, firstClient := net.Pipe()
	firstDone := make(chan struct{})
	go func() {
		handleConnection(firstServer, hub)
		close(firstDone)
	}()

	secondServer, secondClient := net.Pipe()
	secondDone := make(chan struct{})
	go func() {
		handleConnection(secondServer, hub)
		close(secondDone)
	}()

	loginAndDrainJoinMessage(t, firstClient, "Alice", "Alice01")
	loginAndDrainJoinJoinerView(t, secondClient, "Bob", "Bob01")
	drainExpectedSystemMessage(t, firstClient, "Bob#Bob01 joined the chat")

	if err := writeRawFrame(firstClient, []byte("{")); err != nil {
		t.Fatalf("send malformed JSON frame: %v", err)
	}
	waitForHandler(t, firstDone, "invalid-JSON handler")

	drainExpectedSystemMessage(t, secondClient, "Alice#Alice01 left the chat")
	if err := sendMessage(secondClient, Message{Type: "chat", Content: "server remains available"}); err != nil {
		t.Fatalf("send chat after invalid JSON: %v", err)
	}
	chat := receiveClientTestMessage(t, secondClient)
	if chat.Type != "chat" || chat.Username != "Bob" || chat.UserCode != "Bob01" {
		t.Fatalf("chat after invalid JSON = %+v", chat)
	}

	_ = firstClient.Close()
	_ = secondClient.Close()
	waitForHandler(t, secondDone, "remaining client handler")
}

func TestHandleConnectionStopsAfterPreLoginEOF(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	serverConn, clientConn := net.Pipe()
	done := make(chan struct{})
	go func() {
		handleConnection(serverConn, hub)
		close(done)
	}()

	if err := clientConn.Close(); err != nil {
		t.Fatalf("close pre-login client: %v", err)
	}
	waitForHandler(t, done, "pre-login EOF handler")

	serverConn2, clientConn2 := net.Pipe()
	done2 := make(chan struct{})
	go func() {
		handleConnection(serverConn2, hub)
		close(done2)
	}()
	loginAndDrainJoinMessage(t, clientConn2, "Bob", "Bob01")
	if err := clientConn2.Close(); err != nil {
		t.Fatalf("close follow-up client: %v", err)
	}
	waitForHandler(t, done2, "follow-up handler")
}

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

func TestHandleConnectionPrivateChatUsesBoundIdentity(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	firstServer, firstClient := net.Pipe()
	firstDone := make(chan struct{})
	go func() {
		handleConnection(firstServer, hub)
		close(firstDone)
	}()

	secondServer, secondClient := net.Pipe()
	secondDone := make(chan struct{})
	go func() {
		handleConnection(secondServer, hub)
		close(secondDone)
	}()

	loginAndDrainJoinMessage(t, firstClient, "Alice", "A001")
	loginAndDrainJoinJoinerView(t, secondClient, "Bob", "Bob01")
	drainExpectedSystemMessage(t, firstClient, "Bob#Bob01 joined the chat")

	if err := sendMessage(firstClient, Message{
		Type:           "private_chat",
		Username:       "Fake",
		UserCode:       "FAKE01",
		TargetUserCode: "bOb01",
		Content:        "只有 Alice 身份才应被转发",
	}); err != nil {
		t.Fatal(err)
	}

	want := Message{
		Type:           "private_chat",
		MessageID:      "1",
		Username:       "Alice",
		UserCode:       "A001",
		TargetUserCode: "Bob01",
		Content:        "只有 Alice 身份才应被转发",
	}
	if got := receiveClientTestMessage(t, firstClient); !reflect.DeepEqual(got, want) {
		t.Fatalf("sender private message = %+v, want %+v", got, want)
	}
	if got := receiveClientTestMessage(t, secondClient); !reflect.DeepEqual(got, want) {
		t.Fatalf("target private message = %+v, want %+v", got, want)
	}

	_ = firstClient.Close()
	_ = secondClient.Close()
	waitForHandler(t, firstDone, "first private chat handler")
	waitForHandler(t, secondDone, "second private chat handler")
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

func TestHandleConnectionUsersRequest(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	firstServer, firstClient := net.Pipe()
	firstDone := make(chan struct{})
	go func() {
		handleConnection(firstServer, hub)
		close(firstDone)
	}()

	secondServer, secondClient := net.Pipe()
	secondDone := make(chan struct{})
	go func() {
		handleConnection(secondServer, hub)
		close(secondDone)
	}()

	loginAndDrainJoinMessage(t, firstClient, "Zoe", "Z001")
	loginAndDrainJoinMessage(t, secondClient, "Alex", "A001")
	drainExpectedSystemMessage(t, firstClient, "Alex#A001 joined the chat")

	if err := sendMessage(firstClient, Message{Type: "users_request"}); err != nil {
		t.Fatal(err)
	}

	usersResponse := receiveClientTestMessage(t, firstClient)
	want := Message{
		Type:  "users_response",
		Users: []string{"Alex#A001@lobby", "Zoe#Z001@lobby"},
	}
	if !reflect.DeepEqual(usersResponse, want) {
		t.Fatalf("users response = %+v, want %+v", usersResponse, want)
	}

	assertNoMessageWithin(t, secondClient, 200*time.Millisecond)

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

func TestHandleConnectionReturnsErrorForUnknownCommand(t *testing.T) {
	serverConn, clientConn := net.Pipe()
	hub := NewHub()
	go hub.Run()

	done := make(chan struct{})
	go func() {
		handleConnection(serverConn, hub)
		close(done)
	}()

	loginAndDrainJoinMessage(t, clientConn, "Alex", "A001")

	if err := sendMessage(clientConn, Message{Type: "unknown"}); err != nil {
		t.Fatal(err)
	}

	errorMessage := receiveClientTestMessage(t, clientConn)
	if !reflect.DeepEqual(errorMessage, Message{Type: "error", Content: "Expected chat message"}) {
		t.Fatalf("unknown command response = %+v", errorMessage)
	}

	_ = clientConn.Close()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("connection handler did not stop")
	}
}

func TestHandleConnectionQuit(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	firstServer, firstClient := net.Pipe()
	firstDone := make(chan struct{})
	go func() {
		handleConnection(firstServer, hub)
		close(firstDone)
	}()

	secondServer, secondClient := net.Pipe()
	secondDone := make(chan struct{})
	go func() {
		handleConnection(secondServer, hub)
		close(secondDone)
	}()

	loginAndDrainJoinMessage(t, firstClient, "Zoe", "Z001")
	loginAndDrainJoinJoinerView(t, secondClient, "Alex", "A001")
	drainExpectedSystemMessage(t, firstClient, "Alex#A001 joined the chat")

	if err := sendMessage(firstClient, Message{Type: "quit"}); err != nil {
		t.Fatal(err)
	}

	select {
	case <-firstDone:
	case <-time.After(time.Second):
		t.Fatal("first connection handler did not stop after quit")
	}

	leftMessage := receiveClientTestMessage(t, secondClient)
	if leftMessage.Type != "system" || leftMessage.Content != "Zoe#Z001 left the chat" {
		t.Fatalf("leave message = %+v", leftMessage)
	}

	_ = secondClient.Close()

	select {
	case <-secondDone:
	case <-time.After(time.Second):
		t.Fatal("second connection handler did not stop")
	}
}

func TestWritePumpUnregistersAfterWriteFailureAndHealthyClientsContinue(t *testing.T) {
	hub := NewHub()
	go hub.Run()

	healthy := newTestClient(t, "Healthy", "Healthy01")
	if err := registerForTest(t, hub, healthy); err != nil {
		t.Fatalf("register healthy client: %v", err)
	}
	assertMessageReceived(t, healthy.Send, Message{
		Type:    "system",
		Content: "Healthy#Healthy01 joined the chat",
	})

	serverConn, peerConn := net.Pipe()
	defer peerConn.Close()
	failing := newTestClient(t, "Failing", "Failing01")
	failing.Conn = serverConn
	if err := registerForTest(t, hub, failing); err != nil {
		t.Fatalf("register failing client: %v", err)
	}
	assertMessageReceived(t, healthy.Send, Message{
		Type:    "system",
		Content: "Failing#Failing01 joined the chat",
	})
	assertMessageReceived(t, failing.Send, Message{
		Type:    "system",
		Content: "Failing#Failing01 joined the chat",
	})

	done := make(chan struct{})
	go func() {
		failing.writePump(hub)
		close(done)
	}()

	if err := peerConn.Close(); err != nil {
		t.Fatalf("close write peer: %v", err)
	}
	failing.Send <- Message{Type: "chat", Content: "this write must fail"}

	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("timed out waiting for write pump after write failure")
	}
	assertChannelClosed(t, failing.Send)

	assertMessageReceived(t, healthy.Send, Message{
		Type:    "system",
		Content: "Failing#Failing01 left the chat",
	})

	want := Message{Type: "chat", MessageID: "1", Username: "Healthy", Content: "healthy client still receives"}
	hub.Broadcast <- want
	assertMessageReceived(t, healthy.Send, want)
}

const testOperationTimeout = 5 * time.Second

func receiveClientTestMessage(t *testing.T, conn net.Conn) Message {
	t.Helper()

	if err := conn.SetReadDeadline(time.Now().Add(testOperationTimeout)); err != nil {
		t.Fatalf("set read deadline: %v", err)
	}

	message, err := receiveMessage(conn)
	if err != nil {
		t.Fatalf("receive message: %v", err)
	}

	return message
}

func writeRawFrame(conn net.Conn, payload []byte) error {
	frame := make([]byte, 4+len(payload))
	binary.BigEndian.PutUint32(frame[:4], uint32(len(payload)))
	copy(frame[4:], payload)
	_, err := conn.Write(frame)
	return err
}

func waitForHandler(t *testing.T, done <-chan struct{}, name string) {
	t.Helper()

	select {
	case <-done:
	case <-time.After(testOperationTimeout):
		t.Fatalf("timed out waiting for %s to stop", name)
	}
}

func loginAndDrainJoinMessage(t *testing.T, conn net.Conn, username, userCode string) {
	t.Helper()

	if err := sendMessage(conn, Message{
		Type:     "login",
		Username: username,
		UserCode: userCode,
	}); err != nil {
		t.Fatal(err)
	}

	loginOK := receiveClientTestMessage(t, conn)
	if loginOK.Type != "login_ok" || loginOK.Username != username || loginOK.UserCode != userCode {
		t.Fatalf("login response = %+v", loginOK)
	}

	joinMessage := receiveClientTestMessage(t, conn)
	if joinMessage.Type != "system" || joinMessage.Content != username+"#"+userCode+" joined the chat" {
		t.Fatalf("join message = %+v", joinMessage)
	}
}

func loginAndDrainJoinJoinerView(t *testing.T, conn net.Conn, username, userCode string) {
	t.Helper()

	if err := sendMessage(conn, Message{
		Type:     "login",
		Username: username,
		UserCode: userCode,
	}); err != nil {
		t.Fatal(err)
	}

	loginOK := receiveClientTestMessage(t, conn)
	if loginOK.Type != "login_ok" || loginOK.Username != username || loginOK.UserCode != userCode {
		t.Fatalf("login response = %+v", loginOK)
	}

	selfJoin := receiveClientTestMessage(t, conn)
	if selfJoin.Type != "system" || selfJoin.Content != username+"#"+userCode+" joined the chat" {
		t.Fatalf("self join message = %+v", selfJoin)
	}
}

func assertNoMessageWithin(t *testing.T, conn net.Conn, timeout time.Duration) {
	t.Helper()

	if err := conn.SetReadDeadline(time.Now().Add(timeout)); err != nil {
		t.Fatalf("set read deadline: %v", err)
	}

	_, err := receiveMessage(conn)
	var netErr net.Error
	if errors.As(err, &netErr) && netErr.Timeout() {
		return
	}
	if err != nil {
		t.Fatalf("expected timeout, got %v", err)
	}

	t.Fatal("expected no message, but received one")
}

func drainExpectedSystemMessage(t *testing.T, conn net.Conn, content string) {
	t.Helper()

	message := receiveClientTestMessage(t, conn)
	if message.Type != "system" || message.Content != content {
		t.Fatalf("system message = %+v", message)
	}
}
