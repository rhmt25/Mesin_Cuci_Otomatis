// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#pragma once
// Logika kebijakan jaringan yang murni (tanpa Arduino/WiFi) sehingga bisa diuji di PC.
#include <stdint.h>

// ---- Watchdog konektivitas -------------------------------------------------
// Link WiFi bisa "zombie": status CONNECTED tetapi tidak ada paket yang lewat. Ping gateway berkala
// mendeteksinya. Watchdog hanya aktif (armed) setelah gateway pernah menjawab ping, supaya router
// yang memblokir ICMP tidak memicu reconnect tanpa henti.
enum ConnAction : uint8_t { CONN_NONE, CONN_RECONNECT, CONN_RESTART_WIFI };
struct ConnWatchdog {
  bool armed;
  uint8_t failStreak;              // gagal ping berturut-turut
  uint8_t reconnectsNoSuccess;     // reconnect berturut-turut tanpa ping berhasil
};
#define CONN_FAILS_BEFORE_RECONNECT 3
#define CONN_RECONNECTS_BEFORE_RESTART 3
void connWdInit(ConnWatchdog& w);
ConnAction connWdReport(ConnWatchdog& w, bool pingOk);

// ---- Kebijakan hotspot ------------------------------------------------------
// Hotspot MATI saat WiFi rumah tersambung. Nyala hanya bila semua SSID gagal (atau tak ada SSID tersimpan),
// tetap nyala selama STA belum tersambung, dan mati lagi setelah STA tersambung dan 2 menit tanpa klien.
#define AP_IDLE_OFF_MS 120000UL
struct ApPolicy {
  bool apOn;
  bool staUp;
  uint32_t staUpSince;
  uint32_t lastClientMs;
};
void apPolicyInit(ApPolicy& p);
// Mengembalikan apakah hotspot harus nyala.
bool apPolicyStep(ApPolicy& p, uint32_t nowMs, bool staConnected, bool allSsidFailed, int apClients);
