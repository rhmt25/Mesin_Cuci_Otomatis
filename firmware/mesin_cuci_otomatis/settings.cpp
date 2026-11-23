// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#include "settings.h"
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include "logger.h"

Settings S;
Calib C;
Stats ST;

static WifiCred wifiList[MAX_WIFI_NETWORKS];
static int wifiN = 0;

void settingsDefaults(Settings& s) {
  s.notif = NOTIF_LENGKAP;
  s.remoteOn = 1;
}

static void sanitize(Settings& s) {
  Settings d;
  settingsDefaults(d);
  if (s.notif > NOTIF_SENYAP) s.notif = d.notif;
  if (s.remoteOn > 1) s.remoteOn = d.remoteOn;
}

void settingsSanitize(Settings& s) { sanitize(s); }

// Versi 3: variabel global lama (air, putar, pewangi, delay) dan Custom Proses lama dihapus; semuanya kini per Tahap.
static void migrateV3() {
  Preferences p;
  p.begin("settings", false);
  if (p.isKey("v3")) {
    p.end();
    return;
  }
  uint8_t notif = p.getUChar("notif", NOTIF_LENGKAP);
  uint8_t remoteOn = p.getUChar("remoteOn", 1);
  p.clear();
  p.putUChar("notif", notif);
  p.putUChar("remoteOn", remoteOn);
  p.putBool("v3", true);
  p.end();
  Preferences cu;   // Custom Proses lama (format blok gabungan) tidak dikonversi
  cu.begin("custom", false);
  cu.clear();
  cu.end();
  Preferences r;    // data pemulihan lama merujuk ke ID program yang sudah tidak ada
  r.begin("resume", false);
  r.clear();
  r.end();
  Preferences st;   // riwayat lama memakai format tanpa nama Proses
  st.begin("stats", false);
  st.remove("hist");
  st.putUChar("hCount", 0);
  st.putUChar("hHead", 0);
  st.end();
}

void settingsLoad() {
  migrateV3();
  Preferences p;
  p.begin("settings", true);
  settingsDefaults(S);
  S.notif = p.getUChar("notif", S.notif);
  S.remoteOn = p.getUChar("remoteOn", S.remoteOn);
  p.end();
  sanitize(S);
}

void settingsSave() {
  sanitize(S);
  Preferences p;
  p.begin("settings", false);
  p.putUChar("notif", S.notif);
  p.putUChar("remoteOn", S.remoteOn);
  p.putBool("v3", true);
  p.end();
}

void settingsReset() {
  settingsDefaults(S);
  settingsSave();
}

// ---------------------------------------------------------------- WiFi ----
static void wifiSave() {
  Preferences p;
  p.begin("wifi", false);
  p.putInt("count", wifiN);
  for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
    char k[8];
    snprintf(k, sizeof(k), "ssid%d", i);
    if (i < wifiN) p.putString(k, wifiList[i].ssid); else p.remove(k);
    snprintf(k, sizeof(k), "pass%d", i);
    if (i < wifiN) p.putString(k, wifiList[i].pass); else p.remove(k);
  }
  p.end();
}

int wifiCount() { return wifiN; }

bool wifiGet(int index, WifiCred& out) {
  if (index < 0 || index >= wifiN) return false;
  out = wifiList[index];
  return true;
}

bool wifiAdd(const String& ssid, const String& pass) {
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) return false;
  for (int i = 0; i < wifiN; i++) {
    if (ssid == wifiList[i].ssid) {   // perbarui password
      strncpy(wifiList[i].pass, pass.c_str(), sizeof(wifiList[i].pass) - 1);
      wifiSave();
      return true;
    }
  }
  if (wifiN >= MAX_WIFI_NETWORKS) return false;
  strncpy(wifiList[wifiN].ssid, ssid.c_str(), sizeof(wifiList[wifiN].ssid) - 1);
  strncpy(wifiList[wifiN].pass, pass.c_str(), sizeof(wifiList[wifiN].pass) - 1);
  wifiN++;
  wifiSave();
  return true;
}

bool wifiRemove(int index) {
  if (index < 0 || index >= wifiN) return false;
  for (int i = index; i < wifiN - 1; i++) wifiList[i] = wifiList[i + 1];
  wifiN--;
  memset(&wifiList[wifiN], 0, sizeof(WifiCred));
  wifiSave();
  return true;
}

bool wifiMove(int from, int to) {
  if (from < 0 || from >= wifiN || to < 0 || to >= wifiN) return false;
  WifiCred tmp = wifiList[from];
  if (from < to) for (int i = from; i < to; i++) wifiList[i] = wifiList[i + 1];
  else for (int i = from; i > to; i--) wifiList[i] = wifiList[i - 1];
  wifiList[to] = tmp;
  wifiSave();
  return true;
}

