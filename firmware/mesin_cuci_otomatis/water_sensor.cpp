// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#include "water_sensor.h"
#include "config.h"

static bool lastRaw = false;
static uint32_t changedAt = 0;

void sensorInit() {
  pinMode(PIN_WATER_LEVEL, INPUT_PULLUP);
  lastRaw = digitalRead(PIN_WATER_LEVEL) == HIGH;
  changedAt = millis();
}

void sensorUpdate() {
  bool raw = digitalRead(PIN_WATER_LEVEL) == HIGH;
  if (raw != lastRaw) {
    lastRaw = raw;
    changedAt = millis();
  }
}

bool sensorRaw() { return lastRaw; }
uint32_t sensorSteadyMs() { return millis() - changedAt; }
bool sensorStable(bool level, uint32_t ms) { return lastRaw == level && sensorSteadyMs() >= ms; }
