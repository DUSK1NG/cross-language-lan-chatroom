package main

import (
	"crypto/sha256"
	"database/sql"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"fmt"
	_ "modernc.org/sqlite"
	"os"
	"strings"
	"time"
)

const (
	authDBPathEnv                 = "CHAT_DB_PATH"
	defaultAuthDBPath             = "chat.db"
	maxOfflineMessagesPerUser     = 100
	maxHistoryPageSize            = 100
	maxHistoryRowsPerConversation = 10000
)

var (
	ErrAccountAlreadyExists    = errors.New("account already exists")
	ErrDeviceIdentityMismatch  = errors.New("device identity does not match the requested account")
	ErrDeviceRequestNotPending = errors.New("device request is not pending")
)

type Account struct {
	Username  string
	UserCode  string
	CreatedAt time.Time
}

type DeviceRequest struct {
	ID        int64
	Username  string
	UserCode  string
	Status    string
	CreatedAt time.Time
}

type DeviceAuthentication struct {
	Account  Account
	Request  DeviceRequest
	Approved bool
}

type AuthStore struct {
	db *sql.DB
}

type HistoryQuery struct {
	UserCode        string
	Room            string
	PeerCode        string
	Private         bool
	BeforeMessageID string
	Limit           int
}

type HistoryPage struct {
	Messages []Message
	HasMore  bool
}

type StoredMessage struct {
	Message         Message
	Kind            string
	ConversationKey string
	AuthorCode      string
	TargetCode      string
}

func resolveDBPath(path string) string {
	if path != "" {
		return path
	}
	if envPath := os.Getenv(authDBPathEnv); envPath != "" {
		return envPath
	}
	return defaultAuthDBPath
}

func openAuthStore(path string) (*AuthStore, error) {
	db, err := sql.Open("sqlite", resolveDBPath(path))
	if err != nil {
		return nil, fmt.Errorf("open auth database: %w", err)
	}
	store := &AuthStore{db: db}
	if err := store.initialize(); err != nil {
		_ = db.Close()
		return nil, err
	}
	return store, nil
}

func (s *AuthStore) initialize() error {
	if s == nil || s.db == nil {
		return errors.New("auth store is not initialized")
	}
	const schema = `
CREATE TABLE IF NOT EXISTS accounts (
    username TEXT NOT NULL,
    normalized_username TEXT NOT NULL UNIQUE,
    user_code TEXT NOT NULL,
    normalized_code TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS offline_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    target_code TEXT NOT NULL,
    sender_username TEXT NOT NULL,
    sender_code TEXT NOT NULL,
    content TEXT NOT NULL,
	message_id TEXT,
    created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_offline_messages_target
    ON offline_messages(target_code, id);
CREATE TABLE IF NOT EXISTS chat_messages (
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
CREATE INDEX IF NOT EXISTS idx_chat_messages_conversation
    ON chat_messages(kind, conversation_key, id);
CREATE TABLE IF NOT EXISTS device_requests (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    token_hash TEXT NOT NULL UNIQUE,
    username TEXT NOT NULL,
    normalized_username TEXT NOT NULL,
    user_code TEXT NOT NULL,
    normalized_code TEXT NOT NULL,
    status TEXT NOT NULL,
    created_at TEXT NOT NULL,
    approved_at TEXT,
    approved_by TEXT
);
CREATE INDEX IF NOT EXISTS idx_device_requests_pending
    ON device_requests(status, normalized_code, id);
`
	if _, err := s.db.Exec(schema); err != nil {
		return fmt.Errorf("initialize auth database: %w", err)
	}
	if _, err := s.db.Exec(`ALTER TABLE offline_messages ADD COLUMN message_id TEXT`); err != nil && !strings.Contains(strings.ToLower(err.Error()), "duplicate column") {
		return fmt.Errorf("migrate offline message id: %w", err)
	}
	if _, err := s.db.Exec(`CREATE UNIQUE INDEX IF NOT EXISTS idx_offline_messages_message_id ON offline_messages(message_id)`); err != nil {
		return fmt.Errorf("index offline message id: %w", err)
	}
	return nil
}

func deviceTokenHash(token string) (string, error) {
	// 32 random bytes encoded as unpadded Base64URL always occupy 43 bytes.
	// Reject any other length before decoding so malformed login frames cannot
	// make the database-authentication path process oversized input.
	if len(token) != 43 {
		return "", errors.New("device token is invalid")
	}
	decoded, err := base64.RawURLEncoding.DecodeString(token)
	if err != nil || len(decoded) != 32 {
		return "", errors.New("device token is invalid")
	}
	digest := sha256.Sum256(decoded)
	return hex.EncodeToString(digest[:]), nil
}

