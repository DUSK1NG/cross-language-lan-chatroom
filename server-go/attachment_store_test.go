package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"
)

func chunkHash(ciphertext []byte) string {
	digest := sha256.Sum256(ciphertext)
	return hex.EncodeToString(digest[:])
}

func TestAuthStoreInitializesAttachmentMetadataSchema(t *testing.T) {
	store, _ := newTestAuthStore(t)

	for _, table := range []string{
		"attachments",
		"attachment_uploads",
		"attachment_chunks",
		"room_quotas",
	} {
		var name string
		err := store.db.QueryRow(
			`SELECT name FROM sqlite_master WHERE type = 'table' AND name = ?`, table,
		).Scan(&name)
		if err != nil || name != table {
			t.Fatalf("attachment table %q was not initialized: name=%q err=%v", table, name, err)
		}
	}
}

func TestAttachmentChunkSizeLeavesRoomForBase64JSONEnvelope(t *testing.T) {
	const want = int64(47 * 1024)
	if attachmentChunkSize != want {
		t.Fatalf("attachment chunk size = %d, want %d", attachmentChunkSize, want)
	}
}

func TestAttachmentInitAcceptsMaximumSizeAndRejectsLargerFile(t *testing.T) {
	store, _ := newTestAuthStore(t)
	request := AttachmentInitRequest{Room: "lobby", AuthorCode: "alice01", LogicalSize: maxAttachmentLogicalBytes}

	created, err := store.InitializeAttachment(request)
	if err != nil {
		t.Fatalf("initialize maximum attachment: %v", err)
	}
	if created.AttachmentID == "" || created.UploadID == "" || created.ChunkSize != attachmentChunkSize {
		t.Fatalf("created attachment = %+v", created)
	}

	_, err = store.InitializeAttachment(AttachmentInitRequest{
		Room: "lobby", AuthorCode: "alice01", LogicalSize: maxAttachmentLogicalBytes + 1,
	})
	if !errors.Is(err, ErrAttachmentTooLarge) {
		t.Fatalf("oversized attachment error = %v, want %v", err, ErrAttachmentTooLarge)
	}

	var reserved int64
	if err := store.db.QueryRow(`SELECT reserved_bytes FROM room_quotas WHERE room = 'lobby'`).Scan(&reserved); err != nil {
		t.Fatal(err)
	}
	if reserved != maxAttachmentLogicalBytes {
		t.Fatalf("reserved bytes = %d, want %d", reserved, maxAttachmentLogicalBytes)
	}
}

func TestAttachmentInitConcurrentReservationsNeverExceedRoomQuota(t *testing.T) {
	store, _ := newTestAuthStore(t)
	const alreadyReserved = int(maxRoomAttachmentBytes/maxAttachmentLogicalBytes) - 1
	for i := 0; i < alreadyReserved; i++ {
		if _, err := store.InitializeAttachment(AttachmentInitRequest{
			Room: "lobby", AuthorCode: "alice01", LogicalSize: maxAttachmentLogicalBytes,
		}); err != nil {
			t.Fatalf("seed reservation %d: %v", i, err)
		}
	}

	results := make(chan error, 2)
	var group sync.WaitGroup
	for i := 0; i < cap(results); i++ {
		group.Add(1)
		go func() {
			defer group.Done()
			_, err := store.InitializeAttachment(AttachmentInitRequest{
				Room: "lobby", AuthorCode: "alice01", LogicalSize: maxAttachmentLogicalBytes,
			})
			results <- err
		}()
	}
	group.Wait()
	close(results)

	var successes, quotaRejections int
	for err := range results {
		switch {
		case err == nil:
			successes++
		case errors.Is(err, ErrRoomQuotaExceeded):
			quotaRejections++
		default:
			t.Fatalf("concurrent reservation error = %v", err)
		}
	}
	if successes != 1 || quotaRejections != 1 {
		t.Fatalf("concurrent reservation results: successes=%d quota_rejections=%d", successes, quotaRejections)
	}
}

