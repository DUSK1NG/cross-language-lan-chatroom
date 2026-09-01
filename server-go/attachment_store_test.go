package main

import (
	"bytes"
	"errors"
	"sync"
	"testing"
)

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
	const alreadyReserved = 39
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
