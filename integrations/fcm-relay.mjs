const routes = new Map([
  ["/token", ["https://oauth2.googleapis.com/token", "application/x-www-form-urlencoded"]],
  ["/messages:send", ["https://fcm.googleapis.com/v1/projects/lan-chat-9125d/messages:send", "application/json"]],
]);
const maxBodyBytes = 16 * 1024;

export default {
  async fetch(request, env) {
    const fail = (status, message) => new Response(message, { status, headers: { "Cache-Control": "no-store" } });
    if (typeof env.RELAY_SECRET !== "string" || !/^[\x21-\x7e]{32,256}$/.test(env.RELAY_SECRET)) return fail(503, "Relay secret is not configured");
    const supplied = new TextEncoder().encode(request.headers.get("X-Relay-Secret") || "");
    const expected = new TextEncoder().encode(env.RELAY_SECRET);
    if (supplied.length !== expected.length || !crypto.subtle.timingSafeEqual(supplied, expected)) return fail(401, "Unauthorized");
    if (request.method !== "POST") return fail(405, "POST required");
    const url = new URL(request.url);
    const route = routes.get(url.pathname);
    if (url.search || !route) return fail(404, "Unknown route");
    if (request.headers.get("Content-Type")?.split(";", 1)[0].trim().toLowerCase() !== route[1]) return fail(415, "Unsupported content type");
    if (!request.body) return fail(400, "Body required");
    const reader = request.body.getReader();
    const chunks = [];
    let size = 0;
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      size += value.byteLength;
      if (size > maxBodyBytes) {
        await reader.cancel();
        return fail(413, "Body exceeds 16384 bytes");
      }
      chunks.push(value);
    }
    const body = new Uint8Array(size);
    let offset = 0;
    for (const chunk of chunks) { body.set(chunk, offset); offset += chunk.byteLength; }
    const headers = new Headers({ "Content-Type": route[1] });
    if (url.pathname === "/messages:send") {
      const authorization = request.headers.get("Authorization");
      if (!authorization || !/^Bearer [A-Za-z0-9._~+\/-]+=*$/.test(authorization)) return fail(401, "Bearer token required");
      headers.set("Authorization", authorization);
    }
    const upstream = await fetch(route[0], { method: "POST", headers, body, redirect: "manual" });
    if (upstream.status >= 300 && upstream.status < 400) {
      await upstream.body?.cancel();
      return fail(502, "Upstream redirect rejected");
    }
    return new Response(upstream.body, { status: upstream.status, headers: {
      "Content-Type": upstream.headers.get("Content-Type") || "application/json",
      "Cache-Control": "no-store",
    } });
  },
};
