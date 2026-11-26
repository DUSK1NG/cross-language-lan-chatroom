package dev.dusk1ng.lanchat;

import android.os.SystemClock;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;

final class AstrBot {
    enum Resource {
        SOURCE("provider-sources", "source_id", "provider_source", "provider_sources", "id"),
        PROVIDER("providers", "provider_id", "provider", "providers", "id"),
        PERSONA("personas", "persona_id", "", "", "persona_id");

        final String route;
        final String idField;
        final String dataField;
        final String listField;
        final String idKey;

        Resource(String route, String idField, String dataField, String listField, String idKey) {
            this.route = route;
            this.idField = idField;
            this.dataField = dataField;
            this.listField = listField;
            this.idKey = idKey;
        }
    }

    private final URI base;
    private final String token;

    AstrBot(String endpoint, String token) throws IOException {
        base = parseUri(endpoint, "AstrBot 地址格式无效");
        if (!"https".equalsIgnoreCase(base.getScheme()) || base.getHost() == null || base.getPort() > 65535
                || (!base.getRawPath().isEmpty() && !base.getRawPath().equals("/"))
                || base.getUserInfo() != null || base.getQuery() != null || base.getFragment() != null) {
            throw new IOException("AstrBot 地址须为 HTTPS 根地址");
        }
        if (token.isEmpty() || token.length() > 1024 || !token.chars().allMatch(ch -> ch >= 33 && ch <= 126)) {
            throw new IOException("请填写 AstrBot OpenAPI Key");
        }
        this.token = token;
    }

    String[] list(Resource resource) throws IOException, JSONException {
        JSONObject response = exchange("GET", resource.route, null);
        JSONArray rows = resource == Resource.PERSONA ? response.getJSONArray("data")
                : response.getJSONObject("data").getJSONArray(resource.listField);
        String[] ids = new String[rows.length()];
        for (int i = 0; i < ids.length; i++) ids[i] = text(rows.getJSONObject(i), resource.idKey);
        return ids;
    }

    JSONObject read(Resource resource, String id) throws IOException, JSONException {
        JSONObject data = readRaw(resource, id);
        return switch (resource) {
            case SOURCE -> new JSONObject().put("api_base", text(data, "api_base"));
            case PROVIDER -> new JSONObject().put("model", text(data, "model"));
            case PERSONA -> new JSONObject().put("system_prompt", text(data, "system_prompt"))
                    .put("custom_error_message", data.isNull("custom_error_message") ? ""
                            : text(data, "custom_error_message"));
        };
    }

    void save(Resource resource, String id, JSONObject fields, boolean create) throws IOException, JSONException {
        requireId(id);
        if (create) {
            for (String existing : list(resource)) {
                if (id.equals(existing)) throw new IOException("配置编号已存在，请读取后编辑");
            }
        }
        JSONObject body = new JSONObject();
        switch (resource) {
            case SOURCE -> {
                String apiBase = text(fields, "api_base");
                String apiKey = text(fields, "api_key");
                URI uri = parseUri(apiBase, "模型 API 地址无效");
                if (!("http".equalsIgnoreCase(uri.getScheme()) || "https".equalsIgnoreCase(uri.getScheme()))
                        || uri.getHost() == null || uri.getUserInfo() != null) {
                    throw new IOException("模型 API 地址须为 HTTP(S) 地址");
                }
                JSONObject config = create ? new JSONObject().put("id", id).put("provider", "openai")
                        .put("type", "openai_chat_completion").put("provider_type", "chat_completion")
                        .put("enable", true).put("key", new JSONArray()).put("timeout", 120)
                        .put("proxy", "").put("custom_headers", new JSONObject()) : readRaw(resource, id);
                Object oldKey = config.get("key");
                boolean multipleKeys = oldKey instanceof JSONArray;
                boolean emptyKey = multipleKeys ? ((JSONArray) oldKey).length() == 0 : "".equals(oldKey);
                if (!multipleKeys && !(oldKey instanceof String)) {
                    throw new IOException("此模型源没有受支持的 API Key 字段");
                }
                if (!create && !apiBase.equals(config.get("api_base")) && !emptyKey && apiKey.isEmpty()) {
                    throw new IOException("修改模型 API 地址时，请同时填写新地址使用的 API Key");
                }
                config.put("api_base", apiBase);
                if (!apiKey.isEmpty()) config.put("key", multipleKeys ? new JSONArray().put(apiKey) : apiKey);
                body.put("source_id", id).put("config", config);
            }
            case PROVIDER -> {
                String model = text(fields, "model");
                if (model.trim().isEmpty()) throw new IOException("请填写模型名称");
                JSONObject config;
                if (create) {
                    String sourceId = text(fields, "source_id");
                    if (sourceId.isEmpty()) throw new IOException("新建模型前请选择模型源");
                    config = new JSONObject().put("id", id).put("provider_source_id", sourceId).put("enable", true);
                } else {
                    config = readRaw(resource, id);
                    body.put("provider_id", id);
                }
                body.put("config", config.put("model", model));
            }
            case PERSONA -> {
                String prompt = text(fields, "system_prompt");
                if (prompt.trim().isEmpty()) throw new IOException("请填写系统提示词");
                body.put("persona_id", id).put("system_prompt", prompt)
                        .put("custom_error_message", text(fields, "custom_error_message"));
            }
        }
        if (create && resource != Resource.SOURCE) exchange("POST", resource.route, body);
        else exchange("PUT", resource.route + "/by-id", body);
    }

