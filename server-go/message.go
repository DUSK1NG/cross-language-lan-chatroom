package main

import (
	"encoding/json"
	"fmt"
	"io"
	"strings"
	"unicode/utf8"
)

const maxUsernameSize = 32
const maxRoomNameSize = 32

type Message struct {
	Type           string   `json:"type"`
	Username       string   `json:"username,omitempty"`
	UserCode       string   `json:"user_code,omitempty"`
	TargetUserCode string   `json:"target_user_code,omitempty"`
	Room           string   `json:"room,omitempty"`
	Users          []string `json:"users,omitempty"`
	Rooms          []string `json:"rooms,omitempty"`
	Content        string   `json:"content,omitempty"`
	Password       string   `json:"password,omitempty"`
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
	case "register":
		if err := validateLoginIdentity(message); err != nil {
			return err
		}
		return validatePassword(message.Password)
	case "login_auth":
		if message.Username == "" || !utf8.ValidString(message.Username) || len([]byte(message.Username)) > maxUsernameSize {
			return fmt.Errorf("invalid username")
		}
		return validatePassword(message.Password)
	case "chat":
		return validateTextContent("chat", message.Content)
	case "private_chat":
		if _, err := normalizeUserCode(message.TargetUserCode); err != nil {
			return fmt.Errorf("invalid target user code: %w", err)
		}
		return validateTextContent("private chat", message.Content)
	case "room_join":
		return validateRoomName(message.Room)
	case "room_leave", "rooms_request":
		return nil
	case "users_request", "quit":
		return nil
	case "users_response":
		if message.Users == nil {
			return fmt.Errorf("users list must be a JSON array")
		}
	case "rooms_response":
		if message.Rooms == nil {
			return fmt.Errorf("rooms list must be a JSON array")
		}
		for _, room := range message.Rooms {
			if err := validateRoomName(room); err != nil {
				return fmt.Errorf("invalid room in rooms list: %w", err)
			}
		}
		for _, user := range message.Users {
			if !utf8.ValidString(user) {
				return fmt.Errorf("users list must contain valid UTF-8 strings")
			}
		}
	case "register_ok", "register_error", "login_ok", "login_error", "system", "error":
		if message.Content == "" {
			return fmt.Errorf("message content must not be empty")
		}
	default:
		return fmt.Errorf("unsupported message type: %s", message.Type)
	}

	return nil
}

func validateLoginIdentity(message Message) error {
	if message.Username == "" || !utf8.ValidString(message.Username) || len([]byte(message.Username)) > maxUsernameSize {
		return fmt.Errorf("invalid username")
	}
	if _, err := normalizeUserCode(message.UserCode); err != nil {
		return err
	}
	return nil
}

func validateRoomName(room string) error {
	if room == "" {
		return fmt.Errorf("room name must not be empty")
	}
	if len(room) > maxRoomNameSize {
		return fmt.Errorf("room name is too long")
	}
	for _, r := range room {
		if !((r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') ||
			(r >= '0' && r <= '9') || r == '_') {
			return fmt.Errorf("room name must contain only ASCII letters, digits, or underscore")
		}
	}
	return nil
}

func validateTextContent(messageKind, content string) error {
	if content == "" {
		return fmt.Errorf("%s content must not be empty", messageKind)
	}
	if !utf8.ValidString(content) {
		return fmt.Errorf("%s content must be valid UTF-8", messageKind)
	}
	if len([]byte(content)) > maxMessageSize {
		return fmt.Errorf("%s content is too long", messageKind)
	}
	return nil
}
