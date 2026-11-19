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
