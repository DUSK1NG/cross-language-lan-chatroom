package main

import "testing"

func drainMessages(messages <-chan Message) []Message {
	var drained []Message
	for {
		select {
		case message := <-messages:
			drained = append(drained, message)
		default:
			return drained
		}
	}
}

func TestHubDoesNotDeliverDuplicateClientMessageID(t *testing.T) {
	store, _ := newTestAuthStore(t)
	hub := NewHub()
	hub.OfflineStore = store

	sender := newTestClient(t, "Alice", "A001")
	sender.AccountBacked = true
	sender.Room = defaultRoomName
	recipient := newTestClient(t, "Bob", "B001")
	recipient.AccountBacked = true
	recipient.Room = defaultRoomName
	hub.Rooms[defaultRoomName] = make(map[*Client]bool)
	for _, client := range []*Client{sender, recipient} {
		hub.Clients[client] = true
		hub.ActiveCodes[client.NormalizedCode] = client
		hub.Rooms[defaultRoomName][client] = true
	}

	message := Message{
		Type:      "chat",
		MessageID: "client-msg-001",
		Username:  "Alice",
		UserCode:  "A001",
		Room:      defaultRoomName,
		Content:   "only once",
	}
	hub.broadcastMessage(message)
	first := <-recipient.Send
	if first.MessageID != message.MessageID || first.Content != message.Content {
		t.Fatalf("first delivery = %+v", first)
	}

	hub.broadcastMessage(message)
	select {
	case duplicate := <-recipient.Send:
		t.Fatalf("duplicate client message was delivered: %+v", duplicate)
	default:
	}

	page, err := store.LoadHistory(HistoryQuery{UserCode: "A001", Room: defaultRoomName, Limit: 10})
	if err != nil {
		t.Fatal(err)
	}
	if len(page.Messages) != 1 || page.Messages[0].MessageID != message.MessageID {
		t.Fatalf("stored history = %+v", page)
	}
}

func TestHubDuplicateClientMessageIDReturnsSameDeliveryReceipt(t *testing.T) {
	store, _ := newTestAuthStore(t)
	hub := NewHub()
	hub.OfflineStore = store

	sender := newTestClient(t, "Alice", "A001")
	sender.AccountBacked = true
	sender.Room = defaultRoomName
	recipient := newTestClient(t, "Bob", "B001")
	recipient.AccountBacked = true
	recipient.Room = defaultRoomName
	hub.Rooms[defaultRoomName] = map[*Client]bool{sender: true, recipient: true}
	for _, client := range []*Client{sender, recipient} {
		hub.Clients[client] = true
		hub.ActiveCodes[client.NormalizedCode] = client
	}

	message := Message{Type: "chat", MessageID: "client-msg-002", Username: "Alice", UserCode: "A001",
		Room: defaultRoomName, Content: "receipt please"}
	hub.broadcastMessage(message)
	hub.broadcastMessage(message)

	var receipts []Message
	for _, sent := range drainMessages(sender.Send) {
		if sent.Type == "delivery_receipt" {
			receipts = append(receipts, sent)
		}
	}
	if len(receipts) != 2 {
		t.Fatalf("delivery receipts = %+v, want two", receipts)
	}
	for _, receipt := range receipts {
		if receipt.MessageID != message.MessageID || receipt.Content != "delivered" {
			t.Fatalf("delivery receipt = %+v", receipt)
		}
	}
}

func TestHubDuplicateReceiptPreservesOriginalDeliveryState(t *testing.T) {
	store, _ := newTestAuthStore(t)
	hub := NewHub()
	hub.OfflineStore = store

	sender := newTestClient(t, "Alice", "A001")
	sender.AccountBacked = true
	sender.Room = defaultRoomName
	recipient := newTestClient(t, "Bob", "B001")
	recipient.AccountBacked = true
	recipient.Room = defaultRoomName
	hub.Rooms[defaultRoomName] = map[*Client]bool{sender: true, recipient: true}
	for _, client := range []*Client{sender, recipient} {
		hub.Clients[client] = true
		hub.ActiveCodes[client.NormalizedCode] = client
	}

	message := Message{Type: "chat", MessageID: "client-msg-003", Username: "Alice", UserCode: "A001",
		Room: defaultRoomName, Content: "persist delivery state"}
	hub.broadcastMessage(message)
	drainMessages(sender.Send)
	drainMessages(recipient.Send)

	hub.removeClient(recipient)
	hub.broadcastMessage(message)

	var receipts []Message
	for _, sent := range drainMessages(sender.Send) {
		if sent.Type == "delivery_receipt" {
			receipts = append(receipts, sent)
		}
	}
	if len(receipts) != 1 || receipts[0].Content != "delivered" {
		t.Fatalf("duplicate receipt after peer leaves = %+v, want delivered", receipts)
	}
}

func TestHubDoesNotDeliverDuplicatePrivateClientMessageID(t *testing.T) {
	store, _ := newTestAuthStore(t)
	hub := NewHub()
	hub.OfflineStore = store

	sender := newTestClient(t, "Alice", "A001")
	sender.AccountBacked = true
	recipient := newTestClient(t, "Bob", "B001")
	recipient.AccountBacked = true
	for _, client := range []*Client{sender, recipient} {
		hub.Clients[client] = true
		hub.ActiveCodes[client.NormalizedCode] = client
	}

	request := PrivateMessageRequest{Sender: sender, TargetCode: recipient.NormalizedCode,
		Content: "only once privately", MessageID: "private-client-msg-001"}
	hub.handlePrivateMessage(request)
	first := <-recipient.Send
	if first.Type != "private_chat" || first.MessageID != request.MessageID {
		t.Fatalf("first private delivery = %+v", first)
	}
	drainMessages(sender.Send)

	hub.handlePrivateMessage(request)
	select {
	case duplicate := <-recipient.Send:
		t.Fatalf("duplicate private client message was delivered: %+v", duplicate)
	default:
	}

	var receipts []Message
	for _, sent := range drainMessages(sender.Send) {
		if sent.Type == "delivery_receipt" {
			receipts = append(receipts, sent)
		}
	}
	if len(receipts) != 1 || receipts[0].MessageID != request.MessageID || receipts[0].Content != "delivered" {
		t.Fatalf("duplicate private receipt = %+v", receipts)
	}
}

func TestAuthStoreMessageIDIsScopedToSenderAndConversation(t *testing.T) {
	store, _ := newTestAuthStore(t)
	first := Message{Type: "chat", MessageID: "scoped-message-id", Username: "Alice", UserCode: "A001",
		Room: "lobby", Content: "in lobby"}
	second := first
	second.Room = "engineering"
	second.Content = "in engineering"

	inserted, err := store.SaveChatMessageIfNew(first)
	if err != nil || !inserted {
		t.Fatalf("save first message = inserted:%v err:%v", inserted, err)
	}
	inserted, err = store.SaveChatMessageIfNew(second)
	if err != nil || !inserted {
		t.Fatalf("save same ID in another conversation = inserted:%v err:%v", inserted, err)
	}
}
