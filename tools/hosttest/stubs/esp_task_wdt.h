#pragma once
#include "Arduino.h"
struct esp_task_wdt_config_t { uint32_t timeout_ms; uint32_t idle_core_mask; bool trigger_panic; };
inline esp_err_t esp_task_wdt_add(void*) { return 0; }
inline esp_err_t esp_task_wdt_reset() { return 0; }
inline esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t*) { return 0; }
inline esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t*) { return 0; }
