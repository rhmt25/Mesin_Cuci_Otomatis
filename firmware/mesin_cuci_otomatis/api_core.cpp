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
#include "proses_store.h"
#include "relay.h"
#include "remote.h"
#include "remote_policy.h"
#include "settings.h"
#include "tahap.h"

// ============================================================================
// Builder JSON
// ============================================================================
static const char* stateName(uint8_t s) {
  return s == MS_RUNNING ? "running" : s == MS_PAUSED ? "paused" : s == MS_DECIDE ? "decide" : "idle";
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
  d["proses"] = s.state == MS_IDLE ? "" : s.name;
  d["slot"] = s.slot == NO_SLOT ? 0 : s.slot + 1;
  d["manual"] = s.manual;
  d["stage"] = s.stage;
  d["tahap"] = s.tahap;
  d["detail"] = s.detail;
  d["pause"] = s.pauseReason;
  d["step"] = s.stepIndex;
  d["steps"] = s.stepCount;
  d["stepLeft"] = s.stepLeftSec;
  d["totalLeft"] = s.totalLeftSec;
  d["progress"] = s.progressPct;
  if (s.state == MS_DECIDE) {
    JsonObject dc = d["decision"].to<JsonObject>();
    dc["code"] = decisionCodeName(s.decision);
    dc["msg"] = s.pauseReason;
    JsonArray opts = dc["opts"].to<JsonArray>();
    JsonArray labels = dc["labels"].to<JsonArray>();
    for (uint8_t b = 1; b <= OP_PAKAI; b <<= 1) {
      if (!(s.decisionOpts & b)) continue;
      opts.add(decisionOptName(b));
      labels.add(decisionOptLabel(b));
    }
  }
  static const char* water[] = {"kosong", "penuh", "mengukur"};
  d["water"] = water[s.waterState];
  d["waterRaw"] = s.waterRaw;
  d["fault"] = s.relayFault;
  d["resume"]["pending"] = s.resumePending;
  d["resume"]["info"] = s.resumeInfo;
  d["resume"]["slot"] = s.resumeSlot == NO_SLOT ? 0 : s.resumeSlot + 1;
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
  {
    String pwMsg;
    RemotePwState ps = remotePasswordState(pwMsg);
    static const char* pwNames[] = {"idle", "wait", "ok", "fail"};
    d["remote"]["pw"] = pwNames[ps];
    d["remote"]["pwMsg"] = pwMsg;
  }
  d["notif"] = notifModeName(S.notif);
  d["uptime"] = s.uptimeSec;
  d["heap"] = ESP.getFreeHeap();
  d["fw"] = FW_VERSION;
  d["reset"] = loggerResetReason();
  d["otaBusy"] = otaBusy();
  d["otaProgress"] = otaProgress();
  d["fs"] = prosesStoreReady();
  return toString(d);
}

