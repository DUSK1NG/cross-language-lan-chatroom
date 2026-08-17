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
		Content: "Hello, Go and C++",
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

	if _, err := receiveMessage(&stream); err == nil {
		t.Fatal("expected malformed JSON to be rejected")
	}
}

func TestReceiveMessageRejectsWrongFieldType(t *testing.T) {
	var stream bytes.Buffer
	if err := writeFrame(&stream, []byte(`{"type":"login","username":123}`)); err != nil {
		t.Fatal(err)
	}

	if _, err := receiveMessage(&stream); err == nil {
		t.Fatal("expected wrong JSON field type to be rejected")
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

	if _, err := receiveMessage(&stream); err == nil {
		t.Fatal("expected invalid UTF-8 payload to be rejected")
	}
}
