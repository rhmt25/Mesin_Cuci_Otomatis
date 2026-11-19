#pragma once
// Cadangkan & pulihkan pengaturan, Custom Proses, dan daftar WiFi (JSON). Khusus web lokal.
// Logika murni (std::string + ArduinoJson) agar bisa diuji di PC.
#include <stddef.h>
#include <string>

#define BACKUP_VERSION 1
#define RESTORE_MAX_BYTES 12288

// includeWifi: sertakan password WiFi (default tidak: hanya nama jaringan)
std::string backupBuild(bool includeWifi);

struct RestoreReport {
  bool ok;
  std::string msg;        // ringkasan / alasan penolakan (Indonesia)
  int settingsChanged;    // jumlah pengaturan yang berubah nilainya
  int customSlots;        // jumlah slot Custom Proses di cadangan
  int wifiNetworks;       // jumlah WiFi di cadangan
  int wifiNoPassword;     // WiFi baru tanpa password (perlu diisi manual)
  int clamped;            // nilai di luar rentang yang disesuaikan
};

// dry = true: hanya validasi + ringkasan, tidak mengubah apa pun. Semua dicek dulu; bila ada yang tidak valid,
// tidak ada yang diterapkan (atomik).
RestoreReport backupApply(const char* json, size_t len, bool dry);
