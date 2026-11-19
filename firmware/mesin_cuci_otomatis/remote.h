#pragma once
// Kontrol jarak jauh: SATU koneksi WebSocket (WSS) keluar ke Cloudflare Worker.
// Perintah/permintaan API diterima lewat koneksi ini, status/log dikirim bila ada penonton,
// dan notifikasi dikirim sebagai pesan "notif" (Worker yang meneruskan ke Telegram).
void remoteBegin();            // buat task di Core 0
bool remoteConfigured();       // REMOTE_URL dan REMOTE_DEVICE_TOKEN terisi di secrets.h
bool remoteConnected();        // koneksi ke server sedang tersambung
