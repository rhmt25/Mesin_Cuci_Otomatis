#pragma once
// Command layer tunggal: Web lokal, Web jarak jauh, dan Serial/Konsol semuanya memanggil
// executeCommand(). Ini yang menjamin paritas kontrol 100%.
#include <Arduino.h>

enum CmdSource : uint8_t { SRC_REMOTE, SRC_WEB, SRC_SERIAL };

struct CmdResult {
  bool ok;
  String msg;
};

CmdResult executeCommand(const String& command, const String& args, CmdSource src);
String normalizeCommand(const String& raw);

// Teks laporan (dipakai Telegram & konsol)
String statusText();
String settingsText();
String helpText();
String historyText();
String statsText();
String wifiListText();
String airText();

// Konsol serial/web: baris diketik seperti di Serial Monitor
String consoleExec(const String& line, CmdSource src);
