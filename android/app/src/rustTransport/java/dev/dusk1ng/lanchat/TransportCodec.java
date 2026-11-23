package dev.dusk1ng.lanchat;

import java.io.IOException;

final class TransportCodec {
    static {
        System.loadLibrary("lanchat_transport");
    }

    private TransportCodec() {}

    static native int readFrameLength(byte[] header) throws IOException;
    static native void writeFrameLength(int length, byte[] header) throws IOException;
    static native String decodePayload(byte[] payload) throws IOException;
    static native int chunkLength(long size, int index) throws IOException;
}
