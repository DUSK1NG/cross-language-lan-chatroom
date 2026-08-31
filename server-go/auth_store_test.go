package main

import (
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"path/filepath"
	"reflect"
	"sync"
	"testing"
	"time"
)

func TestAuthStorePersistsMLSKeyPackagesAndCommitIdempotency(t *testing.T) {
	store, _ := newTestAuthStore(t)
	if err := store.PublishMLSKeyPackage("alice01", "a2V5"); err != nil {
		t.Fatal(err)
	}
	if got, err := store.FetchMLSKeyPackage("alice01"); err != nil || got != "a2V5" {
		t.Fatalf("key package = %q, err = %v", got, err)
	}
	if err := store.PublishMLSKeyPackage("alice01", "bmV3"); err != nil {
		t.Fatal(err)
	}
	if got, _ := store.FetchMLSKeyPackage("alice01"); got != "bmV3" {
		t.Fatalf("updated key package = %q", got)
	}
	if inserted, err := store.SaveMLSProposal("group-1", "lobby", 3, "proposal-1", "cHJvcG9zYWw="); err != nil || !inserted {
		t.Fatalf("proposal inserted=%t err=%v", inserted, err)
	}
	inserted, err := store.SaveMLSCommit("group-1", "lobby", 3, "Y29tbWl0")
	if err != nil || !inserted {
		t.Fatalf("first commit inserted=%t err=%v", inserted, err)
	}
	inserted, err = store.SaveMLSCommit("group-1", "lobby", 3, "Y29tbWl0")
	if err != nil || inserted {
		t.Fatalf("duplicate commit inserted=%t err=%v", inserted, err)
	}
	if _, err := store.SaveMLSCommit("group-1", "lobby", 3, "b3RoZXI="); !errors.Is(err, ErrMLSCommitConflict) {
		t.Fatalf("conflicting commit error = %v", err)
	}
	if _, err := store.SaveMLSCommit("group-1", "lobby", 2, "b2xk"); !errors.Is(err, ErrMLSEpochRollback) {
		t.Fatalf("rollback commit error = %v", err)
	}
}

func TestAuthStoreMLSWelcomeIsOpaqueAndIdempotent(t *testing.T) {
	store, _ := newTestAuthStore(t)
	if inserted, err := store.SaveMLSWelcome("group-1", "lobby", 2, "bob01", "d2VsY29tZQ=="); err != nil || !inserted {
		t.Fatalf("welcome inserted=%t err=%v", inserted, err)
	}
	if got, err := store.FetchMLSWelcome("group-1", 2, "bob01"); err != nil || got != "d2VsY29tZQ==" {
		t.Fatalf("welcome = %q, err = %v", got, err)
	}
	if inserted, err := store.SaveMLSWelcome("group-1", "lobby", 2, "bob01", "d2VsY29tZQ=="); err != nil || inserted {
		t.Fatalf("duplicate welcome inserted=%t err=%v", inserted, err)
	}
	if _, err := store.SaveMLSWelcome("group-1", "lobby", 2, "bob01", "b3RoZXI="); !errors.Is(err, ErrMLSWelcomeConflict) {
		t.Fatalf("conflicting welcome error = %v", err)
	}
}

func TestAuthStoreListsPendingMLSWelcomesForTarget(t *testing.T) {
	store, _ := newTestAuthStore(t)
	if _, _, err := store.SaveMLSProposalForMember("pending-group", "lobby", "alice01", "add", "bob01", 1, "proposal-1", "cHJvcG9zYWw="); err != nil {
		t.Fatal(err)
	}
	if _, _, err := store.SaveMLSCommitForMember("pending-group", "lobby", "alice01", "proposal-1", 1, "Y29tbWl0"); err != nil {
		t.Fatal(err)
	}
	if inserted, err := store.SaveMLSWelcomeForMember("pending-group", "lobby", "alice01", 1, "bob01", "proposal-1", "d2VsY29tZQ=="); err != nil || !inserted {
		t.Fatalf("pending welcome inserted=%t err=%v", inserted, err)
	}
	pending, err := store.PendingMLSWelcomes("bob01")
	if err != nil || len(pending) != 1 {
		t.Fatalf("pending welcomes=%+v err=%v", pending, err)
	}
	if pending[0].GroupID != "pending-group" || pending[0].Room != "lobby" || pending[0].Epoch != 1 ||
		pending[0].TargetCode != "bob01" || pending[0].ProposalID != "proposal-1" ||
		pending[0].Welcome != "d2VsY29tZQ==" || pending[0].WelcomeDigest == "" {
		t.Fatalf("pending welcome = %+v", pending[0])
	}
	accepted, err := store.AcceptMLSWelcomeForMember("pending-group", "lobby", "bob01", 1, "proposal-1", pending[0].WelcomeDigest)
	if err != nil || !accepted {
		t.Fatalf("accept pending welcome = accepted:%v err:%v", accepted, err)
	}
	pending, err = store.PendingMLSWelcomes("bob01")
	if err != nil || len(pending) != 0 {
		t.Fatalf("accepted welcome still pending=%+v err=%v", pending, err)
	}
}

