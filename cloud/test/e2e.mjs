// Uji ujung-ke-ujung Worker + Durable Object di runtime lokal (wrangler dev), dengan simulator ESP32
// dan server Telegram palsu. Tidak memakai akun Cloudflare. Jalankan: npm test
import { spawn, spawnSync } from "node:child_process";
import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { makePasswordHash } from "../src/auth.js";
import { cmdAllowed, isAllowed } from "../src/allow.js";
import { startSim } from "../tools/esp-sim.mjs";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const PORT = 8787, TG_PORT = 8799;
const BASE = `http://127.0.0.1:${PORT}`;
const PASSWORD = "uji-password-panjang";
let pass = 0, fail = 0;
const ok = (c, m) => { if (c) { pass++; console.log("  ok:", m); } else { fail++; console.log("  GAGAL:", m); } };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---------- unit: allowlist (cepat) ----------
console.log("[W1] Allowlist (vektor bersama dengan ESP32)");
{
  const lines = fs.readFileSync(path.join(root, "test", "allow-vectors.txt"), "utf8").split(/\r?\n/);
  let total = 0, bad = 0;
  for (const raw of lines) {
    const line = raw.replace(/\s+$/, "");
    if (!line || line.startsWith("#")) continue;
    const m = line.match(/^(\S+)\s+(allow|deny)(?:\s+(.*))?$/);
    if (!m) continue;
    const [, kind, verdict, rest = ""] = m;
    const expect = verdict === "allow";
    let actual;
    if (kind === "cmd") actual = cmdAllowed(rest.trim().split(/\s+/)[0] || "");
    else if (kind === "apicmd") actual = isAllowed("POST", "/api/cmd", { cmd: rest.trim() }).ok;
    else if (kind === "api") { const [meth, p] = rest.split(/\s+/); actual = isAllowed(meth, p, {}).ok; }
    else continue;
    total++;
    if (actual !== expect) { bad++; console.log("  tidak cocok:", line); }
  }
  ok(total > 80 && bad === 0, `${total} vektor allow/deny cocok dengan kebijakan`);
}

// ---------- Telegram palsu ----------
const tg = [];
const tgServer = http.createServer((req, res) => {
  let body = "";
  req.on("data", (c) => (body += c));
  req.on("end", () => { try { tg.push(JSON.parse(body).text); } catch { tg.push(body); } res.writeHead(200, { "content-type": "application/json" }); res.end('{"ok":true}'); });
});
await new Promise((r) => tgServer.listen(TG_PORT, "127.0.0.1", r));

// ---------- konfigurasi lokal wrangler ----------
const devVars = path.join(root, ".dev.vars");
const hadDevVars = fs.existsSync(devVars);
const backup = hadDevVars ? fs.readFileSync(devVars, "utf8") : null;
fs.writeFileSync(devVars, [
  "DEVICE_TOKEN=tok-perangkat-uji",
  "SESSION_SECRET=rahasia-sesi-uji",
  `REMOTE_PASSWORD_HASH=${await makePasswordHash(PASSWORD)}`,
  "TELEGRAM_BOT_TOKEN=uji",
  "TELEGRAM_CHAT_ID=123",
  `TELEGRAM_API_BASE=http://127.0.0.1:${TG_PORT}`,
  "OFFLINE_MIN=0.05",
  "RPC_TIMEOUT_MS=2000",
].join("\n"));
const stateDir = fs.mkdtempSync(path.join(root, ".wrangler-test-"));

const wr = spawn("npx", ["wrangler", "dev", "--local", "--port", String(PORT), "--persist-to", stateDir, "--ip", "127.0.0.1"], { cwd: root, shell: true });
let wrLog = "";
const ready = new Promise((resolve, reject) => {
  const t = setTimeout(() => reject(new Error("wrangler dev tidak siap:\n" + wrLog.slice(-1500))), 120000);
  const onData = (d) => { wrLog += d.toString(); if (/Ready on/i.test(wrLog)) { clearTimeout(t); resolve(); } };
  wr.stdout.on("data", onData); wr.stderr.on("data", onData);
});

let cleaned = false;
function cleanup() {
  if (cleaned) return;
  cleaned = true;
  try { if (process.platform === "win32") spawnSync("taskkill", ["/pid", String(wr.pid), "/T", "/F"]); else wr.kill("SIGKILL"); } catch { /* sudah berhenti */ }
  tgServer.close();
  try { if (hadDevVars) fs.writeFileSync(devVars, backup); else fs.rmSync(devVars, { force: true }); } catch { /* */ }
  try { fs.rmSync(stateDir, { recursive: true, force: true }); } catch { /* file masih dikunci proses; dibersihkan di jalankan berikutnya */ }
}
process.on("exit", cleanup);

