#include "remote_policy.h"
#include <string.h>

// Harus identik dengan cloud/src/allow.js (diuji paritas dengan cloud/test/allow-vectors.txt).
static const char* const CMD_NAMES[] = {
    "proses", "pilih", "stop", "jeda", "lanjutkan", "lewati", "batalkan",
    "relay", "relay_test", "kalibrasi_relay", "buang_manual",
    "cek_air", "status", "menu", "pengaturan", "riwayat", "statistik", "info", "bantuan",
    "notif", "kalibrasi_air", "reset_semua", "restart", "remote",
};
static const char* const CMD_DENY[] = {"web_password", "set_ssid", "set_password", "hapus_ssid", "hapus_password",
                                       "serial", "heap", "tasks", "log", "reboot"};

static void normalize(const char* raw, char* out, size_t n) {
  while (*raw == ' ' || *raw == '\t' || *raw == '\r' || *raw == '\n') raw++;
  if (*raw == '/') raw++;
  size_t i = 0;
  for (; raw[i] && i + 1 < n; i++) {
    char c = raw[i];
    if (c == '@' && i > 0) break;
    out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
  }
  out[i] = 0;
  while (i > 0 && (out[i - 1] == ' ' || out[i - 1] == '\t' || out[i - 1] == '\r' || out[i - 1] == '\n')) out[--i] = 0;
}

bool remoteCmdAllowed(const char* rawCmd) {
  if (!rawCmd) return false;
  char c[48];
  normalize(rawCmd, c, sizeof(c));
  if (!c[0]) return false;
  for (const char* d : CMD_DENY)
    if (strcmp(c, d) == 0) return false;
  if (strncmp(c, "wifi", 4) == 0) return false;
  for (const char* n : CMD_NAMES)
    if (strcmp(c, n) == 0) return true;
  return false;
}

bool remoteApiAllowed(const char* method, const char* path, const char* cmd) {
  if (!method || !path) return false;
  if (strcmp(method, "GET") == 0) {
    static const char* const ok[] = {"/api/status", "/api/poll", "/api/info", "/api/settings", "/api/logs",
                                     "/api/history", "/api/tahap/meta", "/api/job", "/api/proses", "/api/proses/export"};
    for (const char* p : ok)
      if (strcmp(path, p) == 0) return true;
    return false;
  }
  if (strcmp(method, "DELETE") == 0) return strcmp(path, "/api/proses") == 0;
  if (strcmp(method, "POST") == 0) {
    static const char* const ok[] = {"/api/settings", "/api/proses", "/api/run", "/api/decide"};
    for (const char* p : ok)
      if (strcmp(path, p) == 0) return true;
    if (strcmp(path, "/api/cmd") == 0) return remoteCmdAllowed(cmd);
  }
  return false;
}
