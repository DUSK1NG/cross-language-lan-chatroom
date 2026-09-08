package main

import (
	"crypto/sha256"
	"crypto/subtle"
	"database/sql"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/google/uuid"
)

const (
	maxAttachmentLogicalBytes int64 = 5 * 1024 * 1024 * 1024
	maxRoomAttachmentBytes    int64 = 20 * 1024 * 1024 * 1024
	// 47 KiB leaves room for Base64 expansion and the JSON command envelope
	// inside the protocol's 64 KiB frame cap.
	attachmentChunkSize      int64 = 47 * 1024
	attachmentCipherOverhead int64 = 64
	attachmentCryptoTagSize  int64 = 16
	attachmentUploadTTL            = 24 * time.Hour
)

const maxAttachmentCipherChunkBytes = attachmentChunkSize + attachmentCipherOverhead

var ErrAttachmentTooLarge = errors.New("attachment exceeds the 5 GiB limit")
var ErrRoomQuotaExceeded = errors.New("room attachment quota exceeded")
var ErrInvalidAttachmentSize = errors.New("attachment logical size must be positive")
var ErrAttachmentUploadNotFound = errors.New("attachment upload not found")
var ErrAttachmentNotFound = errors.New("attachment not found")
var ErrAttachmentUploadUnauthorized = errors.New("attachment upload is not owned by author")
var ErrAttachmentUploadExpired = errors.New("attachment upload has expired")
var ErrAttachmentChunkOutOfRange = errors.New("attachment chunk index is out of range")
var ErrAttachmentChunkTooLarge = errors.New("attachment ciphertext chunk is too large")
var ErrAttachmentChunkHashMismatch = errors.New("attachment ciphertext hash does not match")
var ErrAttachmentChunkConflict = errors.New("attachment chunk conflicts with existing data")
var ErrAttachmentChunksIncomplete = errors.New("attachment chunks are incomplete")
var ErrAttachmentAlreadyCommitted = errors.New("attachment is already committed")

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

type AttachmentChunkRequest struct {
	UploadID     string
	AuthorCode   string
	Index        int64
	Ciphertext   []byte
	CipherSHA256 string
}

type AttachmentResume struct {
	ReceivedIndexes []int64
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

type attachmentUploadRow struct {
	authorCode  string
	room        string
	logicalSize int64
	expiresAt   time.Time
}

func (s *AuthStore) loadAttachmentUpload(uploadID, authorCode string) (attachmentUploadRow, error) {
	if _, err := uuid.Parse(uploadID); err != nil {
		return attachmentUploadRow{}, ErrAttachmentUploadNotFound
	}
	normalizedAuthor, err := normalizeUserCode(authorCode)
	if err != nil {
		return attachmentUploadRow{}, fmt.Errorf("invalid attachment author: %w", err)
	}
	var row attachmentUploadRow
	var expiresAt string
	err = s.db.QueryRow(`SELECT author_code, room, logical_size, expires_at FROM attachment_uploads WHERE upload_id = ?`, uploadID).
		Scan(&row.authorCode, &row.room, &row.logicalSize, &expiresAt)
	if errors.Is(err, sql.ErrNoRows) {
		return attachmentUploadRow{}, ErrAttachmentUploadNotFound
	}
	if err != nil {
		return attachmentUploadRow{}, fmt.Errorf("load attachment upload: %w", err)
	}
	if row.authorCode != normalizedAuthor {
		return attachmentUploadRow{}, ErrAttachmentUploadUnauthorized
	}
	row.expiresAt, err = time.Parse(time.RFC3339Nano, expiresAt)
	if err != nil {
		return attachmentUploadRow{}, fmt.Errorf("parse attachment expiry: %w", err)
	}
	if !time.Now().UTC().Before(row.expiresAt) {
		return attachmentUploadRow{}, ErrAttachmentUploadExpired
	}
	return row, nil
}

func validateAttachmentChunk(request AttachmentChunkRequest, logicalSize int64) ([]byte, error) {
	if request.Index < 0 || request.Index >= (logicalSize+attachmentChunkSize-1)/attachmentChunkSize {
		return nil, ErrAttachmentChunkOutOfRange
	}
	if len(request.Ciphertext) == 0 || int64(len(request.Ciphertext)) > maxAttachmentCipherChunkBytes {
		return nil, ErrAttachmentChunkTooLarge
	}
	if len(request.CipherSHA256) != sha256HexSize || strings.ToLower(request.CipherSHA256) != request.CipherSHA256 {
		return nil, ErrAttachmentChunkHashMismatch
	}
	want, err := hex.DecodeString(request.CipherSHA256)
	if err != nil || len(want) != sha256.Size {
		return nil, ErrAttachmentChunkHashMismatch
	}
	got := sha256.Sum256(request.Ciphertext)
	if subtle.ConstantTimeCompare(got[:], want) != 1 {
		return nil, ErrAttachmentChunkHashMismatch
	}
	return got[:], nil
}

func (s *AuthStore) chunkPath(uploadID string, index int64) string {
	return filepath.Join(s.attachmentRoot, ".partial", uploadID, strconv.FormatInt(index, 10)+".bin")
}

func writeAttachmentChunk(path string, ciphertext []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return err
	}
	temporary, err := os.CreateTemp(filepath.Dir(path), ".chunk-")
	if err != nil {
		return err
	}
	temporaryPath := temporary.Name()
	defer func() { _ = os.Remove(temporaryPath) }()
	if _, err := temporary.Write(ciphertext); err != nil {
		_ = temporary.Close()
		return err
	}
	if err := temporary.Sync(); err != nil {
		_ = temporary.Close()
		return err
	}
	if err := temporary.Close(); err != nil {
		return err
	}
	return os.Rename(temporaryPath, path)
}

