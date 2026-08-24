package main

import (
	"database/sql"
	"errors"
	"fmt"
	"net"
	"path/filepath"
	"testing"
)

func newTestAuthStore(t *testing.T) (*AuthStore, string) {
	t.Helper()
	dbPath := filepath.Join(t.TempDir(), "accounts.db")
	store, err := openAuthStore(dbPath)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = store.Close() })
	return store, dbPath
}

func TestAuthStoreRejectsDuplicateUsernameAndCode(t *testing.T) {
	store, _ := newTestAuthStore(t)
	if err := store.Register("Alice", "ALICE01"); err != nil {
		t.Fatal(err)
	}
	if err := store.Register("alice", "OTHER01"); !errors.Is(err, ErrAccountAlreadyExists) {
		t.Fatalf("duplicate username error = %v", err)
	}
	if err := store.Register("Bob", "alice01"); !errors.Is(err, ErrAccountAlreadyExists) {
		t.Fatalf("duplicate code error = %v", err)
	}
}

func TestAuthStoreEnsureIdentityCreatesAndReusesIdentity(t *testing.T) {
	store, _ := newTestAuthStore(t)
	account, err := store.EnsureIdentity("Alice", "ALICE01")
	if err != nil {
		t.Fatal(err)
	}
	if account.Username != "Alice" || account.UserCode != "ALICE01" {
		t.Fatalf("created account = %+v", account)
	}
	reused, err := store.EnsureIdentity("alice", "alice01")
	if err != nil || reused.Username != "Alice" || reused.UserCode != "ALICE01" {
		t.Fatalf("reused account = %+v, error = %v", reused, err)
	}
}

func TestAuthStoreRejectsMismatchedExistingIdentity(t *testing.T) {
	store, _ := newTestAuthStore(t)
	if _, err := store.EnsureIdentity("Alice", "ALICE01"); err != nil {
		t.Fatal(err)
	}
	if _, err := store.EnsureIdentity("Bob", "ALICE01"); !errors.Is(err, ErrAccountAlreadyExists) {
		t.Fatalf("mismatched identity error = %v", err)
	}
}

func TestAuthStorePersistsAcrossRestart(t *testing.T) {
	store, dbPath := newTestAuthStore(t)
	if _, err := store.EnsureIdentity("Alice", "ALICE01"); err != nil {
		t.Fatal(err)
	}
	if err := store.Close(); err != nil {
		t.Fatal(err)
	}
	reopened, err := openAuthStore(dbPath)
	if err != nil {
		t.Fatal(err)
	}
	defer reopened.Close()
	account, err := reopened.EnsureIdentity("alice", "alice01")
	if err != nil {
		t.Fatal(err)
	}
	if account.Username != "Alice" || account.UserCode != "ALICE01" {
		t.Fatalf("reopened account = %+v", account)
	}
}

func TestHandleConnectionWaitsForRoomOwnerApproval(t *testing.T) {
	store, _ := newTestAuthStore(t)
	hub := NewHub()
	admin := newTestClient(t, "Host", "HOST01")
	admin.IsAdmin = true
	hub.Clients[admin] = true
	hub.ActiveCodes[admin.NormalizedCode] = admin
	go hub.Run()

	serverConn, clientConn := net.Pipe()
	done := make(chan struct{})
	go func() {
		handleConnectionWithStore(serverConn, hub, store)
		close(done)
	}()

	if err := sendMessage(clientConn, Message{Type: "login", Username: "Bob", UserCode: "BOB001"}); err != nil {
		t.Fatal(err)
	}
	pending := receiveClientTestMessage(t, clientConn)
	if pending.Type != "login_pending" || pending.MessageID == "" {
		t.Fatalf("pending login response = %+v", pending)
	}
	approval := <-admin.Send
	if approval.Type != "connection_approval_request" || approval.MessageID != pending.MessageID ||
		approval.Username != "Bob" || approval.UserCode != "BOB001" {
		t.Fatalf("connection approval notification = %+v", approval)
	}

	hub.AdminAction <- AdminActionRequest{Sender: admin, Action: "approve_connection", MessageID: pending.MessageID}
	assertMessageReceived(t, admin.Send, Message{Type: "connection_approval_result", MessageID: pending.MessageID,
		Username: "Bob", UserCode: "BOB001", Content: "approved"})
	loginOK := receiveClientTestMessage(t, clientConn)
	if loginOK.Type != "login_ok" || loginOK.Username != "Bob" || loginOK.UserCode != "BOB001" {
		t.Fatalf("approved login response = %+v", loginOK)
	}
	if found, err := store.HasUserCode("BOB001"); err != nil || !found {
		t.Fatalf("approved identity persisted = %t, error = %v", found, err)
	}
	_ = clientConn.Close()
	waitForHandler(t, done, "approved connection")
}

