package main

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"math/big"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestResolveTLSPathsUsesFlagsBeforeEnvironment(t *testing.T) {
	t.Setenv(tlsCertEnv, "env-cert.pem")
	t.Setenv(tlsKeyEnv, "env-key.pem")

	certPath, keyPath, err := resolveTLSPaths("flag-cert.pem", "flag-key.pem")
	if err != nil {
		t.Fatal(err)
	}
	if certPath != "flag-cert.pem" || keyPath != "flag-key.pem" {
		t.Fatalf("resolved paths = %q, %q", certPath, keyPath)
	}
}

func TestResolveTLSPathsUsesEnvironmentWhenFlagsAreEmpty(t *testing.T) {
	t.Setenv(tlsCertEnv, "env-cert.pem")
	t.Setenv(tlsKeyEnv, "env-key.pem")

	certPath, keyPath, err := resolveTLSPaths("", "")
	if err != nil {
		t.Fatal(err)
	}
	if certPath != "env-cert.pem" || keyPath != "env-key.pem" {
		t.Fatalf("resolved paths = %q, %q", certPath, keyPath)
	}
}

func TestResolveTLSPathsRejectsMissingConfiguration(t *testing.T) {
	t.Setenv(tlsCertEnv, "")
	t.Setenv(tlsKeyEnv, "")

	_, _, err := resolveTLSPaths("", "")
	if err == nil {
		t.Fatal("expected missing TLS configuration error")
	}
	if !strings.Contains(err.Error(), "-cert/-key") {
		t.Fatalf("error = %q, want flag guidance", err)
	}
}

func TestLoadTLSConfigLoadsCertificateAndKey(t *testing.T) {
	certPath, keyPath := writeTestCertificate(t)

	config, err := loadTLSConfig(certPath, keyPath)
	if err != nil {
		t.Fatal(err)
	}
	if len(config.Certificates) != 1 {
		t.Fatalf("certificate count = %d, want 1", len(config.Certificates))
	}
	if config.MinVersion != tls.VersionTLS12 {
		t.Fatalf("minimum TLS version = %#x, want TLS 1.2", config.MinVersion)
	}
}

func TestLoadTLSConfigRejectsInvalidCertificateFiles(t *testing.T) {
	tempDir := t.TempDir()
	certPath := filepath.Join(tempDir, "cert.pem")
	keyPath := filepath.Join(tempDir, "key.pem")
	if err := os.WriteFile(certPath, []byte("not a certificate"), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(keyPath, []byte("not a key"), 0o600); err != nil {
		t.Fatal(err)
	}

	if _, err := loadTLSConfig(certPath, keyPath); err == nil {
		t.Fatal("expected invalid certificate error")
	}
}

func writeTestCertificate(t *testing.T) (string, string) {
	t.Helper()

	privateKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatal(err)
	}
	now := time.Now()
	template := &x509.Certificate{
		SerialNumber:          big.NewInt(1),
		Subject:               pkix.Name{CommonName: "localhost"},
		NotBefore:             now.Add(-time.Minute),
		NotAfter:              now.Add(time.Hour),
		KeyUsage:              x509.KeyUsageKeyEncipherment | x509.KeyUsageDigitalSignature,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
		DNSNames:              []string{"localhost"},
	}
	derBytes, err := x509.CreateCertificate(rand.Reader, template, template, &privateKey.PublicKey, privateKey)
	if err != nil {
		t.Fatal(err)
	}

	tempDir := t.TempDir()
	certPath := filepath.Join(tempDir, "cert.pem")
	keyPath := filepath.Join(tempDir, "key.pem")
	certFile, err := os.Create(certPath)
	if err != nil {
		t.Fatal(err)
	}
	if err := pem.Encode(certFile, &pem.Block{Type: "CERTIFICATE", Bytes: derBytes}); err != nil {
		certFile.Close()
		t.Fatal(err)
	}
	if err := certFile.Close(); err != nil {
		t.Fatal(err)
	}

	keyFile, err := os.Create(keyPath)
	if err != nil {
		t.Fatal(err)
	}
	if err := pem.Encode(keyFile, &pem.Block{Type: "RSA PRIVATE KEY", Bytes: x509.MarshalPKCS1PrivateKey(privateKey)}); err != nil {
		keyFile.Close()
		t.Fatal(err)
	}
	if err := keyFile.Close(); err != nil {
		t.Fatal(err)
	}

	return certPath, keyPath
}
