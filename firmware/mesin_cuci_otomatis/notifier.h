// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#pragma once
// Notifier: filter mode (Lengkap/Ringkas/Senyap), batching 1,5 dtk, antrean saat offline.
// Dipanggil hanya dari task remote; teks dikirim ke Worker yang meneruskannya ke Telegram.
#include <Arduino.h>

void notifierInit();
void notifierPoll();                       // tarik log baru dari logger
void notifierSetOnline(bool online);
bool notifierNextMessage(String& out);     // true bila ada pesan siap kirim
const char* notifModeName(uint8_t mode);
