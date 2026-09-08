package main

import (
	"bytes"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"reflect"
	"strings"
	"testing"
)

func TestValidateMLSControlMessages(t *testing.T) {
	encoded := base64.StdEncoding.EncodeToString([]byte("opaque"))
	for _, message := range []Message{
		{Type: "mls.key_package.publish", KeyPackage: encoded},
		{Type: "mls.key_package.fetch", TargetUserCode: "Bob01", Room: "lobby"},
		{Type: "mls.group.commit", GroupID: "room-group", Room: "lobby", Commit: encoded},
		{Type: "mls.group.proposal", GroupID: "room-group", Room: "lobby", ProposalID: "p1", Epoch: 1, Proposal: encoded},
		{Type: "mls.group.welcome", GroupID: "room-group", Room: "lobby", TargetUserCode: "Bob01", Welcome: encoded},
	} {
		if err := validateMessage(message); err != nil {
			t.Fatalf("valid MLS message %+v rejected: %v", message, err)
		}
	}
}

func TestValidateMLSProposalRejectsMalformedOpaqueData(t *testing.T) {
	if err := validateMessage(Message{Type: "mls.group.proposal", GroupID: "g", Room: "lobby", ProposalID: "p1", Epoch: 1, Proposal: "not base64"}); err == nil {
		t.Fatal("malformed proposal was accepted")
	}
}

func TestValidateMLSControlRejectsMalformedOpaqueData(t *testing.T) {
	if err := validateMessage(Message{Type: "mls.key_package.publish", KeyPackage: "not-base64"}); err == nil {
		t.Fatal("malformed key package was accepted")
	}
	if err := validateMessage(Message{Type: "mls.group.commit", GroupID: "g", Commit: ""}); err == nil {
		t.Fatal("empty commit was accepted")
	}
}

func TestValidateUserCode(t *testing.T) {
	tests := []struct {
		name    string
		code    string
		wantErr bool
	}{
		{name: "too short", code: "A1", wantErr: true},
		{name: "valid", code: "Alex2026"},
		{name: "too long", code: strings.Repeat("a", 17), wantErr: true},
		{name: "special character", code: "Alex-01", wantErr: true},
		{name: "non ASCII", code: "小明01", wantErr: true},
		{name: "invalid utf8", code: string([]byte{0xff, 0xfe, 0xfd}), wantErr: true},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			err := validateUserCode(test.code)
			if (err != nil) != test.wantErr {
				t.Fatalf("validateUserCode(%q) error = %v, wantErr = %v", test.code, err, test.wantErr)
			}
		})
	}
}

func TestValidateMessageRejectsOversizedCommandID(t *testing.T) {
	message := Message{Type: "attachment.resume", UploadID: "upload-1", CommandID: strings.Repeat("x", maxCommandIDSize+1)}
	if err := validateMessage(message); err == nil {
		t.Fatal("oversized command id was accepted")
	}
}

func TestValidateAttachmentCommitRequiresUploadID(t *testing.T) {
	if err := validateMessage(Message{Type: "attachment.commit", UploadID: "upload-1"}); err != nil {
		t.Fatalf("valid attachment commit rejected: %v", err)
	}
	if err := validateMessage(Message{Type: "attachment.commit"}); err == nil {
		t.Fatal("attachment commit without upload id was accepted")
	}
}

func TestValidateAttachmentDownloadRequiresAttachmentIDAndIndex(t *testing.T) {
	if err := validateMessage(Message{Type: "attachment.download", AttachmentID: "attachment-1", ChunkIndex: 0}); err != nil {
		t.Fatalf("valid attachment download rejected: %v", err)
	}
	if err := validateMessage(Message{Type: "attachment.download", ChunkIndex: 0}); err == nil {
		t.Fatal("attachment download without attachment id was accepted")
	}
}

