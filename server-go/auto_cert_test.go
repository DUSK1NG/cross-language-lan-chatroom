package main

import (
	"crypto/x509"
	"encoding/pem"
	"os"
	"path/filepath"
	"testing"
)

func TestEnsureSelfSignedCertificateCreatesUsableCertificate(t *testing.T) {
	dir := t.TempDir()
	certPath := filepath.Join(dir, "certs", "server-lan.crt")
	keyPath := filepath.Join(dir, "certs", "server-lan.key")

	if err := ensureSelfSignedCertificate(certPath, keyPath); err != nil {
		t.Fatalf("ensureSelfSignedCertificate() error = %v", err)
	}

	config, err := loadTLSConfig(certPath, keyPath)
	if err != nil {
		t.Fatalf("loadTLSConfig() error = %v", err)
	}
	if config.MinVersion == 0 {
		t.Fatal("TLS configuration must set a minimum version")
	}

	pemBytes, err := os.ReadFile(certPath)
	if err != nil {
		t.Fatalf("read certificate: %v", err)
	}
	block, _ := pem.Decode(pemBytes)
	if block == nil {
		t.Fatal("generated certificate is not PEM")
	}
	certificate, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		t.Fatalf("parse generated certificate: %v", err)
	}
	if err := certificate.VerifyHostname("localhost"); err != nil {
		t.Fatalf("certificate does not support localhost: %v", err)
	}
	if err := certificate.VerifyHostname("127.0.0.1"); err != nil {
		t.Fatalf("certificate does not support loopback IP: %v", err)
	}
}

func TestEnsureSelfSignedCertificateRejectsPartialPair(t *testing.T) {
	dir := t.TempDir()
	certPath := filepath.Join(dir, "server-lan.crt")
	keyPath := filepath.Join(dir, "server-lan.key")
	if err := os.WriteFile(certPath, []byte("existing certificate"), 0o600); err != nil {
		t.Fatal(err)
	}

	if err := ensureSelfSignedCertificate(certPath, keyPath); err == nil {
		t.Fatal("expected an error for a partial certificate/key pair")
	}
	if _, err := os.Stat(keyPath); !os.IsNotExist(err) {
		t.Fatalf("key file should not be created for a partial pair, stat error = %v", err)
	}
}
