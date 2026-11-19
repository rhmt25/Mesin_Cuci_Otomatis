// Allowlist jarak jauh. Kontrol penuh, kecuali aksi berisiko (flash firmware, WiFi, password, perangkat, konsol, restore).
// ESP32 menegakkan aturan yang sama (pertahanan berlapis).

const CMD_NAMES = new Set([
  "program", "full_proses", "soak", "wash", "soak_and_wash", "rinse", "rinse_parfume", "spin", "water_in", "water_out",
  "add_parfume", "motor_on", "custom", "kalibrasi_relay", "relay", "relay_test",
  "stop", "jeda", "lanjutkan", "lanjutkan_proses", "lewati", "batalkan",
  "cek_air", "status", "pengaturan", "cek_variabel", "riwayat", "statistik", "info", "bantuan", "menu",
  "pola", "level_air", "mode_buang", "notif", "pra_cek_isi", "mulai_nanti", "selesai_jam", "kalibrasi_air",
  "reset_semua", "reset_variabel", "restart",
]);
const CMD_PATTERNS = [/^set_[a-z_]+$/, /^reset_[a-z_]+$/, /^mode_[a-z_]+$/];
// Selalu ditolak, apa pun pola di atas
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
  return CMD_NAMES.has(c) || CMD_PATTERNS.some((re) => re.test(c));
}

const GET_OK = new Set(["/api/status", "/api/poll", "/api/info", "/api/settings", "/api/logs", "/api/history", "/api/custom"]);
const POST_OK = new Set(["/api/cmd", "/api/settings", "/api/custom"]);
const DELETE_OK = new Set(["/api/custom"]);

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
