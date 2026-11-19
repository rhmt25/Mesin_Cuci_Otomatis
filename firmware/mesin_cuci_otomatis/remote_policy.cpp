#include "remote_policy.h"
#include <string.h>

static const char* const CMD_NAMES[] = {
    "program", "full_proses", "soak", "wash", "soak_and_wash", "rinse", "rinse_parfume", "spin", "water_in",
    "water_out", "add_parfume", "motor_on", "custom", "kalibrasi_relay", "relay", "relay_test",
    "stop", "jeda", "lanjutkan", "lanjutkan_proses", "lewati", "batalkan",
    "cek_air", "status", "pengaturan", "cek_variabel", "riwayat", "statistik", "info", "bantuan", "menu",
    "pola", "level_air", "mode_buang", "notif", "pra_cek_isi", "mulai_nanti", "selesai_jam", "kalibrasi_air",
    "reset_semua", "reset_variabel", "restart", "remote",
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

// awalan + sisa hanya huruf kecil dan garis bawah, minimal 1 karakter
static bool prefixed(const char* c, const char* prefix) {
  size_t pl = strlen(prefix);
  if (strncmp(c, prefix, pl) != 0 || c[pl] == 0) return false;
  for (const char* p = c + pl; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || *p == '_')) return false;
  return true;
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
  return prefixed(c, "set_") || prefixed(c, "reset_") || prefixed(c, "mode_");
}

bool remoteApiAllowed(const char* method, const char* path, const char* cmd) {
  if (!method || !path) return false;
  if (strcmp(method, "GET") == 0) {
    static const char* const ok[] = {"/api/status", "/api/poll", "/api/info", "/api/settings",
                                     "/api/logs", "/api/history", "/api/custom"};
    for (const char* p : ok)
      if (strcmp(path, p) == 0) return true;
    return false;
  }
  if (strcmp(method, "DELETE") == 0) return strcmp(path, "/api/custom") == 0;
  if (strcmp(method, "POST") == 0) {
    if (strcmp(path, "/api/settings") == 0 || strcmp(path, "/api/custom") == 0) return true;
    if (strcmp(path, "/api/cmd") == 0) return remoteCmdAllowed(cmd);
  }
  return false;
}
