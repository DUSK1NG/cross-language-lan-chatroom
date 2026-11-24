package dev.dusk1ng.lanchat;

import android.util.Base64;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.Closeable;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.LongConsumer;

import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

final class ChatConnection implements Closeable {
    static final String HOST = "1.15.129.115";
    static final int PORT = 8888;
    private static final int MAX_FRAME_BYTES = 65536;
    static final int MAX_TEXT_BYTES = MAX_FRAME_BYTES - 2048;
    static final int MAX_FILE_BYTES = 32 * 1024 * 1024;
    static final int CHUNK_BYTES = 40 * 1024;

    private final SSLSocket socket;
    private final DataInputStream input;
    private final DataOutputStream output;
    private final byte[] readHeader = new byte[Integer.BYTES];
    private final byte[] writeHeader = new byte[Integer.BYTES];
    private final Map<String, CompletableFuture<JSONObject>> requests = new ConcurrentHashMap<>();

    static ChatConnection open() throws IOException {
        Socket tcp = new Socket();
        try {
            tcp.connect(new InetSocketAddress(HOST, PORT), 10000);
            SSLSocketFactory factory = (SSLSocketFactory) SSLSocketFactory.getDefault();
            SSLSocket tls = (SSLSocket) factory.createSocket(tcp, HOST, PORT, true);
            SSLParameters parameters = tls.getSSLParameters();
            parameters.setEndpointIdentificationAlgorithm("HTTPS");
            tls.setSSLParameters(parameters);
            tls.setSoTimeout(15000);
            tls.startHandshake();
            return new ChatConnection(tls);
        } catch (IOException error) {
            try {
                tcp.close();
            } catch (IOException closeError) {
                error.addSuppressed(closeError);
            }
            throw error;
        }
    }

    private ChatConnection(SSLSocket socket) throws IOException {
        this.socket = socket;
        input = new DataInputStream(new BufferedInputStream(socket.getInputStream()));
        output = new DataOutputStream(new BufferedOutputStream(socket.getOutputStream()));
    }

    static final class AuthenticationException extends IOException {
        AuthenticationException(String message) { super(message); }
    }

    record Attachment(String id, String name, long size) {
        static Attachment read(JSONObject packet) throws JSONException {
            String id = packet.getString("attachment_id");
            String name = packet.getString("file_name");
            long size = packet.optLong("logical_size", 0);
            if (!id.matches("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}") || !validName(name)
                    || size < 0 || size > MAX_FILE_BYTES) {
                throw new JSONException("服务器返回了无效的附件编号、文件名或大小");
            }
            return new Attachment(id, name, size);
        }

        static boolean validName(String name) {
            return name != null && !name.isEmpty() && !name.endsWith(".") && !name.endsWith(" ")
                    && name.getBytes(StandardCharsets.UTF_8).length <= 255
                    && name.codePoints().noneMatch(c -> Character.isISOControl(c) || "<>:\"/\\|?*".indexOf(c) >= 0);
        }
    }

    JSONObject login(String username, String password, String code) throws JSONException, IOException {
        JSONObject request = packet(code == null ? "account_login" : "account_register");
        request.put("username", username);
        request.put("account_password", password);
        if (code != null) request.put("user_code", code);
        request.put("capabilities", new JSONArray().put("online-group-management-v1")
                .put("online-group-attachments-v1"));
        send(request);
        JSONObject response = receive();
        if (!response.getString("type").equals("login_ok")) {
            String reason = response.getString("content");
            if ("用户名或密码不正确".equals(reason)) throw new AuthenticationException(reason);
            throw new IOException(reason);
        }
        // The foreground client requests its contact list every two minutes.
        socket.setSoTimeout(150000);
        return response;
    }

    JSONObject receive() throws IOException, JSONException {
        input.readFully(readHeader);
        int length = TransportCodec.readFrameLength(readHeader);
        byte[] payload = new byte[length];
        input.readFully(payload);
        return new JSONObject(TransportCodec.decodePayload(payload));
    }