func (s *AuthStore) storedAttachmentChunkMatches(path, wantHash string, wantSize int64) (bool, error) {
	stored, err := os.ReadFile(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return false, nil
		}
		return false, err
	}
	digest := sha256.Sum256(stored)
	return int64(len(stored)) == wantSize && hex.EncodeToString(digest[:]) == wantHash, nil
}

// StoreAttachmentChunk accepts only opaque ciphertext. It serializes local
// metadata and file writes so an identical retry is idempotent while a
// conflicting retry cannot replace already accepted bytes.
func (s *AuthStore) StoreAttachmentChunk(request AttachmentChunkRequest) (bool, error) {
	if s == nil || s.db == nil {
		return false, errors.New("auth store is not initialized")
	}
	s.attachmentMu.Lock()
	defer s.attachmentMu.Unlock()
	upload, err := s.loadAttachmentUpload(request.UploadID, request.AuthorCode)
	if err != nil {
		return false, err
	}
	digest, err := validateAttachmentChunk(request, upload.logicalSize)
	if err != nil {
		return false, err
	}
	var existingHash string
	var existingSize int64
	err = s.db.QueryRow(`SELECT cipher_sha256, cipher_size FROM attachment_chunks WHERE upload_id = ? AND chunk_index = ?`,
		request.UploadID, request.Index).Scan(&existingHash, &existingSize)
	if err == nil {
		if existingHash == request.CipherSHA256 && existingSize == int64(len(request.Ciphertext)) {
			stored, readErr := os.ReadFile(s.chunkPath(request.UploadID, request.Index))
			if readErr != nil {
				return false, fmt.Errorf("accepted attachment chunk is missing from disk: %w", readErr)
			}
			storedDigest := sha256.Sum256(stored)
			if int64(len(stored)) != existingSize ||
				hex.EncodeToString(storedDigest[:]) != existingHash {
				return false, errors.New("accepted attachment chunk on disk failed integrity check")
			}
			return true, nil
		}
		return false, ErrAttachmentChunkConflict
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return false, fmt.Errorf("lookup attachment chunk: %w", err)
	}
	path := s.chunkPath(request.UploadID, request.Index)
	if info, statErr := os.Stat(path); statErr == nil {
		matches, verifyErr := s.storedAttachmentChunkMatches(path, hex.EncodeToString(digest), int64(len(request.Ciphertext)))
		if verifyErr != nil {
			return false, fmt.Errorf("verify orphan attachment chunk: %w", verifyErr)
		}
		if matches {
			if _, insertErr := s.db.Exec(`INSERT INTO attachment_chunks(upload_id, chunk_index, cipher_size, cipher_sha256, created_at)
				VALUES (?, ?, ?, ?, ?)`, request.UploadID, request.Index, len(request.Ciphertext), hex.EncodeToString(digest),
				time.Now().UTC().Format(time.RFC3339Nano)); insertErr != nil {
				return false, fmt.Errorf("recover attachment chunk metadata: %w", insertErr)
			}
			return true, nil
		}
		if !info.IsDir() {
			return false, ErrAttachmentChunkConflict
		}
	} else if !errors.Is(statErr, os.ErrNotExist) {
		return false, fmt.Errorf("inspect attachment chunk: %w", statErr)
	}
	if err := writeAttachmentChunk(path, request.Ciphertext); err != nil {
		return false, fmt.Errorf("write attachment chunk: %w", err)
	}
	if _, err := s.db.Exec(`INSERT INTO attachment_chunks(upload_id, chunk_index, cipher_size, cipher_sha256, created_at)
		VALUES (?, ?, ?, ?, ?)`, request.UploadID, request.Index, len(request.Ciphertext), hex.EncodeToString(digest),
		time.Now().UTC().Format(time.RFC3339Nano)); err != nil {
		_ = os.Remove(path)
		return false, fmt.Errorf("save attachment chunk: %w", err)
	}
	return false, nil
}

