// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#include "ota.h"
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include "config.h"
#include "logger.h"
#include "machine.h"
#include "relay.h"

static volatile bool busy = false;
static volatile uint8_t progress = 0;
static size_t expectedSize = 0;
static size_t written = 0;
static bool markedValid = false;

bool otaBusy() { return busy; }
uint8_t otaProgress() { return progress; }

static void stopMachineForFlash() {
  machineHold(true);
  MachineCmd c = {};
  c.type = MC_STOP;
  c.ws = WS_ALL;
  machinePost(c);
  relayAllOff();
}

// ----------------------------------------------------------------- Web flash ----
bool otaWebBegin(size_t totalSize, String& err) {
  if (!machineIsIdle()) {
    err = "Mesin sedang bekerja. Hentikan proses dulu.";
    return false;
  }
  if (busy) {
    err = "Pembaruan lain sedang berjalan.";
    return false;
  }
  stopMachineForFlash();
  size_t size = totalSize ? totalSize : UPDATE_SIZE_UNKNOWN;
  if (!Update.begin(size, U_FLASH)) {
    machineHold(false);
    err = String("Update.begin gagal: ") + Update.errorString();
    return false;
  }
  busy = true;
  progress = 0;
  written = 0;
  expectedSize = totalSize;
  logf(LOG_WARN, "Flash web: mulai (%u byte)", (unsigned)totalSize);
  return true;
}

bool otaWebWrite(uint8_t* data, size_t len) {
  if (!busy) return false;
  if (Update.write(data, len) != len) return false;
  written += len;
  if (expectedSize) progress = (uint8_t)((uint64_t)written * 100 / expectedSize);
  return true;
}

bool otaWebEnd(String& err) {
  if (!busy) {
    err = "Tidak ada pembaruan aktif.";
    return false;
  }
  bool ok = Update.end(true);   // verifikasi ukuran & checksum
  busy = false;
  if (!ok) {
    machineHold(false);
    err = String("Verifikasi gagal: ") + Update.errorString();
    logf(LOG_ERROR, "Flash web gagal: %s", Update.errorString());
    return false;
  }
  progress = 100;
  logf(LOG_WARN, "Flash web sukses, restart ke firmware baru");
  return true;
}

void otaWebAbort() {
  if (!busy) return;
  Update.abort();
  busy = false;
  machineHold(false);
  logf(LOG_ERROR, "Flash web dibatalkan");
}

// ------------------------------------------------------------ Info & rollback ----
static bool partitionHasImage(const esp_partition_t* p) {
  if (!p) return false;
  uint8_t magic = 0;
  if (esp_partition_read(p, 0, &magic, 1) != ESP_OK) return false;
  return magic == 0xE9;
}

String otaInfoJson() {
  JsonDocument doc;
  const esp_partition_t* run = esp_ota_get_running_partition();
  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  doc["version"] = FW_VERSION;
  doc["build"] = String(__DATE__) + " " + __TIME__;
  doc["running"] = run ? run->label : "?";
  doc["other"] = other ? other->label : "?";
  doc["sketchSize"] = ESP.getSketchSize();
  doc["freeSpace"] = ESP.getFreeSketchSpace();
  bool canRollback = other && other != run && partitionHasImage(other);
  doc["canRollback"] = canRollback;
  if (canRollback) {
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(other, &desc) == ESP_OK) {
      doc["previousBuild"] = String(desc.date) + " " + desc.time;
      doc["previousVersion"] = desc.version;
    }
  }
  String out;
  serializeJson(doc, out);
  return out;
}

bool otaRollback(String& err) {
  if (!machineIsIdle()) {
    err = "Mesin sedang bekerja.";
    return false;
  }
  const esp_partition_t* run = esp_ota_get_running_partition();
  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  if (!other || other == run || !partitionHasImage(other)) {
    err = "Tidak ada firmware sebelumnya.";
    return false;
  }
  if (esp_ota_set_boot_partition(other) != ESP_OK) {
    err = "Gagal mengatur partisi boot.";
    return false;
  }
  logf(LOG_WARN, "Rollback: boot berikutnya memakai partisi %s", other->label);
  return true;
}

void otaMarkValidIfHealthy() {
  if (markedValid || millis() < 30000) return;
  markedValid = true;
  // Bila bootloader mendukung rollback, ini menandai firmware valid. Bila tidak, tidak berefek.
  esp_ota_mark_app_valid_cancel_rollback();
}
