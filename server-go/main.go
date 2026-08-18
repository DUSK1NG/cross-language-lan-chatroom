package main

import (
	"crypto/tls"
	"flag"
	"log"
)

const listenAddress = "0.0.0.0:8888"

func main() {
	certPath := flag.String("cert", "", "path to the TLS certificate PEM file")
	keyPath := flag.String("key", "", "path to the TLS private key PEM file")
	flag.Parse()

	tlsConfig, err := loadTLSConfig(*certPath, *keyPath)
	if err != nil {
		log.Fatalf("TLS configuration error: %v", err)
	}

	listener, err := tls.Listen("tcp", listenAddress, tlsConfig)
	if err != nil {
		log.Fatalf("failed to listen on %s: %v", listenAddress, err)
	}
	defer listener.Close()

	log.Printf("listening on %s", listenAddress)
	hub := NewHub()
	go hub.Run()

	for {
		conn, err := listener.Accept()
		if err != nil {
			log.Printf("failed to accept client: %v", err)
			continue
		}

		go handleConnection(conn, hub)
	}
}