func TestAuthStoreBlocksMLSProgressWhileWelcomePending(t *testing.T) {
	store, _ := newTestAuthStore(t)
	if _, err := store.EnsureIdentity("Alice", "alice01"); err != nil {
		t.Fatal(err)
	}
	if _, err := store.EnsureIdentity("Bob", "bob01"); err != nil {
		t.Fatal(err)
	}
	if _, _, err := store.SaveMLSProposalForMember("serial-group", "lobby", "alice01", "add", "bob01", 1, "proposal-1", "cHJvcG9zYWw="); err != nil {
		t.Fatal(err)
	}
	if _, _, err := store.SaveMLSCommitForMember("serial-group", "lobby", "alice01", "proposal-1", 1, "Y29tbWl0"); err != nil {
		t.Fatal(err)
	}
	if _, err := store.SaveMLSWelcomeForMember("serial-group", "lobby", "alice01", 1, "bob01", "proposal-1", "d2VsY29tZQ=="); err != nil {
		t.Fatal(err)
	}
	if _, _, err := store.SaveMLSProposalForMember("serial-group", "lobby", "alice01", "add", "bob01", 2, "proposal-2", "cHJvcG9zYWwy"); !errors.Is(err, ErrMLSGroupPendingWelcome) {
		t.Fatalf("proposal while welcome pending error=%v", err)
	}
	if _, _, err := store.SaveMLSCommitForMember("serial-group", "lobby", "alice01", "proposal-2", 2, "Y29tbWl0Mg=="); !errors.Is(err, ErrMLSGroupPendingWelcome) {
		t.Fatalf("commit while welcome pending error=%v", err)
	}
	if _, err := store.SaveMLSProposal("serial-group", "lobby", 2, "legacy-proposal-2", "cHJvcG9zYWwy"); !errors.Is(err, ErrMLSGroupPendingWelcome) {
		t.Fatalf("legacy proposal while welcome pending error=%v", err)
	}
}

func TestAuthStoreDoesNotSilentlyDropStalePendingWelcome(t *testing.T) {
	store, _ := newTestAuthStore(t)
	if _, err := store.EnsureIdentity("Alice", "alice01"); err != nil {
		t.Fatal(err)
	}
	if _, err := store.EnsureIdentity("Bob", "bob01"); err != nil {
		t.Fatal(err)
	}
	if _, _, err := store.SaveMLSProposalForMember("stale-group", "lobby", "alice01", "add", "bob01", 1, "proposal-1", "cHJvcG9zYWw="); err != nil {
		t.Fatal(err)
	}
	if _, _, err := store.SaveMLSCommitForMember("stale-group", "lobby", "alice01", "proposal-1", 1, "Y29tbWl0"); err != nil {
		t.Fatal(err)
	}
	if _, err := store.SaveMLSWelcomeForMember("stale-group", "lobby", "alice01", 1, "bob01", "proposal-1", "d2VsY29tZQ=="); err != nil {
		t.Fatal(err)
	}
	if _, err := store.db.Exec(`UPDATE mls_groups SET current_epoch=2 WHERE group_id='stale-group'`); err != nil {
		t.Fatal(err)
	}
	pending, err := store.PendingMLSWelcomes("bob01")
	if !errors.Is(err, ErrMLSWelcomeStale) {
		t.Fatalf("stale pending welcomes=%+v err=%v", pending, err)
	}
}

