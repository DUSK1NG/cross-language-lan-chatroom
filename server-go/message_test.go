package main

import (
	"bytes"
	"encoding/json"
	"reflect"
	"strings"
	"testing"
)

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