func (s *AuthStore) findAccount(username, userCode string) (Account, bool, error) {
	var account Account
	var createdAt string
	err := s.db.QueryRow(`SELECT username, user_code, created_at FROM accounts
WHERE normalized_username = ? OR normalized_code = ?`,
		normalizeUsername(username), strings.ToLower(userCode)).Scan(
		&account.Username, &account.UserCode, &createdAt)
	if errors.Is(err, sql.ErrNoRows) {
		return Account{}, false, nil
	}
	if err != nil {
		return Account{}, false, fmt.Errorf("query account identity: %w", err)
	}
	account.CreatedAt, _ = time.Parse(time.RFC3339Nano, createdAt)
	return account, true, nil
}

func (s *AuthStore) pendingRequestForCode(normalizedCode string) (DeviceRequest, bool, error) {
	var request DeviceRequest
	var createdAt string
	err := s.db.QueryRow(`SELECT id, username, user_code, status, created_at
FROM device_requests WHERE normalized_code = ? AND status = 'pending' ORDER BY id LIMIT 1`, normalizedCode).
		Scan(&request.ID, &request.Username, &request.UserCode, &request.Status, &createdAt)
	if errors.Is(err, sql.ErrNoRows) {
		return DeviceRequest{}, false, nil
	}
	if err != nil {
		return DeviceRequest{}, false, fmt.Errorf("query pending device request: %w", err)
	}
	request.CreatedAt, _ = time.Parse(time.RFC3339Nano, createdAt)
	return request, true, nil
}

func (s *AuthStore) insertDeviceRequest(tokenHash, username, userCode, status string) (DeviceRequest, error) {
	createdAt := time.Now().UTC()
	result, err := s.db.Exec(`INSERT INTO device_requests
(token_hash, username, normalized_username, user_code, normalized_code, status, created_at)
VALUES (?, ?, ?, ?, ?, ?, ?)`, tokenHash, username, normalizeUsername(username), userCode,
		strings.ToLower(userCode), status, createdAt.Format(time.RFC3339Nano))
	if err != nil {
		return DeviceRequest{}, fmt.Errorf("store device request: %w", err)
	}
	id, err := result.LastInsertId()
	if err != nil {
		return DeviceRequest{}, fmt.Errorf("read device request id: %w", err)
	}
	return DeviceRequest{ID: id, Username: username, UserCode: userCode, Status: status, CreatedAt: createdAt}, nil
}

// AuthenticateDevice accepts only a token that was previously approved by a
// room administrator. The first local administrator device is seeded through
// the loopback-only bootstrap path; every other new device becomes pending.
func (s *AuthStore) AuthenticateDevice(username, userCode, deviceToken string, localAdminBootstrap bool) (DeviceAuthentication, error) {
	if s == nil || s.db == nil {
		return DeviceAuthentication{}, errors.New("auth store is not initialized")
	}
	identity := Message{Type: "login", Username: username, UserCode: userCode}
	if err := validateMessage(identity); err != nil {
		return DeviceAuthentication{}, fmt.Errorf("invalid account identity: %w", err)
	}
	tokenHash, err := deviceTokenHash(deviceToken)
	if err != nil {
		return DeviceAuthentication{}, err
	}

	var request DeviceRequest
	var normalizedUsername, normalizedCode, createdAt string
	err = s.db.QueryRow(`SELECT id, username, user_code, status, created_at, normalized_username, normalized_code
FROM device_requests WHERE token_hash = ?`, tokenHash).Scan(
		&request.ID, &request.Username, &request.UserCode, &request.Status, &createdAt,
		&normalizedUsername, &normalizedCode)
	if err == nil {
		request.CreatedAt, _ = time.Parse(time.RFC3339Nano, createdAt)
		if normalizedUsername != normalizeUsername(username) || normalizedCode != strings.ToLower(userCode) {
			return DeviceAuthentication{}, ErrDeviceIdentityMismatch
		}
		if request.Status != "approved" {
			return DeviceAuthentication{Request: request}, nil
		}
		account, found, accountErr := s.findAccount(username, userCode)
		if accountErr != nil {
			return DeviceAuthentication{}, accountErr
		}
		if !found || normalizeUsername(account.Username) != normalizeUsername(username) ||
			strings.ToLower(account.UserCode) != strings.ToLower(userCode) {
			return DeviceAuthentication{}, ErrDeviceIdentityMismatch
		}
		return DeviceAuthentication{Account: account, Request: request, Approved: true}, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return DeviceAuthentication{}, fmt.Errorf("query device credential: %w", err)
	}

	account, accountExists, err := s.findAccount(username, userCode)
	if err != nil {
		return DeviceAuthentication{}, err
	}
	if accountExists && (normalizeUsername(account.Username) != normalizeUsername(username) ||
		strings.ToLower(account.UserCode) != strings.ToLower(userCode)) {
		return DeviceAuthentication{}, ErrAccountAlreadyExists
	}
	if localAdminBootstrap {
		if !accountExists {
			account, err = s.EnsureIdentity(username, userCode)
			if err != nil {
				return DeviceAuthentication{}, err
			}
		}
		request, err = s.insertDeviceRequest(tokenHash, account.Username, account.UserCode, "approved")
		if err != nil {
			return DeviceAuthentication{}, err
		}
		return DeviceAuthentication{Account: account, Request: request, Approved: true}, nil
	}

	if pending, found, pendingErr := s.pendingRequestForCode(strings.ToLower(userCode)); pendingErr != nil {
		return DeviceAuthentication{}, pendingErr
	} else if found {
		if normalizeUsername(pending.Username) != normalizeUsername(username) {
			return DeviceAuthentication{}, ErrAccountAlreadyExists
		}
		return DeviceAuthentication{Request: pending}, nil
	}
	request, err = s.insertDeviceRequest(tokenHash, username, userCode, "pending")
	if err != nil {
		return DeviceAuthentication{}, err
	}
	return DeviceAuthentication{Request: request}, nil
}

