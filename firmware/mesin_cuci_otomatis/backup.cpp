#include "backup.h"
#include <ArduinoJson.h>
#include <stddef.h>
#include <string.h>
#include "programs.h"
#include "settings.h"

#ifndef FW_VERSION
#include "config.h"
#endif

// Tabel bidang pengaturan (tanpa finishAt: sekali pakai). Satu sumber untuk ekspor & impor.
enum FieldType : uint8_t { F_INT, F_U8, F_FLOAT };
struct Field {
  const char* key;
  FieldType type;
  size_t offset;
};
#define FIELD(k, t, member) {k, t, offsetof(Settings, member)}
static const Field fields[] = {
    FIELD("pola", F_U8, pola), FIELD("soak", F_INT, soak), FIELD("wash", F_INT, wash), FIELD("rinse", F_INT, rinse),
    FIELD("parfume", F_FLOAT, parfume), FIELD("spin", F_INT, spin), FIELD("fillLevel", F_U8, fillLevel),
    FIELD("fillPercent", F_U8, fillPercent), FIELD("fillTime", F_INT, fillTime), FIELD("flH", F_INT, flH),
    FIELD("flM", F_INT, flM), FIELD("flS", F_INT, flS), FIELD("fillRetryCount", F_INT, fillRetryCount),
    FIELD("fillRetryWait", F_INT, fillRetryWait), FIELD("drainMode", F_U8, drainMode),
    FIELD("drainTime", F_INT, drainTime), FIELD("drainSessionLimit", F_INT, drainSessionLimit),
    FIELD("drainProbeSec", F_INT, drainProbeSec), FIELD("fillPrecheck", F_U8, fillPrecheck),
    FIELD("unjamCount", F_INT, unjamCount), FIELD("dH", F_INT, dH), FIELD("dM", F_INT, dM), FIELD("dS", F_INT, dS),
    FIELD("notif", F_U8, notif), FIELD("remoteOn", F_U8, remoteOn),
};
static const size_t nFields = sizeof(fields) / sizeof(fields[0]);

static double getField(const Settings& s, const Field& f) {
  const char* base = reinterpret_cast<const char*>(&s) + f.offset;
  switch (f.type) {
    case F_INT: return *reinterpret_cast<const int*>(base);
    case F_U8: return *reinterpret_cast<const uint8_t*>(base);
    default: return *reinterpret_cast<const float*>(base);
  }
}
static void setField(Settings& s, const Field& f, double v) {
  char* base = reinterpret_cast<char*>(&s) + f.offset;
  switch (f.type) {
    case F_INT: *reinterpret_cast<int*>(base) = (int)v; break;
    case F_U8: *reinterpret_cast<uint8_t*>(base) = (uint8_t)v; break;
    default: *reinterpret_cast<float*>(base) = (float)v; break;
  }
}

