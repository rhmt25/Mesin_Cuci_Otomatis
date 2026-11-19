#pragma once
// Sensor level air dengan debounce non-blocking. HIGH = ada air.
#include <Arduino.h>

void sensorInit();
void sensorUpdate();                        // panggil tiap tick mesin
bool sensorRaw();                           // true = ada air
bool sensorStable(bool level, uint32_t ms); // nilai == level dan tidak berubah >= ms
uint32_t sensorSteadyMs();                  // sudah berapa lama nilai tidak berubah