func (s *AuthStore) PendingDeviceRequests() ([]DeviceRequest, error) {
	if s == nil || s.db == nil {
		return nil, errors.New("auth store is not initialized")
	}
	rows, err := s.db.Query(`SELECT id, username, user_code, status, created_at
FROM device_requests WHERE status = 'pending' ORDER BY id`)
	if err != nil {
		return nil, fmt.Errorf("query pending device requests: %w", err)
	}
	defer rows.Close()
	requests := make([]DeviceRequest, 0)
	for rows.Next() {
		var request DeviceRequest
		var createdAt string
		if err := rows.Scan(&request.ID, &request.Username, &request.UserCode, &request.Status, &createdAt); err != nil {
			return nil, err
		}
		request.CreatedAt, _ = time.Parse(time.RFC3339Nano, createdAt)
		requests = append(requests, request)
	}
	return requests, rows.Err()
}

func (s *AuthStore) ResolveDeviceRequest(requestID int64, approve bool, approvedBy string) (DeviceRequest, error) {
	if s == nil || s.db == nil {
		return DeviceRequest{}, errors.New("auth store is not initialized")
	}
	var request DeviceRequest
	var createdAt string
	err := s.db.QueryRow(`SELECT id, username, user_code, status, created_at
FROM device_requests WHERE id = ?`, requestID).Scan(
		&request.ID, &request.Username, &request.UserCode, &request.Status, &createdAt)
	if errors.Is(err, sql.ErrNoRows) {
		return DeviceRequest{}, ErrDeviceRequestNotPending
	}
	if err != nil {
		return DeviceRequest{}, fmt.Errorf("load device request: %w", err)
	}
	request.CreatedAt, _ = time.Parse(time.RFC3339Nano, createdAt)
	if request.Status != "pending" {
		return DeviceRequest{}, ErrDeviceRequestNotPending
	}
	if approve {
		if _, err := s.EnsureIdentity(request.Username, request.UserCode); err != nil {
			return DeviceRequest{}, err
		}
		request.Status = "approved"
	} else {
		request.Status = "denied"
	}
	_, err = s.db.Exec(`UPDATE device_requests SET status = ?, approved_at = ?, approved_by = ? WHERE id = ?`,
		request.Status, time.Now().UTC().Format(time.RFC3339Nano), approvedBy, request.ID)
	if err != nil {
		return DeviceRequest{}, fmt.Errorf("resolve device request: %w", err)
	}
	return request, nil
}

func (s *AuthStore) HasUserCode(userCode string) (bool, error) {
	if s == nil || s.db == nil {
		return false, errors.New("auth store is not initialized")
	}
	var count int
	err := s.db.QueryRow(`SELECT COUNT(*) FROM accounts WHERE normalized_code = ?`, strings.ToLower(userCode)).Scan(&count)
	return count > 0, err
}

