package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net"
	"os"
	"sync"
	"time"
)

const (
	lanDiscoveryPort     = 38888
	lanDiscoveryService  = "lan-chat"
	lanDiscoveryVersion  = 1
	lanDiscoveryInterval = time.Second
)

// lanDiscoveryAnnouncement contains only public, local-network metadata. The
// certificate is intentionally included so a member can pin it after an
// explicit join action; the private key never leaves the host machine.
type lanDiscoveryAnnouncement struct {
	Service           string `json:"service"`
	Version           int    `json:"version"`
	HostName          string `json:"hostName"`
	Port              int    `json:"port"`
	CertificateBase64 string `json:"certificateBase64"`
	FingerprintSHA256 string `json:"fingerprintSha256"`
}

func buildLanDiscoveryAnnouncement(certificatePEM []byte, hostName string, port int) (lanDiscoveryAnnouncement, error) {
	if len(certificatePEM) == 0 {
		return lanDiscoveryAnnouncement{}, fmt.Errorf("discovery certificate is empty")
	}
	if len(certificatePEM) > 16*1024 {
		return lanDiscoveryAnnouncement{}, fmt.Errorf("discovery certificate exceeds 16 KiB")
	}
	if hostName == "" {
		hostName = "LAN Chat Host"
	}
	if port < 1 || port > 65535 {
		return lanDiscoveryAnnouncement{}, fmt.Errorf("invalid discovery port %d", port)
	}
	fingerprint := sha256.Sum256(certificatePEM)
	return lanDiscoveryAnnouncement{
		Service:           lanDiscoveryService,
		Version:           lanDiscoveryVersion,
		HostName:          hostName,
		Port:              port,
		CertificateBase64: base64.StdEncoding.EncodeToString(certificatePEM),
		FingerprintSHA256: hex.EncodeToString(fingerprint[:]),
	}, nil
}

// broadcastAddressForIPv4 returns the directed broadcast address for a usable
// IPv4 interface. Virtual-LAN addresses (such as Radmin's 26.0.0.0/8) are
// intentionally treated the same as private LAN addresses. Loopback,
// link-local, unspecified, and host-route interfaces cannot discover peers.
func broadcastAddressForIPv4(ip net.IP, mask net.IPMask) (net.IP, bool) {
	ipv4 := ip.To4()
	ones, bits := mask.Size()
	if ipv4 == nil || bits != net.IPv4len*8 || ones < 0 || ones >= net.IPv4len*8 ||
		ipv4.IsLoopback() || ipv4.IsLinkLocalUnicast() || ipv4.IsUnspecified() {
		return nil, false
	}
	broadcast := make(net.IP, net.IPv4len)
	for index := range broadcast {
		broadcast[index] = ipv4[index] | ^mask[index]
	}
	return broadcast, !broadcast.IsUnspecified()
}

func localBroadcastAddresses() []net.IP {
	addresses := []net.IP{net.IPv4bcast}
	seen := map[string]bool{net.IPv4bcast.String(): true}
	interfaces, err := net.Interfaces()
	if err != nil {
		return addresses
	}
	for _, iface := range interfaces {
		if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		interfaceAddresses, err := iface.Addrs()
		if err != nil {
			continue
		}
		for _, address := range interfaceAddresses {
			ipNet, ok := address.(*net.IPNet)
			if !ok || ipNet.IP.To4() == nil || len(ipNet.Mask) != net.IPv4len {
				continue
			}
			broadcast, ok := broadcastAddressForIPv4(ipNet.IP, ipNet.Mask)
			if !ok || seen[broadcast.String()] {
				continue
			}
			seen[broadcast.String()] = true
			addresses = append(addresses, broadcast)
		}
	}
	return addresses
}

// startLanDiscovery sends a compact, best-effort announcement once per second
// on the local IPv4 broadcast domains. Failure to advertise never affects the
// TLS chat listener: members can still use the manual connection form.
func startLanDiscovery(certPath string, port int, displayName string) (func(), error) {
	certificatePEM, err := os.ReadFile(certPath)
	if err != nil {
		return nil, fmt.Errorf("read discovery certificate: %w", err)
	}
	if displayName == "" {
		displayName, err = os.Hostname()
		if err != nil || displayName == "" {
			displayName = "LAN Chat Host"
		}
	}
	announcement, err := buildLanDiscoveryAnnouncement(certificatePEM, displayName, port)
	if err != nil {
		return nil, err
	}
	payload, err := json.Marshal(announcement)
	if err != nil {
		return nil, fmt.Errorf("encode discovery announcement: %w", err)
	}

	connection, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4zero, Port: 0})
	if err != nil {
		return nil, fmt.Errorf("open discovery socket: %w", err)
	}
	done := make(chan struct{})
	var stopOnce sync.Once
	send := func() {
		for _, address := range localBroadcastAddresses() {
			_, _ = connection.WriteToUDP(payload, &net.UDPAddr{IP: address, Port: lanDiscoveryPort})
		}
	}
	go func() {
		defer connection.Close()
		send()
		ticker := time.NewTicker(lanDiscoveryInterval)
		defer ticker.Stop()
		for {
			select {
			case <-ticker.C:
				send()
			case <-done:
				return
			}
		}
	}()
	return func() { stopOnce.Do(func() { close(done) }) }, nil
}
