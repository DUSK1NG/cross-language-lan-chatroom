package main

import (
	"net"
	"sync"
)

type ClientRegistry struct {
	mu      sync.Mutex
	clients map[net.Conn]string
}

func NewClientRegistry() *ClientRegistry {
	return &ClientRegistry{
		clients: make(map[net.Conn]string),
	}
}

func (r *ClientRegistry) Add(conn net.Conn, username string) {
	r.mu.Lock()
	defer r.mu.Unlock()

	if r.clients == nil {
		r.clients = make(map[net.Conn]string)
	}
	r.clients[conn] = username
}

func (r *ClientRegistry) Remove(conn net.Conn) {
	r.mu.Lock()
	defer r.mu.Unlock()

	delete(r.clients, conn)
}

func (r *ClientRegistry) Count() int {
	r.mu.Lock()
	defer r.mu.Unlock()

	return len(r.clients)
}
