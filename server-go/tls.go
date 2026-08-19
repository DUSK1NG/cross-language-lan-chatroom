package main

import (
	"crypto/tls"
	"fmt"
	"os"
)

const (
	tlsCertEnv = "CHAT_TLS_CERT"
	tlsKeyEnv  = "CHAT_TLS_KEY"
)

func resolveTLSPaths(certPath, keyPath string) (string, string, error) {
	if certPath == "" {
		certPath = os.Getenv(tlsCertEnv)
	}
	if keyPath == "" {
		keyPath = os.Getenv(tlsKeyEnv)
	}
	if certPath == "" || keyPath == "" {
		return "", "", fmt.Errorf("TLS certificate and private key are required; use -cert/-key or %s/%s", tlsCertEnv, tlsKeyEnv)
	}
	return certPath, keyPath, nil
}

func loadTLSConfig(certPath, keyPath string) (*tls.Config, error) {
	certPath, keyPath, err := resolveTLSPaths(certPath, keyPath)
	if err != nil {
		return nil, err
	}

	certificate, err := tls.LoadX509KeyPair(certPath, keyPath)
	if err != nil {
		return nil, fmt.Errorf("load TLS certificate and private key: %w", err)
	}

	return &tls.Config{
		Certificates: []tls.Certificate{certificate},
		MinVersion:   tls.VersionTLS12,
	}, nil
}
