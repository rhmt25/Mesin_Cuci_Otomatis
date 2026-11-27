// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#pragma once
// Konfigurasi pusat: pin, konstanta, default. Kredensial ada di secrets.h.
#include <Arduino.h>
#include "secrets.h"

#define FW_VERSION "2.0.0"
#define DEVICE_HOSTNAME "mesincuci"

// --- Skala waktu (Wokwi: 60 -> 1 menit proses = 1 detik) -------------------
#ifndef TIME_SCALE
#define TIME_SCALE 1
#endif
inline uint32_t scaledMs(uint32_t ms) {
  uint32_t v = ms / TIME_SCALE;
  return v ? v : 1;
}
inline uint32_t minToMs(uint32_t minutes) { return scaledMs(minutes * 60000UL); }
inline uint32_t secToMs(uint32_t seconds) { return scaledMs(seconds * 1000UL); }

// --- Relay ----------------------------------------------------------------
#ifndef RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW 1   // modul relay Normally Open, trigger LOW
#endif
#ifndef RELAY_OPEN_DRAIN
#define RELAY_OPEN_DRAIN 1   // HIGH = mengambang, aman untuk relay 5V
#endif

// --- Peta pin (ESP32 WROOM) ------------------------------------------------
#define PIN_MOTOR_LEFT    25   // dulu GPIO5 (strapping pin)
#define PIN_MOTOR_RIGHT   18
#define PIN_WATER_IN      17
#define PIN_WATER_OUT     16
#define PIN_SPIN_MOTOR    4
#define PIN_PERFUME       13
#define PIN_WATER_LEVEL   27   // INPUT_PULLUP, HIGH = ada air
#define PIN_SENSOR_SRC    14   // sumber sensor/tombol, tetap HIGH

// --- Pengaman & timing mesin ----------------------------------------------
#define TICK_MS                 20
#define WATCHDOG_SEC            10
#define MOTOR_REVERSE_GAP_MS    500        // jeda minimal ganti arah (nyata)
#define MOTOR_MAX_ON_MS         65000      // failsafe: motor cuci tidak boleh menyala terus lebih dari ini (nyata)
#define PERFUME_MAX_ON_MS       240000     // failsafe: pompa pewangi tidak boleh menyala terus lebih dari ini (nyata)
#define SPIN_MAX_ON_MS          3660000    // failsafe: motor spin (Tahap Spin maks 60 mnt + 1 mnt cadangan, nyata)
#define MANUAL_MAX_ON_MS        60000      // kontrol relay manual otomatis mati setelah ini (nyata)
// Lama sensor air harus terus kena air / terus tidak kena sebelum dipercaya (diatur di Sistem > Mesin)
#define WATER_CONFIRM_DEF_SEC   10
#define WATER_CONFIRM_MIN_SEC   1
#define WATER_CONFIRM_MAX_SEC   60
#define FILL_SETTLE_MS          2000
#define UNJAM_RELEASE_MS        3000
#define UNJAM_PULL_MS           30000
#define ANOMALY_SLOW_FACTOR     1.5f
#define ANOMALY_FAST_FACTOR     0.2f

// --- Log / jaringan --------------------------------------------------------
#define LOG_RING_SIZE           100
#define LOG_TEXT_LEN            96
#define RTC_TRACE_LINES         40
#define RTC_TRACE_LEN           80
#define MAX_WIFI_NETWORKS       5
#define WIFI_TRY_TIMEOUT_MS     10000
#define NOTIF_BATCH_MS          1500
#define NOTIF_OFFLINE_MAX       30
#define NTP_TZ                  "WIB-7"

// Daya pancar WiFi. Default ESP32 (19,5 dBm) pada perangkat ini membuat ±20-35% paket hilang; 15 dBm cukup untuk web lokal
// tetapi sambungan TLS/WebSocket ke Worker tetap tersendat (loss 21% terukur); 11 dBm: loss 2% dan semua uji jarak jauh lulus.
// Ciri suplai 5V/USB yang drop saat memancar. Naikkan hanya bila suplai daya sudah diperbaiki.
#ifndef WIFI_TX_POWER
#define WIFI_TX_POWER WIFI_POWER_11dBm
#endif

// Password wajib diisi di secrets.h (tidak ada nilai bawaan di kode)
#ifndef AP_PASSWORD
#error "AP_PASSWORD belum diisi di secrets.h (lihat secrets.example.h)"
#endif
#ifndef WEB_PASSWORD
#error "WEB_PASSWORD belum diisi di secrets.h (lihat secrets.example.h)"
#endif
// Kontrol jarak jauh (Cloudflare Worker). Kosong = nonaktif. Diisi di secrets.h:
//   #define REMOTE_URL "wss://mesin-cuci-relay.<akun>.workers.dev/device"
//   #define REMOTE_DEVICE_TOKEN "ISI_DEVICE_TOKEN"   (nilai dari cloud/.secrets.local.json)
#ifndef REMOTE_URL
#define REMOTE_URL ""
#endif
#ifndef REMOTE_DEVICE_TOKEN
#define REMOTE_DEVICE_TOKEN ""
#endif
#ifndef WIFI_SSID_2
#define WIFI_SSID_2 ""
#endif
#ifndef WIFI_PASSWORD_2
#define WIFI_PASSWORD_2 ""
#endif
