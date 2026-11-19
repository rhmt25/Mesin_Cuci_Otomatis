#include "api_core.h"
#include <ArduinoJson.h>
#include "backup.h"
#include "commands.h"
#include "config.h"
#include "logger.h"
#include "machine.h"
#include "net_wifi.h"
#include "notifier.h"
#include "ota.h"
#include "programs.h"
#include "relay.h"
#include "remote.h"
#include "remote_policy.h"
#include "settings.h"

// ============================================================================
// Builder JSON
// ============================================================================
static const char* stateName(uint8_t s) {
  return s == MS_RUNNING ? "running" : s == MS_PAUSED ? "paused" : "idle";
}

static String toString(JsonDocument& d) {
  String out;
  serializeJson(d, out);
  return out;
}

static String msgJson(bool ok, const String& msg) {
  JsonDocument d;
  d["ok"] = ok;
  d["msg"] = msg;
  return toString(d);
}

String apiStatusJson() {
  MachineStatus s;
  machineGetStatus(s);
  JsonDocument d;
  d["type"] = "status";
  d["state"] = stateName(s.state);
  d["program"] = s.state == MS_IDLE ? "" : s.name;
  d["stage"] = s.stage;
  d["detail"] = s.detail;
  d["pause"] = s.pauseReason;
  d["step"] = s.stepIndex;
  d["steps"] = s.stepCount;
  d["stepLeft"] = s.stepLeftSec;
  d["totalLeft"] = s.totalLeftSec;
  d["progress"] = s.progressPct;
  static const char* water[] = {"kosong", "penuh", "mengukur"};
  d["water"] = water[s.waterState];
  d["waterRaw"] = s.waterRaw;
  d["fault"] = s.relayFault;
  d["resume"]["pending"] = s.resumePending;
  d["resume"]["info"] = s.resumeInfo;
  for (int i = 0; i < R_COUNT; i++) d["relays"][relayName((RelayId)i)] = s.relays[i];
  d["wifi"]["online"] = netOnline();
  d["wifi"]["ssid"] = netSsid();
  d["wifi"]["rssi"] = netRssi();
  d["wifi"]["ip"] = netIp();
  d["wifi"]["ap"] = netApSsid();
  d["wifi"]["apOn"] = netApOn();
  d["wifi"]["apClients"] = netApClients();
  d["wifi"]["time"] = netTimeString();
  d["remote"]["configured"] = remoteConfigured();
  d["remote"]["enabled"] = S.remoteOn != 0;
  d["remote"]["connected"] = remoteConnected();
  d["notif"] = notifModeName(S.notif);
  d["uptime"] = s.uptimeSec;
  d["heap"] = ESP.getFreeHeap();
  d["fw"] = FW_VERSION;
  d["reset"] = loggerResetReason();
  d["otaBusy"] = otaBusy();
  d["otaProgress"] = otaProgress();
  return toString(d);
}

static String settingsJson() {
  JsonDocument d;
  d["pola"] = S.pola;
  d["soak"] = S.soak;
  d["wash"] = S.wash;
  d["rinse"] = S.rinse;
  d["parfume"] = S.parfume;
  d["spin"] = S.spin;
  d["fillLevel"] = S.fillLevel;
  d["fillPercent"] = S.fillPercent;
  d["fillTime"] = S.fillTime;
  d["flH"] = S.flH; d["flM"] = S.flM; d["flS"] = S.flS;
  d["fillRetryCount"] = S.fillRetryCount;
  d["fillRetryWait"] = S.fillRetryWait;
  d["drainMode"] = S.drainMode;
  d["drainTime"] = S.drainTime;
  d["drainSessionLimit"] = S.drainSessionLimit;
  d["drainProbeSec"] = S.drainProbeSec;
  d["fillPrecheck"] = S.fillPrecheck;
  d["unjamCount"] = S.unjamCount;
  d["dH"] = S.dH; d["dM"] = S.dM; d["dS"] = S.dS;
  d["finishAt"] = S.finishAt;
  d["notif"] = S.notif;
  d["calib"]["fillSec"] = (uint32_t)(C.avgFillMs / 1000);
  d["calib"]["nFill"] = C.nFill;
  d["timeSynced"] = netTimeSynced();
  return toString(d);
}

