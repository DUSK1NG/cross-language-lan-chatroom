package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestInitializeLocalHostCreatesTLSIdentityAndDatabase(t *testing.T) {
	dir := t.TempDir()
	certPath := filepath.Join(dir, "certs", "server-lan.crt")
	keyPath := filepath.Join(dir, "certs", "server-lan.key")
	dbPath := filepath.Join(dir, "state", "chat.db")

	if err := initializeLocalHost(certPath, keyPath, dbPath); err != nil {
		t.Fatalf("initializeLocalHost() error = %v", err)
	}
	for _, path := range []string{certPath, keyPath, dbPath} {
		info, err := os.Stat(path)
		if err != nil {
			t.Fatalf("expected initialized file %q: %v", path, err)
		}
		if info.IsDir() {
			t.Fatalf("initialized path is a directory: %q", path)
		}
	}
	if _, err := loadTLSConfig(certPath, keyPath); err != nil {
		t.Fatalf("generated TLS identity is not loadable: %v", err)
	}

	if err := initializeLocalHost(certPath, keyPath, dbPath); err != nil {
		t.Fatalf("second initializeLocalHost() error = %v", err)
	}
}
