#pragma once
// TEMPLATE kredensial. Salin file ini menjadi secrets.h di folder yang sama,
// lalu isi nilainya. secrets.h tidak boleh di-commit.

#ifndef WIFI_SSID
#define WIFI_SSID "NAMA_WIFI"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "PASSWORD_WIFI"
#endif

// Kontrol jarak jauh (Cloudflare Worker, lihat cloud/README.md). Kosongkan untuk menonaktifkan.
// Notifikasi Telegram dikirim oleh Worker (token bot disimpan sebagai secret Worker, bukan di ESP32).
#ifndef REMOTE_URL
#define REMOTE_URL "wss://mesin-cuci-relay.AKUN-KAMU.workers.dev/device"
#endif
#ifndef REMOTE_DEVICE_TOKEN
#define REMOTE_DEVICE_TOKEN "ISI_TOKEN_PERANGKAT"
#endif

// Daftar WiFi cadangan awal (opsional, sampai 4 tambahan; bisa juga lewat web)
#ifndef WIFI_SSID_2
#define WIFI_SSID_2 ""
#endif
#ifndef WIFI_PASSWORD_2
#define WIFI_PASSWORD_2 ""
#endif

// Hotspot ESP32 (SSID: MesinCuci-XXXX). Minimal 8 karakter.
#ifndef AP_PASSWORD
#define AP_PASSWORD "PASSWORD_HOTSPOT"
#endif

// Password dashboard web (bisa diganti dari dashboard)
#ifndef WEB_PASSWORD
#define WEB_PASSWORD "PASSWORD_WEB"
#endif

