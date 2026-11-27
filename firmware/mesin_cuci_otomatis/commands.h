// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#pragma once
// Command layer tunggal: Web lokal, Web jarak jauh, dan Serial/Konsol semuanya memanggil
// executeCommand(). Ini yang menjamin paritas kontrol 100%.
#include <Arduino.h>
#include "tahap.h"

enum CmdSource : uint8_t { SRC_REMOTE, SRC_WEB, SRC_SERIAL };

struct CmdResult {
  bool ok;
  String msg;
};

CmdResult executeCommand(const String& command, const String& args, CmdSource src);
String normalizeCommand(const String& raw);

// Dipakai juga oleh API (/api/run, /api/decide): cek mesin siap, validasi, kirim ke mesin
CmdResult commandRunProses(int slot, uint32_t waitSec, int finishAtMin, uint8_t ws = WS_CUCI);   // slot 1..10
CmdResult commandRunManual(const Tahap& t, uint8_t ws = WS_CUCI);
CmdResult commandDecide(const String& choice, uint8_t ws = WS_CUCI);       // ulang|lanjut|batal|buang|pakai

// Teks laporan (dipakai konsol/perintah)
String statusText();
String settingsText();
String helpText();
String historyText();
String statsText();
String wifiListText();
String prosesListText();

// Konsol serial/web: baris diketik seperti di Serial Monitor
String consoleExec(const String& line, CmdSource src);
