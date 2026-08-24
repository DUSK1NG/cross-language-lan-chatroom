package main

import (
	"testing"
	"time"
)

func TestInboundRateLimiterAllowsBurstThenRefills(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	limiter := newInboundRateLimiter(now)
	for index := 0; index < int(inboundMessageBurst); index++ {
		if !limiter.allow(now) {
			t.Fatalf("message %d in burst was rejected", index+1)
		}
	}
	if limiter.allow(now) {
		t.Fatal("message beyond burst was accepted")
	}
	if !limiter.allow(now.Add(125 * time.Millisecond)) {
		t.Fatal("limiter did not refill one token")
	}
}

func TestInboundRateLimiterDoesNotGrantTokensWhenClockMovesBackwards(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	limiter := newInboundRateLimiter(now)
	for index := 0; index < int(inboundMessageBurst); index++ {
		if !limiter.allow(now) {
			t.Fatalf("message %d in burst was rejected", index+1)
		}
	}
	if limiter.allow(now.Add(-time.Second)) {
		t.Fatal("backward clock movement granted a token")
	}
}