static void addLogEntries(JsonArray arr, const LogEntry* e, size_t n) {
  for (size_t i = 0; i < n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["s"] = e[i].seq;
    o["t"] = e[i].ms;
    o["e"] = e[i].epoch;
    o["l"] = e[i].level;
    o["m"] = e[i].text;
  }
}

// Log dibaca per potongan kecil (stack) agar tidak ada alokasi heap besar yang bisa gagal.
#define LOGS_MAX_PER_REPLY 50
String apiLogsJson(uint32_t since, size_t maxCount, uint32_t* lastOut) {
  if (maxCount > LOGS_MAX_PER_REPLY) maxCount = LOGS_MAX_PER_REPLY;
  if (maxCount == 0) maxCount = 1;
  JsonDocument d;
  d["type"] = "log";
  JsonArray arr = d["entries"].to<JsonArray>();
  LogEntry chunk[6];
  uint32_t last = since;
  size_t total = 0;
  while (total < maxCount) {
    size_t want = min<size_t>(6, maxCount - total);
    size_t n = logRead(last, chunk, want);
    if (n == 0) break;
    addLogEntries(arr, chunk, n);
    last = chunk[n - 1].seq;
    total += n;
  }
  d["last"] = last;
  if (lastOut) *lastOut = last;
  return toString(d);
}

// Memori heap tinggal sedikit? Tolak permintaan berat daripada berisiko crash.
bool apiLowMemory(size_t needBytes) { return ESP.getMaxAllocHeap() < needBytes; }

static String historyJson() {
  JsonDocument d;
  JsonArray arr = d["history"].to<JsonArray>();
  static const char* res[] = {"selesai", "dihentikan", "error"};
  for (int i = 0; i < ST.histCount; i++) {
    int pos = (ST.histHead - 1 - i + 20) % 10;
    const HistoryEntry& e = ST.hist[pos];
    JsonObject o = arr.add<JsonObject>();
    o["program"] = programName(e.program);
    o["start"] = e.startEpoch;
    o["dur"] = e.durSec;
    o["result"] = res[e.result < 3 ? e.result : 2];
  }
  d["stats"]["cycles"] = ST.totalCycles;
  d["stats"]["motorSec"] = ST.motorSec + relayMotorSeconds();
  d["stats"]["avgFillSec"] = (uint32_t)(C.avgFillMs / 1000);
  return toString(d);
}

static String wifiJson() {
  JsonDocument d;
  JsonArray arr = d["networks"].to<JsonArray>();
  String active = netSsid();
  for (int i = 0; i < wifiCount(); i++) {
    WifiCred c;
    wifiGet(i, c);
    JsonObject o = arr.add<JsonObject>();
    o["i"] = i + 1;
    o["ssid"] = c.ssid;
    o["secured"] = c.pass[0] != 0;   // password tidak pernah dikirim
    o["active"] = active == c.ssid;
  }
  d["max"] = MAX_WIFI_NETWORKS;
  d["ap"] = netApSsid();
  d["apOn"] = netApOn();
  d["online"] = netOnline();
  d["ssid"] = active;
  d["rssi"] = netRssi();
  d["ip"] = netIp();
  return toString(d);
}

