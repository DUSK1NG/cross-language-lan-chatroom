package main

import (
	"errors"
	"fmt"
	"time"

	"github.com/google/uuid"
)

const (
	maxAttachmentLogicalBytes int64 = 500 * 1024 * 1024
	maxRoomAttachmentBytes    int64 = 20 * 1024 * 1024 * 1024
	attachmentChunkSize       int64 = 48 * 1024
	attachmentUploadTTL             = 24 * time.Hour
)

var ErrAttachmentTooLarge = errors.New("attachment exceeds the 500 MiB limit")
var ErrRoomQuotaExceeded = errors.New("room attachment quota exceeded")
var ErrInvalidAttachmentSize = errors.New("attachment logical size must be positive")

// AttachmentInitRequest contains only server-verifiable upload metadata. In
// particular, clients must never send a file path or plaintext file name.
type AttachmentInitRequest struct {
	Room        string
	AuthorCode  string
	LogicalSize int64
}

type AttachmentUpload struct {
	AttachmentID string
	UploadID     string
	ChunkSize    int64
	ExpiresAt    time.Time
}

// initializeAttachmentSchema owns the durable, server-visible portion of an
// encrypted attachment. File names and encryption metadata are deliberately
// absent: they stay inside the client-encrypted manifest.
func (s *AuthStore) initializeAttachmentSchema() error {
	const schema = `
CREATE TABLE IF NOT EXISTS attachments (
    attachment_id TEXT PRIMARY KEY,
    room TEXT NOT NULL,
    logical_size INTEGER NOT NULL,
    whole_sha256 TEXT NOT NULL DEFAULT '',
    reference_count INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL,
    committed_at TEXT
);
CREATE TABLE IF NOT EXISTS attachment_uploads (
    upload_id TEXT PRIMARY KEY,
    attachment_id TEXT NOT NULL UNIQUE,
    room TEXT NOT NULL,
    author_code TEXT NOT NULL,
    logical_size INTEGER NOT NULL,
    reserved_bytes INTEGER NOT NULL,
    chunk_size INTEGER NOT NULL,
    expires_at TEXT NOT NULL,
    created_at TEXT NOT NULL,
    FOREIGN KEY (attachment_id) REFERENCES attachments(attachment_id)
);
CREATE TABLE IF NOT EXISTS attachment_chunks (
    upload_id TEXT NOT NULL,
    chunk_index INTEGER NOT NULL,
    cipher_size INTEGER NOT NULL,
    cipher_sha256 TEXT NOT NULL,
    created_at TEXT NOT NULL,
    PRIMARY KEY(upload_id, chunk_index),
    FOREIGN KEY (upload_id) REFERENCES attachment_uploads(upload_id)
);
CREATE TABLE IF NOT EXISTS room_quotas (
    room TEXT PRIMARY KEY,
    reserved_bytes INTEGER NOT NULL DEFAULT 0,
    used_bytes INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_attachment_uploads_expiry
    ON attachment_uploads(expires_at);
CREATE INDEX IF NOT EXISTS idx_attachments_room
    ON attachments(room);
`
	if _, err := s.db.Exec(schema); err != nil {
		return fmt.Errorf("initialize attachment schema: %w", err)
	}
	return nil
}

// InitializeAttachment reserves room quota and creates an upload token in one
// transaction. The transfer protocol adds chunks later; this phase does not
// accept or persist file bytes.
func (s *AuthStore) InitializeAttachment(request AttachmentInitRequest) (AttachmentUpload, error) {
	if s == nil || s.db == nil {
		return AttachmentUpload{}, errors.New("auth store is not initialized")
	}
	if err := validateRoomName(request.Room); err != nil {
		return AttachmentUpload{}, err
	}
	authorCode, err := normalizeUserCode(request.AuthorCode)
	if err != nil {
		return AttachmentUpload{}, fmt.Errorf("invalid attachment author: %w", err)
	}
	if request.LogicalSize <= 0 {
		return AttachmentUpload{}, ErrInvalidAttachmentSize
	}
	if request.LogicalSize > maxAttachmentLogicalBytes {
		return AttachmentUpload{}, ErrAttachmentTooLarge
	}

	// SQLite serializes writes, and this process-local mutex ensures callers on
	// separate goroutines cannot turn a contention error into a false failure.
	s.attachmentMu.Lock()
	defer s.attachmentMu.Unlock()

	tx, err := s.db.Begin()
	if err != nil {
		return AttachmentUpload{}, fmt.Errorf("begin attachment initialization: %w", err)
	}
	defer func() { _ = tx.Rollback() }()

	quota, err := tx.Exec(`INSERT INTO room_quotas(room, reserved_bytes, used_bytes)
		VALUES (?, ?, 0)
		ON CONFLICT(room) DO UPDATE SET reserved_bytes = room_quotas.reserved_bytes + excluded.reserved_bytes
		WHERE room_quotas.reserved_bytes + room_quotas.used_bytes + excluded.reserved_bytes <= ?`,
		request.Room, request.LogicalSize, maxRoomAttachmentBytes)
	if err != nil {
		return AttachmentUpload{}, fmt.Errorf("reserve attachment quota: %w", err)
	}
	changed, err := quota.RowsAffected()
	if err != nil {
		return AttachmentUpload{}, fmt.Errorf("check attachment quota reservation: %w", err)
	}
	if changed != 1 {
		return AttachmentUpload{}, ErrRoomQuotaExceeded
	}

	createdAt := time.Now().UTC()
	result := AttachmentUpload{
		AttachmentID: uuid.NewString(),
		UploadID:     uuid.NewString(),
		ChunkSize:    attachmentChunkSize,
		ExpiresAt:    createdAt.Add(attachmentUploadTTL),
	}
	if _, err := tx.Exec(`INSERT INTO attachments(attachment_id, room, logical_size, whole_sha256, created_at)
		VALUES (?, ?, ?, '', ?)`, result.AttachmentID, request.Room, request.LogicalSize,
		createdAt.Format(time.RFC3339Nano)); err != nil {
		return AttachmentUpload{}, fmt.Errorf("create attachment metadata: %w", err)
	}
	if _, err := tx.Exec(`INSERT INTO attachment_uploads(
		upload_id, attachment_id, room, author_code, logical_size, reserved_bytes, chunk_size, expires_at, created_at
	) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`,
		result.UploadID, result.AttachmentID, request.Room, authorCode, request.LogicalSize,
		request.LogicalSize, result.ChunkSize, result.ExpiresAt.Format(time.RFC3339Nano),
		createdAt.Format(time.RFC3339Nano)); err != nil {
		return AttachmentUpload{}, fmt.Errorf("create attachment upload: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return AttachmentUpload{}, fmt.Errorf("commit attachment initialization: %w", err)
	}
	return result, nil
}
