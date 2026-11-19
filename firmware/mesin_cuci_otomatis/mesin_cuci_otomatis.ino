// Mesin Cuci Otomatis - firmware ESP32 (WROOM)
// Core 1: kontrol mesin (machine.cpp). Core 0: WiFi, Telegram, Web.
// Kredensial ada di secrets.h (salin dari secrets.example.h).
#include <WiFi.h>
#include "config.h"
#include "logger.h"
#include "relay.h"
#include "water_sensor.h"
#include "settings.h"
#include "machine.h"
#include "commands.h"
#include "net_wifi.h"
#include "ota.h"
#include "telegram_ui.h"
#include "web_server.h"

static String serialLine;

// Baris yang diketik di Serial Monitor diproses seperti konsol web.
static void pollSerial() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      if (serialLine.length()) {
        logf(LOG_DEBUG, "> %s", serialLine.c_str());
        String out = consoleExec(serialLine, SRC_SERIAL);
        if (out.length()) logStr(LOG_DEBUG, out);
      }
      serialLine = "";
    } else if (serialLine.length() < 200) {
      serialLine += ch;
    }
  }
}

void setup() {
  relayInit();               // paling awal: semua relay mati sebelum hal lain
  Serial.begin(115200);
  loggerInit();
  logf(LOG_INFO, "Mesin Cuci v%s boot (%s)", FW_VERSION, loggerResetReason());

  sensorInit();
  settingsLoad();
  calibLoad();
  statsLoad();
  wifiStoreInit();
  machineBegin();            // task kontrol mesin di Core 1
  netBegin();                // WiFi + hotspot + OTA di Core 0
  webBegin();                // dashboard web + WebSocket
#ifndef NO_TELEGRAM
  telegramBegin();           // Telegram + notifikasi
#endif
}

static void logHeapPeriodically() {
  static uint32_t last = 0;
  if (millis() - last < 30000) return;
  last = millis();
  logf(LOG_DEBUG, "Heap bebas %u B, blok terbesar %u B, minimum %u B", (unsigned)ESP.getFreeHeap(),
       (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getMinFreeHeap());
}

void loop() {
  pollSerial();
  logHeapPeriodically();
  otaMarkValidIfHealthy();
  vTaskDelay(pdMS_TO_TICKS(20));
}
