package main

import (
	"encoding/binary"
	"fmt"
	"io"
)

const maxMessageSize = 64 * 1024

func writeFull(writer io.Writer, data []byte) error {
	for len(data) > 0 {
		written, err := writer.Write(data)
		if written < 0 || written > len(data) {
			return io.ErrShortWrite
		}
		data = data[written:]
		if err != nil {
			return err
		}
		if written == 0 {
			return io.ErrShortWrite
		}
	}
	return nil
}

func writeFrame(writer io.Writer, payload []byte) error {
	if len(payload) == 0 {
		return fmt.Errorf("payload must not be empty")
	}
	if len(payload) > maxMessageSize {
		return fmt.Errorf("payload is too large: %d bytes", len(payload))
	}

	var header [4]byte
	binary.BigEndian.PutUint32(header[:], uint32(len(payload)))
	if err := writeFull(writer, header[:]); err != nil {
		return fmt.Errorf("write frame length: %w", err)
	}
	if err := writeFull(writer, payload); err != nil {
		return fmt.Errorf("write frame payload: %w", err)
	}
	return nil
}

func readFrame(reader io.Reader) ([]byte, error) {
	var header [4]byte
	if _, err := io.ReadFull(reader, header[:]); err != nil {
		return nil, fmt.Errorf("read frame length: %w", err)
	}

	payloadLength := binary.BigEndian.Uint32(header[:])
	if payloadLength == 0 {
		return nil, fmt.Errorf("payload must not be empty")
	}
	if payloadLength > maxMessageSize {
		return nil, fmt.Errorf("payload is too large: %d bytes", payloadLength)
	}

	payload := make([]byte, payloadLength)
	if _, err := io.ReadFull(reader, payload); err != nil {
		return nil, fmt.Errorf("read frame payload: %w", err)
	}
	return payload, nil
}
