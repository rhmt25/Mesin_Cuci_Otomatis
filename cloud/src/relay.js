// Durable Object "Relay": satu instans untuk satu mesin.
// - menerima SATU koneksi WebSocket dari ESP32 (WebSocket Hibernation: tidak ditagih saat diam)
// - menyimpan cache status/log terakhir (browser tidak pernah membangunkan ESP32 untuk membaca)
// - meneruskan permintaan API ke ESP32 (RPC) dan menunggu balasan
// - mengirim notifikasi Telegram dan alarm "mesin tidak terhubung"
import { DurableObject } from "cloudflare:workers";
import { sendTelegram, wibTime } from "./telegram.js";

const MAX_LOGS = 60;
const VIEWER_PING_MS = 5000;
const NOTIF_BATCH_MS = 2000;

const json = (obj, status = 200) =>
  new Response(JSON.stringify(obj), { status, headers: { "content-type": "application/json", "cache-control": "no-store" } });

export class Relay extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.env = env;
    this.pending = new Map();
    this.reqId = 0;
    this.loaded = false;
    this.state = null;
    this.notifBuf = [];
    this.flushing = false;
    this.lastViewerSent = 0;
  }

  async load() {
    if (this.loaded) return;
    const saved = await this.ctx.storage.get("state");
    this.state = saved || { status: null, statusAt: 0, logs: [], seq: 0, lastSeen: 0, offlineNotified: false, fw: "" };
    this.loaded = true;
  }
  async persist() {
    await this.ctx.storage.put("state", this.state);
  }
  offlineMs() {
    const min = Number(this.env.OFFLINE_MIN);
    return (Number.isFinite(min) && min > 0 ? min : 5) * 60000;
  }
  device() {
    return this.ctx.getWebSockets("device")[0] || null;
  }

  async fetch(request) {
    const url = new URL(request.url);
    switch (url.pathname) {
      case "/device": {
        if (request.headers.get("Upgrade") !== "websocket") return new Response("Expected websocket", { status: 426 });
        for (const old of this.ctx.getWebSockets("device")) {
          try { old.close(1000, "diganti koneksi baru"); } catch { /* sudah tertutup */ }
        }
        const pair = new WebSocketPair();
        const [client, server] = Object.values(pair);
        this.ctx.acceptWebSocket(server, ["device"]);
        return new Response(null, { status: 101, webSocket: client });
      }
      case "/rpc": return this.rpc(request);
      case "/poll": return this.poll(url);
      case "/info": return this.info();
      case "/rl": return this.rateLimit(request);
      default: return new Response("not found", { status: 404 });
    }
  }

  // ---------------------------------------------------------------- browser ----
  async info() {
    await this.load();
    return json({ remote: true, online: !!this.device(), lastSeen: this.state.lastSeen, fw: this.state.fw });
  }

  async poll(url) {
    await this.load();
    const since = Number(url.searchParams.get("since")) || 0;
    const dev = this.device();
    const now = Date.now();
    if (dev && now - this.lastViewerSent > VIEWER_PING_MS) {   // ada penonton: ESP32 mulai menyiarkan status
      this.lastViewerSent = now;
      try { dev.send(JSON.stringify({ t: "viewers", n: 1, ttl: 15 })); } catch { /* koneksi putus */ }
    }
    const entries = this.state.logs.filter((e) => e.s > since).slice(0, 30);
    const last = entries.length ? entries[entries.length - 1].s : since;
    return json({
      status: this.state.status,
      log: { entries, last },
      remote: { online: !!dev, lastSeen: this.state.lastSeen, statusAge: this.state.statusAt ? now - this.state.statusAt : null },
    });
  }

  async rpc(request) {
    await this.load();
    const dev = this.device();
    if (!dev) return json({ ok: false, msg: "Mesin offline (tidak terhubung ke server)" }, 503);
    const body = await request.json();
    const id = ++this.reqId;
    const timeout = Number(this.env.RPC_TIMEOUT_MS) || 8000;
    const result = await new Promise((resolve) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        resolve({ code: 504, body: { ok: false, msg: "Mesin tidak menjawab" } });
      }, timeout);
      this.pending.set(id, { resolve, timer });
      try {
        dev.send(JSON.stringify({ t: "req", id, ...body }));
      } catch {
        clearTimeout(timer);
        this.pending.delete(id);
        resolve({ code: 503, body: { ok: false, msg: "Mesin offline" } });
      }
    });
    return json(result.body, result.code);
  }

  // Batas laju login per alamat IP: 5 gagal -> kunci 5 menit
  async rateLimit(request) {
    const { op, ip } = await request.json();
    const key = `rl:${ip}`;
    const now = Date.now();
    const rec = (await this.ctx.storage.get(key)) || { n: 0, lockUntil: 0 };
    if (op === "check") return json({ locked: rec.lockUntil > now, retryMs: Math.max(0, rec.lockUntil - now) });
    if (op === "fail") {
      rec.n += 1;
      if (rec.n >= 5) { rec.lockUntil = now + 5 * 60000; rec.n = 0; }
      await this.ctx.storage.put(key, rec);
      return json({ locked: rec.lockUntil > now });
    }
    if (op === "ok") { await this.ctx.storage.delete(key); return json({ ok: true }); }
    return json({ ok: false }, 400);
  }

  // ----------------------------------------------------------------- perangkat ----
  async webSocketMessage(ws, message) {
    await this.load();
    let msg;
    try { msg = JSON.parse(typeof message === "string" ? message : new TextDecoder().decode(message)); } catch { return; }
    const now = Date.now();
    const wasOffline = this.state.offlineNotified;
    this.state.lastSeen = now;

    switch (msg.t) {
      case "hello":
        this.state.fw = msg.fw || "";
        if (msg.d) { this.state.status = msg.d; this.state.statusAt = now; }
        await this.touch(wasOffline);
        break;
      case "hb":
        if (msg.d) { this.state.status = msg.d; this.state.statusAt = now; }
        if (msg.fw) this.state.fw = msg.fw;
        await this.touch(wasOffline);
        break;
      case "status":
        if (msg.d) { this.state.status = msg.d; this.state.statusAt = now; }
        break;
      case "log":
        for (const e of msg.entries || []) {
          this.state.logs.push({ s: ++this.state.seq, t: e.t, e: e.e, l: e.l, m: e.m });
        }
        if (this.state.logs.length > MAX_LOGS) this.state.logs.splice(0, this.state.logs.length - MAX_LOGS);
        break;
      case "notif":
        if (msg.text) this.queueNotif(String(msg.text));
        break;
      case "res": {
        const p = this.pending.get(msg.id);
        if (p) {
          clearTimeout(p.timer);
          this.pending.delete(msg.id);
          p.resolve({ code: Number(msg.code) || 200, body: msg.body });
        }
        break;
      }
      default: break;
    }
  }

  // hello/hb: simpan, atur ulang alarm offline, kabari bila mesin tersambung kembali setelah alarm
  async touch(wasOffline) {
    await this.persist();
    await this.ctx.storage.setAlarm(Date.now() + this.offlineMs());
    if (wasOffline) {
      this.state.offlineNotified = false;
      await this.persist();
      await sendTelegram(this.env, `✅ Mesin terhubung kembali (${wibTime(Date.now())} WIB)`);
    }
  }

  queueNotif(text) {
    this.notifBuf.push(text);
    if (this.flushing) return;
    this.flushing = true;
    this.ctx.waitUntil((async () => {
      await new Promise((r) => setTimeout(r, NOTIF_BATCH_MS));   // gabung pesan yang datang berdekatan
      const text2 = this.notifBuf.splice(0).join("\n");
      this.flushing = false;
      if (text2) await sendTelegram(this.env, text2);
    })());
  }

  async webSocketClose(ws) {
    try { ws.close(1000, "tutup"); } catch { /* sudah tertutup */ }
  }
  async webSocketError() { /* alarm offline menangani */ }

  async alarm() {
    await this.load();
    if (!this.state.lastSeen) return;
    const idle = Date.now() - this.state.lastSeen;
    if (idle >= this.offlineMs() - 500) {
      if (!this.state.offlineNotified) {
        this.state.offlineNotified = true;
        await this.persist();
        await sendTelegram(this.env, `⚠ Mesin tidak terhubung sejak ${wibTime(this.state.lastSeen)} WIB`);
      }
    } else {
      await this.ctx.storage.setAlarm(this.state.lastSeen + this.offlineMs());
    }
  }
}
