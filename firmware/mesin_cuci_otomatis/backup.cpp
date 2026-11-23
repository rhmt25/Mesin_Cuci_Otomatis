// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#include "backup.h"
#include <ArduinoJson.h>
#include <stddef.h>
#include <string.h>
#include "settings.h"

#ifndef FW_VERSION
#include "config.h"
#endif

// Pengaturan global yang tersisa. Satu sumber untuk ekspor & impor.
enum FieldType : uint8_t { F_U8 };
struct Field {
  const char* key;
  FieldType type;
  size_t offset;
};
#define FIELD(k, t, member) {k, t, offsetof(Settings, member)}
static const Field fields[] = {
    FIELD("notif", F_U8, notif),
    FIELD("remoteOn", F_U8, remoteOn),
};
static const size_t nFields = sizeof(fields) / sizeof(fields[0]);

static double getField(const Settings& s, const Field& f) { return *(reinterpret_cast<const uint8_t*>(&s) + f.offset); }
static void setField(Settings& s, const Field& f, double v) { *(reinterpret_cast<uint8_t*>(&s) + f.offset) = (uint8_t)v; }

// ---------------------------------------------------------------- ekspor ----
std::string backupBuild(bool includeWifi) {
  JsonDocument d;
  d["app"] = "mesin-cuci";
  d["version"] = BACKUP_VERSION;
  d["fw"] = FW_VERSION;
  JsonObject st = d["settings"].to<JsonObject>();
  for (size_t i = 0; i < nFields; i++) st[fields[i].key] = (int)getField(S, fields[i]);
  JsonArray wifi = d["wifi"].to<JsonArray>();
  for (int i = 0; i < wifiCount(); i++) {
    WifiCred c;
    wifiGet(i, c);
    JsonObject o = wifi.add<JsonObject>();
    o["ssid"] = JsonString((const char*)c.ssid);
    if (includeWifi) o["pass"] = JsonString((const char*)c.pass);
    else o["secured"] = c.pass[0] != 0;
  }
  d["includesWifiPasswords"] = includeWifi;
  std::string out;
  serializeJson(d, out);
  return out;
}

// ---------------------------------------------------------------- impor ----
static RestoreReport reject(const std::string& why) {
  RestoreReport r;
  r.ok = false;
  r.msg = why;
  r.settingsChanged = r.oldIgnored = r.wifiNetworks = r.wifiNoPassword = r.clamped = 0;
  return r;
}

RestoreReport backupApply(const char* json, size_t len, bool dry) {
  if (!json || len == 0) return reject("Berkas kosong.");
  if (len > RESTORE_MAX_BYTES) return reject("Berkas terlalu besar (maks 12 KB).");
  JsonDocument d;
  if (deserializeJson(d, json, len) != DeserializationError::Ok) return reject("Berkas bukan JSON yang valid.");
  if (strcmp(d["app"] | "", "mesin-cuci") != 0) return reject("Bukan berkas cadangan Mesin Cuci.");
  int ver = d["version"] | 0;
  if (ver != BACKUP_VERSION && ver != 1) return reject("Versi cadangan tidak didukung.");

  RestoreReport rep;
  rep.ok = true;
  rep.settingsChanged = rep.oldIgnored = rep.wifiNetworks = rep.wifiNoPassword = rep.clamped = 0;

  // ---- pengaturan ----
  Settings next = S;
  JsonObjectConst st = d["settings"];
  if (!st.isNull()) {
    for (size_t i = 0; i < nFields; i++) {
      JsonVariantConst v = st[fields[i].key];
      if (v.isNull()) continue;
      if (!v.is<double>() && !v.is<int>()) return reject(std::string("Nilai pengaturan tidak valid: ") + fields[i].key);
      setField(next, fields[i], v.as<double>());
    }
    // variabel global versi lama (air, putar, pewangi, ...) kini ada di tiap Tahap: diabaikan
    for (JsonPairConst kv : st) {
      bool known = false;
      for (size_t i = 0; i < nFields; i++)
        if (strcmp(kv.key().c_str(), fields[i].key) == 0) known = true;
      if (!known) rep.oldIgnored++;
    }
  }
  JsonArrayConst oldCustom = d["custom"];   // Custom Proses lama (format blok) tidak dikonversi
  if (!oldCustom.isNull()) rep.oldIgnored += (int)oldCustom.size();
  Settings clampedNext = next;
  settingsSanitize(clampedNext);
  for (size_t i = 0; i < nFields; i++) {
    double before = getField(next, fields[i]), after = getField(clampedNext, fields[i]);
    if (before != after) rep.clamped++;
    if (after != getField(S, fields[i])) rep.settingsChanged++;
  }

  // ---- WiFi ----
  WifiCred nets[MAX_WIFI_NETWORKS];
  int nNets = 0;
  JsonArrayConst wifi = d["wifi"];
  if (!wifi.isNull()) {
    if (wifi.size() > MAX_WIFI_NETWORKS) return reject("Terlalu banyak WiFi di berkas (maks 5).");
    for (JsonObjectConst o : wifi) {
      const char* ssid = o["ssid"] | "";
      const char* pass = o["pass"] | "";
      if (!ssid[0] || strlen(ssid) > 32 || strlen(pass) > 64) return reject("Data WiFi tidak valid.");
      WifiCred& c = nets[nNets++];
      memset(&c, 0, sizeof(c));
      strlcpy(c.ssid, ssid, sizeof(c.ssid));
      if (!o["pass"].isNull()) {
        strlcpy(c.pass, pass, sizeof(c.pass));
      } else {   // password tidak ada di berkas: pertahankan yang sudah tersimpan untuk SSID yang sama
        bool kept = false;
        for (int i = 0; i < wifiCount() && !kept; i++) {
          WifiCred old;
          wifiGet(i, old);
          if (strcmp(old.ssid, ssid) == 0) { strlcpy(c.pass, old.pass, sizeof(c.pass)); kept = true; }
        }
        if (!kept && (o["secured"] | false)) rep.wifiNoPassword++;
      }
      rep.wifiNetworks++;
    }
  }

  char buf[220];
  snprintf(buf, sizeof(buf), "%d pengaturan berubah, %d WiFi%s%s%s", rep.settingsChanged, rep.wifiNetworks,
           rep.wifiNoPassword ? " (ada yang perlu password diisi manual)" : "",
           rep.clamped ? ", beberapa nilai disesuaikan ke rentang yang valid" : "",
           rep.oldIgnored ? "; variabel global dan Custom Proses versi lama diabaikan (kini per Tahap, buat ulang di tab Proses)" : "");
  rep.msg = buf;
  if (dry) return rep;

  // ---- terapkan (semua sudah valid) ----
  S = clampedNext;
  settingsSave();
  if (!wifi.isNull()) {
    while (wifiCount() > 0) wifiRemove(0);
    for (int i = 0; i < nNets; i++) wifiAdd(nets[i].ssid, nets[i].pass);
  }
  rep.msg = std::string("Dipulihkan: ") + buf;
  return rep;
}
