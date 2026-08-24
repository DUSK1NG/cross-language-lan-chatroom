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
	autoCert := flag.Bool("auto-cert", false, "generate a local self-signed certificate when both TLS files are absent")
	initializeLocalHostOnly := flag.Bool("initialize-local-host", false, "create the local TLS identity and chat database, then exit")
	lanDiscovery := flag.Bool("lan-discovery", false, "broadcast this local host to LAN Chat clients on the local network")
	discoveryName := flag.String("discovery-name", "", "display name announced to nearby LAN Chat clients")
	dbPath := flag.String("db", "", "path to the SQLite account database")
	adminCode := flag.String("admin-code", "", "user code granted administrator permissions")
	flag.Parse()
	if *initializeLocalHostOnly {
		if err := initializeLocalHost(*certPath, *keyPath, *dbPath); err != nil {
			log.Fatalf("local host initialization error: %v", err)
		}
		log.Printf("local host files initialized")
		return
	}
	if *autoCert {
		if err := ensureSelfSignedCertificate(*certPath, *keyPath); err != nil {
			log.Fatalf("automatic TLS certificate setup error: %v", err)
		}
	}

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
	if *lanDiscovery {
		stopDiscovery, discoveryErr := startLanDiscovery(*certPath, 8888, strings.TrimSpace(*discoveryName))
		if discoveryErr != nil {
			log.Printf("LAN discovery disabled: %v", discoveryErr)
		} else {
			defer stopDiscovery()
			log.Printf("LAN discovery broadcasting on UDP %d", lanDiscoveryPort)
		}
	}
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
