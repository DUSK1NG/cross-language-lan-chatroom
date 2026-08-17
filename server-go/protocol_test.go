package main

import (
	"bytes"
	"encoding/binary"
	"testing"
)

func TestFrameRoundTripKeepsBackToBackMessagesSeparate(t *testing.T) {
	var stream bytes.Buffer
	if err := writeFrame(&stream, []byte("Hello")); err != nil {
		t.Fatal(err)
	}
	if err := writeFrame(&stream, []byte("World")); err != nil {
		t.Fatal(err)
	}

	first, err := readFrame(&stream)
	if err != nil || string(first) != "Hello" {
		t.Fatalf("first frame = %q, err = %v", first, err)
	}
	second, err := readFrame(&stream)
	if err != nil || string(second) != "World" {
		t.Fatalf("second frame = %q, err = %v", second, err)
	}
}

func TestWriteFrameUsesBigEndianPayloadLength(t *testing.T) {
	var stream bytes.Buffer
	if err := writeFrame(&stream, []byte("Hello")); err != nil {
		t.Fatal(err)
	}

	wantHeader := []byte{0x00, 0x00, 0x00, 0x05}
	if !bytes.Equal(stream.Bytes()[:4], wantHeader) {
		t.Fatalf("frame header = %v, want %v", stream.Bytes()[:4], wantHeader)
	}
}

func TestWriteFrameRejectsOversizedPayload(t *testing.T) {
	payload := bytes.Repeat([]byte{'x'}, maxMessageSize+1)
	var stream bytes.Buffer

	if err := writeFrame(&stream, payload); err == nil {
		t.Fatal("expected oversized payload to be rejected")
	}
}

func TestReadFrameRejectsZeroLength(t *testing.T) {
	var stream bytes.Buffer
	var header [4]byte
	binary.BigEndian.PutUint32(header[:], 0)
	stream.Write(header[:])

	if _, err := readFrame(&stream); err == nil {
		t.Fatal("expected zero-length frame to be rejected")
	}
}

func TestReadFrameRejectsOversizedLength(t *testing.T) {
	var stream bytes.Buffer
	var header [4]byte
	binary.BigEndian.PutUint32(header[:], maxMessageSize+1)
	stream.Write(header[:])

	if _, err := readFrame(&stream); err == nil {
		t.Fatal("expected oversized frame to be rejected")
	}
}