func (s *AuthStore) SaveOfflineMessage(targetCode string, message Message) error {
	if s == nil || s.db == nil {
		return errors.New("auth store is not initialized")
	}
	if err := validateTextContent("offline message", message.Content); err != nil {
		return err
	}
	createdAt := message.CreatedAt
	if createdAt == "" {
		createdAt = time.Now().UTC().Format(time.RFC3339Nano)
	}
	_, err := s.db.Exec(`INSERT INTO offline_messages
        (target_code, sender_username, sender_code, content, message_id, created_at)
        VALUES (?, ?, ?, ?, ?, ?)`, strings.ToLower(targetCode), message.Username,
		message.UserCode, message.Content, message.MessageID, createdAt)
	if err != nil {
		return fmt.Errorf("save offline message: %w", err)
	}
	_, err = s.db.Exec(`DELETE FROM offline_messages
        WHERE target_code = ? AND id NOT IN
        (SELECT id FROM offline_messages WHERE target_code = ? ORDER BY id DESC LIMIT ?)`,
		strings.ToLower(targetCode), strings.ToLower(targetCode), maxOfflineMessagesPerUser)
	return err
}

func (s *AuthStore) SaveChatMessage(message Message) error {
	if s == nil || s.db == nil {
		return errors.New("auth store is not initialized")
	}
	if message.MessageID == "" {
		return errors.New("chat message id is required")
	}
	if message.Type != "chat" && message.Type != "private_chat" {
		return fmt.Errorf("unsupported history message type: %s", message.Type)
	}
	if err := validateTextContent("chat message", message.Content); err != nil {
		return err
	}
	if _, err := normalizeUserCode(message.UserCode); err != nil {
		return err
	}
	kind := "room"
	conversationKey := message.Room
	targetCode := ""
	if message.Type == "private_chat" {
		kind = "private"
		normalizedSender, _ := normalizeUserCode(message.UserCode)
		normalizedTarget, err := normalizeUserCode(message.TargetUserCode)
		if err != nil {
			return err
		}
		targetCode = normalizedTarget
		conversationKey = privateConversationKey(normalizedSender, normalizedTarget)
	} else if err := validateRoomName(message.Room); err != nil {
		return err
	}
	createdAt := message.CreatedAt
	if createdAt == "" {
		createdAt = time.Now().UTC().Format(time.RFC3339Nano)
	}
	_, err := s.db.Exec(`INSERT OR IGNORE INTO chat_messages
        (message_id, kind, conversation_key, room, sender_username, sender_code, target_code, content, created_at, recalled)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0)`, message.MessageID, kind, conversationKey, message.Room,
		message.Username, strings.ToLower(message.UserCode), targetCode, message.Content, createdAt)
	if err != nil {
		return fmt.Errorf("save chat message: %w", err)
	}
	if _, err := s.db.Exec(`DELETE FROM chat_messages
        WHERE kind = ? AND conversation_key = ? AND id NOT IN
        (SELECT id FROM chat_messages WHERE kind = ? AND conversation_key = ? ORDER BY id DESC LIMIT ?)`,
		kind, conversationKey, kind, conversationKey, maxHistoryRowsPerConversation); err != nil {
		return fmt.Errorf("prune chat history: %w", err)
	}
	return nil
}

func privateConversationKey(first, second string) string {
	if strings.Compare(first, second) < 0 {
		return first + ":" + second
	}
	return second + ":" + first
}

