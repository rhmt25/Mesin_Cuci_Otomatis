// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#pragma once
// Cadangkan & pulihkan pengaturan dan daftar WiFi (Proses diekspor satu per satu) (JSON). Khusus web lokal.
// Logika murni (std::string + ArduinoJson) agar bisa diuji di PC.
#include <stddef.h>
#include <string>

#define BACKUP_VERSION 3
#define RESTORE_MAX_BYTES 12288

// includeWifi: sertakan password WiFi (default tidak: hanya nama jaringan)
std::string backupBuild(bool includeWifi);

struct RestoreReport {
  bool ok;
  std::string msg;        // ringkasan / alasan penolakan (Indonesia)
  int settingsChanged;    // jumlah pengaturan yang berubah nilainya
  int oldIgnored;         // jumlah data versi lama yang diabaikan (variabel global, Custom Proses)
  int wifiNetworks;       // jumlah WiFi di cadangan
  int wifiNoPassword;     // WiFi baru tanpa password (perlu diisi manual)
  int clamped;            // nilai di luar rentang yang disesuaikan
};

// dry = true: hanya validasi + ringkasan, tidak mengubah apa pun. Semua dicek dulu; bila ada yang tidak valid,
// tidak ada yang diterapkan (atomik).
RestoreReport backupApply(const char* json, size_t len, bool dry);
