package main

import (
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	_ "modernc.org/sqlite"
	"os"
	"strings"
	"time"
	"unicode/utf8"
)

const (
	authDBPathEnv                 = "CHAT_DB_PATH"
	defaultAuthDBPath             = "chat.db"
	maxOfflineMessagesPerUser     = 100
	maxHistoryPageSize            = 100
	maxHistoryRowsPerConversation = 10000
)

var ErrAccountAlreadyExists = errors.New("account already exists")
var ErrMLSCommitConflict = errors.New("MLS commit conflicts with existing epoch")
var ErrMLSEpochRollback = errors.New("MLS commit epoch rolls back current group epoch")
var ErrMLSProposalMissing = errors.New("MLS commit has no accepted proposal")
var ErrMLSWelcomeConflict = errors.New("MLS welcome conflicts with existing target")
var ErrMLSProposalConflict = errors.New("MLS proposal conflicts with existing proposal")
var ErrMLSGroupMemberRequired = errors.New("MLS group member authorization required")
var ErrMLSMemberConflict = errors.New("MLS group member state conflicts")

func nullableCrypto(value json.RawMessage) any {
	if len(value) == 0 {
		return nil
	}
	return string(value)
}

type Account struct {
	Username  string
	UserCode  string
	CreatedAt time.Time
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
	SearchQuery     string
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
    message_id TEXT NOT NULL,
    kind TEXT NOT NULL,
    conversation_key TEXT NOT NULL,
    room TEXT,
    sender_username TEXT NOT NULL,
    sender_code TEXT NOT NULL,
    target_code TEXT,
    content TEXT NOT NULL,
	crypto_json TEXT,
    delivery_state TEXT NOT NULL DEFAULT 'sent',
    created_at TEXT NOT NULL,
    recalled INTEGER NOT NULL DEFAULT 0,
    UNIQUE(sender_code, conversation_key, message_id)
);
CREATE INDEX IF NOT EXISTS idx_chat_messages_conversation
    ON chat_messages(kind, conversation_key, id);
CREATE INDEX IF NOT EXISTS idx_chat_messages_search_scope
    ON chat_messages(kind, conversation_key, content, id);
