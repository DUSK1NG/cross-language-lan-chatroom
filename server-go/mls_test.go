package main

import (
	"crypto/sha256"
	"encoding/hex"
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
	// Once the legacy group row exists, subsequent welcomes must use the
	// authenticated proposal/commit path rather than reviving the fallback.
	hub.handleMLSGroupWelcome(MLSGroupWelcomeRequest{Sender: alice, GroupID: "g", Room: defaultRoomName,
		Epoch: 1, TargetCode: carol.UserCode, Welcome: "d2VsY29tZQ==", CommandID: "w3"})
	if got := <-alice.Send; got.Type != "error" || got.Content != "MLS group welcome access denied" {
		t.Fatalf("legacy group welcome must enter strict mode = %+v", got)
	}
	_ = bob // retain setup's second member as an explicit authorized-room fixture
}

func TestHubMLSCommitRoutesIdempotentAcknowledgement(t *testing.T) {
	hub, alice, _ := setupMLSHub(t)
	if _, err := hub.OfflineStore.SaveMLSProposal("g", defaultRoomName, 4, "p1", "cHJvcG9zYWw="); err != nil {
		t.Fatal(err)
	}
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

func TestHubMLSCommitRejectsUnsolicitedEpoch(t *testing.T) {
	hub, alice, bob := setupMLSHub(t)
	request := MLSGroupCommitRequest{Sender: alice, GroupID: "unproposed", Room: defaultRoomName, Epoch: 1,
		Commit: "Y29tbWl0", CommandID: "unrequested"}
	hub.handleMLSGroupCommit(request)
	if got := <-alice.Send; got.Type != "error" || got.Content != "MLS group commit requires an accepted proposal" {
		t.Fatalf("unsolicited commit response = %+v", got)
	}
	select {
	case got := <-bob.Send:
		t.Fatalf("unsolicited commit was broadcast: %+v", got)
	default:
	}
}

func TestHubMLSProposalRoutesToExistingMembersBeforeCommit(t *testing.T) {
	hub, alice, bob := setupMLSHub(t)
	carol := newTestClient(t, "Carol", "Carol01")
	carol.Room = defaultRoomName
	hub.Clients[carol] = true
	hub.ActiveCodes[carol.NormalizedCode] = carol
	hub.RoomDefinitions[defaultRoomName].Allowed[carol.NormalizedCode] = true
	proposal := MLSGroupProposalRequest{Sender: alice, GroupID: "g", Room: defaultRoomName, Epoch: 1,
		ProposalID: "p1", Action: "add", TargetCode: carol.UserCode, Proposal: "cHJvcG9zYWw=", CommandID: "p-cmd"}
	hub.handleMLSGroupProposal(proposal)
	if got := <-alice.Send; got.Type != "mls.group.proposal" || got.Content != "stored" || got.CommandID != "p-cmd" {
		t.Fatalf("proposal acknowledgement = %+v", got)
	}
	select {
	case got := <-bob.Send:
		t.Fatalf("non-member received proposal: %+v", got)
	default:
	}
	proposal.CommandID = "p-duplicate"
	hub.handleMLSGroupProposal(proposal)
	if got := <-alice.Send; got.Content != "duplicate" {
		t.Fatalf("duplicate proposal acknowledgement = %+v", got)
	}
	select {
	case got := <-bob.Send:
		t.Fatalf("duplicate proposal was rebroadcast: %+v", got)
	default:
	}
	proposal.CommandID = "p-conflict"
	proposal.Proposal = "b3RoZXI="
	hub.handleMLSGroupProposal(proposal)
	if got := <-alice.Send; got.Type != "error" || got.Content != "MLS group proposal conflict" {
		t.Fatalf("conflicting proposal response = %+v", got)
	}
	commit := MLSGroupCommitRequest{Sender: alice, GroupID: "g", Room: defaultRoomName, Epoch: 1,
		ProposalID: "p1", Commit: "Y29tbWl0", CommandID: "c-cmd"}
	hub.handleMLSGroupCommit(commit)
	if got := <-alice.Send; got.Type != "mls.group.commit" || got.Content != "stored" || got.CommandID != "c-cmd" {
		t.Fatalf("commit acknowledgement = %+v", got)
	}
	select {
	case got := <-bob.Send:
		t.Fatalf("non-member received commit: %+v", got)
	default:
	}
	digestBytes := sha256.Sum256([]byte("d2VsY29tZQ=="))
	hub.handleMLSGroupWelcome(MLSGroupWelcomeRequest{Sender: alice, GroupID: "g", Room: defaultRoomName,
		Epoch: 1, ProposalID: "p1", WelcomeDigest: "tampered", TargetCode: carol.UserCode, Welcome: "d2VsY29tZQ==", CommandID: "w-tampered"})
	if got := <-alice.Send; got.Type != "error" || got.Content != "MLS group welcome access denied" {
		t.Fatalf("tampered welcome = %+v", got)
	}
	hub.handleMLSGroupWelcome(MLSGroupWelcomeRequest{Sender: alice, GroupID: "g", Room: defaultRoomName,
		Epoch: 1, ProposalID: "p1", WelcomeDigest: hex.EncodeToString(digestBytes[:]), TargetCode: carol.UserCode, Welcome: "d2VsY29tZQ==", CommandID: "w1"})
	if got := <-carol.Send; got.Type != "mls.group.welcome" || got.Welcome != "d2VsY29tZQ==" {
		t.Fatalf("welcome delivery after commit = %+v", got)
	}
	if got := <-alice.Send; got.Type != "mls.group.welcome" || got.Content != "stored" || got.CommandID != "w1" {
		t.Fatalf("welcome acknowledgement = %+v", got)
	}
	hub.handleMLSGroupWelcomeAccept(MLSGroupWelcomeAcceptRequest{Sender: carol, GroupID: "g", Room: defaultRoomName,
		Epoch: 1, ProposalID: "p1", TargetCode: carol.UserCode, WelcomeDigest: hex.EncodeToString(digestBytes[:]), CommandID: "wa1"})
	if got := <-carol.Send; got.Type != "mls.group.welcome.accept" || got.Content != "activated" {
		t.Fatalf("welcome accept = %+v", got)
	}
	commit.CommandID = "c-duplicate"
	hub.handleMLSGroupCommit(commit)
	if got := <-alice.Send; got.Content != "duplicate" {
		t.Fatalf("duplicate commit acknowledgement = %+v", got)
	}
	select {
	case got := <-bob.Send:
		t.Fatalf("duplicate commit was rebroadcast: %+v", got)
	default:
	}
}

func TestHubMLSProposalRequiresExistingRoomMember(t *testing.T) {
	hub, _, _ := setupMLSHub(t)
	outsider := newTestClient(t, "Outsider", "Outsider01")
	hub.Clients[outsider] = true
	outsider.Room = defaultRoomName
	hub.handleMLSGroupProposal(MLSGroupProposalRequest{Sender: outsider, GroupID: "g", Room: defaultRoomName,
		Epoch: 1, ProposalID: "p1", Proposal: "cHJvcG9zYWw=", CommandID: "p1"})
	if got := <-outsider.Send; got.Type != "error" || got.Content != "MLS group proposal access denied" {
		t.Fatalf("unauthorized proposal response = %+v", got)
	}
}

func TestHubMLSProposalAllowsAuthenticatedOfflineTargetInPublicRoom(t *testing.T) {
	hub, alice, _ := setupMLSHub(t)
	carol := newTestClient(t, "Carol", "Carol01")
	if _, err := hub.OfflineStore.EnsureIdentity("Carol", "Carol01"); err != nil {
		t.Fatal(err)
	}
	// Carol's account has previously authenticated, but her current socket is
	// offline. A public room still authorizes a valid account as a target.
	proposal := MLSGroupProposalRequest{Sender: alice, GroupID: "offline-target", Room: defaultRoomName, Epoch: 1,
		ProposalID: "p1", Action: "add", TargetCode: carol.UserCode, Proposal: "cHJvcG9zYWw=", CommandID: "p1"}
	hub.handleMLSGroupProposal(proposal)
	if got := <-alice.Send; got.Type != "mls.group.proposal" || got.Content != "stored" {
		t.Fatalf("offline public-room target proposal=%+v", got)
	}
}

func TestHubMLSProgressIsSerializedUntilWelcomeAccepted(t *testing.T) {
	hub, alice, bob := setupMLSHub(t)
	proposal := MLSGroupProposalRequest{Sender: alice, GroupID: "serial", Room: defaultRoomName, Epoch: 1,
		ProposalID: "p1", Action: "add", TargetCode: bob.UserCode, Proposal: "cHJvcG9zYWw=", CommandID: "p1"}
	hub.handleMLSGroupProposal(proposal)
	if got := <-alice.Send; got.Type != "mls.group.proposal" || got.Content != "stored" {
		t.Fatalf("proposal acknowledgement = %+v", got)
	}
	commit := MLSGroupCommitRequest{Sender: alice, GroupID: "serial", Room: defaultRoomName, Epoch: 1,
		ProposalID: "p1", Commit: "Y29tbWl0", CommandID: "c1"}
	hub.handleMLSGroupCommit(commit)
	if got := <-alice.Send; got.Type != "mls.group.commit" || got.Content != "stored" {
		t.Fatalf("commit acknowledgement = %+v", got)
	}
	digest := sha256.Sum256([]byte("d2VsY29tZQ=="))
	hub.handleMLSGroupWelcome(MLSGroupWelcomeRequest{Sender: alice, GroupID: "serial", Room: defaultRoomName,
		Epoch: 1, ProposalID: "p1", TargetCode: bob.UserCode, WelcomeDigest: hex.EncodeToString(digest[:]),
		Welcome: "d2VsY29tZQ==", CommandID: "w1"})
	if got := <-bob.Send; got.Type != "mls.group.welcome" {
		t.Fatalf("pending welcome delivery = %+v", got)
	}
	if got := <-alice.Send; got.Type != "mls.group.welcome" || got.Content != "stored" {
		t.Fatalf("welcome acknowledgement = %+v", got)
	}

	hub.handleMLSGroupProposal(MLSGroupProposalRequest{Sender: alice, GroupID: "serial", Room: defaultRoomName, Epoch: 2,
		ProposalID: "p2", Action: "add", TargetCode: bob.UserCode, Proposal: "cHJvcG9zYWwy", CommandID: "p2"})
	if got := <-alice.Send; got.Type != "error" || got.Content != "MLS group has pending welcome" {
		t.Fatalf("proposal during pending welcome = %+v", got)
	}
	hub.handleMLSGroupCommit(MLSGroupCommitRequest{Sender: alice, GroupID: "serial", Room: defaultRoomName, Epoch: 2,
		ProposalID: "p2", Commit: "Y29tbWl0Mg==", CommandID: "c2"})
	if got := <-alice.Send; got.Type != "error" || got.Content != "MLS group has pending welcome" {
		t.Fatalf("commit during pending welcome = %+v", got)
	}
}

func TestHubMLSAuthoritativeMembershipSnapshotAndWelcomeActivation(t *testing.T) {
	hub, alice, bob := setupMLSHub(t)
	carol := newTestClient(t, "Carol", "Carol01")
	carol.Room = defaultRoomName
	hub.Clients[carol] = true
	hub.ActiveCodes[carol.NormalizedCode] = carol
	hub.Rooms[defaultRoomName][carol] = true
	hub.RoomDefinitions[defaultRoomName].Allowed[carol.NormalizedCode] = true
	proposal := MLSGroupProposalRequest{Sender: alice, GroupID: "strict", Room: defaultRoomName, Epoch: 1,
		ProposalID: "p1", Action: "add", TargetCode: bob.UserCode, Proposal: "cHJvcG9zYWw=", CommandID: "p1"}
	hub.handleMLSGroupProposal(proposal)
	if got := <-alice.Send; got.Content != "stored" {
		t.Fatalf("proposal ack=%+v", got)
	}
	if members, err := hub.OfflineStore.MLSGroupMembers("strict"); err != nil || len(members) != 1 || members[0] != alice.NormalizedCode {
		t.Fatalf("pre-commit members=%v err=%v", members, err)
	}
	select {
	case got := <-bob.Send:
		t.Fatalf("target received proposal: %+v", got)
	default:
	}
	hub.handleMLSGroupCommit(MLSGroupCommitRequest{Sender: alice, GroupID: "strict", Room: defaultRoomName, Epoch: 1,
		ProposalID: "p1", Commit: "Y29tbWl0", CommandID: "c1"})
	if got := <-alice.Send; got.Content != "stored" {
		t.Fatalf("commit ack=%+v", got)
	}
	select {
	case got := <-bob.Send:
		t.Fatalf("target received commit: %+v", got)
	default:
	}
	digestBytes := sha256.Sum256([]byte("d2VsY29tZQ=="))
	hub.handleMLSGroupWelcome(MLSGroupWelcomeRequest{Sender: alice, GroupID: "strict", Room: defaultRoomName,
		Epoch: 1, ProposalID: "p1", WelcomeDigest: hex.EncodeToString(digestBytes[:]), TargetCode: bob.UserCode, Welcome: "d2VsY29tZQ==", CommandID: "w1"})
	if got := <-bob.Send; got.Type != "mls.group.welcome" {
		t.Fatalf("welcome=%+v", got)
	}
	if got := <-alice.Send; got.Content != "stored" {
		t.Fatalf("welcome ack=%+v", got)
	}
	members, err := hub.OfflineStore.MLSGroupMembers("strict")
	if err != nil || len(members) != 1 {
		t.Fatalf("pending-welcome members=%v err=%v", members, err)
	}
	hub.handleMLSGroupWelcomeAccept(MLSGroupWelcomeAcceptRequest{Sender: bob, GroupID: "strict", Room: defaultRoomName,
		Epoch: 1, ProposalID: "p1", TargetCode: bob.UserCode, WelcomeDigest: "tampered", CommandID: "wa-tampered"})
	if got := <-bob.Send; got.Type != "error" || got.Content != "MLS welcome accept conflict" {
		t.Fatalf("tampered welcome accept=%+v", got)
	}
	hub.handleMLSGroupWelcomeAccept(MLSGroupWelcomeAcceptRequest{Sender: bob, GroupID: "strict", Room: defaultRoomName,
		Epoch: 1, ProposalID: "p1", TargetCode: bob.UserCode, WelcomeDigest: hex.EncodeToString(digestBytes[:]), CommandID: "wa1"})
	if got := <-bob.Send; got.Type != "mls.group.welcome.accept" || got.Content != "activated" {
		t.Fatalf("welcome accept=%+v", got)
	}
	members, err = hub.OfflineStore.MLSGroupMembers("strict")
	if err != nil || len(members) != 2 {
		t.Fatalf("post-accept members=%v err=%v", members, err)
	}
	hub.handleMLSGroupWelcomeAccept(MLSGroupWelcomeAcceptRequest{Sender: bob, GroupID: "strict", Room: defaultRoomName,
		Epoch: 1, ProposalID: "p1", TargetCode: bob.UserCode, WelcomeDigest: hex.EncodeToString(digestBytes[:]), CommandID: "wa-duplicate"})
	if got := <-bob.Send; got.Type != "mls.group.welcome.accept" || got.Content != "duplicate" {
		t.Fatalf("duplicate welcome accept=%+v", got)
	}
	hub.handleMLSGroupProposal(MLSGroupProposalRequest{Sender: carol, GroupID: "strict", Room: defaultRoomName,
		Epoch: 2, ProposalID: "unauthorized-proposal", Action: "add", TargetCode: carol.UserCode,
		Proposal: "cHJvcG9zYWw=", CommandID: "unauthorized-proposal"})
	if got := <-carol.Send; got.Type != "error" || got.Content != "MLS group proposal access denied" {
		t.Fatalf("unauthorized proposal=%+v", got)
	}
	// A room-invited Carol is not an MLS member and cannot advance the group.
	hub.handleMLSGroupCommit(MLSGroupCommitRequest{Sender: carol, GroupID: "strict", Room: defaultRoomName,
		Epoch: 2, ProposalID: "p1", Commit: "bm8=", CommandID: "unauthorized"})
	if got := <-carol.Send; got.Type != "error" || got.Content != "MLS group commit access denied" {
		t.Fatalf("unauthorized commit=%+v", got)
	}
	// A fully removed group remains in strict mode and cannot be revived by a
	// legacy-looking welcome. A new group id and creator proposal are required.
	hub.handleMLSGroupProposal(MLSGroupProposalRequest{Sender: alice, GroupID: "strict", Room: defaultRoomName,
		Epoch: 2, ProposalID: "remove-bob", Action: "remove", TargetCode: bob.UserCode,
		Proposal: "cmVtb3Zl", CommandID: "remove-proposal"})
	if got := <-alice.Send; got.Content != "stored" {
		t.Fatalf("remove proposal=%+v", got)
	}
	hub.handleMLSGroupCommit(MLSGroupCommitRequest{Sender: alice, GroupID: "strict", Room: defaultRoomName,
		Epoch: 2, ProposalID: "remove-bob", Commit: "cmVtb3ZlLWNvbW1pdA==", CommandID: "remove-commit"})
	if got := <-alice.Send; got.Content != "stored" {
		t.Fatalf("remove commit=%+v", got)
	}
	hub.handleMLSGroupWelcome(MLSGroupWelcomeRequest{Sender: alice, GroupID: "strict", Room: defaultRoomName,
		Epoch: 3, ProposalID: "legacy", WelcomeDigest: "invalid", TargetCode: bob.UserCode,
		Welcome: "bGVnYWN5", CommandID: "revive"})
	if got := <-alice.Send; got.Type != "error" {
		t.Fatalf("revived group welcome=%+v", got)
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
	if err := sendMessage(clientConn, Message{Type: "mls.group.proposal", CommandID: "proposal1",
		GroupID: "group1", Room: defaultRoomName, Epoch: 1, ProposalID: "proposal1", Proposal: "cHJvcG9zYWw="}); err != nil {
		t.Fatal(err)
	}
	response, err = receiveMessage(clientConn)
	if err != nil {
		t.Fatal(err)
	}
	if response.Type != "mls.group.proposal" || response.Content != "stored" || response.CommandID != "proposal1" {
		t.Fatalf("MLS proposal response = %+v", response)
	}
	if err := sendMessage(clientConn, Message{Type: "mls.group.commit", CommandID: "commit-before-proposal",
		GroupID: "group-without-proposal", Room: defaultRoomName, Epoch: 1, Commit: "Y29tbWl0"}); err != nil {
		t.Fatal(err)
	}
	response, err = receiveMessage(clientConn)
	if err != nil {
		t.Fatal(err)
	}
	if response.Type != "error" || response.Content != "MLS group commit requires an accepted proposal" {
		t.Fatalf("unsolicited MLS commit response = %+v", response)
	}
}

func TestMLSWelcomeReplaysToAuthenticatedReconnectUntilAccepted(t *testing.T) {
	store, _ := newTestAuthStore(t)
	if _, err := store.EnsureIdentity("Alice", "alice01"); err != nil {
		t.Fatal(err)
	}
	if _, err := store.EnsureIdentity("Bob", "bob01"); err != nil {
		t.Fatal(err)
	}
	if _, _, err := store.SaveMLSProposalForMember("replay-group", defaultRoomName, "alice01", "add", "bob01", 1, "proposal-1", "cHJvcG9zYWw="); err != nil {
		t.Fatal(err)
	}
	if _, _, err := store.SaveMLSCommitForMember("replay-group", defaultRoomName, "alice01", "proposal-1", 1, "Y29tbWl0"); err != nil {
		t.Fatal(err)
	}
	if _, err := store.SaveMLSWelcomeForMember("replay-group", defaultRoomName, "alice01", 1, "bob01", "proposal-1", "d2VsY29tZQ=="); err != nil {
		t.Fatal(err)
	}
	pending, err := store.PendingMLSWelcomes("bob01")
	if err != nil || len(pending) != 1 {
		t.Fatalf("initial pending welcomes=%+v err=%v", pending, err)
	}

	hub := NewHub()
	hub.OfflineStore = store
	hub.AdminCode = "bob01"
	go hub.Run()
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = listener.Close() })
	accepted := make(chan net.Conn, 2)
	go func() {
		for {
			conn, acceptErr := listener.Accept()
			if acceptErr != nil {
				return
			}
			accepted <- conn
		}
	}()

	receiveType := func(conn net.Conn, want string, timeout time.Duration) (Message, error) {
		if err := conn.SetReadDeadline(time.Now().Add(timeout)); err != nil {
			return Message{}, err
		}
		for {
			message, receiveErr := receiveMessage(conn)
			if receiveErr != nil {
				return Message{}, receiveErr
			}
			if message.Type == want {
				return message, nil
			}
		}
	}

	dial := func() net.Conn {
		conn, dialErr := net.Dial("tcp", listener.Addr().String())
		if dialErr != nil {
			t.Fatal(dialErr)
		}
		serverConn := <-accepted
		go handleConnectionWithStore(serverConn, hub, store)
		return conn
	}
	first := dial()
	t.Cleanup(func() { _ = first.Close() })
	if err := sendMessage(first, Message{Type: "login", Username: "Bob", UserCode: "bob01"}); err != nil {
		t.Fatal(err)
	}
	if _, err := receiveType(first, "login_ok", time.Second); err != nil {
		t.Fatalf("first login: %v", err)
	}
	welcome, err := receiveType(first, "mls.group.welcome", time.Second)
	if err != nil {
		t.Fatalf("first pending welcome: %v", err)
	}
	if welcome.GroupID != "replay-group" || welcome.ProposalID != "proposal-1" || welcome.Welcome != "d2VsY29tZQ==" {
		t.Fatalf("first pending welcome=%+v", welcome)
	}
	if err := sendMessage(first, Message{Type: "mls.group.welcome.accept", GroupID: welcome.GroupID,
		Room: welcome.Room, Epoch: welcome.Epoch, ProposalID: welcome.ProposalID,
		TargetUserCode: "bob01", WelcomeDigest: welcome.WelcomeDigest, CommandID: "accept-1"}); err != nil {
		t.Fatal(err)
	}
	if acceptedMessage, err := receiveType(first, "mls.group.welcome.accept", time.Second); err != nil || acceptedMessage.Content != "activated" {
		t.Fatalf("first welcome accept=%+v err=%v", acceptedMessage, err)
	}
	if members, err := store.MLSGroupMembers("replay-group"); err != nil || len(members) != 2 {
		t.Fatalf("members after accept=%v err=%v", members, err)
	}
	_ = first.Close()

	second := dial()
	defer second.Close()
	if err := sendMessage(second, Message{Type: "login", Username: "Bob", UserCode: "bob01"}); err != nil {
		t.Fatal(err)
	}
	if _, err := receiveType(second, "login_ok", time.Second); err != nil {
		t.Fatalf("second login: %v", err)
	}
	if _, err := receiveType(second, "mls.group.welcome", 250*time.Millisecond); err == nil {
		t.Fatal("accepted welcome was replayed after reconnect")
	}
}