static String customListJson() {
  JsonDocument d;
  d["max"] = CUSTOM_SLOTS;
  d["maxItems"] = CUSTOM_MAX_ITEMS;
  JsonArray slots = d["slots"].to<JsonArray>();
  for (int s = 0; s < CUSTOM_SLOTS; s++) {
    CustomProc p;
    if (!customLoad(s, p)) {
      slots.add(nullptr);
      continue;
    }
    JsonObject o = slots.add<JsonObject>();
    o["slot"] = s + 1;
    o["name"] = p.name;
    o["steps"] = customStepCount(p);
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
  return toString(d);
}

static ApiResult customSaveApi(JsonVariant json) {
  CustomProc p;
  memset(&p, 0, sizeof(p));
  String name = json["name"] | "";
  name.trim();
  strlcpy(p.name, name.c_str(), sizeof(p.name));
  JsonArray items = json["items"].as<JsonArray>();
  if (items.isNull() || items.size() > CUSTOM_MAX_ITEMS)
    return {400, msgJson(false, String("Langkah harus 1-") + CUSTOM_MAX_ITEMS)};
  for (JsonObject it : items) {
    int t = customTypeFromName(String((const char*)(it["t"] | "")));
    if (t < 0) return {400, msgJson(false, "Jenis langkah tidak dikenal")};
    CustomItem& ci = p.items[p.count++];
    ci.type = t;
    ci.a = it["a"] | 0;
    ci.b = it["b"] | 0;
    ci.pola = it["p"] | 3;
    ci.cups2 = it["c"] | 0;
  }
  String err;
  if (!customValidate(p, err)) return {400, msgJson(false, err)};
  int slot = json["slot"] | 0;   // 1..5, 0 = slot kosong pertama
  if (slot < 1 || slot > CUSTOM_SLOTS) {
    slot = 0;
    for (int s = 0; s < CUSTOM_SLOTS && !slot; s++) {
      CustomProc tmp;
      if (!customLoad(s, tmp)) slot = s + 1;
    }
    if (!slot) return {400, msgJson(false, "Penyimpanan penuh (5 proses). Hapus satu dulu.")};
  }
  customSave(slot - 1, p);
  logf(LOG_INFO, "Custom proses disimpan: slot %d \"%s\" (%u langkah)", slot, p.name, (unsigned)customStepCount(p));
  return {200, String("{\"ok\":true,\"slot\":") + slot + ",\"msg\":\"Tersimpan di slot " + slot + "\"}"};
}

struct KeyMap {
  const char* key;
  const char* cmd;
};
static const KeyMap settingKeys[] = {
    {"pola", "pola"},
    {"soak", "set_soak_session"}, {"wash", "set_wash_time"}, {"rinse", "set_rinse_time"},
    {"spin", "set_spin_time"}, {"parfume", "set_jumlah_parfume"},
    {"fillTime", "set_waktu_pengisian_air"}, {"drainTime", "set_waktu_pembuangan_air"},
    {"drainSessionLimit", "set_batas_sesi_pembuangan_air"}, {"fillRetryCount", "set_coba_ulang_isi"},
    {"fillRetryWait", "set_tunggu_coba_ulang"}, {"unjamCount", "set_unjam"},
    {"drainProbeSec", "set_probe_buang"}, {"fillPrecheck", "pra_cek_isi"},
    {"notif", "notif"}, {"level", "level_air"}, {"drainModeName", "mode_buang"},
};

// ============================================================================
// Dispatcher
// ============================================================================
ApiResult apiHandle(ApiSource src, const char* method, const String& path, const QueryFn& query, const String& body) {
  bool isGet = strcmp(method, "GET") == 0;
  bool isPost = strcmp(method, "POST") == 0;
  bool isDelete = strcmp(method, "DELETE") == 0;
  CmdSource csrc = src == API_REMOTE ? SRC_REMOTE : SRC_WEB;

  JsonDocument doc;
  if (isPost && path != "/api/restore") {
    if (deserializeJson(doc, body) != DeserializationError::Ok) return {400, msgJson(false, "JSON tidak valid")};
  }

  // Jarak jauh: allowlist (juga ditegakkan di Worker; pertahanan berlapis)
  if (src == API_REMOTE) {
    String cmd = (isPost && path == "/api/cmd") ? String((const char*)(doc["cmd"] | "")) : String("");
    if (!remoteApiAllowed(method, path.c_str(), cmd.c_str())) return {403, msgJson(false, "Tidak diizinkan dari jarak jauh")};
  }

  if (isGet) {
    if (path == "/api/status") return {200, apiStatusJson()};
    if (path == "/api/info") return {200, String("{\"remote\":false,\"online\":true,\"fw\":\"") + FW_VERSION + "\"}"};
    if (path == "/api/poll") {
      if (apiLowMemory(8192)) return {503, msgJson(false, "memori rendah, coba lagi")};
      uint32_t since = (uint32_t)query("since").toInt();
      String out;
      out.reserve(2800);
      out += "{\"status\":";
      out += apiStatusJson();
      out += ",\"log\":";
      out += apiLogsJson(since, 30);
      out += "}";
      return {200, out};
    }
    if (path == "/api/settings") return {200, settingsJson()};
    if (path == "/api/logs") {
      if (apiLowMemory(12288)) return {503, msgJson(false, "memori rendah, coba lagi")};
      String limit = query("limit");
      long lim = limit.length() ? limit.toInt() : 50;
      return {200, apiLogsJson((uint32_t)query("since").toInt(), lim > 0 ? (size_t)lim : 1)};
    }
    if (path == "/api/history") return {200, historyJson()};
    if (path == "/api/custom") return {200, customListJson()};
    if (path == "/api/wifi") return {200, wifiJson()};
    if (path == "/api/console") {
      JsonDocument d;
      d["reset"] = loggerResetReason();
      d["trace"] = loggerPreviousTrace();
      return {200, toString(d)};
    }
    if (path == "/api/update/info") return {200, otaInfoJson()};
    if (path == "/api/backup") {
      if (apiLowMemory(24576)) return {503, msgJson(false, "memori rendah, coba lagi")};
      return {200, String(backupBuild(query("wifi") == "1").c_str())};
    }
  } else if (isPost) {
    if (path == "/api/cmd") {
      String cmd = doc["cmd"] | "";
      String args = doc["args"] | "";
      if (cmd.length() == 0) return {400, msgJson(false, "cmd kosong")};
      logf(LOG_DEBUG, "%s: /%s %s", src == API_REMOTE ? "Remote" : "Web", cmd.c_str(), args.c_str());
      CmdResult r = executeCommand(cmd, args, csrc);
      return {200, msgJson(r.ok, r.msg)};
    }
    if (path == "/api/settings") {
      JsonDocument res;
      JsonArray msgs = res["results"].to<JsonArray>();
      bool all = true;
      for (const KeyMap& k : settingKeys) {
        if (doc[k.key].isNull()) continue;
        String v = doc[k.key].as<String>();
        CmdResult cr = executeCommand(k.cmd, v, csrc);
        all = all && cr.ok;
        JsonObject o = msgs.add<JsonObject>();
        o["key"] = k.key;
        o["ok"] = cr.ok;
        o["msg"] = cr.msg;
      }
      res["ok"] = all;
      return {200, toString(res)};
    }
    if (path == "/api/custom") return customSaveApi(doc.as<JsonVariant>());
    if (path == "/api/restore") {
      bool dry = query("dry") == "1";
      if (apiLowMemory(24576)) return {503, msgJson(false, "memori rendah, coba lagi")};
      RestoreReport rep = backupApply(body.c_str(), body.length(), dry);
      JsonDocument d;
      d["ok"] = rep.ok;
      d["dry"] = dry;
      d["msg"] = rep.msg.c_str();
      d["settingsChanged"] = rep.settingsChanged;
      d["customSlots"] = rep.customSlots;
      d["wifiNetworks"] = rep.wifiNetworks;
      d["wifiNoPassword"] = rep.wifiNoPassword;
      d["clamped"] = rep.clamped;
      if (rep.ok && !dry) logf(LOG_WARN, "Pengaturan dipulihkan dari cadangan: %s", rep.msg.c_str());
      return {rep.ok ? 200 : 400, toString(d)};
    }
    if (path == "/api/wifi") {
      String ssid = doc["ssid"] | "";
      String pass = doc["pass"] | "";
      if (pass.length() == 0) pass = "-";
      CmdResult r = executeCommand("wifi_tambah", ssid + "|" + pass, SRC_WEB);
      return {200, msgJson(r.ok, r.msg)};
    }
    if (path == "/api/console") {
      String line = doc["line"] | "";
      line.trim();
      if (line.length() == 0) return {200, "{\"ok\":true}"};
      logf(LOG_DEBUG, "> %s", line.c_str());
      String out = consoleExec(line, SRC_WEB);
      int start = 0;   // keluaran dikirim baris per baris ke log agar tampil di konsol
      while (start < (int)out.length()) {
        int nl = out.indexOf('\n', start);
        String part = out.substring(start, nl < 0 ? out.length() : nl);
        if (part.length()) logStr(LOG_DEBUG, part);
        if (nl < 0) break;
        start = nl + 1;
      }
      return {200, "{\"ok\":true}"};
    }
  } else if (isDelete) {
    if (path == "/api/custom") {
      int slot = query("slot").toInt();
      if (slot < 1 || slot > CUSTOM_SLOTS || !customDelete(slot - 1)) return {400, msgJson(false, "Slot tidak valid")};
      return {200, msgJson(true, "Proses dihapus")};
    }
    if (path == "/api/wifi") {
      CmdResult r = executeCommand("wifi_hapus", query("i"), SRC_WEB);
      return {200, msgJson(r.ok, r.msg)};
    }
  }
  return {404, msgJson(false, "tidak ditemukan")};
}