func (s *AuthStore) ResumeAttachmentUpload(uploadID, authorCode string) (AttachmentResume, error) {
	if s == nil || s.db == nil {
		return AttachmentResume{}, errors.New("auth store is not initialized")
	}
	s.attachmentMu.Lock()
	defer s.attachmentMu.Unlock()
	if _, err := s.loadAttachmentUpload(uploadID, authorCode); err != nil {
		return AttachmentResume{}, err
	}
	rows, err := s.db.Query(`SELECT chunk_index, cipher_size, cipher_sha256 FROM attachment_chunks WHERE upload_id = ? ORDER BY chunk_index`, uploadID)
	if err != nil {
		return AttachmentResume{}, fmt.Errorf("list attachment chunks: %w", err)
	}
	defer rows.Close()
	var result AttachmentResume
	var invalidIndexes []int64
	for rows.Next() {
		var index int64
		var size int64
		var hash string
		if err := rows.Scan(&index, &size, &hash); err != nil {
			return AttachmentResume{}, fmt.Errorf("scan attachment chunk index: %w", err)
		}
		matches, verifyErr := s.storedAttachmentChunkMatches(s.chunkPath(uploadID, index), hash, size)
		if verifyErr != nil {
			return AttachmentResume{}, fmt.Errorf("verify attachment chunk %d: %w", index, verifyErr)
		}
		if !matches {
			invalidIndexes = append(invalidIndexes, index)
			continue
		}
		result.ReceivedIndexes = append(result.ReceivedIndexes, index)
	}
	if err := rows.Err(); err != nil {
		return AttachmentResume{}, fmt.Errorf("iterate attachment chunk indexes: %w", err)
	}
	for _, index := range invalidIndexes {
		if _, deleteErr := s.db.Exec(`DELETE FROM attachment_chunks WHERE upload_id = ? AND chunk_index = ?`, uploadID, index); deleteErr != nil {
			return AttachmentResume{}, fmt.Errorf("remove invalid attachment chunk %d: %w", index, deleteErr)
		}
		_ = os.Remove(s.chunkPath(uploadID, index))
	}
	return result, nil
}

type AttachmentCommit struct {
	AttachmentID      string
	LogicalSize       int64
	WholeCipherSHA256 string
}

type AttachmentDownloadChunk struct {
	AttachmentID string
	Index        int64
	Ciphertext   []byte
	CipherSHA256 string
	Last         bool
}

func (s *AuthStore) ReadAttachmentChunk(attachmentID string, index int64) (AttachmentDownloadChunk, error) {
	if s == nil || s.db == nil {
		return AttachmentDownloadChunk{}, errors.New("auth store is not initialized")
	}
	if _, err := uuid.Parse(attachmentID); err != nil {
		return AttachmentDownloadChunk{}, ErrAttachmentNotFound
	}
	var logicalSize, chunkSize int64
	var storedChunkSize sql.NullInt64
	var committedAt sql.NullString
	if err := s.db.QueryRow(`SELECT logical_size, (SELECT chunk_size FROM attachment_uploads WHERE attachment_id=attachments.attachment_id LIMIT 1), committed_at FROM attachments WHERE attachment_id=?`, attachmentID).
		Scan(&logicalSize, &storedChunkSize, &committedAt); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return AttachmentDownloadChunk{}, ErrAttachmentNotFound
		}
		return AttachmentDownloadChunk{}, fmt.Errorf("load attachment: %w", err)
	}
	if storedChunkSize.Valid {
		chunkSize = storedChunkSize.Int64
	}
	// Committed uploads no longer have an upload row, so use the protocol chunk size.
	if chunkSize <= 0 {
		chunkSize = attachmentChunkSize
	}
	if !committedAt.Valid || index < 0 || index >= (logicalSize+chunkSize-1)/chunkSize {
		if !committedAt.Valid {
			return AttachmentDownloadChunk{}, ErrAttachmentNotFound
		}
		return AttachmentDownloadChunk{}, ErrAttachmentChunkOutOfRange
	}
	path := filepath.Join(s.attachmentRoot, attachmentID, strconv.FormatInt(index, 10)+".bin")
	ciphertext, err := os.ReadFile(path)
	if err != nil {
		return AttachmentDownloadChunk{}, fmt.Errorf("read attachment chunk: %w", err)
	}
	digest := sha256.Sum256(ciphertext)
	plainSize := chunkSize
	if remaining := logicalSize - index*chunkSize; remaining < plainSize {
		plainSize = remaining
	}
	if int64(len(ciphertext)) != plainSize+attachmentCryptoTagSize {
		return AttachmentDownloadChunk{}, errors.New("attachment chunk failed integrity check")
	}
	return AttachmentDownloadChunk{AttachmentID: attachmentID, Index: index, Ciphertext: ciphertext,
		CipherSHA256: hex.EncodeToString(digest[:]), Last: index+1 == (logicalSize+chunkSize-1)/chunkSize}, nil
}

