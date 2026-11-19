#pragma once
// Pengganti Serial.print*: tulis ke USB + ring buffer (log web/konsol, notifier)
// + jejak crash di memori RTC.
#include <Arduino.h>
#include "config.h"

enum LogLevel : uint8_t { LOG_DEBUG, LOG_INFO, LOG_STEP, LOG_WARN, LOG_ERROR, LOG_DONE };

struct LogEntry {
  uint32_t seq;
  uint32_t ms;
  uint32_t epoch;   // 0 bila jam belum sinkron
  uint8_t level;
  char text[LOG_TEXT_LEN];
};

void loggerInit();
void loggerSetSerialDebug(bool on);   // DEBUG ke USB hanya bila diaktifkan (hemat waktu task)
bool loggerSerialDebug();
void logf(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void logStr(LogLevel level, const String& text);

uint32_t logLastSeq();
// Salin entri dengan seq > sinceSeq (paling lama dulu). Mengembalikan jumlah.
size_t logRead(uint32_t sinceSeq, LogEntry* out, size_t maxCount);

const char* logLevelName(uint8_t level);
const char* logLevelEmoji(uint8_t level);

// Info reset & jejak crash dari boot sebelumnya
const char* loggerResetReason();
String loggerPreviousTrace();