CREATE TABLE IF NOT EXISTS mls_key_packages (
    user_code TEXT PRIMARY KEY,
    key_package TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS mls_groups (
    group_id TEXT PRIMARY KEY,
    room TEXT NOT NULL,
    current_epoch INTEGER NOT NULL DEFAULT -1
);
CREATE TABLE IF NOT EXISTS mls_group_epochs (
    group_id TEXT NOT NULL,
    epoch INTEGER NOT NULL,
    commit_data TEXT NOT NULL,
    created_at TEXT NOT NULL,
    PRIMARY KEY(group_id, epoch)
);
CREATE TABLE IF NOT EXISTS mls_group_proposals (
    group_id TEXT NOT NULL,
    epoch INTEGER NOT NULL,
    proposal_id TEXT NOT NULL,
    proposal_data TEXT NOT NULL,
    proposal_digest TEXT NOT NULL,
    created_at TEXT NOT NULL,
    PRIMARY KEY(group_id, epoch, proposal_id),
    UNIQUE(group_id, epoch, proposal_digest)
);
CREATE TABLE IF NOT EXISTS mls_group_welcomes (
    group_id TEXT NOT NULL,
    epoch INTEGER NOT NULL,
    target_code TEXT NOT NULL,
    welcome TEXT NOT NULL,
    created_at TEXT NOT NULL,
    PRIMARY KEY(group_id, epoch, target_code)
);
CREATE TABLE IF NOT EXISTS mls_group_members (
    group_id TEXT NOT NULL,
    user_code TEXT NOT NULL,
    active INTEGER NOT NULL DEFAULT 1,
    joined_epoch INTEGER NOT NULL DEFAULT 0,
    removed_epoch INTEGER,
    PRIMARY KEY(group_id, user_code)
);
`
	if _, err := s.db.Exec(schema); err != nil {
		return fmt.Errorf("initialize auth database: %w", err)
	}
	if _, err := s.db.Exec(`ALTER TABLE offline_messages ADD COLUMN message_id TEXT`); err != nil && !strings.Contains(strings.ToLower(err.Error()), "duplicate column") {
		return fmt.Errorf("migrate offline message id: %w", err)
	}
	if _, err := s.db.Exec(`ALTER TABLE chat_messages ADD COLUMN delivery_state TEXT NOT NULL DEFAULT 'sent'`); err != nil && !strings.Contains(strings.ToLower(err.Error()), "duplicate column") {
		return fmt.Errorf("migrate chat message delivery state: %w", err)
	}
	if _, err := s.db.Exec(`ALTER TABLE chat_messages ADD COLUMN crypto_json TEXT`); err != nil && !strings.Contains(strings.ToLower(err.Error()), "duplicate column") {
		return fmt.Errorf("migrate chat message crypto envelope: %w", err)
	}
	if err := s.migrateChatMessageIdentitySchema(); err != nil {
		return err
	}
	for _, migration := range []string{
		`ALTER TABLE mls_group_proposals ADD COLUMN action TEXT NOT NULL DEFAULT 'add'`,
		`ALTER TABLE mls_group_proposals ADD COLUMN target_code TEXT NOT NULL DEFAULT ''`,
		`ALTER TABLE mls_group_proposals ADD COLUMN member_snapshot TEXT NOT NULL DEFAULT '[]'`,
	} {
		if _, err := s.db.Exec(migration); err != nil && !strings.Contains(strings.ToLower(err.Error()), "duplicate column") {
			return fmt.Errorf("migrate MLS proposal metadata: %w", err)
		}
	}
	if _, err := s.db.Exec(`CREATE UNIQUE INDEX IF NOT EXISTS idx_offline_messages_message_id ON offline_messages(message_id)`); err != nil {
		return fmt.Errorf("index offline message id: %w", err)
	}
	return nil
}

// MLSGroupMembership is the server's authoritative membership snapshot. Room
// invitations are deliberately not consulted by MLS proposal/commit paths.
type MLSGroupMembership struct {
	Action  string
	Target  string
	Members []string
}

func (s *AuthStore) MLSGroupMembers(groupID string) ([]string, error) {
	if s == nil || s.db == nil {
		return nil, errors.New("auth store is not initialized")
	}
	if err := validateMLSGroupID(groupID); err != nil {
		return nil, err
	}
	rows, err := s.db.Query(`SELECT user_code FROM mls_group_members WHERE group_id = ? AND active = 1 ORDER BY user_code`, groupID)
	if err != nil {
		return nil, fmt.Errorf("query MLS group members: %w", err)
	}
	defer rows.Close()
	var members []string
	for rows.Next() {
		var code string
		if err := rows.Scan(&code); err != nil {
			return nil, err
		}
		members = append(members, code)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	return members, nil
}

func encodeMLSMemberSnapshot(members []string) (string, error) {
	value, err := json.Marshal(members)
	if err != nil {
		return "", err
	}
	return string(value), nil
}

func decodeMLSMemberSnapshot(value string) ([]string, error) {
	var members []string
	if value == "" {
		return members, nil
	}
	if err := json.Unmarshal([]byte(value), &members); err != nil {
		return nil, err
	}
	return members, nil
}

func (s *AuthStore) PublishMLSKeyPackage(userCode, keyPackage string) error {
	if s == nil || s.db == nil {
		return errors.New("auth store is not initialized")
	}
	code, err := normalizeUserCode(userCode)
	if err != nil {
		return err
	}
	if err := validateOpaqueMLS("key package", keyPackage); err != nil {
		return err
	}
	_, err = s.db.Exec(`INSERT INTO mls_key_packages(user_code, key_package, updated_at)
VALUES (?, ?, ?)
ON CONFLICT(user_code) DO UPDATE SET key_package = excluded.key_package, updated_at = excluded.updated_at`,
		code, keyPackage, time.Now().UTC().Format(time.RFC3339Nano))
	if err != nil {
		return fmt.Errorf("save MLS key package: %w", err)
	}
	return nil
}

func (s *AuthStore) FetchMLSKeyPackage(userCode string) (string, error) {
	if s == nil || s.db == nil {
		return "", errors.New("auth store is not initialized")
	}
	code, err := normalizeUserCode(userCode)
	if err != nil {
		return "", err
	}
	var keyPackage string
	if err := s.db.QueryRow(`SELECT key_package FROM mls_key_packages WHERE user_code = ?`, code).Scan(&keyPackage); err != nil {
		return "", fmt.Errorf("fetch MLS key package: %w", err)
	}
	return keyPackage, nil
}

func mlsEpochValue(epoch uint64) (int64, error) {
	if epoch > uint64(^uint64(0)>>1) {
		return 0, errors.New("MLS epoch is too large")
	}
	return int64(epoch), nil
}

func (s *AuthStore) SaveMLSCommit(groupID, room string, epoch uint64, commit string) (bool, error) {
	if s == nil || s.db == nil {
		return false, errors.New("auth store is not initialized")
	}
	if err := validateMLSGroupID(groupID); err != nil {
		return false, err
	}
	if room != "" {
		if err := validateRoomName(room); err != nil {
			return false, err
		}
	}
	if err := validateOpaqueMLS("MLS group commit", commit); err != nil {
		return false, err
	}
	epochValue, err := mlsEpochValue(epoch)
	if err != nil {
		return false, err
	}
	tx, err := s.db.Begin()
	if err != nil {
		return false, fmt.Errorf("begin MLS commit: %w", err)
	}
	defer func() { _ = tx.Rollback() }()
	var existingCommit string
	err = tx.QueryRow(`SELECT commit_data FROM mls_group_epochs WHERE group_id = ? AND epoch = ?`, groupID, epochValue).Scan(&existingCommit)
	if err == nil {
		if existingCommit != commit {
			return false, ErrMLSCommitConflict
		}
		return false, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return false, fmt.Errorf("query MLS commit: %w", err)
	}
	var currentEpoch int64
	var existingRoom string
	err = tx.QueryRow(`SELECT room, current_epoch FROM mls_groups WHERE group_id = ?`, groupID).Scan(&existingRoom, &currentEpoch)
	groupExists := err == nil
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return false, fmt.Errorf("query MLS group: %w", err)
	}
	if groupExists {
		if room != "" && existingRoom != "" && room != existingRoom {
			return false, ErrMLSCommitConflict
		}
		if epochValue < currentEpoch {
			return false, ErrMLSEpochRollback
		}
	} else {
		if _, err := tx.Exec(`INSERT INTO mls_groups(group_id, room, current_epoch) VALUES (?, ?, -1)`, groupID, room); err != nil {
			return false, fmt.Errorf("create MLS group: %w", err)
		}
	}
	// A commit is accepted only after at least one proposal for this exact
	// group/epoch has been durably accepted in the same database. Keeping this
	// check inside the write transaction closes the check-then-insert race and
	// prevents members from advancing a group with an unsolicited commit.
	var proposalCount int
	if err := tx.QueryRow(`SELECT COUNT(1) FROM mls_group_proposals WHERE group_id = ? AND epoch = ?`, groupID, epochValue).Scan(&proposalCount); err != nil {
		return false, fmt.Errorf("query MLS proposal for commit: %w", err)
	}
	if proposalCount == 0 {
		return false, ErrMLSProposalMissing
	}
	if _, err := tx.Exec(`INSERT INTO mls_group_epochs(group_id, epoch, commit_data, created_at) VALUES (?, ?, ?, ?)`,
		groupID, epochValue, commit, time.Now().UTC().Format(time.RFC3339Nano)); err != nil {
		return false, fmt.Errorf("save MLS commit: %w", err)
	}
	if _, err := tx.Exec(`UPDATE mls_groups SET room = CASE WHEN room = '' THEN ? ELSE room END, current_epoch = CASE WHEN current_epoch < ? THEN ? ELSE current_epoch END WHERE group_id = ?`, room, epochValue, epochValue, groupID); err != nil {
		return false, fmt.Errorf("update MLS group: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return false, fmt.Errorf("commit MLS commit: %w", err)
	}
	return true, nil
}

// SaveMLSCommitForMember is the strict authenticated commit path. The
// proposal's persisted snapshot determines recipients and the remove action
// is applied atomically with the epoch advance.
func (s *AuthStore) SaveMLSCommitForMember(groupID, room, senderCode, proposalID string,
	epoch uint64, commit string) (bool, MLSGroupMembership, error) {
	var result MLSGroupMembership
	if s == nil || s.db == nil {
		return false, result, errors.New("auth store is not initialized")
	}
	if err := validateMLSGroupID(groupID); err != nil {
		return false, result, err
	}
	if err := validateRoomName(room); err != nil {
		return false, result, err
	}
	sender, err := normalizeUserCode(senderCode)
	if err != nil {
		return false, result, err
	}
	if err := validateOpaqueMLS("MLS group commit", commit); err != nil {
		return false, result, err
	}
	epochValue, err := mlsEpochValue(epoch)
	if err != nil {
		return false, result, err
	}
	tx, err := s.db.Begin()
	if err != nil {
		return false, result, fmt.Errorf("begin MLS member commit: %w", err)
	}
	defer func() { _ = tx.Rollback() }()
	var existingCommit string
	err = tx.QueryRow(`SELECT commit_data FROM mls_group_epochs WHERE group_id=? AND epoch=?`, groupID, epochValue).Scan(&existingCommit)
	if err == nil {
		var active int
		if err := tx.QueryRow(`SELECT COUNT(1) FROM mls_group_members WHERE group_id=? AND user_code=? AND active=1`, groupID, sender).Scan(&active); err != nil || active == 0 {
			return false, result, ErrMLSGroupMemberRequired
		}
		if existingCommit != commit {
			return false, result, ErrMLSCommitConflict
		}
		return false, result, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return false, result, fmt.Errorf("query MLS commit: %w", err)
	}
	var existingRoom string
	var currentEpoch int64
	if err := tx.QueryRow(`SELECT room,current_epoch FROM mls_groups WHERE group_id=?`, groupID).Scan(&existingRoom, &currentEpoch); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return false, result, ErrMLSGroupMemberRequired
		}
		return false, result, err
	}
	if existingRoom != room {
		return false, result, ErrMLSCommitConflict
	}
	if currentEpoch >= 0 && epochValue != currentEpoch+1 {
		if epochValue < currentEpoch {
			return false, result, ErrMLSEpochRollback
		}
		return false, result, ErrMLSCommitConflict
	}
	var senderActive int
	if err := tx.QueryRow(`SELECT COUNT(1) FROM mls_group_members WHERE group_id=? AND user_code=? AND active=1`, groupID, sender).Scan(&senderActive); err != nil {
		return false, result, err
	}
	if senderActive == 0 {
		return false, result, ErrMLSGroupMemberRequired
	}
	var action, target, snapshot string
	proposalQuery := `SELECT action,target_code,member_snapshot FROM mls_group_proposals WHERE group_id=? AND epoch=?`
	args := []any{groupID, epochValue}
	if proposalID != "" {
		proposalQuery += ` AND proposal_id=?`
		args = append(args, proposalID)
	}
	proposalQuery += ` ORDER BY proposal_id LIMIT 1`
	if err := tx.QueryRow(proposalQuery, args...).Scan(&action, &target, &snapshot); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return false, result, ErrMLSProposalMissing
		}
		return false, result, err
	}
	members, err := decodeMLSMemberSnapshot(snapshot)
	if err != nil {
		return false, result, fmt.Errorf("decode MLS member snapshot: %w", err)
	}
	for _, member := range members {
		if member == "" {
			return false, result, ErrMLSMemberConflict
		}
	}
	if _, err := tx.Exec(`INSERT INTO mls_group_epochs(group_id,epoch,commit_data,created_at) VALUES (?,?,?,?)`, groupID, epochValue, commit, time.Now().UTC().Format(time.RFC3339Nano)); err != nil {
		return false, result, fmt.Errorf("save MLS member commit: %w", err)
	}
	if _, err := tx.Exec(`UPDATE mls_groups SET current_epoch=? WHERE group_id=?`, epochValue, groupID); err != nil {
		return false, result, err
	}
	if action == "remove" {
		if _, err := tx.Exec(`UPDATE mls_group_members SET active=0,removed_epoch=? WHERE group_id=? AND user_code=? AND active=1`, epochValue, groupID, target); err != nil {
			return false, result, err
		}
	}
	result = MLSGroupMembership{Action: action, Target: target, Members: members}
	if err := tx.Commit(); err != nil {
		return false, result, fmt.Errorf("commit MLS member commit: %w", err)
	}
	return true, result, nil
}

// SaveMLSProposal stores an opaque proposal and enforces idempotency by both
// caller proposal id and payload digest. It deliberately does not advance the
// group's epoch; only a committed handshake advances group state.
func (s *AuthStore) SaveMLSProposal(groupID, room string, epoch uint64, proposalID, proposal string) (bool, error) {
	if s == nil || s.db == nil {
		return false, errors.New("auth store is not initialized")
	}
	if err := validateMLSGroupID(groupID); err != nil {
		return false, err
	}
	if room != "" {
		if err := validateRoomName(room); err != nil {
			return false, err
		}
	}
	if proposalID == "" || !utf8.ValidString(proposalID) || len([]byte(proposalID)) > maxMessageSize {
		return false, errors.New("MLS proposal id must be valid and no longer than message limit")
	}
	if err := validateOpaqueMLS("MLS group proposal", proposal); err != nil {
		return false, err
	}
	epochValue, err := mlsEpochValue(epoch)
	if err != nil {
		return false, err
	}
	digestBytes := sha256.Sum256([]byte(proposal))
	digest := hex.EncodeToString(digestBytes[:])
	tx, err := s.db.Begin()
	if err != nil {
		return false, fmt.Errorf("begin MLS proposal: %w", err)
	}
	defer func() { _ = tx.Rollback() }()
	var existingData string
	err = tx.QueryRow(`SELECT proposal_data FROM mls_group_proposals WHERE group_id = ? AND epoch = ? AND proposal_id = ?`, groupID, epochValue, proposalID).Scan(&existingData)
	if err == nil {
		if existingData != proposal {
			return false, ErrMLSProposalConflict
		}
		return false, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return false, fmt.Errorf("query MLS proposal: %w", err)
	}
	var existingDigest string
	err = tx.QueryRow(`SELECT proposal_digest FROM mls_group_proposals WHERE group_id = ? AND epoch = ? AND proposal_digest = ?`, groupID, epochValue, digest).Scan(&existingDigest)
	if err == nil {
		return false, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return false, fmt.Errorf("query MLS proposal digest: %w", err)
	}
	var currentEpoch int64
	var existingRoom string
	err = tx.QueryRow(`SELECT room, current_epoch FROM mls_groups WHERE group_id = ?`, groupID).Scan(&existingRoom, &currentEpoch)
	groupExists := err == nil
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return false, fmt.Errorf("query MLS group for proposal: %w", err)
	}
	if groupExists {
		if room != "" && existingRoom != "" && room != existingRoom {
			return false, ErrMLSProposalConflict
		}
		if epochValue < currentEpoch {
			return false, ErrMLSEpochRollback
		}
	} else if _, err := tx.Exec(`INSERT INTO mls_groups(group_id, room, current_epoch) VALUES (?, ?, -1)`, groupID, room); err != nil {
		return false, fmt.Errorf("create MLS group for proposal: %w", err)
	}
	if _, err := tx.Exec(`INSERT INTO mls_group_proposals(group_id, epoch, proposal_id, proposal_data, proposal_digest, created_at) VALUES (?, ?, ?, ?, ?, ?)`,
		groupID, epochValue, proposalID, proposal, digest, time.Now().UTC().Format(time.RFC3339Nano)); err != nil {
		return false, fmt.Errorf("save MLS proposal: %w", err)
	}
	if _, err := tx.Exec(`UPDATE mls_groups SET room = CASE WHEN room = '' THEN ? ELSE room END WHERE group_id = ?`, room, groupID); err != nil {
		return false, fmt.Errorf("update MLS group for proposal: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return false, fmt.Errorf("commit MLS proposal: %w", err)
	}
	return true, nil
}

// SaveMLSProposalForMember is the authenticated MLS path. It persists the
// action and an immutable pre-commit member snapshot while authorizing only an
// active MLS member. The legacy SaveMLSProposal above remains available for
// old storage fixtures, but Hub control traffic uses this method exclusively.
func (s *AuthStore) SaveMLSProposalForMember(groupID, room, senderCode, action, targetCode string,
	epoch uint64, proposalID, proposal string) (bool, MLSGroupMembership, error) {
	var result MLSGroupMembership
	if s == nil || s.db == nil {
		return false, result, errors.New("auth store is not initialized")
	}
	if err := validateMLSGroupID(groupID); err != nil {
		return false, result, err
	}
	if err := validateRoomName(room); err != nil {
		return false, result, err
	}
	sender, err := normalizeUserCode(senderCode)
	if err != nil {
		return false, result, err
	}
	action = strings.ToLower(strings.TrimSpace(action))
	if action != "add" && action != "remove" {
		return false, result, errors.New("MLS proposal action must be add or remove")
	}
	target, err := normalizeUserCode(targetCode)
	if err != nil {
		return false, result, err
	}
	if proposalID == "" || !utf8.ValidString(proposalID) || len([]byte(proposalID)) > maxMessageSize {
		return false, result, errors.New("MLS proposal id must be valid and no longer than message limit")
	}
	if err := validateOpaqueMLS("MLS group proposal", proposal); err != nil {
		return false, result, err
	}
	epochValue, err := mlsEpochValue(epoch)
	if err != nil {
		return false, result, err
	}
	digestBytes := sha256.Sum256([]byte(proposal))
	digest := hex.EncodeToString(digestBytes[:])
	tx, err := s.db.Begin()
	if err != nil {
		return false, result, fmt.Errorf("begin MLS member proposal: %w", err)
	}
	defer func() { _ = tx.Rollback() }()
	var existingData, existingAction, existingTarget, existingSnapshot string
	err = tx.QueryRow(`SELECT proposal_data, action, target_code, member_snapshot FROM mls_group_proposals WHERE group_id=? AND epoch=? AND proposal_id=?`, groupID, epochValue, proposalID).Scan(&existingData, &existingAction, &existingTarget, &existingSnapshot)
	if err == nil {
		var active int
		if err := tx.QueryRow(`SELECT COUNT(1) FROM mls_group_members WHERE group_id=? AND user_code=? AND active=1`, groupID, sender).Scan(&active); err != nil || active == 0 {
			return false, result, ErrMLSGroupMemberRequired
		}
		if existingData != proposal || existingAction != action || existingTarget != target {
			return false, result, ErrMLSProposalConflict
		}
		members, decodeErr := decodeMLSMemberSnapshot(existingSnapshot)
		if decodeErr != nil {
			return false, result, fmt.Errorf("decode MLS member snapshot: %w", decodeErr)
		}
		result = MLSGroupMembership{Action: existingAction, Target: existingTarget, Members: members}
		return false, result, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return false, result, fmt.Errorf("query MLS proposal: %w", err)
	}
	var existingDigest string
	err = tx.QueryRow(`SELECT proposal_digest FROM mls_group_proposals WHERE group_id=? AND epoch=? AND proposal_digest=?`, groupID, epochValue, digest).Scan(&existingDigest)
	if err == nil {
		return false, result, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return false, result, fmt.Errorf("query MLS proposal digest: %w", err)
	}
	var currentEpoch int64
	var existingRoom string
	err = tx.QueryRow(`SELECT room,current_epoch FROM mls_groups WHERE group_id=?`, groupID).Scan(&existingRoom, &currentEpoch)
	groupExists := err == nil
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return false, result, fmt.Errorf("query MLS group: %w", err)
	}
	if groupExists {
		if existingRoom != "" && existingRoom != room {
			return false, result, ErrMLSProposalConflict
		}
		if epochValue < currentEpoch {
			return false, result, ErrMLSEpochRollback
		}
	} else if _, err := tx.Exec(`INSERT INTO mls_groups(group_id,room,current_epoch) VALUES (?,?, -1)`, groupID, room); err != nil {
		return false, result, fmt.Errorf("create MLS group: %w", err)
	}
	// A newly-created group has exactly its creator as a member. This is done
	// in the same transaction as the first proposal, preventing room invites
	// from being mistaken for MLS membership.
	var memberCount int
	if err := tx.QueryRow(`SELECT COUNT(1) FROM mls_group_members WHERE group_id=? AND active=1`, groupID).Scan(&memberCount); err != nil {
		return false, result, err
	}
	if memberCount == 0 && (!groupExists || currentEpoch < 0) {
		if _, err := tx.Exec(`INSERT INTO mls_group_members(group_id,user_code,active,joined_epoch) VALUES (?,?,1,0) ON CONFLICT(group_id,user_code) DO UPDATE SET active=1, removed_epoch=NULL`, groupID, sender); err != nil {
			return false, result, fmt.Errorf("initialize MLS creator: %w", err)
		}
	}
	var senderActive int
	if err := tx.QueryRow(`SELECT COUNT(1) FROM mls_group_members WHERE group_id=? AND user_code=? AND active=1`, groupID, sender).Scan(&senderActive); err != nil {
		return false, result, err
	}
	if senderActive == 0 {
		return false, result, ErrMLSGroupMemberRequired
	}
	rows, err := tx.Query(`SELECT user_code FROM mls_group_members WHERE group_id=? AND active=1 ORDER BY user_code`, groupID)
	if err != nil {
		return false, result, err
	}
	for rows.Next() {
		var code string
		if err := rows.Scan(&code); err != nil {
			rows.Close()
			return false, result, err
		}
		result.Members = append(result.Members, code)
	}
	rows.Close()
	if action == "add" {
		var active int
		if err := tx.QueryRow(`SELECT COUNT(1) FROM mls_group_members WHERE group_id=? AND user_code=? AND active=1`, groupID, target).Scan(&active); err != nil {
			return false, result, err
		}
		if active != 0 {
			return false, result, ErrMLSMemberConflict
		}
	} else {
		var active int
		if err := tx.QueryRow(`SELECT COUNT(1) FROM mls_group_members WHERE group_id=? AND user_code=? AND active=1`, groupID, target).Scan(&active); err != nil {
			return false, result, err
		}
		if active == 0 {
			return false, result, ErrMLSGroupMemberRequired
		}
	}
	result.Action, result.Target = action, target
	snapshot, err := encodeMLSMemberSnapshot(result.Members)
	if err != nil {
		return false, result, err
	}
	if _, err := tx.Exec(`INSERT INTO mls_group_proposals(group_id,epoch,proposal_id,proposal_data,proposal_digest,action,target_code,member_snapshot,created_at) VALUES (?,?,?,?,?,?,?,?,?)`, groupID, epochValue, proposalID, proposal, digest, action, target, snapshot, time.Now().UTC().Format(time.RFC3339Nano)); err != nil {
		return false, result, fmt.Errorf("save MLS member proposal: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return false, result, fmt.Errorf("commit MLS member proposal: %w", err)
	}
	return true, result, nil
}

func (s *AuthStore) SaveMLSWelcome(groupID, room string, epoch uint64, targetCode, welcome string) (bool, error) {
	if s == nil || s.db == nil {
		return false, errors.New("auth store is not initialized")
	}
	if err := validateMLSGroupID(groupID); err != nil {
		return false, err
	}
	if room != "" {
		if err := validateRoomName(room); err != nil {
			return false, err
		}
	}
	target, err := normalizeUserCode(targetCode)
	if err != nil {
		return false, err
	}
	if err := validateOpaqueMLS("MLS group welcome", welcome); err != nil {
		return false, err
	}
	epochValue, err := mlsEpochValue(epoch)
	if err != nil {
		return false, err
	}
	tx, err := s.db.Begin()
	if err != nil {
		return false, fmt.Errorf("begin MLS welcome: %w", err)
	}
	defer func() { _ = tx.Rollback() }()
	var existingWelcome string
	err = tx.QueryRow(`SELECT welcome FROM mls_group_welcomes WHERE group_id = ? AND epoch = ? AND target_code = ?`, groupID, epochValue, target).Scan(&existingWelcome)
	if err == nil {
		if existingWelcome != welcome {
			return false, ErrMLSWelcomeConflict
		}
		return false, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return false, fmt.Errorf("query MLS welcome: %w", err)
	}
	var existingRoom string
	if err := tx.QueryRow(`SELECT room FROM mls_groups WHERE group_id = ?`, groupID).Scan(&existingRoom); errors.Is(err, sql.ErrNoRows) {
		if _, err := tx.Exec(`INSERT INTO mls_groups(group_id, room, current_epoch) VALUES (?, ?, -1)`, groupID, room); err != nil {
			return false, fmt.Errorf("create MLS group for welcome: %w", err)
		}
	} else if err != nil {
		return false, fmt.Errorf("query MLS group for welcome: %w", err)
	} else if room != "" && existingRoom != "" && room != existingRoom {
		return false, ErrMLSWelcomeConflict
	}
	if _, err := tx.Exec(`INSERT INTO mls_group_welcomes(group_id, epoch, target_code, welcome, created_at) VALUES (?, ?, ?, ?, ?)`,
		groupID, epochValue, target, welcome, time.Now().UTC().Format(time.RFC3339Nano)); err != nil {
		return false, fmt.Errorf("save MLS welcome: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return false, fmt.Errorf("commit MLS welcome: %w", err)
	}
	return true, nil
}

// SaveMLSWelcomeForMember activates an add target only after the matching
// commit and opaque welcome are durably accepted.
func (s *AuthStore) SaveMLSWelcomeForMember(groupID, room, senderCode string, epoch uint64,
	targetCode, welcome string) (bool, error) {
	if s == nil || s.db == nil {
		return false, errors.New("auth store is not initialized")
	}
	if err := validateMLSGroupID(groupID); err != nil {
		return false, err
	}
	if err := validateRoomName(room); err != nil {
		return false, err
	}
	sender, err := normalizeUserCode(senderCode)
	if err != nil {
		return false, err
	}
	target, err := normalizeUserCode(targetCode)
	if err != nil {
		return false, err
	}
	if err := validateOpaqueMLS("MLS group welcome", welcome); err != nil {
		return false, err
	}
	epochValue, err := mlsEpochValue(epoch)
	if err != nil {
		return false, err
	}
	tx, err := s.db.Begin()
	if err != nil {
		return false, fmt.Errorf("begin MLS member welcome: %w", err)
	}
	defer func() { _ = tx.Rollback() }()
	var existingRoom string
	if err := tx.QueryRow(`SELECT room FROM mls_groups WHERE group_id=?`, groupID).Scan(&existingRoom); err != nil {
		return false, ErrMLSGroupMemberRequired
	}
	if existingRoom != room {
		return false, ErrMLSWelcomeConflict
	}
	var active int
	if err := tx.QueryRow(`SELECT COUNT(1) FROM mls_group_members WHERE group_id=? AND user_code=? AND active=1`, groupID, sender).Scan(&active); err != nil {
		return false, err
	}
	if active == 0 {
		return false, ErrMLSGroupMemberRequired
	}
	var action, proposalTarget string
	if err := tx.QueryRow(`SELECT action,target_code FROM mls_group_proposals WHERE group_id=? AND epoch=? ORDER BY proposal_id LIMIT 1`, groupID, epochValue).Scan(&action, &proposalTarget); err != nil {
		return false, ErrMLSProposalMissing
	}
	if action != "add" || proposalTarget != target {
		return false, ErrMLSMemberConflict
	}
	var commit string
	if err := tx.QueryRow(`SELECT commit_data FROM mls_group_epochs WHERE group_id=? AND epoch=?`, groupID, epochValue).Scan(&commit); err != nil {
		return false, ErrMLSProposalMissing
	}
	var existing string
	err = tx.QueryRow(`SELECT welcome FROM mls_group_welcomes WHERE group_id=? AND epoch=? AND target_code=?`, groupID, epochValue, target).Scan(&existing)
	if err == nil {
		if existing != welcome {
			return false, ErrMLSWelcomeConflict
		}
		return false, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return false, err
	}
	if _, err := tx.Exec(`INSERT INTO mls_group_welcomes(group_id,epoch,target_code,welcome,created_at) VALUES (?,?,?,?,?)`, groupID, epochValue, target, welcome, time.Now().UTC().Format(time.RFC3339Nano)); err != nil {
		return false, err
	}
	if _, err := tx.Exec(`INSERT INTO mls_group_members(group_id,user_code,active,joined_epoch,removed_epoch) VALUES (?,?,1,?,NULL) ON CONFLICT(group_id,user_code) DO UPDATE SET active=1,joined_epoch=excluded.joined_epoch,removed_epoch=NULL`, groupID, target, epochValue); err != nil {
		return false, err
	}
	if err := tx.Commit(); err != nil {
		return false, fmt.Errorf("commit MLS member welcome: %w", err)
	}
	return true, nil
}

func (s *AuthStore) FetchMLSWelcome(groupID string, epoch uint64, targetCode string) (string, error) {
	if s == nil || s.db == nil {
		return "", errors.New("auth store is not initialized")
	}
	if err := validateMLSGroupID(groupID); err != nil {
		return "", err
	}
	epochValue, err := mlsEpochValue(epoch)
	if err != nil {
		return "", err
	}
	target, err := normalizeUserCode(targetCode)
	if err != nil {
		return "", err
	}
	var welcome string
	if err := s.db.QueryRow(`SELECT welcome FROM mls_group_welcomes WHERE group_id = ? AND epoch = ? AND target_code = ?`, groupID, epochValue, target).Scan(&welcome); err != nil {
		return "", fmt.Errorf("fetch MLS welcome: %w", err)
	}
	return welcome, nil
}

func (s *AuthStore) migrateChatMessageIdentitySchema() error {
	var schema sql.NullString
	if err := s.db.QueryRow(`SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'chat_messages'`).Scan(&schema); err != nil {
		return fmt.Errorf("read chat message schema: %w", err)
	}
	if !strings.Contains(strings.ToLower(schema.String), "message_id text not null unique") {
		return nil
	}

	tx, err := s.db.Begin()
	if err != nil {
		return fmt.Errorf("begin chat message identity migration: %w", err)
	}
	defer func() { _ = tx.Rollback() }()
	if _, err := tx.Exec(`ALTER TABLE chat_messages RENAME TO chat_messages_legacy`); err != nil {
		return fmt.Errorf("rename legacy chat messages: %w", err)
	}
	if _, err := tx.Exec(`CREATE TABLE chat_messages (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        message_id TEXT NOT NULL,
        kind TEXT NOT NULL,
        conversation_key TEXT NOT NULL,
        room TEXT,
        sender_username TEXT NOT NULL,
        sender_code TEXT NOT NULL,
        target_code TEXT,
        content TEXT NOT NULL,
		crypto_json TEXT,
        delivery_state TEXT NOT NULL DEFAULT 'sent',
        created_at TEXT NOT NULL,
        recalled INTEGER NOT NULL DEFAULT 0,
        UNIQUE(sender_code, conversation_key, message_id)
    )`); err != nil {
		return fmt.Errorf("create migrated chat messages: %w", err)
	}
	if _, err := tx.Exec(`INSERT INTO chat_messages
        (id, message_id, kind, conversation_key, room, sender_username, sender_code, target_code, content, crypto_json, delivery_state, created_at, recalled)
        SELECT id, message_id, kind, conversation_key, room, sender_username, sender_code, target_code, content,
            NULL, COALESCE(delivery_state, 'sent'), created_at, recalled
        FROM chat_messages_legacy`); err != nil {
		return fmt.Errorf("copy legacy chat messages: %w", err)
	}
	if _, err := tx.Exec(`DROP TABLE chat_messages_legacy`); err != nil {
		return fmt.Errorf("drop legacy chat messages: %w", err)
	}
	if _, err := tx.Exec(`CREATE INDEX idx_chat_messages_conversation ON chat_messages(kind, conversation_key, id)`); err != nil {
		return fmt.Errorf("index migrated chat messages: %w", err)
	}
	if _, err := tx.Exec(`CREATE INDEX idx_chat_messages_search_scope ON chat_messages(kind, conversation_key, content, id)`); err != nil {
		return fmt.Errorf("index migrated chat search: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return fmt.Errorf("commit chat message identity migration: %w", err)
	}
	return nil
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

// ResolveConnectionIdentity validates a requested member identity without
// creating it. New identities are persisted only after the room owner accepts
// that specific connection.
func (s *AuthStore) ResolveConnectionIdentity(username, userCode string) (Account, error) {
	if s == nil || s.db == nil {
		return Account{}, errors.New("auth store is not initialized")
	}
	identity := Message{Type: "login", Username: username, UserCode: userCode}
	if err := validateMessage(identity); err != nil {
		return Account{}, fmt.Errorf("invalid account identity: %w", err)
	}
	account, found, err := s.findAccount(username, userCode)
	if err != nil {
		return Account{}, err
	}
	if !found {
		return Account{Username: username, UserCode: userCode}, nil
	}
	if normalizeUsername(account.Username) != normalizeUsername(username) ||
		strings.ToLower(account.UserCode) != strings.ToLower(userCode) {
		return Account{}, ErrAccountAlreadyExists
	}
	return account, nil
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
	_, err := s.SaveChatMessageIfNew(message)
	return err
}

// SaveChatMessageIfNew persists one message and reports whether this call
// created the record. The Hub uses the result to avoid re-broadcasting a
// client retry that carries the same message ID.
func (s *AuthStore) SaveChatMessageIfNew(message Message) (bool, error) {
	if s == nil || s.db == nil {
		return false, errors.New("auth store is not initialized")
	}
	if message.MessageID == "" {
		return false, errors.New("chat message id is required")
	}
	if message.Type != "chat" && message.Type != "private_chat" {
		return false, fmt.Errorf("unsupported history message type: %s", message.Type)
	}
	if len(message.Crypto) == 0 {
		if err := validateTextContent("chat message", message.Content); err != nil {
			return false, err
		}
	} else if err := validateOpaqueCrypto(message.Crypto); err != nil {
		return false, err
	}
	if _, err := normalizeUserCode(message.UserCode); err != nil {
		return false, err
	}
	kind, conversationKey, senderCode, targetCode, err := chatMessageIdentity(message)
	if err != nil {
		return false, err
	}
	createdAt := message.CreatedAt
	if createdAt == "" {
		createdAt = time.Now().UTC().Format(time.RFC3339Nano)
	}
	deliveryState := message.DeliveryState
	if deliveryState == "" {
		deliveryState = "sent"
	}
	result, err := s.db.Exec(`INSERT OR IGNORE INTO chat_messages
		(message_id, kind, conversation_key, room, sender_username, sender_code, target_code, content, crypto_json, delivery_state, created_at, recalled)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)`, message.MessageID, kind, conversationKey, message.Room,
		message.Username, senderCode, targetCode, message.Content, nullableCrypto(message.Crypto), deliveryState, createdAt)
	if err != nil {
		return false, fmt.Errorf("save chat message: %w", err)
	}
	inserted, err := result.RowsAffected()
	if err != nil {
		return false, fmt.Errorf("check saved chat message: %w", err)
	}
	if inserted == 0 {
		return false, nil
	}
	if _, err := s.db.Exec(`DELETE FROM chat_messages
        WHERE kind = ? AND conversation_key = ? AND id NOT IN
        (SELECT id FROM chat_messages WHERE kind = ? AND conversation_key = ? ORDER BY id DESC LIMIT ?)`,
		kind, conversationKey, kind, conversationKey, maxHistoryRowsPerConversation); err != nil {
		return false, fmt.Errorf("prune chat history: %w", err)
	}
	return true, nil
}

func chatMessageIdentity(message Message) (kind, conversationKey, senderCode, targetCode string, err error) {
	senderCode, err = normalizeUserCode(message.UserCode)
	if err != nil {
		return "", "", "", "", err
	}
	kind = "room"
	conversationKey = message.Room
	if message.Type == "private_chat" {
		kind = "private"
		targetCode, err = normalizeUserCode(message.TargetUserCode)
		if err != nil {
			return "", "", "", "", err
		}
		conversationKey = privateConversationKey(senderCode, targetCode)
		return kind, conversationKey, senderCode, targetCode, nil
	}
	if err := validateRoomName(message.Room); err != nil {
		return "", "", "", "", err
	}
	return kind, conversationKey, senderCode, "", nil
}

func (s *AuthStore) UpdateChatMessageDeliveryState(messageID, deliveryState string) error {
	if s == nil || s.db == nil {
		return errors.New("auth store is not initialized")
	}
	if messageID == "" || (deliveryState != "sent" && deliveryState != "delivered") {
		return errors.New("invalid chat delivery state")
	}
	_, err := s.db.Exec(`UPDATE chat_messages SET delivery_state = ? WHERE message_id = ?`, deliveryState, messageID)
	return err
}

func (s *AuthStore) UpdateChatMessageDeliveryStateForMessage(message Message, deliveryState string) error {
	if s == nil || s.db == nil {
		return errors.New("auth store is not initialized")
	}
	if message.MessageID == "" || (deliveryState != "sent" && deliveryState != "delivered") {
		return errors.New("invalid chat delivery state")
	}
	kind, conversationKey, senderCode, _, err := chatMessageIdentity(message)
	if err != nil {
		return err
	}
	_, err = s.db.Exec(`UPDATE chat_messages SET delivery_state = ?
        WHERE message_id = ? AND kind = ? AND conversation_key = ? AND sender_code = ?`,
		deliveryState, message.MessageID, kind, conversationKey, senderCode)
	return err
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
	searchQuery := strings.TrimSpace(query.SearchQuery)
	if len([]byte(searchQuery)) > maxMessageSize {
		return HistoryPage{}, fmt.Errorf("history search query is too long")
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
		if err := s.db.QueryRow(`SELECT id FROM chat_messages WHERE message_id = ? AND kind = ? AND conversation_key = ?`,
			query.BeforeMessageID, kind, conversationKey).Scan(&beforeID); err != nil {
			if errors.Is(err, sql.ErrNoRows) {
				return HistoryPage{Messages: []Message{}}, nil
			}
			return HistoryPage{}, err
		}
	}
	statement := `SELECT message_id, room, sender_username, sender_code, target_code,
		content, crypto_json, delivery_state, created_at, recalled FROM chat_messages
        WHERE kind = ? AND conversation_key = ? AND id < ?`
	arguments := []any{kind, conversationKey, beforeID}
	if searchQuery != "" {
		statement += ` AND instr(content, ?) > 0`
		arguments = append(arguments, searchQuery)
	}
	statement += ` ORDER BY id DESC LIMIT ?`
	arguments = append(arguments, limit+1)
	rows, err := s.db.Query(statement, arguments...)
	if err != nil {
		return HistoryPage{}, fmt.Errorf("load chat history: %w", err)
	}
	defer rows.Close()
	messages := make([]Message, 0, limit)
	for rows.Next() {
		var message Message
		var cryptoJSON sql.NullString
		var recalled int
		if err := rows.Scan(&message.MessageID, &message.Room, &message.Username, &message.UserCode,
			&message.TargetUserCode, &message.Content, &cryptoJSON, &message.DeliveryState, &message.CreatedAt, &recalled); err != nil {
			return HistoryPage{}, err
		}
		if cryptoJSON.Valid {
			message.Crypto = json.RawMessage(cryptoJSON.String)
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
	return s.getStoredMessage(`WHERE message_id = ? ORDER BY id DESC`, messageID)
}

func (s *AuthStore) GetStoredMessageForMessage(message Message) (StoredMessage, error) {
	if s == nil || s.db == nil {
		return StoredMessage{}, errors.New("auth store is not initialized")
	}
	if message.MessageID == "" {
		return StoredMessage{}, errors.New("chat message id is required")
	}
	kind, conversationKey, senderCode, _, err := chatMessageIdentity(message)
	if err != nil {
		return StoredMessage{}, err
	}
	return s.getStoredMessage(`WHERE message_id = ? AND kind = ? AND conversation_key = ? AND sender_code = ?`,
		message.MessageID, kind, conversationKey, senderCode)
}

func (s *AuthStore) getStoredMessage(where string, args ...any) (StoredMessage, error) {
	var stored StoredMessage
	var recalled int
	query := `SELECT message_id, kind, conversation_key, room, sender_username, sender_code,
		target_code, content, crypto_json, delivery_state, created_at, recalled FROM chat_messages ` + where
	var cryptoJSON sql.NullString
	err := s.db.QueryRow(query, args...).Scan(&stored.Message.MessageID, &stored.Kind, &stored.ConversationKey,
		&stored.Message.Room, &stored.Message.Username, &stored.Message.UserCode, &stored.TargetCode,
		&stored.Message.Content, &cryptoJSON, &stored.Message.DeliveryState, &stored.Message.CreatedAt, &recalled)
	if err != nil {
		return StoredMessage{}, err
	}
	if cryptoJSON.Valid {
		stored.Message.Crypto = json.RawMessage(cryptoJSON.String)
	}
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