static String settingsJson() {
  JsonDocument d;
  d["notif"] = S.notif;
  d["remoteOn"] = S.remoteOn;
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
    o["m"] = JsonString((const char*)e[i].text);   // salin: buffer sumber dipakai ulang
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
    o["proses"] = JsonString((const char*)e.name);
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
    o["ssid"] = JsonString((const char*)c.ssid);   // salin: c dipakai ulang tiap iterasi
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

// ============================================================================
// Proses
// ============================================================================
static String prosesListJson() {
  JsonDocument d;
  d["max"] = PROSES_SLOTS;
  d["maxTahap"] = PROSES_MAX_TAHAP;
  d["fs"] = prosesStoreReady();
  JsonArray slots = d["slots"].to<JsonArray>();
  Proses* p = new Proses;
  if (!p) return msgJson(false, "memori rendah");
  for (uint8_t i = 0; i < PROSES_SLOTS; i++) {
    SlotState st = prosesSlotState(i);
    if (st == SLOT_EMPTY) {
      slots.add(nullptr);
      continue;
    }
    JsonObject o = slots.add<JsonObject>();
    o["slot"] = i + 1;
    o["locked"] = machineSlotInUse(i);
    if (st == SLOT_BAD || !prosesLoad(i, *p)) {
      o["bad"] = true;
      continue;
    }
    o["name"] = JsonString((const char*)p->name);   // salin: p dipakai ulang tiap slot
    o["count"] = p->count;
    o["rev"] = p->rev;
    o["est"] = prosesEstimateSec(*p);
  }
  delete p;
  return toString(d);
}

static void addWarnings(JsonDocument& d, const Proses& p) {
  JsonArray w = d["warnings"].to<JsonArray>();
  String list[8];
  int n = prosesWarnings(p, list, 8);
  for (int i = 0; i < n && i < 8; i++) w.add(list[i]);
}

static ApiResult prosesDetail(int slot) {
  if (slot < 1 || slot > PROSES_SLOTS) return {400, msgJson(false, "Slot tidak valid")};
  Proses* p = new Proses;
  if (!p) return {503, msgJson(false, "memori rendah, coba lagi")};
  ApiResult res;
  if (!prosesLoad(slot - 1, *p)) {
    res = {404, msgJson(false, "Proses kosong atau rusak")};
  } else {
    JsonDocument d;
    d["slot"] = slot;
    d["locked"] = machineSlotInUse(slot - 1);
    d["est"] = prosesEstimateSec(*p);
    prosesToJson(*p, d["proses"].to<JsonObject>());
    JsonArray rk = d["ringkas"].to<JsonArray>();
    for (uint8_t i = 0; i < p->count; i++) rk.add(tahapRingkas(p->t[i]));
    addWarnings(d, *p);
    res = {200, toString(d)};
  }
  delete p;
  return res;
}

static int firstEmptySlot() {
  for (uint8_t i = 0; i < PROSES_SLOTS; i++)
    if (prosesSlotState(i) == SLOT_EMPTY) return i + 1;
  return 0;
}

// Simpan (atau uji coba bila dry) satu Proses dari JSON. slot 0 = slot kosong pertama.
static ApiResult prosesSave(JsonVariantConst json, int slot, bool dry) {
  if (apiLowMemory(20480)) return {503, msgJson(false, "memori rendah, coba lagi")};
  Proses* p = new Proses;
  if (!p) return {503, msgJson(false, "memori rendah, coba lagi")};
  ApiResult res;
  String err;
  if (!prosesFromJson(json, *p, err)) {
    res = {400, msgJson(false, err)};
  } else if (!dry && !prosesStoreReady()) {
    res = {503, msgJson(false, "Penyimpanan Proses tidak tersedia")};
  } else {
    if (slot < 0 || slot > PROSES_SLOTS) slot = 0;
    if (!slot && !dry) {
      slot = firstEmptySlot();
      if (!slot) {
        delete p;
        return {400, msgJson(false, String("Penyimpanan penuh (") + PROSES_SLOTS + " Proses). Hapus satu dulu.")};
      }
    }
    if (slot && machineSlotInUse(slot - 1)) {
      delete p;
      return {409, msgJson(false, "Proses sedang dipakai. Stop dulu sebelum mengubah.")};
    }
    if (slot) {   // naikkan revisi dari berkas lama
      Proses* old = new Proses;
      p->rev = 1;
      if (old && prosesLoad(slot - 1, *old)) p->rev = old->rev + 1;
      delete old;
    }
    JsonDocument d;
    d["ok"] = true;
    d["dry"] = dry;
    d["est"] = prosesEstimateSec(*p);
    addWarnings(d, *p);
    if (!dry) {
      if (!prosesSave(slot - 1, *p)) {
        delete p;
        return {500, msgJson(false, "Gagal menyimpan Proses")};
      }
      d["slot"] = slot;
      d["msg"] = String("Tersimpan di slot ") + slot;
      logf(LOG_INFO, "Proses disimpan: slot %d \"%s\" (%u Tahap)", slot, p->name, (unsigned)p->count);
    }
    res = {200, toString(d)};
  }
  delete p;
  return res;
}

static ApiResult runApi(JsonDocument& doc) {
  if (!doc["manual"].isNull()) {
    Tahap t;
    String err;
    if (!tahapFromJson(doc["manual"].as<JsonObjectConst>(), t, err)) return {400, msgJson(false, err)};
    CmdResult r = commandRunManual(t);
    return {r.ok ? 200 : 409, msgJson(r.ok, r.msg)};
  }
  int slot = doc["slot"] | 0;
  uint32_t wait = 0;
  int finishAt = -1;
  if (!doc["tunda"].isNull()) {
    JsonVariantConst v = doc["tunda"];
    if (v.is<int>()) {
      long sec = v.as<long>();
      if (sec < 0 || sec > 86399) return {400, msgJson(false, "Tunda harus 0-86399 detik")};
      wait = (uint32_t)sec;
    } else {
      // "HH:MM:SS" / "HH:MM"
      int h = 0, m = 0, s = 0;
      if (sscanf(v.as<const char*>() ? v.as<const char*>() : "", "%d:%d:%d", &h, &m, &s) < 2 || h < 0 || h > 23 || m < 0 ||
          m > 59 || s < 0 || s > 59)
        return {400, msgJson(false, "Format tunda: HH:MM:SS")};
      wait = (uint32_t)h * 3600 + (uint32_t)m * 60 + (uint32_t)s;
    }
  }
  if (!doc["selesai"].isNull()) {
    int h = 0, m = 0;
    const char* txt = doc["selesai"] | "";
    if (sscanf(txt, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59)
      return {400, msgJson(false, "Format selesai: HH:MM")};
    finishAt = h * 60 + m;
    wait = 0;
  }
  CmdResult r = commandRunProses(slot, wait, finishAt);
  return {r.ok ? 200 : 409, msgJson(r.ok, r.msg)};
}

struct KeyMap {
  const char* key;
  const char* cmd;
};
static const KeyMap settingKeys[] = {
    {"notif", "notif"},
};

// ============================================================================
// Dispatcher
// ============================================================================
ApiResult apiHandle(ApiSource src, const char* method, const String& path, const QueryFn& query, const String& body) {
  bool isGet = strcmp(method, "GET") == 0;
  bool isPost = strcmp(method, "POST") == 0;
  bool isDelete = strcmp(method, "DELETE") == 0;
  CmdSource csrc = src == API_REMOTE ? SRC_REMOTE : SRC_WEB;

  // Jarak jauh: allowlist (juga ditegakkan di Worker; pertahanan berlapis)
  String cmdName;
  JsonDocument doc;
  if (isPost && path != "/api/restore") {
    if (body.length() > 8192) return {413, msgJson(false, "Permintaan terlalu besar")};
    if (deserializeJson(doc, body) != DeserializationError::Ok) return {400, msgJson(false, "JSON tidak valid")};
    if (path == "/api/cmd") cmdName = (const char*)(doc["cmd"] | "");
  }
  if (src == API_REMOTE) {
    if (!remoteApiAllowed(method, path.c_str(), cmdName.c_str())) return {403, msgJson(false, "Tidak diizinkan dari jarak jauh")};
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
    if (path == "/api/tahap/meta") {
      if (apiLowMemory(16384)) return {503, msgJson(false, "memori rendah, coba lagi")};
      return {200, tahapMetaJson()};
    }
    if (path == "/api/proses") {
      if (apiLowMemory(12288)) return {503, msgJson(false, "memori rendah, coba lagi")};
      String sl = query("slot");
      if (sl.length()) return prosesDetail(sl.toInt());
      return {200, prosesListJson()};
    }
    if (path == "/api/proses/export") {
      int slot = query("slot").toInt();
      if (slot < 1 || slot > PROSES_SLOTS) return {400, msgJson(false, "Slot tidak valid")};
      if (apiLowMemory(12288)) return {503, msgJson(false, "memori rendah, coba lagi")};
      Proses* p = new Proses;
      if (!p) return {503, msgJson(false, "memori rendah, coba lagi")};
      ApiResult res;
      if (!prosesLoad(slot - 1, *p)) {
        res = {404, msgJson(false, "Proses kosong atau rusak")};
      } else {
        JsonDocument d;
        prosesToJson(*p, d.to<JsonObject>());
        res = {200, toString(d)};
      }
      delete p;
      return res;
    }
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
      logf(LOG_DEBUG, "%s: /%s %s", src == API_REMOTE ? "Remote" : "Web", cmd.c_str(), cmd == "web_password" ? "(disembunyikan)" : args.c_str());
      CmdResult r = executeCommand(cmd, args, csrc);
      return {200, msgJson(r.ok, r.msg)};
    }
    if (path == "/api/password") {   // khusus lokal: tidak ada di allowlist jarak jauh
      String which = doc["which"] | "";
      String pw = doc["pw"] | "";
      if (pw.length() < 12) return {400, msgJson(false, "Password minimal 12 karakter")};
      if (pw.length() > 64) return {400, msgJson(false, "Password maksimal 64 karakter")};
      if (which == "local") {
        authSet(pw);
        logf(LOG_WARN, "Password web lokal diganti (semua perangkat diingat keluar)");
        return {200, msgJson(true, "Password web lokal diganti. Silakan login ulang.")};
      }
      if (which == "remote") {
        String err;
        if (!remoteSetPassword(pw, err)) return {409, msgJson(false, err)};
        return {200, msgJson(true, "Dikirim ke server, menunggu konfirmasi...")};
      }
      return {400, msgJson(false, "which harus local atau remote")};
    }
    if (path == "/api/run") return runApi(doc);
    if (path == "/api/decide") {
      CmdResult r = commandDecide(String((const char*)(doc["pilih"] | "")));
      return {r.ok ? 200 : 409, msgJson(r.ok, r.msg)};
    }
    if (path == "/api/proses") return prosesSave(doc.as<JsonVariantConst>(), doc["slot"] | 0, doc["dry"] | false);
    if (path == "/api/proses/import") return prosesSave(doc.as<JsonVariantConst>(), query("slot").toInt(), false);
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
    if (path == "/api/restore") {
      bool dry = query("dry") == "1";
      if (apiLowMemory(24576)) return {503, msgJson(false, "memori rendah, coba lagi")};
      RestoreReport rep = backupApply(body.c_str(), body.length(), dry);
      JsonDocument d;
      d["ok"] = rep.ok;
      d["dry"] = dry;
      d["msg"] = rep.msg.c_str();
      d["settingsChanged"] = rep.settingsChanged;
      d["oldIgnored"] = rep.oldIgnored;
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
      logf(LOG_DEBUG, "> %s", line.startsWith("web_password") ? "web_password (disembunyikan)" : line.c_str());
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
    if (path == "/api/proses") {
      int slot = query("slot").toInt();
      if (slot < 1 || slot > PROSES_SLOTS) return {400, msgJson(false, "Slot tidak valid")};
      if (machineSlotInUse(slot - 1)) return {409, msgJson(false, "Proses sedang dipakai. Stop dulu sebelum menghapus.")};
      if (!prosesDelete(slot - 1)) return {500, msgJson(false, "Gagal menghapus")};
      return {200, msgJson(true, "Proses dihapus")};
    }
    if (path == "/api/wifi") {
      CmdResult r = executeCommand("wifi_hapus", query("i"), SRC_WEB);
      return {200, msgJson(r.ok, r.msg)};
    }
  }
  return {404, msgJson(false, "tidak ditemukan")};
}