    boolean completeRequest(JSONObject response) throws JSONException {
        CompletableFuture<JSONObject> result = requests.get(response.optString("command_id"));
        if (result == null) return false;
        if ("error".equals(response.getString("type"))) {
            result.completeExceptionally(new IOException(response.getString("content")));
        } else {
            result.complete(response);
        }
        return true;
    }

    JSONObject request(JSONObject packet) throws IOException, JSONException {
        String id = UUID.randomUUID().toString();
        packet.put("command_id", id);
        CompletableFuture<JSONObject> pending = new CompletableFuture<>();
        requests.put(id, pending);
        try {
            send(packet);
            JSONObject response = pending.get(15, TimeUnit.SECONDS);
            if (!packet.getString("type").equals(response.getString("type"))) {
                throw new IOException("服务器响应类型与请求不符");
            }
            return response;
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IOException("请求已中断", error);
        } catch (ExecutionException error) {
            throw new IOException(error.getCause().getMessage(), error.getCause());
        } catch (TimeoutException error) {
            throw new IOException("服务器没有确认这次请求", error);
        } finally {
            requests.remove(id);
        }
    }

    void uploadAttachment(File source, String name, String peer, boolean group, LongConsumer progress)
            throws IOException, JSONException {
        long size = source.length();
        JSONObject init = request(packet("dm_file_init").put("file_name", name)
                .put(group ? "group_id" : "target_user_code", peer).put("logical_size", size));
        String id = init.getString("attachment_id");
        if (!id.matches("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}")
                || init.getInt("chunk_size") != CHUNK_BYTES) {
            throw new IOException("服务器返回了无效的上传编号或分块大小");
        }
        try (DataInputStream file = new DataInputStream(new FileInputStream(source))) {
            byte[] buffer = new byte[CHUNK_BYTES];
            for (int index = 0; (long) index * CHUNK_BYTES < size; index++) {
                int length = TransportCodec.chunkLength(size, index);
                file.readFully(buffer, 0, length);
                JSONObject ack = request(packet("dm_file_chunk").put("attachment_id", id).put("chunk_index", index)
                        .put("file_data", Base64.encodeToString(buffer, 0, length, Base64.NO_WRAP)));
                if (!id.equals(ack.getString("attachment_id")) || ack.optInt("chunk_index", 0) != index) {
                    throw new IOException("服务器确认的附件分块与请求不符");
                }
                progress.accept((long) index * CHUNK_BYTES + length);
            }
        }
        JSONObject ack = request(packet("dm_file_commit").put("attachment_id", id));
        if (!id.equals(ack.getString("attachment_id"))) throw new IOException("服务器确认的附件与请求不符");
    }

    void downloadAttachment(Attachment attachment, File target, LongConsumer progress) throws IOException, JSONException {
        try (FileOutputStream file = new FileOutputStream(target)) {
            long received = 0;
            int index = 0;
            do {
                JSONObject chunk = request(packet("dm_file_get").put("attachment_id", attachment.id()).put("chunk_index", index));
                byte[] bytes = Base64.decode(chunk.optString("file_data"), Base64.NO_WRAP);
                if (!attachment.equals(Attachment.read(chunk)) || chunk.optInt("chunk_index", 0) != index
                        || chunk.getInt("chunk_size") != CHUNK_BYTES
                        || bytes.length != TransportCodec.chunkLength(attachment.size(), index)) {
                    throw new IOException("下载的附件分块与请求不符");
                }
                file.write(bytes);
                received += bytes.length;
                progress.accept(received);
                index++;
            } while (received < attachment.size());
        } catch (IllegalArgumentException error) {
            throw new IOException("服务器返回了无效的附件编码", error);
        }
    }

    synchronized void send(JSONObject packet) throws IOException {
        byte[] payload = packet.toString().getBytes(StandardCharsets.UTF_8);
        TransportCodec.writeFrameLength(payload.length, writeHeader);
        output.write(writeHeader);
        output.write(payload);
        output.flush();
    }

    static JSONObject packet(String type) throws JSONException {
        return new JSONObject().put("type", type);
    }

    @Override
    public void close() throws IOException {
        for (CompletableFuture<JSONObject> pending : requests.values()) {
            pending.completeExceptionally(new IOException("连接已断开"));
        }
        socket.close();
    }
}