// CleanupExpiredAttachmentUploads removes only unfinished uploads. Committed
// attachments are intentionally not collected here because their reference
// lifecycle is owned by message recall/GC.
func (s *AuthStore) CleanupExpiredAttachmentUploads(now time.Time) (int, error) {
	if s == nil || s.db == nil {
		return 0, errors.New("auth store is not initialized")
	}
	s.attachmentMu.Lock()
	defer s.attachmentMu.Unlock()
	rows, err := s.db.Query(`SELECT upload_id, attachment_id, room, reserved_bytes FROM attachment_uploads WHERE expires_at <= ?`, now.UTC().Format(time.RFC3339Nano))
	if err != nil {
		return 0, fmt.Errorf("list expired attachment uploads: %w", err)
	}
	type expiredUpload struct {
		uploadID, attachmentID, room string
		reserved                     int64
	}
	var expired []expiredUpload
	for rows.Next() {
		var item expiredUpload
		if err := rows.Scan(&item.uploadID, &item.attachmentID, &item.room, &item.reserved); err != nil {
			rows.Close()
			return 0, fmt.Errorf("scan expired attachment upload: %w", err)
		}
		expired = append(expired, item)
	}
	if err := rows.Err(); err != nil {
		rows.Close()
		return 0, fmt.Errorf("iterate expired attachment uploads: %w", err)
	}
	rows.Close()
	for _, item := range expired {
		tx, err := s.db.Begin()
		if err != nil {
			return 0, fmt.Errorf("begin expired attachment cleanup: %w", err)
		}
		if _, err = tx.Exec(`DELETE FROM attachment_chunks WHERE upload_id=?`, item.uploadID); err == nil {
			_, err = tx.Exec(`DELETE FROM attachment_uploads WHERE upload_id=?`, item.uploadID)
		}
		if err == nil {
			_, err = tx.Exec(`DELETE FROM attachments WHERE attachment_id=? AND committed_at IS NULL`, item.attachmentID)
		}
		if err == nil {
			_, err = tx.Exec(`UPDATE room_quotas SET reserved_bytes=reserved_bytes-? WHERE room=?`, item.reserved, item.room)
		}
		if err == nil {
			err = tx.Commit()
		} else {
			_ = tx.Rollback()
		}
		if err != nil {
			return 0, fmt.Errorf("remove expired attachment upload %s: %w", item.uploadID, err)
		}
		if err := os.RemoveAll(filepath.Join(s.attachmentRoot, ".partial", item.uploadID)); err != nil {
			return 0, fmt.Errorf("remove expired attachment files %s: %w", item.uploadID, err)
		}
	}
	return len(expired), nil
}