func (s *AuthStore) LoadHistory(query HistoryQuery) (HistoryPage, error) {
	if s == nil || s.db == nil {
		return HistoryPage{}, errors.New("auth store is not initialized")
	}
	userCode, err := normalizeUserCode(query.UserCode)
	if err != nil {
		return HistoryPage{}, err
	}
	limit := query.Limit
	if limit <= 0 {
		limit = 50
	}
	if limit > maxHistoryPageSize {
		limit = maxHistoryPageSize
	}
	kind := "room"
	conversationKey := query.Room
	if query.Private {
		kind = "private"
		peerCode, err := normalizeUserCode(query.PeerCode)
		if err != nil {
			return HistoryPage{}, err
		}
		conversationKey = privateConversationKey(userCode, peerCode)
	} else if err := validateRoomName(query.Room); err != nil {
		return HistoryPage{}, err
	}

	beforeID := int64(^uint64(0) >> 1)
	if query.BeforeMessageID != "" {
		if err := s.db.QueryRow(`SELECT id FROM chat_messages WHERE message_id = ?`, query.BeforeMessageID).Scan(&beforeID); err != nil {
			if errors.Is(err, sql.ErrNoRows) {
				return HistoryPage{Messages: []Message{}}, nil
			}
			return HistoryPage{}, err
		}
	}
	rows, err := s.db.Query(`SELECT message_id, room, sender_username, sender_code, target_code,
        content, created_at, recalled FROM chat_messages
        WHERE kind = ? AND conversation_key = ? AND id < ? ORDER BY id DESC LIMIT ?`,
		kind, conversationKey, beforeID, limit+1)
	if err != nil {
		return HistoryPage{}, fmt.Errorf("load chat history: %w", err)
	}
	defer rows.Close()
	messages := make([]Message, 0, limit)
	for rows.Next() {
		var message Message
		var recalled int
		if err := rows.Scan(&message.MessageID, &message.Room, &message.Username, &message.UserCode,
			&message.TargetUserCode, &message.Content, &message.CreatedAt, &recalled); err != nil {
			return HistoryPage{}, err
		}
		message.Type = "chat"
		if kind == "private" {
			message.Type = "private_chat"
			message.Private = true
		}
		message.Recalled = recalled != 0
		if message.Recalled {
			message.Content = "消息已撤回"
		}
		messages = append(messages, message)
	}
	if err := rows.Err(); err != nil {
		return HistoryPage{}, err
	}
	hasMore := len(messages) > limit
	if hasMore {
		messages = messages[:limit]
	}
	for left, right := 0, len(messages)-1; left < right; left, right = left+1, right-1 {
		messages[left], messages[right] = messages[right], messages[left]
	}
	return HistoryPage{Messages: messages, HasMore: hasMore}, nil
}

func (s *AuthStore) GetStoredMessage(messageID string) (StoredMessage, error) {
	if s == nil || s.db == nil {
		return StoredMessage{}, errors.New("auth store is not initialized")
	}
	var stored StoredMessage
	var recalled int
	err := s.db.QueryRow(`SELECT kind, conversation_key, room, sender_username, sender_code,
        target_code, content, created_at, recalled FROM chat_messages WHERE message_id = ?`, messageID).Scan(
		&stored.Kind, &stored.ConversationKey, &stored.Message.Room, &stored.Message.Username,
		&stored.Message.UserCode, &stored.TargetCode, &stored.Message.Content,
		&stored.Message.CreatedAt, &recalled)
	if err != nil {
		return StoredMessage{}, err
	}
	stored.Message.MessageID = messageID
	stored.Message.Type = "chat"
	if stored.Kind == "private" {
		stored.Message.Type = "private_chat"
		stored.Message.Private = true
		stored.Message.TargetUserCode = stored.TargetCode
	}
	stored.Message.Recalled = recalled != 0
	stored.AuthorCode = strings.ToLower(stored.Message.UserCode)
	if stored.Message.Recalled {
		stored.Message.Content = "消息已撤回"
	}
	return stored, nil
}

func (s *AuthStore) MarkMessageRecalled(messageID string) error {
	if s == nil || s.db == nil {
		return errors.New("auth store is not initialized")
	}
	result, err := s.db.Exec(`UPDATE chat_messages SET recalled = 1 WHERE message_id = ?`, messageID)
	if err != nil {
		return err
	}
	if affected, _ := result.RowsAffected(); affected == 0 {
		return sql.ErrNoRows
	}
	return nil
}

func (s *AuthStore) TakeOfflineMessages(targetCode string) ([]Message, error) {
	if s == nil || s.db == nil {
		return nil, errors.New("auth store is not initialized")
	}
	tx, err := s.db.Begin()
	if err != nil {
		return nil, err
	}
	rows, err := tx.Query(`SELECT id, sender_username, sender_code, content, message_id, created_at
        FROM offline_messages WHERE target_code = ? ORDER BY id`, strings.ToLower(targetCode))
	if err != nil {
		_ = tx.Rollback()
		return nil, err
	}
	var ids []int64
	var messages []Message
	for rows.Next() {
		var id int64
		var message Message
		if err := rows.Scan(&id, &message.Username, &message.UserCode, &message.Content, &message.MessageID, &message.CreatedAt); err != nil {
			_ = rows.Close()
			_ = tx.Rollback()
			return nil, err
		}
		message.Type = "offline_message"
		message.TargetUserCode = targetCode
		ids = append(ids, id)
		messages = append(messages, message)
	}
	if err := rows.Err(); err != nil {
		_ = rows.Close()
		_ = tx.Rollback()
		return nil, err
	}
	_ = rows.Close()
	if len(ids) > 0 {
		if _, err := tx.Exec(`DELETE FROM offline_messages WHERE target_code = ?`, strings.ToLower(targetCode)); err != nil {
			_ = tx.Rollback()
			return nil, err
		}
	}
	if err := tx.Commit(); err != nil {
		return nil, err
	}
	return messages, nil
}

