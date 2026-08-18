package main

import (
	"database/sql"
	"errors"
	"net"
	"path/filepath"
	"strings"
	"testing"
	"time"
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
	if err := store.Register("Alice", "ALICE01", "correct-password"); err != nil {
		t.Fatal(err)
	}
	if err := store.Register("alice", "OTHER01", "another-password"); !errors.Is(err, ErrAccountAlreadyExists) {
		t.Fatalf("duplicate username error = %v", err)
	}
	if err := store.Register("Bob", "alice01", "another-password"); !errors.Is(err, ErrAccountAlreadyExists) {
		t.Fatalf("duplicate code error = %v", err)
	}
}

func TestAuthStoreRejectsWrongPassword(t *testing.T) {
	store, _ := newTestAuthStore(t)
	if err := store.Register("Alice", "ALICE01", "correct-password"); err != nil {
		t.Fatal(err)
	}
	if _, err := store.Authenticate("Alice", "wrong-password"); !errors.Is(err, ErrInvalidCredentials) {
		t.Fatalf("wrong password error = %v", err)
	}
}

func TestAuthStoreStoresBcryptHashWithoutPlaintext(t *testing.T) {
	store, _ := newTestAuthStore(t)
	password := "correct-password"
	if err := store.Register("Alice", "ALICE01", password); err != nil {
		t.Fatal(err)
	}
	var hash string
	if err := store.db.QueryRow(`SELECT password_hash FROM accounts WHERE normalized_username = ?`, "alice").Scan(&hash); err != nil {
		t.Fatal(err)
	}
	if hash == password || !strings.HasPrefix(hash, "$2") {
		t.Fatalf("password hash = %q; expected bcrypt hash without plaintext", hash)
	}
}

func TestAuthStorePersistsAcrossRestart(t *testing.T) {
	store, dbPath := newTestAuthStore(t)
	if err := store.Register("Alice", "ALICE01", "correct-password"); err != nil {
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
	account, err := reopened.Authenticate("alice", "correct-password")
	if err != nil {
		t.Fatal(err)
	}
	if account.Username != "Alice" || account.UserCode != "ALICE01" || account.PasswordHash == "" {
		t.Fatalf("reopened account = %+v", account)
	}
}

func TestHandleConnectionSupportsRegisterThenPasswordLogin(t *testing.T) {
	store, _ := newTestAuthStore(t)
	hub := NewHub()
	go hub.Run()
	serverConn, clientConn := net.Pipe()
	done := make(chan struct{})
	go func() {
		handleConnectionWithStore(serverConn, hub, store)
		close(done)
	}()

	if err := sendMessage(clientConn, Message{Type: "register", Username: "Alice", UserCode: "ALICE01", Password: "correct-password"}); err != nil {
		t.Fatal(err)
	}
	if response := receiveClientTestMessage(t, clientConn); response.Type != "register_ok" {
		t.Fatalf("register response = %+v", response)
	}
	if err := sendMessage(clientConn, Message{Type: "login_auth", Username: "alice", Password: "correct-password"}); err != nil {
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
	if err := store.Register("Alice", "ALICE01", "correct-password"); err != nil {
		t.Fatal(err)
	}
	hub := NewHub()
	go hub.Run()

	firstServer, firstClient := net.Pipe()
	firstDone := make(chan struct{})
	go func() {
		handleConnectionWithStore(firstServer, hub, store)
		close(firstDone)
	}()
	if err := sendMessage(firstClient, Message{Type: "login_auth", Username: "Alice", Password: "correct-password"}); err != nil {
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
	if err := sendMessage(secondClient, Message{Type: "login_auth", Username: "alice", Password: "correct-password"}); err != nil {
		t.Fatal(err)
	}
	secondLogin := receiveClientTestMessage(t, secondClient)
	if secondLogin.Type != "login_ok" || secondLogin.Username != "Alice" || secondLogin.UserCode != "ALICE01" {
		t.Fatalf("second login response = %+v", secondLogin)
	}
	_ = secondClient.Close()
	waitForHandler(t, secondDone, "second authenticated connection")
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
	if err := store.Register("Bob", "BOB001", "correct-password"); err != nil {
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
