package main

import (
	"fmt"
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

	conn, err := listener.Accept()
	if err != nil {
		log.Fatalf("failed to accept client: %v", err)
	}
	defer conn.Close()

	for frameNumber := 1; frameNumber <= 2; frameNumber++ {
		payload, err := readFrame(conn)
		if err != nil {
			log.Printf("failed to read frame %d: %v", frameNumber, err)
			return
		}

		message := string(payload)
		fmt.Printf("Client sent frame %d: %s\n", frameNumber, message)

		response := []byte("Received: " + message)
		if err := writeFrame(conn, response); err != nil {
			log.Printf("failed to write response for frame %d: %v", frameNumber, err)
			return
		}

		log.Printf("sent response for frame %d: %s", frameNumber, response)
	}
}
