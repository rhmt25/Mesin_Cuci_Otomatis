#pragma once
// TEMPLATE kredensial. Salin file ini menjadi secrets.h di folder yang sama,
// lalu isi nilainya. secrets.h tidak boleh di-commit.

#ifndef WIFI_SSID
#define WIFI_SSID "NAMA_WIFI"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "PASSWORD_WIFI"
#endif

// Token dari @BotFather
#ifndef TELEGRAM_BOT_TOKEN
#define TELEGRAM_BOT_TOKEN "ISI_TOKEN_BOT"
#endif

// Chat ID tujuan notifikasi (lihat dari bot, mis. @userinfobot)
#ifndef TELEGRAM_CHAT_ID
#define TELEGRAM_CHAT_ID "ISI_CHAT_ID"
#endif

// Daftar WiFi cadangan awal (opsional, sampai 4 tambahan; bisa juga lewat Telegram/web)
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

// Password flash lewat jaringan (ArduinoOTA)
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "PASSWORD_OTA"
#endif
