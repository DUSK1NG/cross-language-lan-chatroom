package main

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"fmt"
	"math/big"
	"net"
	"os"
	"path/filepath"
	"time"
)

const autoCertificateValidity = 825 * 24 * time.Hour

// ensureSelfSignedCertificate creates a host-local certificate only when both
// files are absent. Existing certificates are deliberately never replaced.
func ensureSelfSignedCertificate(certPath, keyPath string) error {
	certPath, keyPath, err := resolveTLSPaths(certPath, keyPath)
	if err != nil {
		return err
	}

	certExists := fileExists(certPath)
	keyExists := fileExists(keyPath)
	if certExists && keyExists {
		return nil
	}
	if certExists || keyExists {
		return fmt.Errorf("TLS certificate and private key must both exist or both be absent; refusing to overwrite the existing file")
	}

	if err := os.MkdirAll(filepath.Dir(certPath), 0o700); err != nil {
		return fmt.Errorf("create certificate directory: %w", err)
	}
	if err := os.MkdirAll(filepath.Dir(keyPath), 0o700); err != nil {
		return fmt.Errorf("create private-key directory: %w", err)
	}

	privateKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return fmt.Errorf("generate private key: %w", err)
	}
	serialLimit := new(big.Int).Lsh(big.NewInt(1), 128)
	serialNumber, err := rand.Int(rand.Reader, serialLimit)
	if err != nil {
		return fmt.Errorf("generate certificate serial number: %w", err)
	}
	now := time.Now()
	template := &x509.Certificate{
		SerialNumber:          serialNumber,
		Subject:               pkix.Name{CommonName: "LAN Chat Local Host"},
		NotBefore:             now.Add(-time.Minute),
		NotAfter:              now.Add(autoCertificateValidity),
		KeyUsage:              x509.KeyUsageDigitalSignature | x509.KeyUsageKeyEncipherment | x509.KeyUsageCertSign,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
		IsCA:                  true,
		DNSNames:              []string{"localhost"},
		IPAddresses:           localCertificateIPs(),
	}
	derBytes, err := x509.CreateCertificate(rand.Reader, template, template, &privateKey.PublicKey, privateKey)
	if err != nil {
		return fmt.Errorf("create self-signed certificate: %w", err)
	}

	if err := writePEMFile(keyPath, "RSA PRIVATE KEY", x509.MarshalPKCS1PrivateKey(privateKey)); err != nil {
		return fmt.Errorf("write private key: %w", err)
	}
	if err := writePEMFile(certPath, "CERTIFICATE", derBytes); err != nil {
		_ = os.Remove(keyPath)
		return fmt.Errorf("write certificate: %w", err)
	}
	return nil
}

func fileExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func localCertificateIPs() []net.IP {
	ips := []net.IP{net.ParseIP("127.0.0.1")}
	seen := map[string]bool{"127.0.0.1": true}
	interfaces, err := net.Interfaces()
	if err != nil {
		return ips
	}
	for _, iface := range interfaces {
		if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		addresses, err := iface.Addrs()
		if err != nil {
			continue
		}
		for _, address := range addresses {
			ipNet, ok := address.(*net.IPNet)
			if !ok || ipNet.IP.To4() == nil {
				continue
			}
			ip := ipNet.IP.To4()
			if !ip.IsUnspecified() && !seen[ip.String()] {
				seen[ip.String()] = true
				ips = append(ips, ip)
			}
		}
	}
	return ips
}

func writePEMFile(path, blockType string, bytes []byte) error {
	file, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o600)
	if err != nil {
		return err
	}
	if err := pem.Encode(file, &pem.Block{Type: blockType, Bytes: bytes}); err != nil {
		file.Close()
		_ = os.Remove(path)
		return err
	}
	if err := file.Close(); err != nil {
		_ = os.Remove(path)
		return err
	}
	return nil
}
