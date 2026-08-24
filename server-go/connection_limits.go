package main

import (
	"net"
	"time"
)

const (
	defaultMaxConcurrentClients = 64
	loginDeadline               = 10 * time.Second
	idleReadDeadline            = 5 * time.Minute
	writeDeadline               = 10 * time.Second
	inboundMessagesPerSecond    = 8.0
	inboundMessageBurst         = 16.0
)

// inboundRateLimiter is deliberately connection-local. It keeps a single
// client from monopolizing the Hub without introducing shared locks or global
// state into the message path.
type inboundRateLimiter struct {
	tokens float64
	last   time.Time
}

func newInboundRateLimiter(now time.Time) inboundRateLimiter {
	return inboundRateLimiter{tokens: inboundMessageBurst, last: now}
}

func (l *inboundRateLimiter) allow(now time.Time) bool {
	if l.last.IsZero() {
		*l = newInboundRateLimiter(now)
	}
	if elapsed := now.Sub(l.last); elapsed > 0 {
		l.tokens += elapsed.Seconds() * inboundMessagesPerSecond
		if l.tokens > inboundMessageBurst {
			l.tokens = inboundMessageBurst
		}
		l.last = now
	}
	if l.tokens < 1 {
		return false
	}
	l.tokens--
	return true
}

func setLoginDeadline(conn net.Conn) {
	if conn != nil {
		_ = conn.SetDeadline(time.Now().Add(loginDeadline))
	}
}

func setReadDeadline(conn net.Conn) {
	if conn != nil {
		_ = conn.SetReadDeadline(time.Now().Add(idleReadDeadline))
	}
}

func setWriteDeadline(conn net.Conn) {
	if conn != nil {
		_ = conn.SetWriteDeadline(time.Now().Add(writeDeadline))
	}
}