    private JSONObject readRaw(Resource resource, String id) throws IOException, JSONException {
        requireId(id);
        String query = resource.idField + "=" + URLEncoder.encode(id, "UTF-8");
        JSONObject data = exchange("GET", resource.route + "/by-id?" + query, null).getJSONObject("data");
        if (!resource.dataField.isEmpty()) data = data.getJSONObject(resource.dataField);
        if (!id.equals(text(data, resource.idKey))) throw new IOException("AstrBot 返回了不同的配置编号");
        return data;
    }

    private JSONObject exchange(String method, String path, JSONObject body) throws IOException, JSONException {
        HttpURLConnection request = (HttpURLConnection) base.resolve("/api/v1/" + path).toURL().openConnection();
        request.setInstanceFollowRedirects(false);
        request.setConnectTimeout(12_000);
        request.setReadTimeout(12_000);
        request.setRequestMethod(method);
        request.setRequestProperty("Authorization", "Bearer " + token);
        try {
            if (body != null) {
                byte[] data = body.toString().getBytes(StandardCharsets.UTF_8);
                request.setDoOutput(true);
                request.setRequestProperty("Content-Type", "application/json");
                request.setFixedLengthStreamingMode(data.length);
                try (OutputStream output = request.getOutputStream()) {
                    output.write(data);
                }
            }
            int status = request.getResponseCode();
            if (status < 200 || status >= 300) throw new IOException("AstrBot 请求失败（HTTP " + status + "）");
            try (InputStream input = request.getInputStream()) {
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                byte[] buffer = new byte[4096];
                long deadline = SystemClock.elapsedRealtime() + 12_000;
                for (int count; (count = input.read(buffer)) != -1;) {
                    if (bytes.size() + count > 2 * 1024 * 1024 || SystemClock.elapsedRealtime() > deadline) {
                        throw new IOException("AstrBot 响应过大或读取超时");
                    }
                    bytes.write(buffer, 0, count);
                }
                JSONObject response = new JSONObject(bytes.toString("UTF-8"));
                if (!"ok".equals(response.opt("status"))) throw new IOException("AstrBot 拒绝了配置操作");
                return response;
            }
        } finally {
            request.disconnect();
        }
    }

    private static URI parseUri(String value, String message) throws IOException {
        try {
            return new URI(value);
        } catch (URISyntaxException error) {
            throw new IOException(message);
        }
    }

    private static void requireId(String id) throws IOException {
        if (id.isEmpty() || !id.equals(id.trim()) || id.getBytes(StandardCharsets.UTF_8).length > 256
                || id.chars().anyMatch(Character::isISOControl)) {
            throw new IOException("请填写配置编号，首尾不能有空格");
        }
    }

    private static String text(JSONObject object, String field) throws JSONException {
        Object value = object.get(field);
        if (value instanceof String text) return text;
        throw new JSONException("AstrBot 配置字段 " + field + " 应为文本");
    }
}