func TestResolveConnectionIdentityDoesNotPersistBeforeApproval(t *testing.T) {
	store, _ := newTestAuthStore(t)
	account, err := store.ResolveConnectionIdentity("Cara", "C003")
	if err != nil || account.Username != "Cara" || account.UserCode != "C003" {
		t.Fatalf("resolved connection identity = %+v, error = %v", account, err)
	}
	if found, err := store.HasUserCode("C003"); err != nil || found {
		t.Fatalf("unapproved identity persisted = %t, error = %v", found, err)
	}
}

func TestExistingMemberConnectionStillRequiresRoomOwnerApproval(t *testing.T) {
	store, _ := newTestAuthStore(t)
	if _, err := store.EnsureIdentity("Bob", "BOB001"); err != nil {
		t.Fatalf("seed existing member: %v", err)
	}

	hub := NewHub()
	admin := newTestClient(t, "Host", "HOST01")
	admin.IsAdmin = true
	hub.Clients[admin] = true
	hub.ActiveCodes[admin.NormalizedCode] = admin
	go hub.Run()

	serverConn, clientConn := net.Pipe()
	done := make(chan struct{})
	go func() {
		handleConnectionWithStore(serverConn, hub, store)
		close(done)
	}()

	if err := sendMessage(clientConn, Message{Type: "login", Username: "Bob", UserCode: "BOB001"}); err != nil {
		t.Fatal(err)
	}
	pending := receiveClientTestMessage(t, clientConn)
	if pending.Type != "login_pending" || pending.MessageID == "" {
		t.Fatalf("existing member did not wait for approval: %+v", pending)
	}
	approval := <-admin.Send
	if approval.Type != "connection_approval_request" || approval.MessageID != pending.MessageID {
		t.Fatalf("existing member approval notification = %+v", approval)
	}

	hub.AdminAction <- AdminActionRequest{Sender: admin, Action: "deny_connection", MessageID: pending.MessageID}
	loginError := receiveClientTestMessage(t, clientConn)
	if loginError.Type != "login_error" || loginError.Content != "The room owner declined this connection" {
		t.Fatalf("denied existing member response = %+v", loginError)
	}
	_ = clientConn.Close()
	waitForHandler(t, done, "denied existing member connection")
}

func TestAuthStoreUsesConfiguredDatabasePath(t *testing.T) {
	path := filepath.Join(t.TempDir(), "configured.db")
	t.Setenv(authDBPathEnv, path)
	store, err := openAuthStore("")
	if err != nil {
		t.Fatal(err)
	}
	defer store.Close()
	if resolveDBPath("") != path {
		t.Fatalf("resolved database path = %q, want %q", resolveDBPath(""), path)
	}
	if _, err := sql.Open("sqlite", path); err != nil {
		t.Fatal(err)
	}
}