void wifiStoreInit() {
  memset(wifiList, 0, sizeof(wifiList));
  Preferences p;
  p.begin("wifi", false);
  bool fresh = !p.isKey("count");
  if (!fresh) {
    wifiN = constrain(p.getInt("count", 0), 0, MAX_WIFI_NETWORKS);
    for (int i = 0; i < wifiN; i++) {
      char k[8];
      snprintf(k, sizeof(k), "ssid%d", i);
      p.getString(k, wifiList[i].ssid, sizeof(wifiList[i].ssid));
      snprintf(k, sizeof(k), "pass%d", i);
      p.getString(k, wifiList[i].pass, sizeof(wifiList[i].pass));
    }
    p.end();
    return;
  }
  p.end();
  wifiN = 0;

  // Migrasi dari namespace lama "wifi-config"
  Preferences old;
  if (old.begin("wifi-config", true)) {
    String s = old.getString("ssid", "");
    String pw = old.getString("password", "");
    old.end();
    if (s.length()) {
      wifiAdd(s, pw);
      logf(LOG_INFO, "Migrasi WiFi tersimpan: %s", s.c_str());
    }
  }
  // Default dari secrets.h
  if (strlen(WIFI_SSID)) wifiAdd(WIFI_SSID, WIFI_PASSWORD);
  if (strlen(WIFI_SSID_2)) wifiAdd(WIFI_SSID_2, WIFI_PASSWORD_2);
  wifiSave();
}

// ------------------------------------------------------------- Kalibrasi ----
void calibLoad() {
  Preferences p;
  p.begin("calib", true);
  C.avgFillMs = p.getFloat("fill", 0);
  C.nFill = p.getUShort("nFill", 0);
  p.end();
  Preferences m;   // migrasi: hapus data kalibrasi buang lama
  m.begin("calib", false);
  if (m.isKey("drain")) m.remove("drain");
  if (m.isKey("nDrain")) m.remove("nDrain");
  m.end();
}

void calibSave() {
  Preferences p;
  p.begin("calib", false);
  p.putFloat("fill", C.avgFillMs);
  p.putUShort("nFill", C.nFill);
  p.end();
}

void calibReset() {
  memset(&C, 0, sizeof(C));
  calibSave();
}

// ---------------------------------------------------------------- Resume ----
void resumeSave(uint8_t slot, uint8_t stepIndex, uint16_t rev) {
  Preferences p;
  p.begin("resume", false);
  p.putUChar("slot", slot);
  p.putUChar("step", stepIndex);
  p.putUShort("rev", rev);
  time_t now = time(nullptr);
  p.putUInt("ts", now > 1700000000 ? (uint32_t)now : 0);
  p.putBool("valid", true);
  p.end();
}

bool resumeLoad(ResumeInfo& out) {
  Preferences p;
  p.begin("resume", true);
  out.valid = p.getBool("valid", false);
  out.slot = p.getUChar("slot", 0);
  out.stepIndex = p.getUChar("step", 0);
  out.rev = p.getUShort("rev", 0);
  out.savedAt = p.getUInt("ts", 0);
  p.end();
  return out.valid;
}

void resumeClear() {
  Preferences p;
  p.begin("resume", false);
  p.clear();
  p.end();
}

// ----------------------------------------------------------- Statistik ----
void statsLoad() {
  memset(&ST, 0, sizeof(ST));
  Preferences p;
  p.begin("stats", true);
  ST.totalCycles = p.getUInt("cycles", 0);
  ST.motorSec = p.getUInt("motorSec", 0);
  ST.histCount = p.getUChar("hCount", 0);
  ST.histHead = p.getUChar("hHead", 0);
  if (ST.histCount > 10) ST.histCount = 0;
  if (ST.histHead > 9) ST.histHead = 0;
  if (p.isKey("hist") && p.getBytesLength("hist") == sizeof(ST.hist))
    p.getBytes("hist", ST.hist, sizeof(ST.hist));
  p.end();
}

static void statsSave() {
  Preferences p;
  p.begin("stats", false);
  p.putUInt("cycles", ST.totalCycles);
  p.putUInt("motorSec", ST.motorSec);
  p.putUChar("hCount", ST.histCount);
  p.putUChar("hHead", ST.histHead);
  p.putBytes("hist", ST.hist, sizeof(ST.hist));
  p.end();
}

void statsRecord(const char* name, uint32_t startEpoch, uint32_t durSec, uint8_t result) {
  HistoryEntry& e = ST.hist[ST.histHead];
  e.startEpoch = startEpoch;
  e.durSec = durSec;
  strlcpy(e.name, name ? name : "", sizeof(e.name));
  e.result = result;
  ST.histHead = (ST.histHead + 1) % 10;
  if (ST.histCount < 10) ST.histCount++;
  if (result == RES_DONE) ST.totalCycles++;
  statsSave();
}

void statsAddMotorSec(uint32_t sec) {
  ST.motorSec += sec;
  statsSave();
}

// ------------------------------------------------------------------ Auth ----
static String sha256Hex(const String& text) {
  uint8_t out[32];
  mbedtls_sha256((const uint8_t*)text.c_str(), text.length(), out, 0);
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", out[i]);
  return String(hex);
}