func TestReceiveMessageRejectsOversizedCommandIDAtWireBoundary(t *testing.T) {
	var stream bytes.Buffer
	payload, err := json.Marshal(Message{Type: "attachment.resume", UploadID: "upload-1", CommandID: strings.Repeat("x", maxCommandIDSize+1)})
	if err != nil {
		t.Fatal(err)
	}
	if err := writeFrame(&stream, payload); err != nil {
		t.Fatal(err)
	}
	if _, err := receiveMessage(&stream); err == nil {
		t.Fatal("oversized command id crossed the wire boundary")
	}
}

func TestMaximumAttachmentChunkFitsMessageFrame(t *testing.T) {
	ciphertext := base64.StdEncoding.EncodeToString(make([]byte, maxAttachmentCipherChunkBytes))
	message := Message{
		Type: "attachment.chunk", CommandID: strings.Repeat("x", maxCommandIDSize),
		UploadID: "00000000-0000-0000-0000-000000000000", ChunkIndex: 10485759,
		Ciphertext: ciphertext, CipherSHA256: strings.Repeat("0", sha256HexSize),
	}
	var stream bytes.Buffer
	if err := sendMessage(&stream, message); err != nil {
		t.Fatalf("maximum attachment chunk exceeded frame budget: %v", err)
	}
	if payload, err := readFrame(&stream); err != nil {
		t.Fatal(err)
	} else if len(payload) > maxMessageSize {
		t.Fatalf("frame payload = %d bytes, max %d", len(payload), maxMessageSize)
	}
}

func TestAttachmentInitFiveGiBBoundary(t *testing.T) {
	for _, size := range []int64{977743312, 5 * 1024 * 1024 * 1024} {
		if err := validateMessage(Message{Type: "attachment.init", Room: "lobby", LogicalSize: size}); err != nil {
			t.Fatalf("size %d rejected: %v", size, err)
		}
	}
	if err := validateMessage(Message{Type: "attachment.init", Room: "lobby", LogicalSize: 5*1024*1024*1024 + 1}); err == nil {
		t.Fatal("size above 5 GiB accepted")
	}
}

func TestAttachmentResumeFitsFrameAndPreservesIndexes(t *testing.T) {
	const chunks = (5*1024*1024*1024 + attachmentChunkSize - 1) / attachmentChunkSize
	for _, count := range []int64{0, 8192, 8193, chunks} {
		indexes := make([]int64, count)
		for i := range indexes {
			indexes[i] = int64(i)
		}
		// Include the highest possible chunk even in a partial upload.
		if count > 0 {
			indexes[count-1] = chunks - 1
		}
		response := attachmentResumeMessage("00000000-0000-0000-0000-000000000000", strings.Repeat("x", maxCommandIDSize), indexes)
		var stream bytes.Buffer
		if err := sendMessage(&stream, response); err != nil {
			t.Fatalf("resume with %d chunks exceeds frame: %v", count, err)
		}
		payload, err := readFrame(&stream)
		if err != nil {
			t.Fatal(err)
		}
		var wire Message
		if err := json.Unmarshal(payload, &wire); err != nil {
			t.Fatal(err)
		}
		if count <= 8192 {
			if wire.ReceivedBitmap != "" || len(wire.ReceivedIndexes) != len(indexes) {
				t.Fatal("small resume changed encoding")
			}
			continue
		}
		bitmap, err := hex.DecodeString(wire.ReceivedBitmap)
		if err != nil || len(wire.ReceivedIndexes) != 0 {
			t.Fatalf("invalid bitmap response: %v", err)
		}
		var decoded []int64
		for i, bits := range bitmap {
			for bit := 0; bit < 8; bit++ {
				if bits&(1<<bit) != 0 {
					decoded = append(decoded, int64(i*8+bit))
				}
			}
		}
		if !reflect.DeepEqual(decoded, indexes) {
			t.Fatalf("resume indexes changed for count %d", count)
		}
	}
}

