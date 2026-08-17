package main

import (
	"bytes"
	"encoding/json"
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

	if got != want {
		t.Fatalf("message = %+v, want %+v", got, want)
	}
}

func TestChineseChatMessageRoundTrip(t *testing.T) {
	want := Message{
		Type:    "chat",
		Content: "你好，这是 Go 和 C++。",
	}
	var stream bytes.Buffer

	if err := sendMessage(&stream, want); err != nil {
		t.Fatal(err)
	}
	got, err := receiveMessage(&stream)
	if err != nil {
		t.Fatal(err)
	}

	if got != want {
		t.Fatalf("message = %+v, want %+v", got, want)
	}
}

func TestMessageJSONContainsExpectedFields(t *testing.T) {
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
			name:    "empty chat content",
			message: Message{Type: "chat"},
			wantErr: true,
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