// ---------------------------------------------------------------- ekspor ----
std::string backupBuild(bool includeWifi) {
  JsonDocument d;
  d["app"] = "mesin-cuci";
  d["version"] = BACKUP_VERSION;
  d["fw"] = FW_VERSION;
  JsonObject st = d["settings"].to<JsonObject>();
  for (size_t i = 0; i < nFields; i++) {
    if (fields[i].type == F_FLOAT) st[fields[i].key] = (float)getField(S, fields[i]);
    else st[fields[i].key] = (int)getField(S, fields[i]);
  }
  JsonArray custom = d["custom"].to<JsonArray>();
  for (int slot = 0; slot < CUSTOM_SLOTS; slot++) {
    CustomProc p;
    if (!customLoad(slot, p)) continue;
    JsonObject o = custom.add<JsonObject>();
    o["slot"] = slot + 1;
    o["name"] = p.name;
    JsonArray items = o["items"].to<JsonArray>();
    for (int i = 0; i < p.count; i++) {
      JsonObject it = items.add<JsonObject>();
      it["t"] = customTypeName(p.items[i].type);
      it["a"] = p.items[i].a;
      it["b"] = p.items[i].b;
      it["p"] = p.items[i].pola;
      it["c"] = p.items[i].cups2;
    }
  }
  JsonArray wifi = d["wifi"].to<JsonArray>();
  for (int i = 0; i < wifiCount(); i++) {
    WifiCred c;
    wifiGet(i, c);
    JsonObject o = wifi.add<JsonObject>();
    o["ssid"] = c.ssid;
    if (includeWifi) o["pass"] = c.pass;
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
  r.settingsChanged = r.customSlots = r.wifiNetworks = r.wifiNoPassword = r.clamped = 0;
  return r;
}

RestoreReport backupApply(const char* json, size_t len, bool dry) {
  if (!json || len == 0) return reject("Berkas kosong.");
  if (len > RESTORE_MAX_BYTES) return reject("Berkas terlalu besar (maks 12 KB).");
  JsonDocument d;
  if (deserializeJson(d, json, len) != DeserializationError::Ok) return reject("Berkas bukan JSON yang valid.");
  if (strcmp(d["app"] | "", "mesin-cuci") != 0) return reject("Bukan berkas cadangan Mesin Cuci.");
  if ((d["version"] | 0) != BACKUP_VERSION) return reject("Versi cadangan tidak didukung.");

  RestoreReport rep;
  rep.ok = true;
  rep.settingsChanged = rep.customSlots = rep.wifiNetworks = rep.wifiNoPassword = rep.clamped = 0;

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
  }
  Settings clampedNext = next;
  settingsSanitize(clampedNext);
  for (size_t i = 0; i < nFields; i++) {
    double before = getField(next, fields[i]), after = getField(clampedNext, fields[i]);
    if (before != after) rep.clamped++;
    if (after != getField(S, fields[i])) rep.settingsChanged++;
  }

  // ---- custom proses (semua divalidasi dulu) ----
  CustomProc procs[CUSTOM_SLOTS];
  bool has[CUSTOM_SLOTS] = {false};
  JsonArrayConst custom = d["custom"];
  if (!custom.isNull()) {
    for (JsonObjectConst o : custom) {
      int slot = (o["slot"] | 0) - 1;
      if (slot < 0 || slot >= CUSTOM_SLOTS) return reject("Nomor slot Custom Proses tidak valid.");
      if (has[slot]) return reject("Slot Custom Proses ganda di berkas.");
      CustomProc& p = procs[slot];
      memset(&p, 0, sizeof(p));
      strlcpy(p.name, (const char*)(o["name"] | ""), sizeof(p.name));
      JsonArrayConst items = o["items"];
      if (items.isNull() || items.size() > CUSTOM_MAX_ITEMS) return reject("Langkah Custom Proses tidak valid.");
      for (JsonObjectConst it : items) {
        int t = customTypeFromName(String((const char*)(it["t"] | "")));
        if (t < 0) return reject("Jenis langkah Custom Proses tidak dikenal.");
        CustomItem& ci = p.items[p.count++];
        ci.type = (uint8_t)t;
        ci.a = it["a"] | 0;
        ci.b = it["b"] | 0;
        ci.pola = it["p"] | 3;
        ci.cups2 = it["c"] | 0;
      }
      String err;
      if (!customValidate(p, err)) return reject(std::string("Custom Proses slot ") + std::to_string(slot + 1) + ": " + err.c_str());
      has[slot] = true;
      rep.customSlots++;
    }
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

  char buf[200];
  snprintf(buf, sizeof(buf), "%d pengaturan berubah, %d Custom Proses, %d WiFi%s%s", rep.settingsChanged, rep.customSlots,
           rep.wifiNetworks, rep.wifiNoPassword ? " (ada yang perlu password diisi manual)" : "",
           rep.clamped ? ", beberapa nilai disesuaikan ke rentang yang valid" : "");
  rep.msg = buf;
  if (dry) return rep;

  // ---- terapkan (semua sudah valid) ----
  S = clampedNext;
  settingsSave();
  if (!custom.isNull()) {
    for (int slot = 0; slot < CUSTOM_SLOTS; slot++) {
      if (has[slot]) customSave(slot, procs[slot]);
      else customDelete(slot);
    }
  }
  if (!wifi.isNull()) {
    while (wifiCount() > 0) wifiRemove(0);
    for (int i = 0; i < nNets; i++) wifiAdd(nets[i].ssid, nets[i].pass);
  }
  rep.msg = std::string("Dipulihkan: ") + buf;
  return rep;
}
