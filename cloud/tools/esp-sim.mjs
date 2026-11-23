// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
// Simulator ESP32 untuk uji Worker: terhubung seperti perangkat sungguhan dan menjawab permintaan API.
import WebSocket from "ws";

export function startSim({ url, token, hbMs = 30000, statusMs = 3000, silent = false, fw = "sim-2.0" }) {
  const state = { machine: "idle", program: "", pauseReason: "", relays: { kiri: false }, cmds: [], reqs: [], viewersMsgs: 0, connected: false };
  let ws, hbTimer, statusTimer, closed = false, pwResolve = null;

  const status = () => ({
    type: "status", state: state.machine, program: state.program, stage: "", detail: "", pause: state.pauseReason,
    step: 0, steps: 0, stepLeft: 0, totalLeft: 0, progress: 0, water: "kosong", waterRaw: false, fault: false,
    resume: { pending: false, info: "" }, relays: state.relays, wifi: { online: true, ssid: "sim", rssi: -50, ip: "1.2.3.4", ap: "x", apOn: false, apClients: 0, time: "10:00" },
    notif: "Lengkap", uptime: 1, heap: 100000, fw, reset: "sim", otaBusy: false, otaProgress: 0,
  });
  const send = (o) => { if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(o)); };

  function api(m, p, q, b) {
    state.reqs.push({ m, p, q, b });
    if (m === "GET" && p === "/api/status") return { code: 200, body: status() };
    if (m === "GET" && p === "/api/settings") return { code: 200, body: { pola: 0, soak: 2, wash: 15 } };
    if (m === "POST" && p === "/api/cmd") {
      state.cmds.push(b);
      const c = String(b.cmd || "").replace(/^\//, "");
      if (c === "stop") { state.machine = "idle"; state.program = ""; return { code: 200, body: { ok: true, msg: "Perintah STOP dikirim." } }; }
      if (c === "proses") { state.machine = "running"; state.program = b.args; return { code: 200, body: { ok: true, msg: "Proses dikirim" } }; }
      return { code: 200, body: { ok: true, msg: "ok:" + c } };
    }
    if (m === "GET" && p === "/api/proses") return { code: 200, body: { max: 10, maxTahap: 40, slots: [null, null, null, null, null, null, null, null, null, null] } };
    return { code: 404, body: { ok: false, msg: "sim: tidak ada" } };
  }

  function connect() {
    ws = new WebSocket(url, { headers: { Authorization: `Bearer ${token}` } });
    ws.on("open", () => {
      state.connected = true;
      send({ t: "hello", fw, d: status() });
      clearInterval(hbTimer);
      hbTimer = setInterval(() => send({ t: "hb", fw, d: status() }), hbMs);
    });
    ws.on("message", (raw) => {
      const msg = JSON.parse(raw.toString());
      if (msg.t === "viewers") {
        state.viewersMsgs++;
        send({ t: "status", d: status() });
        clearInterval(statusTimer);
        let left = (msg.ttl || 15) * 1000;
        statusTimer = setInterval(() => { left -= statusMs; if (left <= 0) clearInterval(statusTimer); else send({ t: "status", d: status() }); }, statusMs);
      } else if (msg.t === "setpw_res" && pwResolve) {
        pwResolve(msg); pwResolve = null;
      } else if (msg.t === "req" && !silent) {
        const r = api(msg.m, msg.p, msg.q, msg.b);
        send({ t: "res", id: msg.id, code: r.code, body: r.body });
      } else if (msg.t === "req") {
        state.reqs.push({ m: msg.m, p: msg.p, silent: true });
      }
    });
    ws.on("close", () => { state.connected = false; clearInterval(hbTimer); clearInterval(statusTimer); });
    ws.on("error", () => { /* dicoba lagi oleh pemanggil */ });
  }
  connect();
  return {
    state,
    notify: (text) => send({ t: "notif", text }),
    setPassword: (pw) => new Promise((res) => { pwResolve = res; send({ t: "setpw", id: 1, pw }); setTimeout(() => res({ ok: false, msg: "tanpa jawaban" }), 5000); }),
    log: (entries) => send({ t: "log", entries }),
    close: () => { closed = true; clearInterval(hbTimer); clearInterval(statusTimer); if (ws) ws.close(); },
    reconnect: () => { if (!state.connected && !closed) connect(); },
    isClosed: () => closed,
  };
}

// Pemakaian CLI: node tools/esp-sim.mjs wss://xxx.workers.dev/device TOKEN
if (process.argv[1] && process.argv[1].endsWith("esp-sim.mjs") && process.argv[2]) {
  startSim({ url: process.argv[2], token: process.argv[3] || "", hbMs: 10000 });
  console.log("Simulator ESP32 berjalan. Ctrl+C untuk berhenti.");
}