func TestAuthStoreMLSCommitConcurrentWithoutProposalNeverAdvances(t *testing.T) {
	store, _ := newTestAuthStore(t)
	const attempts = 2
	results := make(chan error, attempts)
	var wait sync.WaitGroup
	wait.Add(attempts)
	for index := 0; index < attempts; index++ {
		go func() {
			defer wait.Done()
			_, err := store.SaveMLSCommit("concurrent-group", "lobby", 1, "Y29tbWl0")
			results <- err
		}()
	}
	wait.Wait()
	close(results)
	for err := range results {
		if err == nil {
			t.Fatal("an unsolicited concurrent commit advanced the MLS group")
		}
	}
	var count int
	if err := store.db.QueryRow(`SELECT COUNT(1) FROM mls_group_epochs WHERE group_id = ?`, "concurrent-group").Scan(&count); err != nil {
		t.Fatal(err)
	}
	if count != 0 {
		t.Fatalf("concurrent unsolicited commits persisted %d epochs", count)
	}
}

func TestAuthStoreMLSProposalIsIdempotentAndRejectsConflicts(t *testing.T) {
	store, _ := newTestAuthStore(t)
	inserted, err := store.SaveMLSProposal("group-1", "lobby", 2, "proposal-1", "cHJvcG9zYWw=")
	if err != nil || !inserted {
		t.Fatalf("first proposal inserted=%t err=%v", inserted, err)
	}
	inserted, err = store.SaveMLSProposal("group-1", "lobby", 2, "proposal-1", "cHJvcG9zYWw=")
	if err != nil || inserted {
		t.Fatalf("duplicate proposal inserted=%t err=%v", inserted, err)
	}
	if _, err := store.SaveMLSProposal("group-1", "lobby", 2, "proposal-1", "b3RoZXI="); !errors.Is(err, ErrMLSProposalConflict) {
		t.Fatalf("conflicting proposal error = %v", err)
	}
	if _, err := store.SaveMLSProposal("group-1", "lobby", 2, "proposal-2", "cHJvcG9zYWw="); err != nil {
		t.Fatalf("same-content proposal should be idempotent: %v", err)
	}
	if _, err := store.SaveMLSCommit("group-1", "lobby", 2, "Y29tbWl0"); err != nil {
		t.Fatalf("commit after proposal: %v", err)
	}
	if _, err := store.SaveMLSProposal("group-1", "lobby", 1, "proposal-old", "b2xk"); !errors.Is(err, ErrMLSEpochRollback) {
		t.Fatalf("proposal rollback error = %v", err)
	}
}