func TestNormalizeUserCodeIsCaseInsensitive(t *testing.T) {
	got, err := normalizeUserCode("AlEx2026")
	if err != nil {
		t.Fatal(err)
	}
	if got != "alex2026" {
		t.Fatalf("normalized code = %q, want alex2026", got)
	}
}

func TestMessageRoundTrip(t *testing.T) {
	want := Message{
		Type:     "login",
		Username: "Alice",
	}
	var stream bytes.Buffer

	if err := sendMessage(&stream, want); err != nil {
		t.Fatal(err)
	}
	got, err := receiveMessage(&stream)
	if err != nil {
		t.Fatal(err)
	}

	if !reflect.DeepEqual(got, want) {
		t.Fatalf("message = %+v, want %+v", got, want)
	}
}

func TestEncryptedMessageRoundTripPreservesOpaqueEnvelope(t *testing.T) {
	want := Message{
		Type: "chat", UserCode: "A001", Room: "lobby", MessageID: "m1",
		ProtocolVersion: "2", Capabilities: []string{"e2ee-envelope-v1"},
		Crypto: json.RawMessage(`{"v":1,"alg":"xchacha20poly1305","ct":"opaque"}`),
	}
	var stream bytes.Buffer
	if err := sendMessage(&stream, want); err != nil {
		t.Fatal(err)
	}
	got, err := receiveMessage(&stream)
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("message = %+v, want %+v", got, want)
	}
	if err := validateMessage(got); err != nil {
		t.Fatalf("opaque encrypted message rejected: %v", err)
	}
}

func TestUsersMessageRoundTrip(t *testing.T) {
	want := Message{
		Type:  "users_response",
		Users: []string{"Alex#A001", "Alex#B002"},
	}
	var stream bytes.Buffer

	if err := sendMessage(&stream, want); err != nil {
		t.Fatal(err)
	}
	got, err := receiveMessage(&stream)
	if err != nil {
		t.Fatal(err)
	}

	if !reflect.DeepEqual(got, want) {
		t.Fatalf("message = %+v, want %+v", got, want)
	}
}

func TestChineseChatMessageRoundTrip(t *testing.T) {
	want := Message{
		Type:    "chat",
		Content: "你好，这是 Go 和 C++ 跨语言聊天室。",
	}
	var stream bytes.Buffer

	if err := sendMessage(&stream, want); err != nil {
		t.Fatal(err)
	}
	got, err := receiveMessage(&stream)
	if err != nil {
		t.Fatal(err)
	}

	if !reflect.DeepEqual(got, want) {
		t.Fatalf("message = %+v, want %+v", got, want)
	}
}

func TestPrivateChatMessageRoundTrip(t *testing.T) {
	want := Message{
		Type:           "private_chat",
		Username:       "Alice",
		UserCode:       "A001",
		TargetUserCode: "bOb01",
		Content:        "你好，这是私聊消息。",
	}
	var stream bytes.Buffer

	if err := sendMessage(&stream, want); err != nil {
		t.Fatal(err)
	}
	got, err := receiveMessage(&stream)
	if err != nil {
		t.Fatal(err)
	}

	if !reflect.DeepEqual(got, want) {
		t.Fatalf("message = %+v, want %+v", got, want)
	}
	if err := validateMessage(got); err != nil {
		t.Fatalf("private chat message should validate: %v", err)
	}
}