func TestCommitAttachmentRequiresAllAuthenticatedChunksAndPublishesAtomically(t *testing.T) {
	store, _ := newTestAuthStore(t)
	upload, err := store.InitializeAttachment(AttachmentInitRequest{
		Room: "lobby", AuthorCode: "alice01", LogicalSize: 5,
	})
	if err != nil {
		t.Fatal(err)
	}
	ciphertext := append(bytes.Repeat([]byte{'e'}, 5), bytes.Repeat([]byte{'t'}, 16)...)
	if _, err := store.StoreAttachmentChunk(AttachmentChunkRequest{
		UploadID: upload.UploadID, AuthorCode: "alice01", Index: 0,
		Ciphertext: ciphertext, CipherSHA256: chunkHash(ciphertext),
	}); err != nil {
		t.Fatal(err)
	}
	committed, err := store.CommitAttachmentUpload(upload.UploadID, "alice01")
	if err != nil {
		t.Fatalf("commit attachment: %v", err)
	}
	if committed.AttachmentID != upload.AttachmentID || committed.LogicalSize != 5 || committed.WholeCipherSHA256 == "" {
		t.Fatalf("commit result = %+v", committed)
	}
	if _, err := store.loadAttachmentUpload(upload.UploadID, "alice01"); !errors.Is(err, ErrAttachmentUploadNotFound) {
		t.Fatalf("upload after commit = %v, want not found", err)
	}
	var committedAt string
	if err := store.db.QueryRow(`SELECT committed_at FROM attachments WHERE attachment_id=?`, upload.AttachmentID).Scan(&committedAt); err != nil || committedAt == "" {
		t.Fatalf("committed metadata: value=%q err=%v", committedAt, err)
	}
	finalPath := filepath.Join(store.attachmentRoot, upload.AttachmentID, "0.bin")
	if got, err := os.ReadFile(finalPath); err != nil || !bytes.Equal(got, ciphertext) {
		t.Fatalf("final ciphertext: got=%q err=%v", got, err)
	}
}

func TestCommitAttachmentRejectsMissingChunkAndLeavesUploadResumable(t *testing.T) {
	store, _ := newTestAuthStore(t)
	upload, err := store.InitializeAttachment(AttachmentInitRequest{
		Room: "lobby", AuthorCode: "alice01", LogicalSize: attachmentChunkSize + 1,
	})
	if err != nil {
		t.Fatal(err)
	}
	first := bytes.Repeat([]byte{'a'}, int(attachmentChunkSize+16))
	if _, err := store.StoreAttachmentChunk(AttachmentChunkRequest{
		UploadID: upload.UploadID, AuthorCode: "alice01", Index: 0,
		Ciphertext: first, CipherSHA256: chunkHash(first),
	}); err != nil {
		t.Fatal(err)
	}
	if _, err := store.CommitAttachmentUpload(upload.UploadID, "alice01"); !errors.Is(err, ErrAttachmentChunksIncomplete) {
		t.Fatalf("missing chunk error = %v, want %v", err, ErrAttachmentChunksIncomplete)
	}
	if _, err := store.loadAttachmentUpload(upload.UploadID, "alice01"); err != nil {
		t.Fatalf("upload should remain resumable: %v", err)
	}
}

