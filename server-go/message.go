package main

import (
	"encoding/json"
	"fmt"
	"io"
	"strings"
	"unicode/utf8"
)

const maxUsernameSize = 32

type Message struct {
	Type     string   `json:"type"`
	Username string   `json:"username,omitempty"`
	UserCode string   `json:"user_code,omitempty"`
	Users    []string `json:"users,omitempty"`
	Content  string   `json:"content,omitempty"`
}

func validateUserCode(code string) error {
	if code == "" {
		return fmt.Errorf("user code must not be empty")
	}
	if !utf8.ValidString(code) {
		return fmt.Errorf("user code must be valid UTF-8")
	}
	if len(code) < 3 || len(code) > 16 {
		return fmt.Errorf("user code must be 3 to 16 bytes")
	}
	for _, r := range code {
		if !((r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9')) {
			return fmt.Errorf("user code must contain only ASCII letters and digits")
		}
	}
	return nil
}

func normalizeUserCode(code string) (string, error) {
	if err := validateUserCode(code); err != nil {
		return "", err
	}
	return strings.ToLower(code), nil
}

func sendMessage(writer io.Writer, message Message) error {
	payload, err := json.Marshal(message)
	if err != nil {
		return fmt.Errorf("marshal message: %w", err)
	}
	if err := writeFrame(writer, payload); err != nil {
		return fmt.Errorf("write message frame: %w", err)
	}
	return nil
}

func receiveMessage(reader io.Reader) (Message, error) {
	payload, err := readFrame(reader)
	if err != nil {
		return Message{}, fmt.Errorf("read message frame: %w", err)
	}
	if !utf8.Valid(payload) {
		return Message{}, fmt.Errorf("message payload must be valid UTF-8")
	}

	var message Message
	if err := json.Unmarshal(payload, &message); err != nil {
		return Message{}, fmt.Errorf("unmarshal message: %w", err)
	}
	if message.Type == "" {
		return Message{}, fmt.Errorf("message type is required")
	}
	return message, nil
}

func validateMessage(message Message) error {
	if message.Type == "" {
		return fmt.Errorf("message type is required")
	}

	switch message.Type {
	case "login":
		if message.Username == "" {
			return fmt.Errorf("username must not be empty")
		}
		if !utf8.ValidString(message.Username) {
			return fmt.Errorf("username must be valid UTF-8")
		}
		if len([]byte(message.Username)) > maxUsernameSize {
			return fmt.Errorf("username is too long")
		}
		if message.UserCode == "" {
			return fmt.Errorf("user code must not be empty")
		}
		if _, err := normalizeUserCode(message.UserCode); err != nil {
			return err
		}
	case "chat":
		if message.Content == "" {
			return fmt.Errorf("chat content must not be empty")
		}
		if !utf8.ValidString(message.Content) {
			return fmt.Errorf("chat content must be valid UTF-8")
		}
		if len([]byte(message.Content)) > maxMessageSize {
			return fmt.Errorf("chat content is too long")
		}
	case "users_request", "quit":
		return nil
	case "users_response":
		if message.Users == nil {
			return fmt.Errorf("users list must be a JSON array")
		}
		for _, user := range message.Users {
			if !utf8.ValidString(user) {
				return fmt.Errorf("users list must contain valid UTF-8 strings")
			}
		}
	case "login_ok", "login_error", "system", "error":
		if message.Content == "" {
			return fmt.Errorf("message content must not be empty")
		}
	default:
		return fmt.Errorf("unsupported message type: %s", message.Type)
	}

	return nil
}
