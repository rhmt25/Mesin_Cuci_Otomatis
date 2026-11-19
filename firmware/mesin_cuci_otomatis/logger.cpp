#include "logger.h"
#include <esp_system.h>
#include <time.h>
#include <stdarg.h>

static LogEntry ring[LOG_RING_SIZE];
static uint32_t nextSeq = 1;
static SemaphoreHandle_t logMutex = nullptr;

// Jejak crash: bertahan saat soft-reset / watchdog
#define TRACE_MAGIC 0xC0FFEE42u
RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint8_t rtcHead;
RTC_NOINIT_ATTR static char rtcTrace[RTC_TRACE_LINES][RTC_TRACE_LEN];
static String previousTrace;
static const char* resetReasonText = "tidak diketahui";
static volatile bool serialDebug = false;

static const char* reasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "Daya dinyalakan";
    case ESP_RST_SW: return "Restart perangkat lunak";
    case ESP_RST_PANIC: return "Crash (panic)";
    case ESP_RST_INT_WDT: return "Watchdog interupsi";
    case ESP_RST_TASK_WDT: return "Watchdog task";
    case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_BROWNOUT: return "Tegangan drop (brownout)";
    case ESP_RST_DEEPSLEEP: return "Deep sleep";
    case ESP_RST_EXT: return "Reset eksternal";
    default: return "Lainnya";
  }
}

// Buang byte UTF-8 yang terpotong di akhir buffer
static void trimUtf8(char* buf) {
  size_t len = strlen(buf);
  size_t i = len;
  while (i > 0 && (buf[i - 1] & 0xC0) == 0x80) i--;   // lewati byte lanjutan
  if (i > 0 && (buf[i - 1] & 0x80)) {                  // byte awal multibyte
    uint8_t lead = buf[i - 1];
    size_t need = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : 2;
    if (len - (i - 1) < need) buf[i - 1] = 0;
  }
}

void loggerInit() {
  if (!logMutex) logMutex = xSemaphoreCreateMutex();
  esp_reset_reason_t reason = esp_reset_reason();
  resetReasonText = reasonName(reason);

  bool valid = (rtcMagic == TRACE_MAGIC) && reason != ESP_RST_POWERON && rtcHead < RTC_TRACE_LINES;
  if (valid) {
    for (int i = 0; i < RTC_TRACE_LINES; i++) {
      const char* line = rtcTrace[(rtcHead + i) % RTC_TRACE_LINES];
      if (line[0]) {
        previousTrace += line;
        previousTrace += '\n';
      }
    }
  }
  rtcMagic = TRACE_MAGIC;
  rtcHead = 0;
  memset(rtcTrace, 0, sizeof(rtcTrace));
}

const char* logLevelName(uint8_t level) {
  static const char* names[] = {"DEBUG", "INFO", "STEP", "WARN", "ERROR", "DONE"};
  return level < 6 ? names[level] : "?";
}

const char* logLevelEmoji(uint8_t level) {
  static const char* e[] = {"", "", "\xE2\x96\xB6 ", "\xE2\x9A\xA0\xEF\xB8\x8F ", "\xE2\x9D\x8C ",
                            "\xE2\x9C\x85 "};
  return level < 6 ? e[level] : "";
}

static void pushEntry(LogLevel level, const char* text) {
  if (!logMutex) return;
  xSemaphoreTake(logMutex, portMAX_DELAY);
  LogEntry& e = ring[nextSeq % LOG_RING_SIZE];
  e.seq = nextSeq++;
  e.ms = millis();
  time_t now = time(nullptr);
  e.epoch = now > 1700000000 ? (uint32_t)now : 0;
  e.level = level;
  strncpy(e.text, text, LOG_TEXT_LEN - 1);
  e.text[LOG_TEXT_LEN - 1] = 0;
  trimUtf8(e.text);

  // jejak RTC (potongan singkat)
  char* slot = rtcTrace[rtcHead];
  snprintf(slot, RTC_TRACE_LEN, "%lu %c %s", (unsigned long)(e.ms / 1000),
           "DISWED"[level < 6 ? level : 0], e.text);
  trimUtf8(slot);
  rtcHead = (rtcHead + 1) % RTC_TRACE_LINES;

  if (level > LOG_DEBUG || serialDebug) Serial.printf("[%8lu] %-5s %s\n", (unsigned long)e.ms, logLevelName(level), e.text);
  xSemaphoreGive(logMutex);
}

void logf(LogLevel level, const char* fmt, ...) {
  char buf[LOG_TEXT_LEN + 32];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  pushEntry(level, buf);
}

void logStr(LogLevel level, const String& text) { pushEntry(level, text.c_str()); }

uint32_t logLastSeq() { return nextSeq - 1; }

size_t logRead(uint32_t sinceSeq, LogEntry* out, size_t maxCount) {
  if (!logMutex) return 0;
  size_t n = 0;
  xSemaphoreTake(logMutex, portMAX_DELAY);
  uint32_t last = nextSeq - 1;
  uint32_t first = last >= LOG_RING_SIZE ? last - LOG_RING_SIZE + 1 : 1;
  uint32_t from = sinceSeq + 1 > first ? sinceSeq + 1 : first;
  for (uint32_t s = from; s <= last && n < maxCount; s++) out[n++] = ring[s % LOG_RING_SIZE];
  xSemaphoreGive(logMutex);
  return n;
}

void loggerSetSerialDebug(bool on) { serialDebug = on; }
bool loggerSerialDebug() { return serialDebug; }

const char* loggerResetReason() { return resetReasonText; }
String loggerPreviousTrace() { return previousTrace; }
