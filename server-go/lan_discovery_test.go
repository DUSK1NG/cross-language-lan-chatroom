package main

import (
	"crypto/sha256"
	"encoding/hex"
	"net"
	"testing"
)

func TestBuildLanDiscoveryAnnouncementIncludesOnlyPublicCertificateData(t *testing.T) {
	certificate := []byte("-----BEGIN CERTIFICATE-----\npublic\n-----END CERTIFICATE-----\n")
	announcement, err := buildLanDiscoveryAnnouncement(certificate, "Alice's PC", 8888)
	if err != nil {
		t.Fatalf("buildLanDiscoveryAnnouncement returned error: %v", err)
	}

	expectedFingerprint := sha256.Sum256(certificate)
	if announcement.Service != lanDiscoveryService || announcement.Version != lanDiscoveryVersion {
		t.Fatalf("unexpected discovery protocol: %+v", announcement)
	}
	if announcement.HostName != "Alice's PC" || announcement.Port != 8888 {
		t.Fatalf("unexpected host metadata: %+v", announcement)
	}
	if announcement.FingerprintSHA256 != hex.EncodeToString(expectedFingerprint[:]) {
		t.Fatalf("fingerprint = %q", announcement.FingerprintSHA256)
	}
	if announcement.CertificateBase64 == "" {
		t.Fatal("certificate was not included")
	}
}

func TestBuildLanDiscoveryAnnouncementRejectsInvalidInput(t *testing.T) {
	if _, err := buildLanDiscoveryAnnouncement(nil, "Host", 8888); err == nil {
		t.Fatal("empty certificate was accepted")
	}
	if _, err := buildLanDiscoveryAnnouncement([]byte("certificate"), "Host", 0); err == nil {
		t.Fatal("invalid port was accepted")
	}
}

func TestBroadcastAddressForIPv4KeepsVirtualLanAndRejectsUnsafeAddresses(t *testing.T) {
	tests := []struct {
		name string
		ip   string
		mask string
		want string
		ok   bool
	}{
		{name: "radmin virtual lan", ip: "26.12.34.56", mask: "255.0.0.0", want: "26.255.255.255", ok: true},
		{name: "private lan", ip: "192.168.10.8", mask: "255.255.255.0", want: "192.168.10.255", ok: true},
		{name: "loopback", ip: "127.0.0.1", mask: "255.0.0.0", ok: false},
		{name: "link local", ip: "169.254.1.2", mask: "255.255.0.0", ok: false},
		{name: "proxy benchmark network", ip: "198.18.0.1", mask: "255.255.255.252", ok: false},
		{name: "unspecified", ip: "0.0.0.0", mask: "0.0.0.0", ok: false},
		{name: "host route", ip: "26.12.34.56", mask: "255.255.255.255", ok: false},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			broadcast, ok := broadcastAddressForIPv4(net.ParseIP(test.ip), net.IPMask(net.ParseIP(test.mask).To4()))
			if ok != test.ok {
				t.Fatalf("ok = %v, want %v", ok, test.ok)
			}
			if ok && broadcast.String() != test.want {
				t.Fatalf("broadcast = %s, want %s", broadcast, test.want)
			}
		})
	}
}
