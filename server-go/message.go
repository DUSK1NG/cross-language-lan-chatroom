package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"strings"
	"unicode/utf8"
)

const maxUsernameSize = 32
const maxRoomNameSize = 32
const sha256HexSize = 64

// OnlineUser 是 users_response 中的结构化在线成员信息。
// Users 字段仍保留，用于与旧客户端兼容。
type OnlineUser struct {
	Username string `json:"username"`
	UserCode string `json:"user_code"`
	Room     string `json:"room"`
	IsAdmin  bool   `json:"is_admin"`
}

// RoomInfo 是 rooms_response 中的结构化频道信息。
// CanManage 由服务端基于当前连接身份计算，客户端不能自行声明。
type RoomInfo struct {
	Name      string `json:"name"`
	OwnerCode string `json:"owner_code,omitempty"`
	Private   bool   `json:"private"`
	CanManage bool   `json:"can_manage"`
}

type Message struct {
	Type            string       `json:"type"`
	MessageID       string       `json:"message_id,omitempty"`
	CommandID       string       `json:"command_id,omitempty"`
	Username        string       `json:"username,omitempty"`
	UserCode        string       `json:"user_code,omitempty"`
	TargetUserCode  string       `json:"target_user_code,omitempty"`
	Room            string       `json:"room,omitempty"`
	Private         bool         `json:"private,omitempty"`
	Users           []string     `json:"users,omitempty"`
	Rooms           []string     `json:"rooms,omitempty"`
	UserDetails     []OnlineUser `json:"user_details,omitempty"`
	RoomDetails     []RoomInfo   `json:"room_details,omitempty"`
	Content         string       `json:"content,omitempty"`
	DeliveryState   string       `json:"delivery_state,omitempty"`
	CreatedAt       string       `json:"created_at,omitempty"`
	BeforeMessageID string       `json:"before_message_id,omitempty"`
	SearchQuery     string       `json:"search_query,omitempty"`
	Limit           int          `json:"limit,omitempty"`
	HasMore         bool         `json:"has_more,omitempty"`
	Recalled        bool         `json:"recalled,omitempty"`
	Messages        []Message    `json:"messages,omitempty"`
	// Optional negotiation metadata is ignored by legacy peers. Crypto is kept
	// as raw JSON so the server can route/store an envelope without decrypting it.
	ProtocolVersion string          `json:"protocol_version,omitempty"`
	Capabilities    []string        `json:"capabilities,omitempty"`
	Crypto          json.RawMessage `json:"crypto,omitempty"`
	// MLS control payloads remain opaque to the server. They are base64 text on
	// the wire so legacy JSON framing and the 64 KiB payload limit still apply.
	GroupID       string `json:"group_id,omitempty"`
	Epoch         uint64 `json:"epoch,omitempty"`
	ProposalID    string `json:"proposal_id,omitempty"`
	Action        string `json:"action,omitempty"`
	KeyPackage    string `json:"key_package,omitempty"`
	Proposal      string `json:"proposal,omitempty"`
	Commit        string `json:"commit,omitempty"`
	Welcome       string `json:"welcome,omitempty"`
	WelcomeDigest string `json:"welcome_digest,omitempty"`
	// Password is retained only so old database/test fixtures still compile;
	// password authentication is removed and this field never crosses the wire.
	Password string `json:"-"`
	IsAdmin  bool   `json:"is_admin,omitempty"`
}

func validateOpaqueCrypto(value json.RawMessage) error {
	if len(value) == 0 {
		return fmt.Errorf("crypto envelope must not be empty")
	}
	if len(value) > maxMessageSize {
		return fmt.Errorf("crypto envelope is too large")
	}
	var envelope map[string]json.RawMessage
	if err := json.Unmarshal(value, &envelope); err != nil || len(envelope) == 0 {
		return fmt.Errorf("crypto envelope must be a JSON object")
	}
	return nil
}

func validateOpaqueMLS(label, value string) error {
	if value == "" {
		return fmt.Errorf("%s must not be empty", label)
	}
	if len(value) > maxMessageSize {
		return fmt.Errorf("%s is too large", label)
	}
	if _, err := base64.StdEncoding.DecodeString(value); err != nil {
		return fmt.Errorf("%s must be base64: %w", label, err)
	}
	return nil
}