let sim;
try {
  await ready;
  await sleep(500);
  const call = async (method, p, body, cookie, extraHeaders = {}) => {
    const r = await fetch(BASE + p, { method, headers: { "content-type": "application/json", ...(cookie ? { Cookie: cookie } : {}), ...extraHeaders }, body: body ? JSON.stringify(body) : undefined });
    let j = null; try { j = await r.json(); } catch { /* bukan JSON */ }
    return { code: r.status, json: j, headers: r.headers };
  };

  console.log("[W2] Halaman & login");
  const page = await fetch(BASE + "/");
  const html = await page.text();
  ok(page.status === 200 && /Mesin Cuci/.test(html), "UI jarak jauh dilayani di /");
  ok((await call("GET", "/api/status")).code === 401, "API tanpa login = 401");
  ok((await call("POST", "/api/login", { password: "salah" })).code === 401, "password salah = 401");
  const good = await call("POST", "/api/login", { password: PASSWORD, remember: true });
  ok(good.code === 200 && good.json.ok, "login benar");
  const setCookie = good.headers.get("set-cookie") || "";
  ok(/HttpOnly/.test(setCookie) && /SameSite=Strict/.test(setCookie) && /Max-Age=\d+/.test(setCookie), "cookie HttpOnly + SameSite=Strict + Max-Age (ingat perangkat)");
  const cookie = setCookie.split(";")[0];
  const temp = await call("POST", "/api/login", { password: PASSWORD });
  ok(!/Max-Age/.test(temp.headers.get("set-cookie") || ""), "tanpa 'ingat' = cookie sesi (tanpa Max-Age)");
  ok((await call("POST", "/api/cmd", { cmd: "stop" }, "sid=palsu.palsu.palsu")).code === 401, "cookie palsu ditolak");
  ok((await call("POST", "/api/cmd", { cmd: "stop" }, cookie, { Origin: "https://jahat.example" })).code === 403, "Origin asing ditolak (CSRF)");

  console.log("[W3] Mesin offline");
  const off = await call("GET", "/api/info", null, cookie);
  ok(off.code === 200 && off.json.online === false, "info: mesin offline");
  const offCmd = await call("POST", "/api/cmd", { cmd: "stop" }, cookie);
  ok(offCmd.code === 503 && /offline/i.test(offCmd.json.msg), "perintah saat offline = 503 + pesan jelas");
  ok((await call("POST", "/api/device-token")).code !== 200, "endpoint tak dikenal tidak terbuka");

  console.log("[W4] Perangkat terhubung");
  const wsBad = new (await import("ws")).default(`ws://127.0.0.1:${PORT}/device`, { headers: { Authorization: "Bearer salah" } });
  const badCode = await new Promise((r) => { wsBad.on("unexpected-response", (_q, res) => r(res.statusCode)); wsBad.on("error", () => {}); setTimeout(() => r(0), 3000); });
  ok(badCode === 401, "perangkat dengan token salah ditolak (401)");
  sim = startSim({ url: `ws://127.0.0.1:${PORT}/device`, token: "tok-perangkat-uji", hbMs: 800, statusMs: 300 });
  await sleep(1200);
  const on = await call("GET", "/api/info", null, cookie);
  ok(on.json.online === true && on.json.fw === "sim-2.0", "info: mesin online, versi terbaca");
  const poll = await call("GET", "/api/poll?since=0", null, cookie);
  ok(poll.code === 200 && poll.json.status && poll.json.status.state === "idle" && poll.json.remote.online, "poll: status dari cache");
  await sleep(600);
  ok(sim.state.viewersMsgs >= 1, "ESP32 diberi tahu ada penonton (mulai menyiarkan status)");

  console.log("[W5] Perintah diteruskan");
  const stop = await call("POST", "/api/cmd", { cmd: "stop", args: "" }, cookie);
  ok(stop.code === 200 && stop.json.ok && /STOP/.test(stop.json.msg), "stop diteruskan ke ESP32 dan balasannya kembali");
  const prog = await call("POST", "/api/cmd", { cmd: "program", args: "normal" }, cookie);
  ok(prog.code === 200 && sim.state.machine === "running", "program normal mengubah status mesin");
  await sleep(1200);
  const poll2 = await call("GET", "/api/poll?since=0", null, cookie);
  ok(poll2.json.status.state === "running", "poll berikutnya menampilkan status terbaru");
  const settings = await call("GET", "/api/settings", null, cookie);
  ok(settings.code === 200 && settings.json.wash === 15, "GET /api/settings diteruskan");
  for (const [m, p, b] of [["POST", "/api/cmd", { cmd: "wifi_tambah", args: "x|y" }], ["POST", "/api/cmd", { cmd: "web_password", args: "baru12345" }], ["POST", "/api/update", {}], ["GET", "/api/wifi", null], ["GET", "/api/console", null], ["GET", "/api/devices", null], ["POST", "/api/restore", {}]]) {
    const r = await call(m, p, b, cookie);
    ok(r.code === 403, `ditolak di Worker: ${m} ${p}${b && b.cmd ? " " + b.cmd : ""}`);
  }
  ok(!sim.state.reqs.some((r) => /wifi|update|console|devices|restore/.test(r.p) || (r.b && /wifi|web_password/.test(r.b.cmd || ""))), "permintaan terlarang TIDAK pernah sampai ke ESP32");

  console.log("[W6] Log & notifikasi Telegram");
  sim.log([{ t: 1, e: 0, l: 3, m: "Peringatan uji" }, { t: 2, e: 0, l: 4, m: "Error uji" }]);
  await sleep(400);
  const lp = await call("GET", "/api/poll?since=0", null, cookie);
  ok(lp.json.log.entries.length >= 2 && lp.json.log.entries[0].s === 1, "log dari ESP32 masuk cache dengan nomor urut Worker");
  const lp2 = await call("GET", `/api/poll?since=${lp.json.log.last}`, null, cookie);
  ok(lp2.json.log.entries.length === 0, "since= hanya mengembalikan log baru");
  tg.length = 0;
  sim.notify("✅ Proses Normal selesai");
  sim.notify("▶ Tahap berikutnya");
  await sleep(3000);
  ok(tg.length === 1 && /Normal selesai/.test(tg[0]) && /Tahap berikutnya/.test(tg[0]), "dua notifikasi berdekatan digabung jadi satu pesan Telegram");

  console.log("[W7] Alarm offline & tersambung kembali");
  tg.length = 0;
  sim.close();
  await sleep(5500);   // OFFLINE_MIN=0,05 menit = 3 dtk
  ok(tg.some((t) => /tidak terhubung/.test(t)), "alarm 'mesin tidak terhubung' terkirim");
  const n = tg.filter((t) => /tidak terhubung/.test(t)).length;
  await sleep(3500);
  ok(tg.filter((t) => /tidak terhubung/.test(t)).length === n, "alarm tidak berulang");
  sim = startSim({ url: `ws://127.0.0.1:${PORT}/device`, token: "tok-perangkat-uji", hbMs: 800 });
  await sleep(2000);
  ok(tg.some((t) => /terhubung kembali/.test(t)), "pesan 'terhubung kembali' terkirim");

  console.log("[W8] Timeout RPC");
  sim.close();
  sim = startSim({ url: `ws://127.0.0.1:${PORT}/device`, token: "tok-perangkat-uji", hbMs: 800, silent: true });
  await sleep(1200);
  const t0 = Date.now();
  const to = await call("POST", "/api/cmd", { cmd: "jeda" }, cookie);
  ok(to.code === 504 && Date.now() - t0 >= 1800 && Date.now() - t0 < 5000, "ESP32 tidak menjawab = 504 setelah ±2 dtk");

  console.log("[W9] Batas laju login");
  let locked = false;
  for (let i = 0; i < 7; i++) { const r = await call("POST", "/api/login", { password: "salah" }); if (r.code === 429) { locked = true; break; } }
  ok(locked, "5 kali salah -> 429 (dikunci)");
  const afterLock = await call("POST", "/api/login", { password: PASSWORD });
  ok(afterLock.code === 429, "password benar pun ditolak saat dikunci");
} catch (e) {
  fail++;
  console.log("  GAGAL (pengecualian):", e && e.stack ? e.stack : e);
} finally {
  try { if (sim) sim.close(); } catch { /* */ }
  cleanup();
  console.log(`\n${pass + fail} pemeriksaan Worker, ${fail} gagal`);
  process.exit(fail ? 1 : 0);
}