func TestAuthStoreStoresAndTakesOfflineMessages(t *testing.T) {
	store, _ := newTestAuthStore(t)
	if err := store.Register("Bob", "BOB001"); err != nil {
		t.Fatalf("register Bob: %v", err)
	}
	message := Message{Type: "private_chat", Username: "Alice", UserCode: "A001", Content: "你好，Bob"}
	if err := store.SaveOfflineMessage("bob001", message); err != nil {
		t.Fatalf("save offline message: %v", err)
	}
	messages, err := store.TakeOfflineMessages("BOB001")
	if err != nil || len(messages) != 1 {
		t.Fatalf("take offline messages = %#v, %v", messages, err)
	}
	if messages[0].Type != "offline_message" || messages[0].Username != "Alice" || messages[0].Content != message.Content {
		t.Fatalf("offline message = %+v", messages[0])
	}
	remaining, err := store.TakeOfflineMessages("BOB001")
	if err != nil || len(remaining) != 0 {
		t.Fatalf("offline messages were not removed: %#v, %v", remaining, err)
	}
}

func TestAuthStorePersistsAndPagesRoomHistory(t *testing.T) {
	store, dbPath := newTestAuthStore(t)
	defer store.Close()

	for i := 1; i <= 3; i++ {
		if err := store.SaveChatMessage(Message{
			Type: "chat", MessageID: fmt.Sprintf("room-%d", i), Username: "Alice", UserCode: "A001",
			Room: "lobby", Content: fmt.Sprintf("message %d", i), CreatedAt: fmt.Sprintf("2026-08-22T10:00:0%dZ", i),
		}); err != nil {
			t.Fatal(err)
		}
	}

	page, err := store.LoadHistory(HistoryQuery{UserCode: "A001", Room: "lobby", Limit: 2})
	if err != nil {
		t.Fatal(err)
	}
	if len(page.Messages) != 2 || !page.HasMore || page.Messages[0].MessageID != "room-2" || page.Messages[1].MessageID != "room-3" {
		t.Fatalf("first history page = %+v", page)
	}

	older, err := store.LoadHistory(HistoryQuery{UserCode: "A001", Room: "lobby", BeforeMessageID: "room-2", Limit: 2})
	if err != nil {
		t.Fatal(err)
	}
	if len(older.Messages) != 1 || older.Messages[0].MessageID != "room-1" || older.HasMore {
		t.Fatalf("older history page = %+v", older)
	}

	if err := store.Close(); err != nil {
		t.Fatal(err)
	}
	reopened, err := openAuthStore(dbPath)
	if err != nil {
		t.Fatal(err)
	}
	defer reopened.Close()
	reloaded, err := reopened.LoadHistory(HistoryQuery{UserCode: "A001", Room: "lobby", Limit: 10})
	if err != nil || len(reloaded.Messages) != 3 {
		t.Fatalf("reloaded history = %+v, error = %v", reloaded, err)
	}
}

func TestAuthStoreRecallKeepsTombstoneAndPrivateHistoryIsIsolated(t *testing.T) {
	store, _ := newTestAuthStore(t)
	defer store.Close()

	messages := []Message{
		{Type: "private_chat", MessageID: "dm-1", Username: "Alice", UserCode: "A001", TargetUserCode: "B001", Content: "secret", CreatedAt: "2026-08-22T10:00:00Z"},
		{Type: "private_chat", MessageID: "dm-2", Username: "Alice", UserCode: "A001", TargetUserCode: "C001", Content: "other", CreatedAt: "2026-08-22T10:00:01Z"},
	}
	for _, message := range messages {
		if err := store.SaveChatMessage(message); err != nil {
			t.Fatal(err)
		}
	}
	if err := store.MarkMessageRecalled("dm-1"); err != nil {
		t.Fatal(err)
	}

	page, err := store.LoadHistory(HistoryQuery{UserCode: "B001", PeerCode: "A001", Private: true, Limit: 10})
	if err != nil || len(page.Messages) != 1 {
		t.Fatalf("private history = %+v, error = %v", page, err)
	}
	if page.Messages[0].MessageID != "dm-1" || !page.Messages[0].Recalled || page.Messages[0].Content != "消息已撤回" {
		t.Fatalf("recalled private message = %+v", page.Messages[0])
	}

	other, err := store.LoadHistory(HistoryQuery{UserCode: "B001", PeerCode: "C001", Private: true, Limit: 10})
	if err != nil || len(other.Messages) != 0 {
		t.Fatalf("private history leaked across peers = %+v, error = %v", other, err)
	}
}
