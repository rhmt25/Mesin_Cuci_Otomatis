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
  s.pola = POLA_NORMAL;
  s.soak = 2;
  s.wash = 15;
  s.rinse = 10;
  s.parfume = 1.0f;
  s.spin = 10;
  s.fillLevel = FILL_SENSOR;
  s.fillPercent = 50;
  s.fillTime = 30;
  s.flH = 0; s.flM = 30; s.flS = 0;
  s.fillRetryCount = 3;
  s.fillRetryWait = 15;
  s.drainMode = DRAIN_SENSOR;
  s.drainTime = 5;
  s.drainSessionLimit = 3;
  s.drainExtraSec = 60;
  s.drainMaxMin = 5;
  s.unjamCount = 2;
  s.dH = 0; s.dM = 0; s.dS = 0;
  s.finishAt = -1;
  s.notif = NOTIF_LENGKAP;
}

static int clampInt(int v, int lo, int hi, int def) { return (v < lo || v > hi) ? def : v; }

static void sanitize(Settings& s) {
  Settings d;
  settingsDefaults(d);
  if (s.pola > POLA_HIJAB) s.pola = d.pola;
  s.soak = clampInt(s.soak, 0, 1440, d.soak);
  s.wash = clampInt(s.wash, 0, 1440, d.wash);
  s.rinse = clampInt(s.rinse, 0, 1440, d.rinse);
  if (s.parfume < 0 || s.parfume > 1440 || (int)(s.parfume * 2) != s.parfume * 2) s.parfume = d.parfume;
  s.spin = clampInt(s.spin, 0, 1440, d.spin);
  if (s.fillLevel > FILL_TIME) s.fillLevel = d.fillLevel;
  if (s.fillPercent != 25 && s.fillPercent != 50 && s.fillPercent != 75) s.fillPercent = d.fillPercent;
  s.fillTime = clampInt(s.fillTime, 1, 1440, d.fillTime);
  s.flH = clampInt(s.flH, 0, 23, 0);
  s.flM = clampInt(s.flM, 0, 59, 30);
  s.flS = clampInt(s.flS, 0, 59, 0);
  if (s.flH == 0 && s.flM == 0 && s.flS == 0) s.flM = 30;
  s.fillRetryCount = clampInt(s.fillRetryCount, 0, 20, d.fillRetryCount);
  s.fillRetryWait = clampInt(s.fillRetryWait, 0, 240, d.fillRetryWait);
  if (s.drainMode > DRAIN_TIME) s.drainMode = d.drainMode;
  s.drainTime = clampInt(s.drainTime, 1, 1440, d.drainTime);
  s.drainSessionLimit = clampInt(s.drainSessionLimit, 1, 1440, d.drainSessionLimit);
  s.drainExtraSec = clampInt(s.drainExtraSec, 0, 600, d.drainExtraSec);
  s.drainMaxMin = clampInt(s.drainMaxMin, 1, 60, d.drainMaxMin);
  s.unjamCount = clampInt(s.unjamCount, 0, 10, d.unjamCount);
  s.dH = clampInt(s.dH, 0, 23, 0);
  s.dM = clampInt(s.dM, 0, 59, 0);
  s.dS = clampInt(s.dS, 0, 59, 0);
  s.finishAt = (s.finishAt < -1 || s.finishAt >= 1440) ? -1 : s.finishAt;
  if (s.notif > NOTIF_SENYAP) s.notif = d.notif;
}

uint32_t fillLimitSeconds() { return (uint32_t)S.flH * 3600UL + (uint32_t)S.flM * 60UL + (uint32_t)S.flS; }

void settingsLoad() {
  Preferences p;
  p.begin("settings", true);
  settingsDefaults(S);
  S.pola = p.getUChar("pola", S.pola);
  S.soak = p.getInt("soak", S.soak);
  S.wash = p.getInt("wash", S.wash);
  S.rinse = p.getInt("rinse", S.rinse);
  S.parfume = p.getFloat("parfume", S.parfume);
  S.spin = p.getInt("spin", S.spin);
  S.fillLevel = p.getUChar("fillLevel", S.fillLevel);
  S.fillPercent = p.getUChar("fillPct", S.fillPercent);
  S.fillTime = p.getInt("fillTime", S.fillTime);
  S.flH = p.getInt("flH", S.flH);
  S.flM = p.getInt("flM", S.flM);
  S.flS = p.getInt("flS", S.flS);
  S.fillRetryCount = p.getInt("fRetry", S.fillRetryCount);
  S.fillRetryWait = p.getInt("fRetryWait", S.fillRetryWait);
  S.drainMode = p.getUChar("drainMode", S.drainMode);
  S.drainTime = p.getInt("drainTime", S.drainTime);
  S.drainSessionLimit = p.getInt("drainLimit", S.drainSessionLimit);
  S.drainExtraSec = p.getInt("drainExtra", S.drainExtraSec);
  S.drainMaxMin = p.getInt("drainMax", S.drainMaxMin);
  S.unjamCount = p.getInt("unjam", S.unjamCount);
  S.dH = p.getInt("dH", S.dH);
  S.dM = p.getInt("dM", S.dM);
  S.dS = p.getInt("dS", S.dS);
  S.finishAt = p.getInt("finishAt", S.finishAt);
  S.notif = p.getUChar("notif", S.notif);
  p.end();
  sanitize(S);
}

