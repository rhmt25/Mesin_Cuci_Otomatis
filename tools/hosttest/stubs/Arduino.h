#pragma once
// Stub Arduino/FreeRTOS minimal untuk uji logika mesin di PC (g++).
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <string>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define OUTPUT_OPEN_DRAIN 3
#define ESP_ARDUINO_VERSION_MAJOR 3
#define RTC_NOINIT_ATTR
#define PROGMEM
#define ESP_OK 0
typedef int esp_err_t;

using std::max;
using std::min;
template <class T, class A, class B> T constrain(T v, A lo, B hi) { return v < lo ? lo : (v > hi ? hi : v); }

unsigned long millis();
inline uint32_t esp_random() { return (uint32_t)rand() * 2654435761u; }
void pinMode(uint8_t, uint8_t);
void digitalWrite(uint8_t, uint8_t);
int digitalRead(uint8_t);
size_t strlcpy(char* dst, const char* src, size_t n);
#ifdef _WIN32
#define localtime_r(t, tm) (*(tm) = *localtime(t), (tm))
#endif
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& c) : s(c) {}
  String(char c) : s(1, c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  unsigned length() const { return (unsigned)s.size(); }
  const char* c_str() const { return s.c_str(); }
  bool equalsIgnoreCase(const String& o) const {
    if (s.size() != o.s.size()) return false;
    for (size_t i = 0; i < s.size(); i++)
      if (tolower(s[i]) != tolower(o.s[i])) return false;
    return true;
  }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(const char* o) { s += o; return *this; }
  bool operator==(const String& o) const { return s == o.s; }
  bool operator==(const char* o) const { return s == o; }
  bool operator!=(const char* o) const { return s != o; }
  char operator[](unsigned i) const { return s[i]; }
};
inline String operator+(const String& a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, const char* b) { String r(a); r += b; return r; }
inline String operator+(const char* a, const String& b) { String r(a); r += b; return r; }

// --- FreeRTOS ---
typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
struct HostQueue { std::deque<std::string> q; size_t item; };
typedef HostQueue* QueueHandle_t;
typedef void* SemaphoreHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(x) (x)
QueueHandle_t xQueueCreate(unsigned n, unsigned item);
int xQueueSend(QueueHandle_t q, const void* item, TickType_t);
int xQueueReceive(QueueHandle_t q, void* item, TickType_t);
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return (void*)1; }
inline int xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
inline TickType_t xTaskGetTickCount() { return 0; }
inline void vTaskDelayUntil(TickType_t*, TickType_t) {}
inline int xTaskCreatePinnedToCore(void (*)(void*), const char*, unsigned, void*, unsigned, TaskHandle_t*, int) { return pdTRUE; }
