#pragma once
// Notifier Telegram: filter mode (Lengkap/Ringkas/Senyap), batching 1,5 dtk,
// antrean saat offline. Dipanggil hanya dari task Telegram.
#include <Arduino.h>

void notifierInit();
void notifierPoll();                       // tarik log baru dari logger
void notifierSetOnline(bool online);
bool notifierNextMessage(String& out);     // true bila ada pesan siap kirim
const char* notifModeName(uint8_t mode);