func TestAuthStorePersistsOpaqueCryptoEnvelope(t *testing.T) {
	store, _ := newTestAuthStore(t)
	want := json.RawMessage(`{"v":1,"alg":"xchacha20poly1305","ct":"opaque"}`)
	if _, err := store.SaveChatMessageIfNew(Message{Type: "chat", MessageID: "encrypted-1", Username: "Alice", UserCode: "A001", Room: "lobby", Crypto: want}); err != nil {
		t.Fatal(err)
	}
	page, err := store.LoadHistory(HistoryQuery{UserCode: "A001", Room: "lobby", Limit: 10})
	if err != nil || len(page.Messages) != 1 || !reflect.DeepEqual(page.Messages[0].Crypto, want) {
		t.Fatalf("encrypted history = %+v err=%v", page.Messages, err)
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

func TestAuthStoreMigratesLegacyGlobalMessageIDConstraint(t *testing.T) {
	dbPath := filepath.Join(t.TempDir(), "legacy-chat.db")
	db, err := sql.Open("sqlite", dbPath)
	if err != nil {
		t.Fatal(err)
	}
	_, err = db.Exec(`CREATE TABLE chat_messages (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        message_id TEXT NOT NULL UNIQUE,
        kind TEXT NOT NULL,
        conversation_key TEXT NOT NULL,
        room TEXT,
        sender_username TEXT NOT NULL,
        sender_code TEXT NOT NULL,
        target_code TEXT,
        content TEXT NOT NULL,
        created_at TEXT NOT NULL,
        recalled INTEGER NOT NULL DEFAULT 0
    );
    INSERT INTO chat_messages
        (message_id, kind, conversation_key, room, sender_username, sender_code, target_code, content, created_at, recalled)
        VALUES ('legacy-message-id', 'room', 'lobby', 'lobby', 'Alice', 'a001', '', 'kept after migration', '2026-08-24T00:00:00Z', 0);`)
	if err != nil {
		t.Fatal(err)
	}
	if err := db.Close(); err != nil {
		t.Fatal(err)
	}

	store, err := openAuthStore(dbPath)
	if err != nil {
		t.Fatal(err)
	}
	defer store.Close()
	inserted, err := store.SaveChatMessageIfNew(Message{Type: "chat", MessageID: "legacy-message-id",
		Username: "Alice", UserCode: "A001", Room: "engineering", Content: "allowed in another conversation"})
	if err != nil || !inserted {
		t.Fatalf("save scoped replacement after legacy migration = inserted:%v err:%v", inserted, err)
	}
	history, err := store.LoadHistory(HistoryQuery{UserCode: "A001", Room: "lobby", Limit: 10})
	if err != nil || len(history.Messages) != 1 || history.Messages[0].Content != "kept after migration" {
		t.Fatalf("legacy history after migration = %+v err:%v", history, err)
	}
}

func TestAuthStoreSearchHistoryIsUnicodeAndConversationScoped(t *testing.T) {
	store, _ := newTestAuthStore(t)
	messages := []Message{
		{Type: "chat", MessageID: "search-1", Username: "Alice", UserCode: "A001", Room: "lobby", Content: "你好，局域网聊天"},
		{Type: "chat", MessageID: "search-2", Username: "Alice", UserCode: "A001", Room: "engineering", Content: "你好，工程频道"},
		{Type: "chat", MessageID: "search-3", Username: "Bob", UserCode: "B001", Room: "lobby", Content: "你好，来自 Bob"},
	}
	for _, message := range messages {
		if err := store.SaveChatMessage(message); err != nil {
			t.Fatal(err)
		}
	}

	page, err := store.LoadHistory(HistoryQuery{UserCode: "A001", Room: "lobby", SearchQuery: "你好", Limit: 10})
	if err != nil {
		t.Fatal(err)
	}
	if len(page.Messages) != 2 || page.Messages[0].MessageID != "search-1" || page.Messages[1].MessageID != "search-3" {
		t.Fatalf("scoped Unicode search = %+v", page)
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

func TestConnectionApprovalTimeoutReturnsReadableLoginError(t *testing.T) {
	originalTimeout := connectionApprovalTimeout
	connectionApprovalTimeout = 10 * time.Millisecond
	t.Cleanup(func() { connectionApprovalTimeout = originalTimeout })

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
	_ = <-admin.Send

	loginError := receiveClientTestMessage(t, clientConn)
	if loginError.Type != "login_error" || loginError.Content != "The room owner did not approve the connection in time" {
		t.Fatalf("timeout login response = %+v", loginError)
	}
	_ = clientConn.Close()
	waitForHandler(t, done, "timed out connection")
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

func TestApprovedReconnectReplacesStaleActiveSession(t *testing.T) {
	store, _ := newTestAuthStore(t)
	if _, err := store.EnsureIdentity("Bob", "BOB001"); err != nil {
		t.Fatalf("seed existing member: %v", err)
	}

	hub := NewHub()
	admin := newTestClient(t, "Host", "HOST01")
	admin.IsAdmin = true
	hub.Clients[admin] = true
	hub.ActiveCodes[admin.NormalizedCode] = admin
	stale := newTestClient(t, "Bob", "BOB001")
	stale.AccountBacked = true
	hub.Clients[stale] = true
	hub.ActiveCodes[stale.NormalizedCode] = stale
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
		t.Fatalf("reconnect pending login response = %+v", pending)
	}
	approval := <-admin.Send
	if approval.Type != "connection_approval_request" || approval.MessageID != pending.MessageID {
		t.Fatalf("reconnect approval notification = %+v", approval)
	}

	hub.AdminAction <- AdminActionRequest{Sender: admin, Action: "approve_connection", MessageID: pending.MessageID}
	assertMessageReceived(t, admin.Send, Message{Type: "connection_approval_result", MessageID: pending.MessageID,
		Username: "Bob", UserCode: "BOB001", Content: "approved"})
	loginOK := receiveClientTestMessage(t, clientConn)
	if loginOK.Type != "login_ok" || loginOK.Username != "Bob" || loginOK.UserCode != "BOB001" {
		t.Fatalf("approved reconnect response = %+v", loginOK)
	}
	if active := hub.ActiveCodes[stale.NormalizedCode]; active == stale {
		t.Fatal("stale session still owns the active user code")
	}

	_ = clientConn.Close()
	waitForHandler(t, done, "approved reconnect")
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
