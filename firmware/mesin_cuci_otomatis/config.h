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
#define WATER_STABLE_MS         10000      // sensor harus stabil 10 dtk
#define FILL_SETTLE_MS          2000
#define UNJAM_RELEASE_MS        3000
#define UNJAM_PULL_MS           30000
#define ANOMALY_SLOW_FACTOR     1.5f
#define ANOMALY_FAST_FACTOR     0.2f

// --- Log / jaringan --------------------------------------------------------
#define LOG_RING_SIZE           200
#define LOG_TEXT_LEN            128
#define RTC_TRACE_LINES         40
#define RTC_TRACE_LEN           80
#define MAX_WIFI_NETWORKS       5
#define WIFI_TRY_TIMEOUT_MS     10000
#define MAX_WS_CLIENTS          3
#define NOTIF_BATCH_MS          1500
#define NOTIF_OFFLINE_MAX       60
#define NTP_TZ                  "WIB-7"

// Password wajib diisi di secrets.h (tidak ada nilai bawaan di kode)
#ifndef AP_PASSWORD
#error "AP_PASSWORD belum diisi di secrets.h (lihat secrets.example.h)"
#endif
#ifndef WEB_PASSWORD
#error "WEB_PASSWORD belum diisi di secrets.h (lihat secrets.example.h)"
#endif
#ifndef OTA_PASSWORD
#error "OTA_PASSWORD belum diisi di secrets.h (lihat secrets.example.h)"
#endif
#ifndef WIFI_SSID_2
#define WIFI_SSID_2 ""
#endif
#ifndef WIFI_PASSWORD_2
#define WIFI_PASSWORD_2 ""
#endif