// CommitAttachmentUpload verifies every ciphertext chunk, makes the object
// visible as one directory, and moves the reservation to used quota.
func (s *AuthStore) CommitAttachmentUpload(uploadID, authorCode string) (AttachmentCommit, error) {
	if s == nil || s.db == nil {
		return AttachmentCommit{}, errors.New("auth store is not initialized")
	}
	s.attachmentMu.Lock()
	defer s.attachmentMu.Unlock()
	upload, err := s.loadAttachmentUpload(uploadID, authorCode)
	if err != nil {
		return AttachmentCommit{}, err
	}
	var attachmentID string
	var logicalSize, chunkSize int64
	if err := s.db.QueryRow(`SELECT attachment_id, logical_size, chunk_size FROM attachment_uploads WHERE upload_id=?`, uploadID).
		Scan(&attachmentID, &logicalSize, &chunkSize); err != nil {
		return AttachmentCommit{}, fmt.Errorf("load attachment commit metadata: %w", err)
	}
	chunkCount := (logicalSize + chunkSize - 1) / chunkSize
	rows, err := s.db.Query(`SELECT chunk_index, cipher_size, cipher_sha256 FROM attachment_chunks WHERE upload_id=? ORDER BY chunk_index`, uploadID)
	if err != nil {
		return AttachmentCommit{}, fmt.Errorf("list attachment chunks for commit: %w", err)
	}
	defer rows.Close()
	hash := sha256.New()
	for expected := int64(0); expected < chunkCount; expected++ {
		if !rows.Next() {
			return AttachmentCommit{}, ErrAttachmentChunksIncomplete
		}
		var index, size int64
		var digest string
		if err := rows.Scan(&index, &size, &digest); err != nil {
			return AttachmentCommit{}, fmt.Errorf("scan attachment chunk for commit: %w", err)
		}
		if index != expected {
			return AttachmentCommit{}, ErrAttachmentChunksIncomplete
		}
		plainSize := chunkSize
		if remaining := logicalSize - index*chunkSize; remaining < plainSize {
			plainSize = remaining
		}
		if size != plainSize+attachmentCryptoTagSize {
			return AttachmentCommit{}, ErrAttachmentChunksIncomplete
		}
		path := s.chunkPath(uploadID, index)
		file, err := os.Open(path)
		if err != nil {
			return AttachmentCommit{}, fmt.Errorf("open attachment chunk %d: %w", index, err)
		}
		written, copyErr := io.Copy(hash, file)
		closeErr := file.Close()
		if copyErr != nil || closeErr != nil || written != size {
			return AttachmentCommit{}, fmt.Errorf("read attachment chunk %d: integrity check failed", index)
		}
		stored, readErr := os.ReadFile(path)
		if readErr != nil || int64(len(stored)) != size {
			return AttachmentCommit{}, fmt.Errorf("attachment chunk %d failed integrity check", index)
		}
		digestBytes := sha256.Sum256(stored)
		if hex.EncodeToString(digestBytes[:]) != digest {
			return AttachmentCommit{}, fmt.Errorf("attachment chunk %d failed integrity check", index)
		}
	}
	if rows.Next() {
		return AttachmentCommit{}, ErrAttachmentChunksIncomplete
	}
	whole := hex.EncodeToString(hash.Sum(nil))
	partialDir := filepath.Join(s.attachmentRoot, ".partial", uploadID)
	finalDir := filepath.Join(s.attachmentRoot, attachmentID)
	if _, err := os.Stat(finalDir); err == nil {
		return AttachmentCommit{}, ErrAttachmentAlreadyCommitted
	}
	if err := os.MkdirAll(s.attachmentRoot, 0o700); err != nil {
		return AttachmentCommit{}, fmt.Errorf("prepare attachment directory: %w", err)
	}
	if err := os.Rename(partialDir, finalDir); err != nil {
		return AttachmentCommit{}, fmt.Errorf("publish attachment: %w", err)
	}
	tx, err := s.db.Begin()
	if err != nil {
		return AttachmentCommit{}, fmt.Errorf("begin attachment commit: %w", err)
	}
	defer func() { _ = tx.Rollback() }()
	now := time.Now().UTC().Format(time.RFC3339Nano)
	if _, err = tx.Exec(`UPDATE attachments SET whole_sha256=?, committed_at=? WHERE attachment_id=? AND committed_at IS NULL`, whole, now, attachmentID); err != nil {
		return AttachmentCommit{}, fmt.Errorf("mark attachment committed: %w", err)
	}
	if _, err = tx.Exec(`UPDATE room_quotas SET reserved_bytes=reserved_bytes-?, used_bytes=used_bytes+? WHERE room=?`, logicalSize, logicalSize, upload.room); err != nil {
		return AttachmentCommit{}, fmt.Errorf("move attachment quota: %w", err)
	}
	if _, err = tx.Exec(`DELETE FROM attachment_chunks WHERE upload_id=?`, uploadID); err != nil {
		return AttachmentCommit{}, fmt.Errorf("remove committed chunk metadata: %w", err)
	}
	if _, err = tx.Exec(`DELETE FROM attachment_uploads WHERE upload_id=?`, uploadID); err != nil {
		return AttachmentCommit{}, fmt.Errorf("remove committed upload: %w", err)
	}
	if err = tx.Commit(); err != nil {
		return AttachmentCommit{}, fmt.Errorf("commit attachment transaction: %w", err)
	}
	return AttachmentCommit{AttachmentID: attachmentID, LogicalSize: logicalSize, WholeCipherSHA256: whole}, nil
}
