package main

import (
	"database/sql"
	"errors"
	"fmt"
	"os"
	"strings"
	"time"
	"unicode/utf8"

	"golang.org/x/crypto/bcrypt"
	_ "modernc.org/sqlite"
)

const (
	authDBPathEnv             = "CHAT_DB_PATH"
	defaultAuthDBPath         = "chat.db"
	minPasswordBytes          = 8
	maxPasswordBytes          = 72
	maxOfflineMessagesPerUser = 100
)

var (
	ErrAccountAlreadyExists = errors.New("account already exists")
	ErrInvalidCredentials   = errors.New("invalid credentials")
)

type Account struct {
	Username     string
	UserCode     string
	PasswordHash string
	CreatedAt    time.Time
}

type AuthStore struct {
	db *sql.DB
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
    created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_offline_messages_target
    ON offline_messages(target_code, id);
`
	if _, err := s.db.Exec(schema); err != nil {
		return fmt.Errorf("initialize auth database: %w", err)
	}
	return nil
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
	_, err := s.db.Exec(`INSERT INTO offline_messages
        (target_code, sender_username, sender_code, content, created_at)
        VALUES (?, ?, ?, ?, ?)`, strings.ToLower(targetCode), message.Username,
		message.UserCode, message.Content, time.Now().UTC().Format(time.RFC3339Nano))
	if err != nil {
		return fmt.Errorf("save offline message: %w", err)
	}
	_, err = s.db.Exec(`DELETE FROM offline_messages
        WHERE target_code = ? AND id NOT IN
        (SELECT id FROM offline_messages WHERE target_code = ? ORDER BY id DESC LIMIT ?)`,
		strings.ToLower(targetCode), strings.ToLower(targetCode), maxOfflineMessagesPerUser)
	return err
}

func (s *AuthStore) TakeOfflineMessages(targetCode string) ([]Message, error) {
	if s == nil || s.db == nil {
		return nil, errors.New("auth store is not initialized")
	}
	tx, err := s.db.Begin()
	if err != nil {
		return nil, err
	}
	rows, err := tx.Query(`SELECT id, sender_username, sender_code, content
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
		if err := rows.Scan(&id, &message.Username, &message.UserCode, &message.Content); err != nil {
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

func (s *AuthStore) Close() error {
	if s == nil || s.db == nil {
		return nil
	}
	return s.db.Close()
}

func validatePassword(password string) error {
	if !utf8.ValidString(password) {
		return errors.New("password must be valid UTF-8")
	}
	length := len([]byte(password))
	if length < minPasswordBytes || length > maxPasswordBytes {
		return fmt.Errorf("password must be %d to %d bytes", minPasswordBytes, maxPasswordBytes)
	}
	return nil
}

func normalizeUsername(username string) string {
	return strings.ToLower(username)
}

func (s *AuthStore) Register(username, userCode, password string) error {
	if s == nil || s.db == nil {
		return errors.New("auth store is not initialized")
	}
	message := Message{Type: "login", Username: username, UserCode: userCode}
	if err := validateMessage(message); err != nil {
		return fmt.Errorf("invalid account identity: %w", err)
	}
	if err := validatePassword(password); err != nil {
		return err
	}

	hash, err := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
	if err != nil {
		return fmt.Errorf("hash password: %w", err)
	}

	_, err = s.db.Exec(`
INSERT INTO accounts (username, normalized_username, user_code, normalized_code, password_hash, created_at)
VALUES (?, ?, ?, ?, ?, ?)`, username, normalizeUsername(username), userCode, strings.ToLower(userCode), string(hash), time.Now().UTC().Format(time.RFC3339Nano))
	if err != nil {
		if strings.Contains(strings.ToLower(err.Error()), "unique") {
			return ErrAccountAlreadyExists
		}
		return fmt.Errorf("insert account: %w", err)
	}
	return nil
}

func (s *AuthStore) Authenticate(username, password string) (Account, error) {
	if s == nil || s.db == nil {
		return Account{}, errors.New("auth store is not initialized")
	}
	if username == "" || !utf8.ValidString(username) || len([]byte(username)) > maxUsernameSize {
		return Account{}, ErrInvalidCredentials
	}
	if err := validatePassword(password); err != nil {
		return Account{}, ErrInvalidCredentials
	}

	var account Account
	var createdAt string
	err := s.db.QueryRow(`
SELECT username, user_code, password_hash, created_at
FROM accounts WHERE normalized_username = ?`, normalizeUsername(username)).Scan(
		&account.Username, &account.UserCode, &account.PasswordHash, &createdAt)
	if errors.Is(err, sql.ErrNoRows) {
		return Account{}, ErrInvalidCredentials
	}
	if err != nil {
		return Account{}, fmt.Errorf("query account: %w", err)
	}
	if err := bcrypt.CompareHashAndPassword([]byte(account.PasswordHash), []byte(password)); err != nil {
		return Account{}, ErrInvalidCredentials
	}
	account.CreatedAt, _ = time.Parse(time.RFC3339Nano, createdAt)
	return account, nil
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
