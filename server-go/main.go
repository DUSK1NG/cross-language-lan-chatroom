package main

import (
	"fmt"
	"io"
	"log"
	"net"
	"strings"
)

const listenAddress = "0.0.0.0:8888"

func main() {
	listener, err := net.Listen("tcp", listenAddress)
	if err != nil {
		log.Fatalf("failed to listen on %s: %v", listenAddress, err)
	}
	defer listener.Close()

	log.Printf("listening on %s", listenAddress)

	conn, err := listener.Accept()
	if err != nil {
		log.Fatalf("failed to accept client: %v", err)
	}
	defer conn.Close()

	var buffer [1024]byte
	n, err := conn.Read(buffer[:])
	if err != nil {
		if err == io.EOF {
			log.Println("client disconnected before sending a message")
			return
		}
		log.Fatalf("failed to read from client: %v", err)
	}

	message := strings.TrimSpace(string(buffer[:n]))
	fmt.Printf("Client sent: %s\n", message)

	if _, err := conn.Write([]byte("Received")); err != nil {
		log.Fatalf("failed to write response: %v", err)
	}

	log.Println("sent response: Received")
}
