// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
// Durable Object "Relay": satu instans untuk satu mesin.
// - menerima SATU koneksi WebSocket dari ESP32 (WebSocket Hibernation: tidak ditagih saat diam)
// - menyimpan cache status/log terakhir (browser tidak pernah membangunkan ESP32 untuk membaca)
// - meneruskan permintaan API ke ESP32 (RPC) dan menunggu balasan
// - mengirim notifikasi Telegram dan alarm "mesin tidak terhubung"
import { DurableObject } from "cloudflare:workers";
import { applyFail, applyGlobalFail } from "./ratelimit.js";
import { makePasswordHash } from "./auth.js";
import { sendTelegram, wibTime } from "./telegram.js";

const MAX_LOGS = 60;
const VIEWER_PING_MS = 5000;
const NOTIF_BATCH_MS = 2000;

const json = (obj, status = 200) =>
  new Response(JSON.stringify(obj), { status, headers: { "content-type": "application/json", "cache-control": "no-store" } });

export class RelayV3 extends DurableObject {
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
  // Soket perangkat yang masih terbuka dan paling baru (sisa sambungan lama, mis. sebelum ESP32 reboot, bisa belum tertutup)
  device() {
    const list = this.ctx.getWebSockets("device");
    for (let i = list.length - 1; i >= 0; i--) if (list[i].readyState === 1) return list[i];
    return null;
  }

  // Hash password & epoch sesi terkini (hash kosong = pakai secret REMOTE_PASSWORD_HASH dari env)
  async authInfo() {
    if (!this.auth) this.auth = { hash: (await this.ctx.storage.get("pwhash")) || "", epoch: (await this.ctx.storage.get("epoch")) || 0 };
    return this.auth;
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (["/info", "/poll", "/rpc"].includes(url.pathname)) {   // sesi dari sebelum password diganti tidak berlaku lagi
      const auth = await this.authInfo();
      if (Number(url.searchParams.get("ep") || 0) !== auth.epoch) return json({ ok: false, msg: "login" }, 401);
    }
    switch (url.pathname) {
      case "/auth": return json(await this.authInfo());
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
    return json({ remote: true, online: !!this.device(), lastSeen: this.state.lastSeen, fw: this.state.fw, build: "r3" });
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
      } catch (e) {
        clearTimeout(timer);
        this.pending.delete(id);
        resolve({ code: 503, body: { ok: false, msg: "Mesin offline (" + String(e && e.message ? e.message : e).slice(0, 80) + ")" } });
      }
    });
    return json(result.body, result.code);
  }

  // Batas laju login: kunci bertingkat per alamat IP (5 mnt, 15 mnt, 1 jam, 6 jam, 24 jam) + batas global semua IP
  async rateLimit(request) {
    const { op, ip } = await request.json();
    const key = `rl:${ip}`;
    const now = Date.now();
    const rec = (await this.ctx.storage.get(key)) || { n: 0, level: 0, lockUntil: 0, lastFail: 0 };
    const g = (await this.ctx.storage.get("rl:global")) || { start: 0, count: 0, lockUntil: 0 };
    const lockedUntil = () => Math.max(rec.lockUntil || 0, g.lockUntil || 0);
    if (op === "check") return json({ locked: lockedUntil() > now, retryMs: Math.max(0, lockedUntil() - now) });
    if (op === "fail") {
      applyFail(rec, now);
      applyGlobalFail(g, now);
      await this.ctx.storage.put(key, rec);
      await this.ctx.storage.put("rl:global", g);
      return json({ locked: lockedUntil() > now, retryMs: Math.max(0, lockedUntil() - now) });
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
        try { ws.send('{"t":"ack"}'); } catch { /* socket sudah tertutup */ }
        this.state.fw = msg.fw || "";
        if (msg.d) { this.state.status = msg.d; this.state.statusAt = now; }
        await this.touch(wasOffline);
        break;
      case "hb":
        try { ws.send('{"t":"ack"}'); } catch { /* socket sudah tertutup */ }
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
      case "setpw": {   // password jarak jauh diganti dari dashboard lokal ESP32 (hanya soket perangkat yang lolos token)
        const pw = String(msg.pw || "");
        let reply;
        if (pw.length < 12 || pw.length > 128) {
          reply = { ok: false, msg: "Password jarak jauh harus 12-128 karakter" };
        } else {
          const hash = await makePasswordHash(pw);
          const epoch = (await this.authInfo()).epoch + 1;
          await this.ctx.storage.put("pwhash", hash);
          await this.ctx.storage.put("epoch", epoch);
          this.auth = { hash, epoch };
          reply = { ok: true, msg: "Password jarak jauh diganti. Semua sesi jarak jauh keluar." };
          this.queueNotif("🔑 Password jarak jauh diganti dari dashboard lokal mesin.");
        }
        try { ws.send(JSON.stringify({ t: "setpw_res", id: msg.id, ...reply })); } catch { /* soket tertutup */ }
        break;
      }
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