func TestMessageJSONContainsExpectedFields(t *testing.T) {
	t.Run("login", func(t *testing.T) {
		var stream bytes.Buffer
		if err := sendMessage(&stream, Message{
			Type:     "login",
			Username: "Alice",
			UserCode: "A001",
		}); err != nil {
			t.Fatal(err)
		}

		payload, err := readFrame(&stream)
		if err != nil {
			t.Fatal(err)
		}

		var fields map[string]json.RawMessage
		if err := json.Unmarshal(payload, &fields); err != nil {
			t.Fatal(err)
		}
		for _, field := range []string{"type", "username", "user_code"} {
			if _, ok := fields[field]; !ok {
				t.Fatalf("JSON field %q is missing from %s", field, payload)
			}
		}
		if _, ok := fields["content"]; ok {
			t.Fatalf("empty optional content should be omitted: %s", payload)
		}
	})

	t.Run("users_response", func(t *testing.T) {
		var stream bytes.Buffer
		if err := sendMessage(&stream, Message{
			Type:  "users_response",
			Users: []string{"Alex#A001", "Alex#B002"},
		}); err != nil {
			t.Fatal(err)
		}

		payload, err := readFrame(&stream)
		if err != nil {
			t.Fatal(err)
		}

		var fields map[string]json.RawMessage
		if err := json.Unmarshal(payload, &fields); err != nil {
			t.Fatal(err)
		}
		usersJSON, ok := fields["users"]
		if !ok {
			t.Fatalf("JSON field %q is missing from %s", "users", payload)
		}

		var users []string
		if err := json.Unmarshal(usersJSON, &users); err != nil {
			t.Fatalf("users field is not a JSON array: %v", err)
		}
		if !reflect.DeepEqual(users, []string{"Alex#A001", "Alex#B002"}) {
			t.Fatalf("users array = %+v, want %+v", users, []string{"Alex#A001", "Alex#B002"})
		}
		if _, ok := fields["content"]; ok {
			t.Fatalf("empty optional content should be omitted: %s", payload)
		}
	})

	t.Run("private_chat", func(t *testing.T) {
		var stream bytes.Buffer
		if err := sendMessage(&stream, Message{
			Type:           "private_chat",
			TargetUserCode: "BOB01",
			Content:        "你好",
		}); err != nil {
			t.Fatal(err)
		}

		payload, err := readFrame(&stream)
		if err != nil {
			t.Fatal(err)
		}

		var fields map[string]json.RawMessage
		if err := json.Unmarshal(payload, &fields); err != nil {
			t.Fatal(err)
		}
		for _, field := range []string{"type", "target_user_code", "content"} {
			if _, ok := fields[field]; !ok {
				t.Fatalf("JSON field %q is missing from %s", field, payload)
			}
		}
	})
}

func TestValidateMessage(t *testing.T) {
	tests := []struct {
		name    string
		message Message
		wantErr bool
	}{
		{
			name:    "empty type",
			message: Message{Username: "Alice"},
			wantErr: true,
		},
		{
			name:    "empty login username",
			message: Message{Type: "login"},
			wantErr: true,
		},
		{
			name:    "empty login user code",
			message: Message{Type: "login", Username: "Alice"},
			wantErr: true,
		},
		{
			name:    "invalid login user code",
			message: Message{Type: "login", Username: "Alice", UserCode: "A-01"},
			wantErr: true,
		},
		{
			name:    "invalid login username utf8",
			message: Message{Type: "login", Username: string([]byte{0xff}), UserCode: "A001"},
			wantErr: true,
		},
		{
			name:    "long login username",
			message: Message{Type: "login", Username: strings.Repeat("a", 33), UserCode: "A001"},
			wantErr: true,
		},
		{
			name:    "empty chat content",
			message: Message{Type: "chat"},
			wantErr: true,
		},
		{
			name:    "valid users request",
			message: Message{Type: "users_request"},
		},
		{
			name:    "valid quit",
			message: Message{Type: "quit"},
		},
		{
			name:    "valid users response",
			message: Message{Type: "users_response", Users: []string{}},
		},
		{
			name:    "valid system",
			message: Message{Type: "system", Content: "Maintenance"},
		},
		{
			name:    "valid login",
			message: Message{Type: "login", Username: "Alice", UserCode: "A001"},
		},
		{
			name:    "valid chat",
			message: Message{Type: "chat", Content: "Hello"},
		},
		{
			name:    "valid private chat",
			message: Message{Type: "private_chat", TargetUserCode: "BoB01", Content: "Hello"},
		},
		{
			name:    "valid room join",
			message: Message{Type: "room_join", Room: "room_2026"},
		},
		{
			name:    "room join with punctuation",
			message: Message{Type: "room_join", Room: "room-2026"},
			wantErr: true,
		},
		{
			name:    "room join empty",
			message: Message{Type: "room_join"},
			wantErr: true,
		},
		{
			name:    "private chat missing target code",
			message: Message{Type: "private_chat", Content: "Hello"},
			wantErr: true,
		},
		{
			name:    "private chat invalid target code",
			message: Message{Type: "private_chat", TargetUserCode: "Bob-01", Content: "Hello"},
			wantErr: true,
		},
		{
			name:    "private chat empty content",
			message: Message{Type: "private_chat", TargetUserCode: "BOB01"},
			wantErr: true,
		},
		{
			name:    "private chat invalid utf8 content",
			message: Message{Type: "private_chat", TargetUserCode: "BOB01", Content: string([]byte{0xff})},
			wantErr: true,
		},
		{
			name:    "private chat oversized content",
			message: Message{Type: "private_chat", TargetUserCode: "BOB01", Content: strings.Repeat("a", maxMessageSize+1)},
			wantErr: true,
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			err := validateMessage(test.message)
			if (err != nil) != test.wantErr {
				t.Fatalf("validateMessage(%+v) error = %v, wantErr = %v", test.message, err, test.wantErr)
			}
		})
	}
}