void settingsSave() {
  sanitize(S);
  Preferences p;
  p.begin("settings", false);
  p.putUChar("pola", S.pola);
  p.putInt("soak", S.soak);
  p.putInt("wash", S.wash);
  p.putInt("rinse", S.rinse);
  p.putFloat("parfume", S.parfume);
  p.putInt("spin", S.spin);
  p.putUChar("fillLevel", S.fillLevel);
  p.putUChar("fillPct", S.fillPercent);
  p.putInt("fillTime", S.fillTime);
  p.putInt("flH", S.flH);
  p.putInt("flM", S.flM);
  p.putInt("flS", S.flS);
  p.putInt("fRetry", S.fillRetryCount);
  p.putInt("fRetryWait", S.fillRetryWait);
  p.putUChar("drainMode", S.drainMode);
  p.putInt("drainTime", S.drainTime);
  p.putInt("drainLimit", S.drainSessionLimit);
  p.putInt("drainExtra", S.drainExtraSec);
  p.putInt("drainMax", S.drainMaxMin);
  p.putInt("unjam", S.unjamCount);
  p.putInt("dH", S.dH);
  p.putInt("dM", S.dM);
  p.putInt("dS", S.dS);
  p.putInt("finishAt", S.finishAt);
  p.putUChar("notif", S.notif);
  p.end();
}

void settingsReset() {
  uint8_t keepNotif = S.notif;
  settingsDefaults(S);
  S.notif = keepNotif;
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
  C.avgDrainMs = p.getFloat("drain", 0);
  C.nFill = p.getUShort("nFill", 0);
  C.nDrain = p.getUShort("nDrain", 0);
  p.end();
}

void calibSave() {
  Preferences p;
  p.begin("calib", false);
  p.putFloat("fill", C.avgFillMs);
  p.putFloat("drain", C.avgDrainMs);
  p.putUShort("nFill", C.nFill);
  p.putUShort("nDrain", C.nDrain);
  p.end();
}

void calibReset() {
  memset(&C, 0, sizeof(C));
  calibSave();
}

// ---------------------------------------------------------------- Resume ----
void resumeSave(uint8_t program, uint8_t stepIndex) {
  Preferences p;
  p.begin("resume", false);
  p.putUChar("prog", program);
  p.putUChar("step", stepIndex);
  time_t now = time(nullptr);
  p.putUInt("ts", now > 1700000000 ? (uint32_t)now : 0);
  p.putBool("valid", true);
  p.end();
}

bool resumeLoad(ResumeInfo& out) {
  Preferences p;
  p.begin("resume", true);
  out.valid = p.getBool("valid", false);
  out.program = p.getUChar("prog", 0);
  out.stepIndex = p.getUChar("step", 0);
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

void statsRecord(uint8_t program, uint32_t startEpoch, uint32_t durSec, uint8_t result) {
  HistoryEntry& e = ST.hist[ST.histHead];
  e.startEpoch = startEpoch;
  e.durSec = durSec;
  e.program = program;
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

bool authCheck(const String& password) {
  Preferences p;
  p.begin("auth", true);
  String stored = p.getString("hash", "");
  p.end();
  if (stored.length() == 0) return password == WEB_PASSWORD;
  return sha256Hex(password) == stored;
}

// ---------------------------------------------------------------- Sesi ----
static char sessions[MAX_SESSIONS][33];
static uint8_t sessionNext = 0;
static bool sessionsLoaded = false;

static void sessionsLoad() {
  if (sessionsLoaded) return;
  sessionsLoaded = true;
  memset(sessions, 0, sizeof(sessions));
  Preferences p;
  p.begin("sess", true);
  if (p.isKey("tok") && p.getBytesLength("tok") == sizeof(sessions)) p.getBytes("tok", sessions, sizeof(sessions));
  sessionNext = p.getUChar("next", 0) % MAX_SESSIONS;
  p.end();
  for (auto& s : sessions) s[32] = 0;
}

static void sessionsSave() {
  Preferences p;
  p.begin("sess", false);
  p.putBytes("tok", sessions, sizeof(sessions));
  p.putUChar("next", sessionNext);
  p.end();
}

String sessionCreate() {
  sessionsLoad();
  char buf[33];
  for (int i = 0; i < 4; i++) snprintf(buf + i * 8, 9, "%08x", (unsigned)esp_random());
  memcpy(sessions[sessionNext], buf, 33);
  sessionNext = (sessionNext + 1) % MAX_SESSIONS;
  sessionsSave();
  return String(buf);
}

bool sessionValid(const String& token) {
  sessionsLoad();
  if (token.length() != 32) return false;
  for (auto& s : sessions)
    if (s[0] && token == s) return true;
  return false;
}

void sessionRevoke(const String& token) {
  sessionsLoad();
  bool changed = false;
  for (auto& s : sessions)
    if (s[0] && token == s) { memset(s, 0, sizeof(s)); changed = true; }
  if (changed) sessionsSave();
}

void sessionRevokeAll() {
  sessionsLoad();
  memset(sessions, 0, sizeof(sessions));
  sessionsSave();
}

void authSet(const String& password) {
  Preferences p;
  p.begin("auth", false);
  p.putString("hash", sha256Hex(password));
  p.end();
  sessionRevokeAll();   // perangkat lain harus login ulang dengan password baru
}