func TestReadCommittedAttachmentChunkReturnsCiphertextAndRejectsInvalidIndex(t *testing.T) {
	store, _ := newTestAuthStore(t)
	upload, err := store.InitializeAttachment(AttachmentInitRequest{Room: "lobby", AuthorCode: "alice01", LogicalSize: 5})
	if err != nil {
		t.Fatal(err)
	}
	ciphertext := append(bytes.Repeat([]byte{'e'}, 5), bytes.Repeat([]byte{'t'}, 16)...)
	if _, err := store.StoreAttachmentChunk(AttachmentChunkRequest{UploadID: upload.UploadID, AuthorCode: "alice01", Index: 0, Ciphertext: ciphertext, CipherSHA256: chunkHash(ciphertext)}); err != nil {
		t.Fatal(err)
	}
	if _, err := store.CommitAttachmentUpload(upload.UploadID, "alice01"); err != nil {
		t.Fatal(err)
	}
	chunk, err := store.ReadAttachmentChunk(upload.AttachmentID, 0)
	if err != nil || !bytes.Equal(chunk.Ciphertext, ciphertext) || chunk.CipherSHA256 != chunkHash(ciphertext) || chunk.Last != true {
		t.Fatalf("download chunk = %+v err=%v", chunk, err)
	}
	if _, err := store.ReadAttachmentChunk(upload.AttachmentID, 1); !errors.Is(err, ErrAttachmentChunkOutOfRange) {
		t.Fatalf("invalid download index error = %v", err)
	}
}

