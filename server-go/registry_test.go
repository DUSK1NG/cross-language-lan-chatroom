package main

import (
	"fmt"
	"net"
	"sync"
	"testing"
)

func TestClientRegistryAddRemove(t *testing.T) {
	registry := NewClientRegistry()
	client, peer := net.Pipe()
	defer client.Close()
	defer peer.Close()

	if got := registry.Count(); got != 0 {
		t.Fatalf("initial count = %d, want 0", got)
	}

	registry.Add(client, "Alice")
	if got := registry.Count(); got != 1 {
		t.Fatalf("count after Add = %d, want 1", got)
	}

	registry.Remove(client)
	if got := registry.Count(); got != 0 {
		t.Fatalf("count after Remove = %d, want 0", got)
	}
}

func TestClientRegistryConcurrentAccess(t *testing.T) {
	const clientCount = 32
	registry := NewClientRegistry()
	clients := make([]net.Conn, clientCount)
	peers := make([]net.Conn, clientCount)
	start := make(chan struct{})
	var addGroup sync.WaitGroup

	for i := 0; i < clientCount; i++ {
		i := i
		addGroup.Add(1)
		go func() {
			defer addGroup.Done()
			client, peer := net.Pipe()
			clients[i] = client
			peers[i] = peer
			<-start
			registry.Add(client, fmt.Sprintf("User%d", i))
		}()
	}

	close(start)
	addGroup.Wait()
	if got := registry.Count(); got != clientCount {
		t.Fatalf("count after concurrent Add = %d, want %d", got, clientCount)
	}

	var removeGroup sync.WaitGroup
	for _, client := range clients {
		client := client
		removeGroup.Add(1)
		go func() {
			defer removeGroup.Done()
			registry.Remove(client)
		}()
	}
	removeGroup.Wait()

	if got := registry.Count(); got != 0 {
		t.Fatalf("count after concurrent Remove = %d, want 0", got)
	}

	for i := range clients {
		clients[i].Close()
		peers[i].Close()
	}
}
