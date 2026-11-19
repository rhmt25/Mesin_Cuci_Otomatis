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
  s.drainProbeSec = 45;
  s.fillPrecheck = PRECHECK_DRAIN;
  s.unjamCount = 2;
  s.dH = 0; s.dM = 0; s.dS = 0;
  s.finishAt = -1;
  s.notif = NOTIF_LENGKAP;
  s.remoteOn = 1;
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
  s.drainProbeSec = clampInt(s.drainProbeSec, 5, 600, d.drainProbeSec);
  if (s.fillPrecheck > PRECHECK_USE) s.fillPrecheck = d.fillPrecheck;
  s.unjamCount = clampInt(s.unjamCount, 0, 10, d.unjamCount);
  s.dH = clampInt(s.dH, 0, 23, 0);
  s.dM = clampInt(s.dM, 0, 59, 0);
  s.dS = clampInt(s.dS, 0, 59, 0);
  s.finishAt = (s.finishAt < -1 || s.finishAt >= 1440) ? -1 : s.finishAt;
  if (s.notif > NOTIF_SENYAP) s.notif = d.notif;
  if (s.remoteOn > 1) s.remoteOn = d.remoteOn;
}

uint32_t fillLimitSeconds() { return (uint32_t)S.flH * 3600UL + (uint32_t)S.flM * 60UL + (uint32_t)S.flS; }

void settingsLoad() {
  {   // migrasi: kunci pembuangan lama sudah tidak dipakai (sensor hanya di atas)
    Preferences m;
    m.begin("settings", false);
    if (m.isKey("drainExtra")) m.remove("drainExtra");
    if (m.isKey("drainMax")) m.remove("drainMax");
    m.end();
  }
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
  S.drainProbeSec = p.getInt("drainProbe", S.drainProbeSec);
  S.fillPrecheck = p.getUChar("fillPre", S.fillPrecheck);
  S.unjamCount = p.getInt("unjam", S.unjamCount);
  S.dH = p.getInt("dH", S.dH);
  S.dM = p.getInt("dM", S.dM);
  S.dS = p.getInt("dS", S.dS);
  S.finishAt = p.getInt("finishAt", S.finishAt);
  S.notif = p.getUChar("notif", S.notif);
  S.remoteOn = p.getUChar("remoteOn", S.remoteOn);
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
  p.putInt("drainProbe", S.drainProbeSec);
  p.putUChar("fillPre", S.fillPrecheck);
  p.putInt("unjam", S.unjamCount);
  p.putInt("dH", S.dH);
  p.putInt("dM", S.dM);
  p.putInt("dS", S.dS);
  p.putInt("finishAt", S.finishAt);
  p.putUChar("notif", S.notif);
  p.putUChar("remoteOn", S.remoteOn);
  p.end();
}

void settingsReset() {
  uint8_t keepNotif = S.notif, keepRemote = S.remoteOn;
  settingsDefaults(S);
  S.notif = keepNotif;
  S.remoteOn = keepRemote;
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
void resumeSave(uint8_t program, uint8_t stepIndex, uint8_t arg) {
  Preferences p;
  p.begin("resume", false);
  p.putUChar("prog", program);
  p.putUChar("step", stepIndex);
  p.putUChar("arg", arg);
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
  out.arg = p.getUChar("arg", 0);
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

// ----------------------------------------------------------- Custom Proses ----
static void customKey(uint8_t slot, char* k) { snprintf(k, 8, "p%u", slot); }

bool customLoad(uint8_t slot, CustomProc& out) {
  if (slot >= CUSTOM_SLOTS) return false;
  Preferences p;
  p.begin("custom", true);
  char k[8];
  customKey(slot, k);
  bool ok = p.isKey(k) && p.getBytesLength(k) == sizeof(CustomProc);
  if (ok) p.getBytes(k, &out, sizeof(CustomProc));
  p.end();
  if (ok) {
    out.name[sizeof(out.name) - 1] = 0;
    if (out.count == 0 || out.count > CUSTOM_MAX_ITEMS) ok = false;
  }
  return ok;
}

bool customSave(uint8_t slot, const CustomProc& proc) {
  if (slot >= CUSTOM_SLOTS) return false;
  Preferences p;
  p.begin("custom", false);
  char k[8];
  customKey(slot, k);
  p.putBytes(k, &proc, sizeof(CustomProc));
  p.end();
  return true;
}

bool customDelete(uint8_t slot) {
  if (slot >= CUSTOM_SLOTS) return false;
  Preferences p;
  p.begin("custom", false);
  char k[8];
  customKey(slot, k);
  p.remove(k);
  p.end();
  return true;
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
