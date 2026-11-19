#pragma once
// Command layer tunggal: Telegram, Web, dan Serial/Konsol semuanya memanggil
// executeCommand(). Ini yang menjamin paritas kontrol 100%.
#include <Arduino.h>

enum CmdSource : uint8_t { SRC_TELEGRAM, SRC_WEB, SRC_SERIAL };

struct CmdResult {
  bool ok;
  String msg;
};

// Info untuk alur "kirim nilai" di Telegram (perintah tanpa argumen -> tanya satu per satu)
struct CmdInfo {
  String name;            // nama kanonik (alias sudah diselesaikan)
  String fixedArgs;       // argumen bawaan alias (mis. set_mode_gentle -> "gentle")
  uint8_t nPrompts;
  const char* prompts[3];
  char joiner;            // pemisah argumen yang dikumpulkan
  bool danger;            // minta konfirmasi di Telegram
};

CmdResult executeCommand(const String& command, const String& args, CmdSource src);
bool cmdInfo(const String& command, CmdInfo& out);
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