func TestCleanupExpiredAttachmentUploadsReleasesReservationAndPartialFiles(t *testing.T) {
	store, _ := newTestAuthStore(t)
	upload, err := store.InitializeAttachment(AttachmentInitRequest{Room: "lobby", AuthorCode: "alice01", LogicalSize: 5})
	if err != nil {
		t.Fatal(err)
	}
	ciphertext := append(bytes.Repeat([]byte{'e'}, 5), bytes.Repeat([]byte{'t'}, 16)...)
	if _, err := store.StoreAttachmentChunk(AttachmentChunkRequest{UploadID: upload.UploadID, AuthorCode: "alice01", Index: 0, Ciphertext: ciphertext, CipherSHA256: chunkHash(ciphertext)}); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(store.chunkPath(upload.UploadID, 0), ciphertext, 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := store.db.Exec(`UPDATE attachment_uploads SET expires_at=? WHERE upload_id=?`, time.Now().UTC().Add(-time.Minute).Format(time.RFC3339Nano), upload.UploadID); err != nil {
		t.Fatal(err)
	}
	removed, err := store.CleanupExpiredAttachmentUploads(time.Now().UTC())
	if err != nil || removed != 1 {
		t.Fatalf("cleanup removed=%d err=%v", removed, err)
	}
	var reserved int64
	if err := store.db.QueryRow(`SELECT reserved_bytes FROM room_quotas WHERE room='lobby'`).Scan(&reserved); err != nil || reserved != 0 {
		t.Fatalf("reserved bytes=%d err=%v", reserved, err)
	}
	if _, err := os.Stat(filepath.Join(store.attachmentRoot, ".partial", upload.UploadID)); !os.IsNotExist(err) {
		t.Fatalf("partial directory still exists: %v", err)
	}
}

func TestHubAttachmentInitRequiresCurrentRoomMembership(t *testing.T) {
	store, _ := newTestAuthStore(t)
	hub := NewHub()
	hub.OfflineStore = store
	member := newTestClient(t, "Alice", "alice01")
	outsider := newTestClient(t, "Bob", "bob01")
	hub.Clients[member] = true
	hub.Clients[outsider] = true
	hub.Rooms["lobby"] = map[*Client]bool{member: true}

	hub.handleAttachmentInit(AttachmentInitHubRequest{
		Sender: member, Room: "lobby", LogicalSize: 42, CommandID: "attachment-init-1",
	})
	response := <-member.Send
	if response.Type != "attachment.init" || response.Room != "lobby" ||
		response.CommandID != "attachment-init-1" || response.AttachmentID == "" ||
		response.UploadID == "" || response.ChunkSize != attachmentChunkSize {
		t.Fatalf("attachment init response = %+v", response)
	}

	hub.handleAttachmentInit(AttachmentInitHubRequest{
		Sender: outsider, Room: "lobby", LogicalSize: 42, CommandID: "attachment-init-2",
	})
	response = <-outsider.Send
	if response.Type != "error" || response.Content != "Attachment upload access denied" || response.CommandID != "attachment-init-2" {
		t.Fatalf("attachment init denial = %+v", response)
	}
}

func TestHubAttachmentChunkAndResumeRequireCurrentUploadMember(t *testing.T) {
	store, _ := newTestAuthStore(t)
	hub := NewHub()
	hub.OfflineStore = store
	owner := newTestClient(t, "Alice", "alice01")
	outsider := newTestClient(t, "Bob", "bob01")
	hub.Clients[owner] = true
	hub.Clients[outsider] = true
	hub.Rooms["lobby"] = map[*Client]bool{owner: true}
	upload, err := store.InitializeAttachment(AttachmentInitRequest{Room: "lobby", AuthorCode: owner.UserCode, LogicalSize: attachmentChunkSize})
	if err != nil {
		t.Fatal(err)
	}
	ciphertext := []byte("hub-authenticated chunk")
	request := AttachmentChunkHubRequest{Sender: outsider, UploadID: upload.UploadID, Index: 0,
		Ciphertext: base64.StdEncoding.EncodeToString(ciphertext), CipherSHA256: chunkHash(ciphertext), CommandID: "denied"}
	hub.handleAttachmentChunk(request)
	if response := <-outsider.Send; response.Type != "error" || response.CommandID != "denied" {
		t.Fatalf("outsider chunk response = %+v", response)
	}

	request.Sender = owner
	request.CommandID = "stored"
	hub.handleAttachmentChunk(request)
	if response := <-owner.Send; response.Type != "attachment.chunk" || response.Content != "stored" {
		t.Fatalf("owner chunk response = %+v", response)
	}
	request.Sender = outsider
	request.CommandID = "resume-denied"
	hub.handleAttachmentResume(AttachmentResumeHubRequest{Sender: outsider, UploadID: upload.UploadID, CommandID: request.CommandID})
	if response := <-outsider.Send; response.Type != "error" || response.CommandID != "resume-denied" {
		t.Fatalf("outsider resume response = %+v", response)
	}
}

func TestReceiveAttachmentInitRejectsClientFilePath(t *testing.T) {
	var stream bytes.Buffer
	if err := writeFrame(&stream, []byte(`{
		"type":"attachment.init","room":"lobby","logical_size":42,"file_path":"C:\\\\private\\\\report.pdf"
	}`)); err != nil {
		t.Fatal(err)
	}
	if _, err := receiveMessage(&stream); err == nil {
		t.Fatal("attachment init with a client file path was accepted")
	}
}

func TestAttachmentChunksAreIdempotentAndResumeInIndexOrder(t *testing.T) {
	store, _ := newTestAuthStore(t)
	upload, err := store.InitializeAttachment(AttachmentInitRequest{
		Room: "lobby", AuthorCode: "alice01", LogicalSize: attachmentChunkSize * 2,
	})
	if err != nil {
		t.Fatal(err)
	}

	second := []byte("ciphertext-two")
	if duplicate, err := store.StoreAttachmentChunk(AttachmentChunkRequest{
		UploadID: upload.UploadID, AuthorCode: "alice01", Index: 1,
		Ciphertext: second, CipherSHA256: chunkHash(second),
	}); err != nil || duplicate {
		t.Fatalf("store out-of-order chunk: duplicate=%t err=%v", duplicate, err)
	}
	first := []byte("ciphertext-one")
	request := AttachmentChunkRequest{UploadID: upload.UploadID, AuthorCode: "alice01", Index: 0,
		Ciphertext: first, CipherSHA256: chunkHash(first)}
	if duplicate, err := store.StoreAttachmentChunk(request); err != nil || duplicate {
		t.Fatalf("store first chunk: duplicate=%t err=%v", duplicate, err)
	}
	if duplicate, err := store.StoreAttachmentChunk(request); err != nil || !duplicate {
		t.Fatalf("repeat chunk: duplicate=%t err=%v", duplicate, err)
	}

	resume, err := store.ResumeAttachmentUpload(upload.UploadID, "alice01")
	if err != nil || len(resume.ReceivedIndexes) != 2 || resume.ReceivedIndexes[0] != 0 || resume.ReceivedIndexes[1] != 1 {
		t.Fatalf("resume = %+v, err=%v", resume, err)
	}
	if _, err := os.Stat(filepath.Join(store.attachmentRoot, ".partial", upload.UploadID, "0.bin")); err != nil {
		t.Fatalf("stored chunk file: %v", err)
	}
}

func TestAttachmentChunkRejectsInvalidBoundsAndHash(t *testing.T) {
	store, _ := newTestAuthStore(t)
	upload, err := store.InitializeAttachment(AttachmentInitRequest{
		Room: "lobby", AuthorCode: "alice01", LogicalSize: attachmentChunkSize,
	})
	if err != nil {
		t.Fatal(err)
	}
	ciphertext := []byte("ciphertext")
	base := AttachmentChunkRequest{UploadID: upload.UploadID, AuthorCode: "alice01", Index: 0,
		Ciphertext: ciphertext, CipherSHA256: chunkHash(ciphertext)}
	tooLarge := base
	tooLarge.Ciphertext = bytes.Repeat([]byte{1}, int(maxAttachmentCipherChunkBytes+1))
	tooLarge.CipherSHA256 = chunkHash(tooLarge.Ciphertext)
	if _, err := store.StoreAttachmentChunk(tooLarge); !errors.Is(err, ErrAttachmentChunkTooLarge) {
		t.Fatalf("oversized chunk error = %v", err)
	}
	badHash := base
	badHash.CipherSHA256 = strings.Repeat("0", sha256HexSize)
	if _, err := store.StoreAttachmentChunk(badHash); !errors.Is(err, ErrAttachmentChunkHashMismatch) {
		t.Fatalf("bad hash error = %v", err)
	}
	outOfRange := base
	outOfRange.Index = 1
	if _, err := store.StoreAttachmentChunk(outOfRange); !errors.Is(err, ErrAttachmentChunkOutOfRange) {
		t.Fatalf("out-of-range chunk error = %v", err)
	}
}

func TestAttachmentChunkDuplicateRequiresMatchingFileSize(t *testing.T) {
	store, _ := newTestAuthStore(t)
	upload, err := store.InitializeAttachment(AttachmentInitRequest{
		Room: "lobby", AuthorCode: "alice01", LogicalSize: attachmentChunkSize,
	})
	if err != nil {
		t.Fatal(err)
	}

	ciphertext := []byte("ciphertext")
	request := AttachmentChunkRequest{
		UploadID: upload.UploadID, AuthorCode: "alice01", Index: 0,
		Ciphertext: ciphertext, CipherSHA256: chunkHash(ciphertext),
	}
	if duplicate, err := store.StoreAttachmentChunk(request); err != nil || duplicate {
		t.Fatalf("store chunk: duplicate=%t err=%v", duplicate, err)
	}
	path := store.chunkPath(upload.UploadID, 0)
	if err := os.WriteFile(path, []byte("truncated"), 0o600); err != nil {
		t.Fatal(err)
	}
	if duplicate, err := store.StoreAttachmentChunk(request); err == nil || duplicate {
		t.Fatalf("truncated duplicate = %t, err=%v; expected integrity failure", duplicate, err)
	}
}

func TestAttachmentResumeRepairsInvalidDiskMetadata(t *testing.T) {
	store, _ := newTestAuthStore(t)
	upload, err := store.InitializeAttachment(AttachmentInitRequest{Room: "lobby", AuthorCode: "alice01", LogicalSize: attachmentChunkSize})
	if err != nil {
		t.Fatal(err)
	}
	ciphertext := []byte("durable chunk")
	if _, err := store.StoreAttachmentChunk(AttachmentChunkRequest{UploadID: upload.UploadID, AuthorCode: "alice01", Index: 0, Ciphertext: ciphertext, CipherSHA256: chunkHash(ciphertext)}); err != nil {
		t.Fatal(err)
	}
	path := store.chunkPath(upload.UploadID, 0)
	if err := os.WriteFile(path, []byte("corrupt"), 0o600); err != nil {
		t.Fatal(err)
	}
	resumed, err := store.ResumeAttachmentUpload(upload.UploadID, "alice01")
	if err != nil {
		t.Fatal(err)
	}
	if len(resumed.ReceivedIndexes) != 0 {
		t.Fatalf("corrupt chunk reported as received: %+v", resumed.ReceivedIndexes)
	}
	if _, err := os.Stat(path); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("invalid chunk file was not removed: %v", err)
	}
}

