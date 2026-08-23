package main

import (
	"fmt"
	"os"
	"path/filepath"
)

// initializeLocalHost creates the local TLS identity and SQLite state without
// opening the network listener. It is safe to run on every GUI launch: an
// existing certificate/key pair and database are kept intact.
func initializeLocalHost(certPath, keyPath, dbPath string) error {
	if err := ensureSelfSignedCertificate(certPath, keyPath); err != nil {
		return fmt.Errorf("initialize TLS identity: %w", err)
	}

	resolvedDBPath := resolveDBPath(dbPath)
	if err := os.MkdirAll(filepath.Dir(resolvedDBPath), 0o700); err != nil {
		return fmt.Errorf("create database directory: %w", err)
	}
	store, err := openAuthStore(resolvedDBPath)
	if err != nil {
		return fmt.Errorf("initialize chat database: %w", err)
	}
	if err := store.Close(); err != nil {
		return fmt.Errorf("close initialized chat database: %w", err)
	}
	return nil
}
