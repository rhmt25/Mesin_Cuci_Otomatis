// Worker: login, sesi, allowlist, halaman UI jarak jauh, dan relay ke ESP32 lewat Durable Object.
import { verifyPassword, makeSession, verifySession, parseCookie, timingSafeEqual } from "./auth.js";
import { isAllowed } from "./allow.js";

export { Relay } from "./relay.js";

const SESSION_REMEMBER_SEC = 400 * 24 * 3600; // 400 hari (batas cookie browser), diperpanjang tiap dipakai
const SESSION_TEMP_SEC = 24 * 3600;

const jsonResp = (obj, status = 200, extra = {}) =>
  new Response(JSON.stringify(obj), {
    status,
    headers: { "content-type": "application/json", "cache-control": "no-store", "x-content-type-options": "nosniff", ...extra },
  });

const relayStub = (env) => env.RELAY.get(env.RELAY.idFromName("mesin"));

function cookieName(url) {
  return url.protocol === "https:" ? "__Host-sid" : "sid"; // __Host- mewajibkan HTTPS
}
function buildCookie(url, value, maxAge) {
  const secure = url.protocol === "https:" ? "; Secure" : "";
  const age = maxAge === null ? "" : `; Max-Age=${maxAge}`;
  return `${cookieName(url)}=${value}; Path=/; HttpOnly; SameSite=Strict${secure}${age}`;
}

async function isAuthed(request, env, url) {
  return verifySession(env.SESSION_SECRET, parseCookie(request.headers.get("Cookie"), cookieName(url)));
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname;

    // ESP32: koneksi WebSocket permanen
    if (path === "/device") {
      const auth = request.headers.get("Authorization") || "";
      if (!env.DEVICE_TOKEN || !timingSafeEqual(auth, `Bearer ${env.DEVICE_TOKEN}`)) return new Response("unauthorized", { status: 401 });
      return relayStub(env).fetch(request);
    }

    if (!path.startsWith("/api/")) return env.ASSETS.fetch(request); // UI statis

    // Perlindungan CSRF sederhana: permintaan yang mengubah data harus berasal dari origin yang sama
    if (request.method !== "GET") {
      const origin = request.headers.get("Origin");
      if (origin && origin !== url.origin) return jsonResp({ ok: false, msg: "Origin ditolak" }, 403);
    }

    if (path === "/api/login" && request.method === "POST") return login(request, env, url);
    if (path === "/api/logout" && request.method === "POST") {
      return jsonResp({ ok: true }, 200, { "set-cookie": buildCookie(url, "", 0) });
    }

    if (!(await isAuthed(request, env, url))) return jsonResp({ ok: false, msg: "login" }, 401);

    let body = null;
    if (request.method === "POST") {
      try { body = await request.json(); } catch { return jsonResp({ ok: false, msg: "JSON tidak valid" }, 400); }
    }
    const allow = isAllowed(request.method, path, body);
    if (!allow.ok) return jsonResp({ ok: false, msg: allow.reason }, 403);

    const stub = relayStub(env);
    let resp;
    if (path === "/api/info") resp = await stub.fetch("https://relay/info");
    else if (path === "/api/poll") resp = await stub.fetch(`https://relay/poll?since=${encodeURIComponent(url.searchParams.get("since") || "0")}`);
    else {
      resp = await stub.fetch("https://relay/rpc", {
        method: "POST",
        body: JSON.stringify({ m: request.method, p: path, q: Object.fromEntries(url.searchParams), b: body }),
      });
    }
    // perpanjang sesi (cookie "ingat") tiap dipakai
    const out = new Response(resp.body, resp);
    out.headers.set("cache-control", "no-store");
    const tok = parseCookie(request.headers.get("Cookie"), cookieName(url));
    if (tok) out.headers.append("set-cookie", buildCookie(url, tok, SESSION_REMEMBER_SEC));
    return out;
  },
};

async function login(request, env, url) {
  const ip = request.headers.get("CF-Connecting-IP") || "local";
  const stub = relayStub(env);
  const rl = async (op) =>
    (await stub.fetch("https://relay/rl", { method: "POST", body: JSON.stringify({ op, ip }) })).json();

  const state = await rl("check");
  if (state.locked) return jsonResp({ ok: false, msg: `Terlalu banyak percobaan. Coba lagi dalam ${Math.ceil((state.retryMs || 60000) / 60000)} menit.` }, 429);

  let body;
  try { body = await request.json(); } catch { return jsonResp({ ok: false, msg: "JSON tidak valid" }, 400); }
  const good = await verifyPassword(String(body.password || ""), env.REMOTE_PASSWORD_HASH);
  if (!good) {
    const r = await rl("fail");
    return jsonResp({ ok: false, msg: r.locked ? `Terlalu banyak percobaan. Dikunci ${Math.ceil((r.retryMs || 60000) / 60000)} menit.` : "Password salah" }, r.locked ? 429 : 401);
  }
  await rl("ok");
  const remember = !!body.remember;
  const token = await makeSession(env.SESSION_SECRET, remember ? SESSION_REMEMBER_SEC : SESSION_TEMP_SEC);
  return jsonResp({ ok: true }, 200, { "set-cookie": buildCookie(url, token, remember ? SESSION_REMEMBER_SEC : null) });
}