func validateMLSGroupID(groupID string) error {
	if groupID == "" {
		return fmt.Errorf("MLS group id must not be empty")
	}
	if !utf8.ValidString(groupID) || len([]byte(groupID)) > maxMessageSize {
		return fmt.Errorf("MLS group id must be valid UTF-8 and no longer than %d bytes", maxMessageSize)
	}
	return nil
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
		return fmt.Errorf("password registration has been removed")
	case "login_auth":
		return fmt.Errorf("password login has been removed")
	case "chat":
		if len(message.Crypto) > 0 {
			return validateOpaqueCrypto(message.Crypto)
		}
		return validateTextContent("chat", message.Content)
	case "private_chat":
		if _, err := normalizeUserCode(message.TargetUserCode); err != nil {
			return fmt.Errorf("invalid target user code: %w", err)
		}
		if len(message.Crypto) > 0 {
			return validateOpaqueCrypto(message.Crypto)
		}
		return validateTextContent("private chat", message.Content)
	case "history_request":
		if message.Private {
			if _, err := normalizeUserCode(message.TargetUserCode); err != nil {
				return fmt.Errorf("invalid history target user code: %w", err)
			}
		} else if err := validateRoomName(message.Room); err != nil {
			return err
		}
		if message.Limit < 0 || message.Limit > maxHistoryPageSize {
			return fmt.Errorf("history page size must be between 0 and %d", maxHistoryPageSize)
		}
		if !utf8.ValidString(message.SearchQuery) || len([]byte(message.SearchQuery)) > maxMessageSize {
			return fmt.Errorf("history search query must be valid UTF-8 and no longer than %d bytes", maxMessageSize)
		}
	case "history_response":
		if message.Messages == nil {
			return fmt.Errorf("history messages must be a JSON array")
		}
		for _, historyMessage := range message.Messages {
			if historyMessage.MessageID == "" || historyMessage.Username == "" || historyMessage.UserCode == "" {
				return fmt.Errorf("history message identity is required")
			}
		}
	case "room_join":
		return validateRoomName(message.Room)
	case "room_create":
		return validateRoomName(message.Room)
	case "room_action":
		if err := validateRoomName(message.Room); err != nil {
			return err
		}
		if message.Content != "invite" && message.Content != "remove_member" && message.Content != "delete" {
			return fmt.Errorf("unsupported room action")
		}
		if message.Content == "delete" {
			return nil
		}
		if _, err := normalizeUserCode(message.TargetUserCode); err != nil {
			return fmt.Errorf("invalid room member: %w", err)
		}
		return nil
	case "room_leave", "rooms_request":
		return nil
	case "mls.key_package.publish":
		return validateOpaqueMLS("key package", message.KeyPackage)
	case "mls.key_package.fetch":
		if _, err := normalizeUserCode(message.TargetUserCode); err != nil {
			return fmt.Errorf("invalid key package target user code: %w", err)
		}
		if message.Room != "" {
			return validateRoomName(message.Room)
		}
		return nil
	case "mls.group.commit":
		if err := validateMLSGroupID(message.GroupID); err != nil {
			return err
		}
		if message.Room != "" {
			if err := validateRoomName(message.Room); err != nil {
				return err
			}
		}
		if message.ProposalID != "" && (!utf8.ValidString(message.ProposalID) || len([]byte(message.ProposalID)) > maxMessageSize) {
			return fmt.Errorf("MLS group proposal id must be valid UTF-8 and no longer than %d bytes", maxMessageSize)
		}
		return validateOpaqueMLS("MLS group commit", message.Commit)
	case "mls.group.proposal":
		if err := validateMLSGroupID(message.GroupID); err != nil {
			return err
		}
		if message.ProposalID == "" {
			return fmt.Errorf("MLS group proposal id must not be empty")
		}
		if !utf8.ValidString(message.ProposalID) || len([]byte(message.ProposalID)) > maxMessageSize {
			return fmt.Errorf("MLS group proposal id must be valid UTF-8 and no longer than %d bytes", maxMessageSize)
		}
		if message.Room != "" {
			if err := validateRoomName(message.Room); err != nil {
				return err
			}
		}
		if message.Action != "" && message.Action != "add" && message.Action != "remove" {
			return fmt.Errorf("unsupported MLS group proposal action")
		}
		if message.Action != "" {
			if _, err := normalizeUserCode(message.TargetUserCode); err != nil {
				return fmt.Errorf("invalid MLS group proposal target: %w", err)
			}
		}
		return validateOpaqueMLS("MLS group proposal", message.Proposal)
	case "mls.group.welcome":
		if err := validateMLSGroupID(message.GroupID); err != nil {
			return err
		}
		if _, err := normalizeUserCode(message.TargetUserCode); err != nil {
			return fmt.Errorf("invalid welcome target user code: %w", err)
		}
		if message.Room != "" {
			if err := validateRoomName(message.Room); err != nil {
				return err
			}
		}
		return validateOpaqueMLS("MLS group welcome", message.Welcome)
	case "mls.group.welcome.accept":
		if err := validateMLSGroupID(message.GroupID); err != nil {
			return err
		}
		if _, err := normalizeUserCode(message.TargetUserCode); err != nil {
			return fmt.Errorf("invalid welcome accept target user code: %w", err)
		}
		if message.ProposalID == "" || !utf8.ValidString(message.ProposalID) || len([]byte(message.ProposalID)) > maxMessageSize {
			return fmt.Errorf("MLS welcome accept proposal id is invalid")
		}
		if len(message.WelcomeDigest) != sha256HexSize {
			return fmt.Errorf("MLS welcome accept digest is invalid")
		}
		if message.Room != "" {
			if err := validateRoomName(message.Room); err != nil {
				return err
			}
		}
		return nil
	case "users_request", "quit":
		return nil
	case "admin_action":
		if message.Content == "recall" {
			if message.MessageID == "" {
				return fmt.Errorf("message id is required")
			}
			return nil
		}
		if message.Content == "approve_connection" || message.Content == "deny_connection" {
			if message.MessageID == "" {
				return fmt.Errorf("connection request id is required")
			}
			return nil
		}
		if _, err := normalizeUserCode(message.TargetUserCode); err != nil {
			return fmt.Errorf("invalid admin target: %w", err)
		}
		if message.Content != "kick" && message.Content != "mute" {
			return fmt.Errorf("unsupported admin action")
		}
		return nil
	case "message_recalled":
		if message.MessageID == "" {
			return fmt.Errorf("message id is required")
		}
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
	case "offline_message":
		if message.Username == "" || message.UserCode == "" {
			return fmt.Errorf("offline message sender is required")
		}
		return validateTextContent("offline message", message.Content)
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
