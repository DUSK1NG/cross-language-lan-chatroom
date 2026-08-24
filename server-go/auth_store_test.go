package main

import (
	"database/sql"
	"errors"
	"fmt"
	"net"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

const testDeviceToken = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"

func approveTestDevice(t *testing.T, store *AuthStore, username, userCode string) {
	t.Helper()
	authentication, err := store.AuthenticateDevice(username, userCode, testDeviceToken, true)
	if err != nil {
		t.Fatal(err)
	}
	if !authentication.Approved {
		t.Fatalf("device authentication = %+v, want approved", authentication)
	}
}

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

func TestHandleConnectionSupportsPasswordlessLogin(t *testing.T) {
	store, _ := newTestAuthStore(t)
	approveTestDevice(t, store, "Alice", "ALICE01")
	hub := NewHub()
	go hub.Run()
	serverConn, clientConn := net.Pipe()
	done := make(chan struct{})
	go func() {
		handleConnectionWithStore(serverConn, hub, store)
		close(done)
	}()

	if err := sendMessage(clientConn, Message{Type: "login", Username: "Alice", UserCode: "ALICE01", DeviceToken: testDeviceToken}); err != nil {
		t.Fatal(err)
	}
	loginOK := receiveClientTestMessage(t, clientConn)
	if loginOK.Type != "login_ok" || loginOK.Username != "Alice" || loginOK.UserCode != "ALICE01" {
		t.Fatalf("login response = %+v", loginOK)
	}
	_ = clientConn.Close()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("authenticated connection did not stop")
	}
}

func TestHandleConnectionAllowsAuthenticatedAccountToReconnect(t *testing.T) {
	store, _ := newTestAuthStore(t)
	if _, err := store.EnsureIdentity("Alice", "ALICE01"); err != nil {
		t.Fatal(err)
	}
	approveTestDevice(t, store, "Alice", "ALICE01")
	hub := NewHub()
	go hub.Run()

	firstServer, firstClient := net.Pipe()
	firstDone := make(chan struct{})
	go func() {
		handleConnectionWithStore(firstServer, hub, store)
		close(firstDone)
	}()
	if err := sendMessage(firstClient, Message{Type: "login", Username: "Alice", UserCode: "ALICE01", DeviceToken: testDeviceToken}); err != nil {
		t.Fatal(err)
	}
	firstLogin := receiveClientTestMessage(t, firstClient)
	if firstLogin.Type != "login_ok" || firstLogin.UserCode != "ALICE01" {
		t.Fatalf("first login response = %+v", firstLogin)
	}
	_ = firstClient.Close()
	waitForHandler(t, firstDone, "first authenticated connection")

	secondServer, secondClient := net.Pipe()
	secondDone := make(chan struct{})
	go func() {
		handleConnectionWithStore(secondServer, hub, store)
		close(secondDone)
	}()
	if err := sendMessage(secondClient, Message{Type: "login", Username: "alice", UserCode: "alice01", DeviceToken: testDeviceToken}); err != nil {
		t.Fatal(err)
	}
	secondLogin := receiveClientTestMessage(t, secondClient)
	if secondLogin.Type != "login_ok" || secondLogin.Username != "Alice" || secondLogin.UserCode != "ALICE01" {
		t.Fatalf("second login response = %+v", secondLogin)
	}
	_ = secondClient.Close()
	waitForHandler(t, secondDone, "second authenticated connection")
}

func TestAuthStoreRequiresApprovalForNewRemoteDevice(t *testing.T) {
	store, _ := newTestAuthStore(t)
	remoteToken := strings.Repeat("B", 43)
	pending, err := store.AuthenticateDevice("Bob", "BOB001", remoteToken, false)
	if err != nil {
		t.Fatal(err)
	}
	if pending.Approved || pending.Request.ID < 1 || pending.Request.Status != "pending" {
		t.Fatalf("pending authentication = %+v", pending)
	}
	requests, err := store.PendingDeviceRequests()
	if err != nil || len(requests) != 1 || requests[0].ID != pending.Request.ID {
		t.Fatalf("pending requests = %+v, error = %v", requests, err)
	}
	if _, err := store.ResolveDeviceRequest(pending.Request.ID, true, "ADMIN01"); err != nil {
		t.Fatal(err)
	}
	approved, err := store.AuthenticateDevice("Bob", "BOB001", remoteToken, false)
	if err != nil || !approved.Approved || approved.Account.UserCode != "BOB001" {
		t.Fatalf("approved authentication = %+v, error = %v", approved, err)
	}
}

func TestDeviceTokenHashRejectsMalformedCredentials(t *testing.T) {
	for _, token := range []string{"", strings.Repeat("A", 42), strings.Repeat("A", 44), strings.Repeat("!", 43)} {
		if _, err := deviceTokenHash(token); err == nil {
			t.Fatalf("deviceTokenHash(%q) unexpectedly succeeded", token)
		}
	}
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
