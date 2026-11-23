// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#pragma once
// Kontrol jarak jauh: SATU koneksi WebSocket (WSS) keluar ke Cloudflare Worker.
// Perintah/permintaan API diterima lewat koneksi ini, status/log dikirim bila ada penonton,
// dan notifikasi dikirim sebagai pesan "notif" (Worker yang meneruskan ke Telegram).
#include <Arduino.h>
void remoteBegin();            // buat task di Core 0
bool remoteConfigured();       // REMOTE_URL dan REMOTE_DEVICE_TOKEN terisi di secrets.h
bool remoteConnected();        // koneksi ke server sedang tersambung

// Ganti password jarak jauh: dikirim SEKALI lewat koneksi TLS ke Worker (yang menghitung hash). Tidak disimpan di ESP32.
// Mengembalikan false + alasan bila tidak terhubung / sedang menunggu jawaban sebelumnya. Hasil lewat remotePasswordState().
bool remoteSetPassword(const String& pw, String& err);
enum RemotePwState : uint8_t { RPW_IDLE, RPW_WAIT, RPW_OK, RPW_FAIL };
RemotePwState remotePasswordState(String& msg);