// ------------------------------------------------------------------ Sesi ----
struct Dev {
  char token[33];
  char name[32];
  uint32_t created;
  uint32_t lastSeen;
  uint32_t seq;      // urutan pembuatan (yang terkecil = terlama)
};
static Dev devs[MAX_DEVICES];
static uint32_t devSeq = 0;
static bool devsLoaded = false;
static uint32_t lastDevPersist = 0;
static char tmpTok[4][33];
static uint8_t tmpNext = 0;

static uint32_t nowEpoch() {
  time_t t = time(nullptr);
  return t > 1700000000 ? (uint32_t)t : 0;
}

static void devsSave() {
  Preferences p;
  p.begin("dev", false);
  p.putBytes("list", devs, sizeof(devs));
  p.end();
  lastDevPersist = nowEpoch();
}

static void devsLoad() {
  if (devsLoaded) return;
  devsLoaded = true;
  memset(devs, 0, sizeof(devs));
  Preferences p;
  p.begin("dev", true);
  if (p.isKey("list") && p.getBytesLength("list") == sizeof(devs)) p.getBytes("list", devs, sizeof(devs));
  p.end();
  for (auto& d : devs) {
    d.token[32] = 0;
    d.name[sizeof(d.name) - 1] = 0;
    if (d.token[0] && d.seq > devSeq) devSeq = d.seq;
  }
}

String sessionCreate(bool remember, const String& deviceName) {
  devsLoad();
  char buf[33];
  for (int i = 0; i < 4; i++) snprintf(buf + i * 8, 9, "%08x", (unsigned)esp_random());
  if (!remember) {
    memcpy(tmpTok[tmpNext], buf, 33);
    tmpNext = (tmpNext + 1) % 4;
    return String(buf);
  }
  int slot = -1;
  for (int i = 0; i < MAX_DEVICES && slot < 0; i++)
    if (!devs[i].token[0]) slot = i;
  if (slot < 0) {   // penuh: hapus yang terlama
    slot = 0;
    for (int i = 1; i < MAX_DEVICES; i++)
      if (devs[i].seq < devs[slot].seq) slot = i;
  }
  memset(&devs[slot], 0, sizeof(Dev));
  memcpy(devs[slot].token, buf, 33);
  strlcpy(devs[slot].name, deviceName.c_str(), sizeof(devs[slot].name));
  devs[slot].created = devs[slot].lastSeen = nowEpoch();
  devs[slot].seq = ++devSeq;
  devsSave();
  return String(buf);
}

int sessionSlot(const String& token) {
  devsLoad();
  if (token.length() != 32) return -1;
  for (int i = 0; i < MAX_DEVICES; i++)
    if (devs[i].token[0] && token == devs[i].token) return i;
  return -1;
}

bool sessionValid(const String& token) {
  if (token.length() != 32) return false;
  int slot = sessionSlot(token);
  if (slot >= 0) {
    uint32_t now = nowEpoch();
    if (now) {
      devs[slot].lastSeen = now;
      if (now - lastDevPersist > 6 * 3600UL) devsSave();   // simpan jarang agar flash awet
    }
    return true;
  }
  for (auto& t : tmpTok)
    if (t[0] && token == t) return true;
  return false;
}

void sessionRevoke(const String& token) {
  int slot = sessionSlot(token);
  if (slot >= 0) {
    memset(&devs[slot], 0, sizeof(Dev));
    devsSave();
  }
  for (auto& t : tmpTok)
    if (t[0] && token == t) memset(t, 0, sizeof(t));
}

void sessionRevokeAll() {
  devsLoad();
  memset(devs, 0, sizeof(devs));
  memset(tmpTok, 0, sizeof(tmpTok));
  devsSave();
}

int deviceList(DeviceInfo* out, int maxCount, const String& currentToken) {
  devsLoad();
  int cur = sessionSlot(currentToken);
  int n = 0;
  for (int i = 0; i < MAX_DEVICES && n < maxCount; i++) {
    if (!devs[i].token[0]) continue;
    out[n].slot = i;
    strlcpy(out[n].name, devs[i].name, sizeof(out[n].name));
    out[n].created = devs[i].created;
    out[n].lastSeen = devs[i].lastSeen;
    out[n].current = i == cur;
    n++;
  }
  return n;
}

bool deviceRemove(uint8_t slot) {
  devsLoad();
  if (slot >= MAX_DEVICES || !devs[slot].token[0]) return false;
  memset(&devs[slot], 0, sizeof(Dev));
  devsSave();
  return true;
}

int deviceRemoveOthers(const String& currentToken) {
  devsLoad();
  int cur = sessionSlot(currentToken);
  int removed = 0;
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (i == cur || !devs[i].token[0]) continue;
    memset(&devs[i], 0, sizeof(Dev));
    removed++;
  }
  if (removed) devsSave();
  return removed;
}

bool authCheck(const String& password) {
  Preferences p;
  p.begin("auth", true);
  String stored = p.getString("hash", "");
  p.end();
  if (stored.length() == 0) return password == WEB_PASSWORD;
  return sha256Hex(password) == stored;
}

void authSet(const String& password) {
  Preferences p;
  p.begin("auth", false);
  p.putString("hash", sha256Hex(password));
  p.end();
  sessionRevokeAll();   // semua perangkat harus login ulang dengan password baru
}
