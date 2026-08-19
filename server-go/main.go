package main

import (
	"crypto/tls"
	"flag"
	"log"
	"strings"
)

const listenAddress = "0.0.0.0:8888"

func main() {
	certPath := flag.String("cert", "", "path to the TLS certificate PEM file")
	keyPath := flag.String("key", "", "path to the TLS private key PEM file")
	dbPath := flag.String("db", "", "path to the SQLite account database")
	adminCode := flag.String("admin-code", "", "user code granted administrator permissions")
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
	hub.AdminCode = strings.ToLower(*adminCode)
	go hub.Run()
	store, err := openAuthStore(*dbPath)
	if err != nil {
		log.Fatalf("auth database error: %v", err)
	}
	defer store.Close()
	hub.OfflineStore = store
	log.Printf("account database: %s", resolveDBPath(*dbPath))
	if hub.AdminCode != "" {
		log.Printf("administrator code configured: %s", hub.AdminCode)
	}

	for {
		conn, err := listener.Accept()
		if err != nil {
			log.Printf("failed to accept client: %v", err)
			continue
		}

		go handleConnectionWithStore(conn, hub, store)
	}
}
