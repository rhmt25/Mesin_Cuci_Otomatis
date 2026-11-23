// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
// Allowlist jarak jauh. Kontrol penuh, kecuali aksi berisiko (flash firmware, WiFi, password, perangkat, konsol, restore,
// impor Proses). ESP32 menegakkan aturan yang sama (pertahanan berlapis).

const CMD_NAMES = new Set([
  "proses", "pilih", "stop", "jeda", "lanjutkan", "lewati", "batalkan",
  "relay", "relay_test", "kalibrasi_relay", "buang_manual",
  "cek_air", "status", "menu", "pengaturan", "riwayat", "statistik", "info", "bantuan",
  "notif", "kalibrasi_air", "reset_semua", "restart", "remote",
]);
// Selalu ditolak, apa pun yang lain
const CMD_DENY = new Set(["web_password", "set_ssid", "set_password", "hapus_ssid", "hapus_password", "serial", "heap", "tasks", "log", "reboot"]);

export function normalizeCmd(raw) {
  let c = String(raw || "").trim().toLowerCase();
  if (c.startsWith("/")) c = c.slice(1);
  const at = c.indexOf("@");
  if (at > 0) c = c.slice(0, at);
  return c;
}

export function cmdAllowed(raw) {
  const c = normalizeCmd(raw);
  if (!c || CMD_DENY.has(c) || c.startsWith("wifi")) return false;
  return CMD_NAMES.has(c);
}

const GET_OK = new Set(["/api/status", "/api/poll", "/api/info", "/api/settings", "/api/logs", "/api/history",
  "/api/tahap/meta", "/api/job", "/api/proses", "/api/proses/export"]);
const POST_OK = new Set(["/api/cmd", "/api/settings", "/api/proses", "/api/run", "/api/decide"]);
const DELETE_OK = new Set(["/api/proses"]);

// body = objek JSON (untuk /api/cmd). Mengembalikan {ok, reason}.
export function isAllowed(method, path, body) {
  if (method === "GET") return GET_OK.has(path) ? { ok: true } : { ok: false, reason: "Tidak tersedia dari jarak jauh" };
  if (method === "DELETE") return DELETE_OK.has(path) ? { ok: true } : { ok: false, reason: "Tidak tersedia dari jarak jauh" };
  if (method === "POST") {
    if (!POST_OK.has(path)) return { ok: false, reason: "Tidak tersedia dari jarak jauh" };
    if (path === "/api/cmd" && !cmdAllowed(body && body.cmd)) {
      return { ok: false, reason: "Perintah ini hanya bisa dari web lokal (demi keamanan)" };
    }
    return { ok: true };
  }
  return { ok: false, reason: "Metode tidak didukung" };
}
