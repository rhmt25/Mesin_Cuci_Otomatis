// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#pragma once
// Penyimpanan Proses sebagai berkas JSON di LittleFS (partisi "spiffs"). Tulis atomik: pN.tmp -> rename.
#include <Arduino.h>
#include "tahap.h"

enum SlotState : uint8_t { SLOT_EMPTY, SLOT_OK, SLOT_BAD };

// slot di sini = id penyimpanan 0..PROSES_TOTAL-1, yaitu prosesId(ws, nomor)
bool prosesStoreBegin();                   // pasang LittleFS (format hanya bila belum pernah diformat)
bool prosesStoreReady();
SlotState prosesSlotState(uint8_t slot);   // SLOT_BAD = berkas ada tetapi rusak/tidak valid
bool prosesLoad(uint8_t slot, Proses& out);
bool prosesSave(uint8_t slot, const Proses& p);   // false bila gagal tulis
bool prosesDelete(uint8_t slot);

#ifdef HOST_TEST
void prosesStoreHostReset();
void prosesStoreHostPut(uint8_t slot, const String& raw);   // isi mentah (untuk menguji berkas rusak)
String prosesStoreHostGet(uint8_t slot);
void prosesStoreHostFailWrites(bool fail);
#endif
