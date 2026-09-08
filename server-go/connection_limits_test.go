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

func TestAttachmentRateLimiterPacesWithoutDiscardingFrames(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	var limiter attachmentRateLimiter
	for i := 0; i < int(attachmentByteBurst/maxMessageSize); i++ {
		if delay := limiter.delay(now); delay != 0 {
			t.Fatalf("burst frame %d delayed by %v", i, delay)
		}
	}
	const frameTime = time.Duration(float64(maxMessageSize) / attachmentBytesPerSecond * float64(time.Second))
	if delay := limiter.delay(now); delay != frameTime {
		t.Fatalf("excess frame delay = %v, want %v", delay, frameTime)
	}
	if delay := limiter.delay(now.Add(time.Second)); delay != 0 {
		t.Fatalf("refilled frame delayed by %v", delay)
	}
}
