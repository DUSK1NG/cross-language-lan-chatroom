package main

import (
	"log"
	"net"
)

const listenAddress = "0.0.0.0:8888"

func main() {
	listener, err := net.Listen("tcp", listenAddress)
	if err != nil {
		log.Fatalf("failed to listen on %s: %v", listenAddress, err)
	}
	defer listener.Close()

	log.Printf("listening on %s", listenAddress)
	registry := NewClientRegistry()

	for {
		conn, err := listener.Accept()
		if err != nil {
			log.Printf("failed to accept client: %v", err)
			continue
		}

		go handleConnection(conn, registry)
	}
}