func TestValidateCommandMessages(t *testing.T) {
	tests := []struct {
		name    string
		message Message
	}{
		{
			name:    "users request",
			message: Message{Type: "users_request"},
		},
		{
			name:    "quit",
			message: Message{Type: "quit"},
		},
		{
			name:    "empty users response",
			message: Message{Type: "users_response", Users: []string{}},
		},
		{
			name:    "room join",
			message: Message{Type: "room_join", Room: "study_1"},
		},
		{
			name:    "room leave",
			message: Message{Type: "room_leave"},
		},
		{
			name:    "rooms request",
			message: Message{Type: "rooms_request"},
		},
		{
			name:    "rooms response",
			message: Message{Type: "rooms_response", Rooms: []string{"lobby", "study_1"}},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if err := validateMessage(test.message); err != nil {
				t.Fatalf("validateMessage(%+v) returned error: %v", test.message, err)
			}
		})
	}
}

func TestReceiveMessageRejectsMalformedJSON(t *testing.T) {
	var stream bytes.Buffer
	if err := writeFrame(&stream, []byte(`{"type":`)); err != nil {
		t.Fatal(err)
	}

	message, err := receiveMessage(&stream)
	if err == nil {
		t.Fatal("expected malformed JSON to be rejected")
	}
	if !reflect.DeepEqual(message, Message{}) {
		t.Fatalf("malformed JSON returned partial message: %+v", message)
	}
}

func TestReceiveMessageRejectsWrongFieldType(t *testing.T) {
	var stream bytes.Buffer
	if err := writeFrame(&stream, []byte(`{"type":"login","username":123}`)); err != nil {
		t.Fatal(err)
	}

	message, err := receiveMessage(&stream)
	if err == nil {
		t.Fatal("expected wrong JSON field type to be rejected")
	}
	if !reflect.DeepEqual(message, Message{}) {
		t.Fatalf("wrong field type returned partial message: %+v", message)
	}
}

func TestReceiveMessageRejectsPrivateChatWrongTargetType(t *testing.T) {
	var stream bytes.Buffer
	if err := writeFrame(&stream, []byte(`{"type":"private_chat","target_user_code":123,"content":"hello"}`)); err != nil {
		t.Fatal(err)
	}

	message, err := receiveMessage(&stream)
	if err == nil {
		t.Fatal("expected private_chat target_user_code with wrong type to be rejected")
	}
	if !reflect.DeepEqual(message, Message{}) {
		t.Fatalf("wrong target_user_code type returned partial message: %+v", message)
	}
}