func (s *AuthStore) DeleteOfflineMessage(messageID string) error {
	if s == nil || s.db == nil {
		return errors.New("auth store is not initialized")
	}
	if messageID == "" {
		return errors.New("offline message id is required")
	}
	_, err := s.db.Exec(`DELETE FROM offline_messages WHERE message_id = ?`, messageID)
	return err
}

func (s *AuthStore) Close() error {
	if s == nil || s.db == nil {
		return nil
	}
	return s.db.Close()
}

func normalizeUsername(username string) string {
	return strings.ToLower(username)
}

func (s *AuthStore) Register(username, userCode string) error {
	if s == nil || s.db == nil {
		return errors.New("auth store is not initialized")
	}
	message := Message{Type: "login", Username: username, UserCode: userCode}
	if err := validateMessage(message); err != nil {
		return fmt.Errorf("invalid account identity: %w", err)
	}
	_, err := s.db.Exec(`
INSERT INTO accounts (username, normalized_username, user_code, normalized_code, password_hash, created_at)
VALUES (?, ?, ?, ?, '', ?)`, username, normalizeUsername(username), userCode, strings.ToLower(userCode), time.Now().UTC().Format(time.RFC3339Nano))
	if err != nil {
		if strings.Contains(strings.ToLower(err.Error()), "unique") {
			return ErrAccountAlreadyExists
		}
		return fmt.Errorf("insert account: %w", err)
	}
	return nil
}

func (s *AuthStore) HasIdentity(username, userCode string) (bool, error) {
	if s == nil || s.db == nil {
		return false, errors.New("auth store is not initialized")
	}
	var count int
	err := s.db.QueryRow(`
SELECT COUNT(*) FROM accounts
WHERE normalized_username = ? OR normalized_code = ?`, normalizeUsername(username), strings.ToLower(userCode)).Scan(&count)
	if err != nil {
		return false, fmt.Errorf("query account identity: %w", err)
	}
	return count > 0, nil
}

// EnsureIdentity creates a passwordless account on first login and returns the
// canonical identity on subsequent logins. The legacy password_hash column is
// intentionally retained for existing databases but is never read or written
// by the passwordless login flow.
func (s *AuthStore) EnsureIdentity(username, userCode string) (Account, error) {
	if s == nil || s.db == nil {
		return Account{}, errors.New("auth store is not initialized")
	}
	identity := Message{Type: "login", Username: username, UserCode: userCode}
	if err := validateMessage(identity); err != nil {
		return Account{}, fmt.Errorf("invalid account identity: %w", err)
	}

	var account Account
	var createdAt string
	err := s.db.QueryRow(`
SELECT username, user_code, created_at
FROM accounts
WHERE normalized_username = ? OR normalized_code = ?`,
		normalizeUsername(username), strings.ToLower(userCode)).Scan(
		&account.Username, &account.UserCode, &createdAt)
	if err == nil {
		if normalizeUsername(account.Username) != normalizeUsername(username) ||
			strings.ToLower(account.UserCode) != strings.ToLower(userCode) {
			return Account{}, ErrAccountAlreadyExists
		}
		account.CreatedAt, _ = time.Parse(time.RFC3339Nano, createdAt)
		return account, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return Account{}, fmt.Errorf("query account identity: %w", err)
	}

	_, err = s.db.Exec(`
INSERT INTO accounts (username, normalized_username, user_code, normalized_code, password_hash, created_at)
VALUES (?, ?, ?, ?, '', ?)`, username, normalizeUsername(username), userCode,
		strings.ToLower(userCode), time.Now().UTC().Format(time.RFC3339Nano))
	if err != nil {
		if strings.Contains(strings.ToLower(err.Error()), "unique") {
			return Account{}, ErrAccountAlreadyExists
		}
		return Account{}, fmt.Errorf("insert account identity: %w", err)
	}
	account.Username = username
	account.UserCode = userCode
	account.CreatedAt = time.Now().UTC()
	return account, nil
}
