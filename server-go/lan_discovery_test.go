package main

import (
	"crypto/sha256"
	"encoding/hex"
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