func TestReceiveMessageRejectsInvalidUTF8Payload(t *testing.T) {
	var stream bytes.Buffer
	payload := []byte(`{"type":"users_response","users":["`)
	payload = append(payload, 0xff)
	payload = append(payload, []byte(`"]}`)...)
	if err := writeFrame(&stream, payload); err != nil {
		t.Fatal(err)
	}

	message, err := receiveMessage(&stream)
	if err == nil {
		t.Fatal("expected invalid UTF-8 payload to be rejected")
	}
	if !reflect.DeepEqual(message, Message{}) {
		t.Fatalf("invalid UTF-8 returned partial message: %+v", message)
	}
}

func TestReceiveMessageRejectsMissingType(t *testing.T) {
	var stream bytes.Buffer
	if err := writeFrame(&stream, []byte(`{"content":"hello"}`)); err != nil {
		t.Fatal(err)
	}

	message, err := receiveMessage(&stream)
	if err == nil {
		t.Fatal("expected missing type to be rejected")
	}
	if !reflect.DeepEqual(message, Message{}) {
		t.Fatalf("missing type returned partial message: %+v", message)
	}
}

func TestReceiveMessageRejectsUsersNonArray(t *testing.T) {
	var stream bytes.Buffer
	if err := writeFrame(&stream, []byte(`{"type":"users_response","users":"Alice#A001"}`)); err != nil {
		t.Fatal(err)
	}

	message, err := receiveMessage(&stream)
	if err == nil {
		t.Fatal("expected users field with non-array type to be rejected")
	}
	if !reflect.DeepEqual(message, Message{}) {
		t.Fatalf("non-array users field returned partial message: %+v", message)
	}
}

func TestValidateMessageRejectsUsersNull(t *testing.T) {
	var stream bytes.Buffer
	if err := writeFrame(&stream, []byte(`{"type":"users_response","users":null}`)); err != nil {
		t.Fatal(err)
	}

	message, err := receiveMessage(&stream)
	if err != nil {
		t.Fatal(err)
	}
	if err := validateMessage(message); err == nil {
		t.Fatal("expected users:null to be rejected")
	}
}

func TestReceiveMessageRejectsUsersNonStringElement(t *testing.T) {
	var stream bytes.Buffer
	if err := writeFrame(&stream, []byte(`{"type":"users_response","users":["Alice#A001",123]}`)); err != nil {
		t.Fatal(err)
	}

	message, err := receiveMessage(&stream)
	if err == nil {
		t.Fatal("expected users array with non-string element to be rejected")
	}
	if !reflect.DeepEqual(message, Message{}) {
		t.Fatalf("non-string users element returned partial message: %+v", message)
	}
}

func TestValidateMessageRejectsUnsupportedType(t *testing.T) {
	message := Message{Type: "unsupported"}

	if err := validateMessage(message); err == nil {
		t.Fatal("expected unsupported message type to be rejected")
	}
}

func TestMessageRoundTripsStructuredUserAndRoomDetails(t *testing.T) {
	input := Message{
		Type: "users_response",
		UserDetails: []OnlineUser{{
			Username: "Alice",
			UserCode: "A001",
			Room:     "lobby",
			IsAdmin:  true,
		}},
		RoomDetails: []RoomInfo{{
			Name:      "study_group",
			OwnerCode: "A001",
			Private:   true,
			CanManage: true,
		}},
	}

	var buffer bytes.Buffer
	if err := sendMessage(&buffer, input); err != nil {
		t.Fatalf("sendMessage: %v", err)
	}

	got, err := receiveMessage(&buffer)
	if err != nil {
		t.Fatalf("receiveMessage: %v", err)
	}
	if !reflect.DeepEqual(got.UserDetails, input.UserDetails) {
		t.Fatalf("user details = %#v, want %#v", got.UserDetails, input.UserDetails)
	}
	if !reflect.DeepEqual(got.RoomDetails, input.RoomDetails) {
		t.Fatalf("room details = %#v, want %#v", got.RoomDetails, input.RoomDetails)
	}
}
