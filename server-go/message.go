package main

import (
	"encoding/json"
	"fmt"
	"io"
	"unicode/utf8"
)

const maxUsernameSize = 32

type Message struct {
	Type     string `json:"type"`
	Username string `json:"username,omitempty"`
	Content  string `json:"content,omitempty"`
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
	case "login_ok", "login_error", "error":
		if message.Content == "" {
			return fmt.Errorf("message content must not be empty")
		}
	default:
		return fmt.Errorf("unsupported message type: %s", message.Type)
	}

	return nil
}