func TestAttachmentChunkRecoversMatchingOrphanFile(t *testing.T) {
	store, _ := newTestAuthStore(t)
	upload, err := store.InitializeAttachment(AttachmentInitRequest{Room: "lobby", AuthorCode: "alice01", LogicalSize: attachmentChunkSize})
	if err != nil {
		t.Fatal(err)
	}
	ciphertext := []byte("orphan chunk")
	path := store.chunkPath(upload.UploadID, 0)
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, ciphertext, 0o600); err != nil {
		t.Fatal(err)
	}
	duplicate, err := store.StoreAttachmentChunk(AttachmentChunkRequest{UploadID: upload.UploadID, AuthorCode: "alice01", Index: 0, Ciphertext: ciphertext, CipherSHA256: chunkHash(ciphertext)})
	if err != nil || !duplicate {
		t.Fatalf("orphan recovery = duplicate:%t err:%v", duplicate, err)
	}
	resume, err := store.ResumeAttachmentUpload(upload.UploadID, "alice01")
	if err != nil || len(resume.ReceivedIndexes) != 1 || resume.ReceivedIndexes[0] != 0 {
		t.Fatalf("recovered resume = %+v err=%v", resume, err)
	}
}

func TestAttachmentChunkRejectsConflictingOrphanFile(t *testing.T) {
	store, _ := newTestAuthStore(t)
	upload, err := store.InitializeAttachment(AttachmentInitRequest{Room: "lobby", AuthorCode: "alice01", LogicalSize: attachmentChunkSize})
	if err != nil {
		t.Fatal(err)
	}
	path := store.chunkPath(upload.UploadID, 0)
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte("old bytes"), 0o600); err != nil {
		t.Fatal(err)
	}
	newBytes := []byte("new bytes")
	if _, err := store.StoreAttachmentChunk(AttachmentChunkRequest{UploadID: upload.UploadID, AuthorCode: "alice01", Index: 0, Ciphertext: newBytes, CipherSHA256: chunkHash(newBytes)}); !errors.Is(err, ErrAttachmentChunkConflict) {
		t.Fatalf("conflicting orphan error = %v", err)
	}
	stored, err := os.ReadFile(path)
	if err != nil || string(stored) != "old bytes" {
		t.Fatalf("conflicting orphan was overwritten: %q err=%v", stored, err)
	}
}

func TestAttachmentResumeRemovesMissingAndSameSizeCorruptFiles(t *testing.T) {
	store, _ := newTestAuthStore(t)
	upload, err := store.InitializeAttachment(AttachmentInitRequest{Room: "lobby", AuthorCode: "alice01", LogicalSize: attachmentChunkSize * 2})
	if err != nil {
		t.Fatal(err)
	}
	for index, bytes := range map[int64][]byte{0: []byte("missing"), 1: []byte("same-size")} {
		if _, err := store.StoreAttachmentChunk(AttachmentChunkRequest{UploadID: upload.UploadID, AuthorCode: "alice01", Index: index, Ciphertext: bytes, CipherSHA256: chunkHash(bytes)}); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.Remove(store.chunkPath(upload.UploadID, 0)); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(store.chunkPath(upload.UploadID, 1), []byte("same-hash"), 0o600); err != nil {
		t.Fatal(err)
	}
	resume, err := store.ResumeAttachmentUpload(upload.UploadID, "alice01")
	if err != nil || len(resume.ReceivedIndexes) != 0 {
		t.Fatalf("invalid files reported as received: %+v err=%v", resume.ReceivedIndexes, err)
	}
}
