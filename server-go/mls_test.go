package main

import (
	"net"
	"testing"
	"time"
)

func setupMLSHub(t *testing.T) (*Hub, *Client, *Client) {
	t.Helper()
	hub := NewHub()
	store, _ := newTestAuthStore(t)
	hub.OfflineStore = store
	alice := newTestClient(t, "Alice", "Alice01")
	bob := newTestClient(t, "Bob", "Bob01")
	alice.Room = defaultRoomName
	bob.Room = defaultRoomName
	hub.Clients[alice] = true
	hub.Clients[bob] = true
	hub.ActiveCodes[alice.NormalizedCode] = alice
	hub.ActiveCodes[bob.NormalizedCode] = bob
	hub.Rooms[defaultRoomName] = map[*Client]bool{alice: true, bob: true}
	return hub, alice, bob
}

func TestHubMLSFetchRequiresRoomAuthorization(t *testing.T) {
	hub, alice, bob := setupMLSHub(t)
	hub.handleMLSKeyPackagePublish(MLSKeyPackagePublishRequest{Sender: bob, KeyPackage: "a2V5"})
	<-bob.Send // publish acknowledgement

	hub.handleMLSKeyPackageFetch(MLSKeyPackageFetchRequest{Sender: alice, TargetCode: bob.UserCode, Room: "private"})
	if got := <-alice.Send; got.Type != "error" {
		t.Fatalf("unauthorized fetch response = %+v", got)
	}
	hub.handleMLSKeyPackageFetch(MLSKeyPackageFetchRequest{Sender: alice, TargetCode: bob.UserCode, Room: defaultRoomName})
	got := <-alice.Send
	if got.Type != "mls.key_package.fetch" || got.KeyPackage != "a2V5" {
		t.Fatalf("authorized fetch response = %+v", got)
	}
}

func TestHubMLSWelcomeRequiresTargetMembershipOrInvite(t *testing.T) {
	hub, alice, bob := setupMLSHub(t)
	carol := newTestClient(t, "Carol", "Carol01")
	hub.handleMLSGroupWelcome(MLSGroupWelcomeRequest{Sender: alice, GroupID: "g", Room: defaultRoomName,
		Epoch: 1, TargetCode: carol.UserCode, Welcome: "d2VsY29tZQ==", CommandID: "w1"})
	if got := <-alice.Send; got.Type != "error" {
		t.Fatalf("unauthorized welcome response = %+v", got)
	}
	hub.RoomDefinitions[defaultRoomName].Allowed[carol.NormalizedCode] = true
	hub.handleMLSGroupWelcome(MLSGroupWelcomeRequest{Sender: alice, GroupID: "g", Room: defaultRoomName,
		Epoch: 1, TargetCode: carol.UserCode, Welcome: "d2VsY29tZQ==", CommandID: "w2"})
	if got := <-alice.Send; got.Type != "mls.group.welcome" || got.Content != "stored" {
		t.Fatalf("welcome acknowledgement = %+v", got)
	}
	_ = bob // retain setup's second member as an explicit authorized-room fixture
}

func TestHubMLSCommitRoutesIdempotentAcknowledgement(t *testing.T) {
	hub, alice, _ := setupMLSHub(t)
	request := MLSGroupCommitRequest{Sender: alice, GroupID: "g", Room: defaultRoomName, Epoch: 4,
		Commit: "Y29tbWl0", CommandID: "c1"}
	hub.handleMLSGroupCommit(request)
	if got := <-alice.Send; got.Type != "mls.group.commit" || got.Content != "stored" {
		t.Fatalf("first commit response = %+v", got)
	}
	request.CommandID = "c2"
	hub.handleMLSGroupCommit(request)
	if got := <-alice.Send; got.Type != "mls.group.commit" || got.Content != "duplicate" {
		t.Fatalf("duplicate commit response = %+v", got)
	}
}

func TestMLSControlTraversesReadAndWritePumps(t *testing.T) {
	hub := NewHub()
	store, _ := newTestAuthStore(t)
	hub.OfflineStore = store
	go hub.Run()
	serverConn, clientConn := net.Pipe()
	t.Cleanup(func() { _ = clientConn.Close() })
	go handleConnectionWithStore(serverConn, hub, nil)
	_ = clientConn.SetDeadline(time.Now().Add(time.Second))
	if err := sendMessage(clientConn, Message{Type: "login", Username: "Alice", UserCode: "Alice01"}); err != nil {
		t.Fatal(err)
	}
	if response, err := receiveMessage(clientConn); err != nil || response.Type != "login_ok" {
		t.Fatalf("login response = %+v, err=%v", response, err)
	}
	// Registration presence is queued before the readPump starts; consume it
	// before asserting the control-message response.
	if response, err := receiveMessage(clientConn); err != nil || response.Type != "system" {
		t.Fatalf("presence response = %+v, err=%v", response, err)
	}
	if err := sendMessage(clientConn, Message{Type: "mls.key_package.publish", CommandID: "kp1", KeyPackage: "a2V5"}); err != nil {
		t.Fatal(err)
	}
	response, err := receiveMessage(clientConn)
	if err != nil {
		t.Fatal(err)
	}
	if response.Type != "mls.key_package.publish" || response.Content != "stored" || response.CommandID != "kp1" {
		t.Fatalf("MLS publish response = %+v", response)
	}
}
