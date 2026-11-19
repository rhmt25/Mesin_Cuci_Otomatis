#include "web_server.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <time.h>
#include <memory>
#include "commands.h"
#include "config.h"
#include "logger.h"
#include "machine.h"
#include "net_wifi.h"
#include "notifier.h"
#include "ota.h"
#include "settings.h"
#include "web_ui.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// ============================================================================
// Sesi login
// ============================================================================
#define MAX_SESSIONS 4
static String sessions[MAX_SESSIONS];
static uint8_t sessionNext = 0;
static uint8_t loginFails = 0;
static uint32_t lockUntil = 0;
static volatile bool uploadOk = false;

static String newToken() {
  char buf[33];
  for (int i = 0; i < 4; i++) snprintf(buf + i * 8, 9, "%08x", (unsigned)esp_random());
  return String(buf);
}

static String cookieToken(const String& cookie) {
  int p = cookie.indexOf("sid=");
  if (p < 0) return "";
  int e = cookie.indexOf(';', p);
  return cookie.substring(p + 4, e < 0 ? cookie.length() : e);
}

static bool tokenValid(const String& t) {
  if (t.length() != 32) return false;
  for (int i = 0; i < MAX_SESSIONS; i++)
    if (sessions[i] == t) return true;
  return false;
}

static bool authed(AsyncWebServerRequest* r) {
  if (!r->hasHeader("Cookie")) return false;
  return tokenValid(cookieToken(r->header("Cookie")));
}

#define REQUIRE_AUTH(r)                                                  \
  if (!authed(r)) {                                                      \
    (r)->send(401, "application/json", "{\"ok\":false,\"msg\":\"login\"}"); \
    return;                                                              \
  }

static void sendJson(AsyncWebServerRequest* r, int code, const String& body) {
  r->send(code, "application/json", body);
}

static void scheduleRestart(uint32_t delayMs) {
  xTaskCreate(
      [](void* p) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)(uintptr_t)p));
        ESP.restart();
      },
      "restart", 2048, (void*)(uintptr_t)delayMs, 1, nullptr);
}

// ============================================================================
// Builder JSON
// ============================================================================
static const char* stateName(uint8_t s) {
  return s == MS_RUNNING ? "running" : s == MS_PAUSED ? "paused" : "idle";
}

static String statusJson() {
  MachineStatus s;
  machineGetStatus(s);
  JsonDocument d;
  d["type"] = "status";
  d["state"] = stateName(s.state);
  d["program"] = s.state == MS_IDLE ? "" : programName(s.program);
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
  d["resume"]["pending"] = s.resumePending;
  d["resume"]["info"] = s.resumeInfo;
  for (int i = 0; i < R_COUNT; i++) d["relays"][relayName((RelayId)i)] = s.relays[i];
  d["wifi"]["online"] = netOnline();
  d["wifi"]["ssid"] = netSsid();
  d["wifi"]["rssi"] = netRssi();
  d["wifi"]["ip"] = netIp();
  d["wifi"]["ap"] = netApSsid();
  d["wifi"]["apClients"] = netApClients();
  d["wifi"]["time"] = netTimeString();
  d["notif"] = notifModeName(S.notif);
  d["uptime"] = s.uptimeSec;
  d["heap"] = ESP.getFreeHeap();
  d["fw"] = FW_VERSION;
  d["reset"] = loggerResetReason();
  d["otaBusy"] = otaBusy();
  d["otaProgress"] = otaProgress();
  String out;
  serializeJson(d, out);
  return out;
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
  d["drainExtraSec"] = S.drainExtraSec;
  d["drainMaxMin"] = S.drainMaxMin;
  d["unjamCount"] = S.unjamCount;
  d["dH"] = S.dH; d["dM"] = S.dM; d["dS"] = S.dS;
  d["finishAt"] = S.finishAt;
  d["notif"] = S.notif;
  d["calib"]["fillSec"] = (uint32_t)(C.avgFillMs / 1000);
  d["calib"]["drainSec"] = (uint32_t)(C.avgDrainMs / 1000);
  d["calib"]["nFill"] = C.nFill;
  d["calib"]["nDrain"] = C.nDrain;
  d["timeSynced"] = netTimeSynced();
  String out;
  serializeJson(d, out);
  return out;
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

static String logsJson(uint32_t since, size_t maxCount) {
  if (maxCount > LOG_RING_SIZE) maxCount = LOG_RING_SIZE;
  if (maxCount == 0) maxCount = 1;
  std::unique_ptr<LogEntry[]> bufHolder(new LogEntry[maxCount]);
  LogEntry* buf = bufHolder.get();
  size_t n = logRead(since, buf, maxCount);
  JsonDocument d;
  d["type"] = "log";
  d["last"] = n ? buf[n - 1].seq : since;
  addLogEntries(d["entries"].to<JsonArray>(), buf, n);
  String out;
  serializeJson(d, out);
  return out;
}

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
  d["stats"]["avgDrainSec"] = (uint32_t)(C.avgDrainMs / 1000);
  String out;
  serializeJson(d, out);
  return out;
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
  d["online"] = netOnline();
  d["ssid"] = active;
  d["rssi"] = netRssi();
  d["ip"] = netIp();
  String out;
  serializeJson(d, out);
  return out;
}

static String cmdResultJson(const CmdResult& r) {
  JsonDocument d;
  d["ok"] = r.ok;
  d["msg"] = r.msg;
  String out;
  serializeJson(d, out);
  return out;
}

// ============================================================================
// Handler
// ============================================================================
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
    {"drainExtraSec", "set_tambahan_buang"}, {"drainMaxMin", "set_maks_buang"},
    {"notif", "notif"}, {"level", "level_air"}, {"drainModeName", "mode_buang"},
};

static void setupRoutes() {
  // ---- halaman ----
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    AsyncWebServerResponse* res = r->beginResponse(200, "text/html", WEB_UI_GZ, WEB_UI_GZ_LEN);
    res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "no-cache");
    r->send(res);
  });

  // ---- login ----
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/login", [](AsyncWebServerRequest* r, JsonVariant& json) {
    if (millis() < lockUntil) {
      sendJson(r, 429, "{\"ok\":false,\"msg\":\"Terlalu banyak percobaan. Tunggu sebentar.\"}");
      return;
    }
    String pw = json["password"] | "";
    if (!authCheck(pw)) {
      if (++loginFails >= 5) {
        lockUntil = millis() + 30000;
        loginFails = 0;
      }
      sendJson(r, 401, "{\"ok\":false,\"msg\":\"Password salah\"}");
      return;
    }
    loginFails = 0;
    String token = newToken();
    sessions[sessionNext] = token;
    sessionNext = (sessionNext + 1) % MAX_SESSIONS;
    AsyncWebServerResponse* res = r->beginResponse(200, "application/json", "{\"ok\":true}");
    res->addHeader("Set-Cookie", "sid=" + token + "; Path=/; HttpOnly; SameSite=Strict");
    r->send(res);
  }));

  server.on("/api/logout", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (r->hasHeader("Cookie")) {
      String t = cookieToken(r->header("Cookie"));
      for (int i = 0; i < MAX_SESSIONS; i++)
        if (sessions[i] == t) sessions[i] = "";
    }
    sendJson(r, 200, "{\"ok\":true}");
  });

  // ---- status & data ----
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    REQUIRE_AUTH(r);
    sendJson(r, 200, statusJson());
  });
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* r) {
    REQUIRE_AUTH(r);
    sendJson(r, 200, settingsJson());
  });
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/settings", [](AsyncWebServerRequest* r, JsonVariant& json) {
    REQUIRE_AUTH(r);
    JsonDocument res;
    JsonArray msgs = res["results"].to<JsonArray>();
    bool all = true;
    for (const KeyMap& k : settingKeys) {
      if (json[k.key].isNull()) continue;
      String v = json[k.key].as<String>();
      CmdResult cr = executeCommand(k.cmd, v, SRC_WEB);
      all = all && cr.ok;
      JsonObject o = msgs.add<JsonObject>();
      o["key"] = k.key;
      o["ok"] = cr.ok;
      o["msg"] = cr.msg;
    }
    res["ok"] = all;
    String out;
    serializeJson(res, out);
    sendJson(r, 200, out);
  }));
  server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* r) {
    REQUIRE_AUTH(r);
    uint32_t since = r->hasParam("since") ? r->getParam("since")->value().toInt() : 0;
    size_t limit = r->hasParam("limit") ? r->getParam("limit")->value().toInt() : 100;
    sendJson(r, 200, logsJson(since, limit));
  });
  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest* r) {
    REQUIRE_AUTH(r);
    sendJson(r, 200, historyJson());
  });

  // ---- perintah (SEMUA perintah Telegram) ----
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/cmd", [](AsyncWebServerRequest* r, JsonVariant& json) {
    REQUIRE_AUTH(r);
    String cmd = json["cmd"] | "";
    String args = json["args"] | "";
    if (cmd.length() == 0) {
      sendJson(r, 400, "{\"ok\":false,\"msg\":\"cmd kosong\"}");
      return;
    }
    logf(LOG_DEBUG, "Web: /%s %s", cmd.c_str(), args.c_str());
    sendJson(r, 200, cmdResultJson(executeCommand(cmd, args, SRC_WEB)));
  }));

  // ---- WiFi ----
  server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest* r) {
    REQUIRE_AUTH(r);
    sendJson(r, 200, wifiJson());
  });
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/wifi", [](AsyncWebServerRequest* r, JsonVariant& json) {
    REQUIRE_AUTH(r);
    String ssid = json["ssid"] | "";
    String pass = json["pass"] | "";
    if (pass.length() == 0) pass = "-";
    sendJson(r, 200, cmdResultJson(executeCommand("wifi_tambah", ssid + "|" + pass, SRC_WEB)));
  }));
  server.on("/api/wifi", HTTP_DELETE, [](AsyncWebServerRequest* r) {
    REQUIRE_AUTH(r);
    String i = r->hasParam("i") ? r->getParam("i")->value() : "";
    sendJson(r, 200, cmdResultJson(executeCommand("wifi_hapus", i, SRC_WEB)));
  });

  // ---- konsol (serial monitor web) ----
  server.on("/api/console", HTTP_GET, [](AsyncWebServerRequest* r) {
    REQUIRE_AUTH(r);
    JsonDocument d;
    d["reset"] = loggerResetReason();
    d["trace"] = loggerPreviousTrace();
    String out;
    serializeJson(d, out);
    sendJson(r, 200, out);
  });
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/console", [](AsyncWebServerRequest* r, JsonVariant& json) {
    REQUIRE_AUTH(r);
    String line = json["line"] | "";
    line.trim();
    if (line.length() == 0) {
      sendJson(r, 200, "{\"ok\":true}");
      return;
    }
    logf(LOG_DEBUG, "> %s", line.c_str());
    String out = consoleExec(line, SRC_WEB);
    // keluaran dikirim baris per baris ke log agar tampil di konsol (WebSocket)
    int start = 0;
    while (start < (int)out.length()) {
      int nl = out.indexOf('\n', start);
      String part = out.substring(start, nl < 0 ? out.length() : nl);
      if (part.length()) logStr(LOG_DEBUG, part);
      if (nl < 0) break;
      start = nl + 1;
    }
    sendJson(r, 200, "{\"ok\":true}");
  }));

  // ---- flash firmware ----
  server.on("/api/update/info", HTTP_GET, [](AsyncWebServerRequest* r) {
    REQUIRE_AUTH(r);
    sendJson(r, 200, otaInfoJson());
  });
  server.on("/api/rollback", HTTP_POST, [](AsyncWebServerRequest* r) {
    REQUIRE_AUTH(r);
    String err;
    if (!otaRollback(err)) {
      sendJson(r, 400, "{\"ok\":false,\"msg\":\"" + err + "\"}");
      return;
    }
    sendJson(r, 200, "{\"ok\":true,\"msg\":\"Restart ke firmware sebelumnya...\"}");
    scheduleRestart(1500);
  });
  server.on(
      "/api/update", HTTP_POST,
      [](AsyncWebServerRequest* r) {
        REQUIRE_AUTH(r);
        if (uploadOk) {
          sendJson(r, 200, "{\"ok\":true,\"msg\":\"Firmware terpasang. Restart...\"}");
          scheduleRestart(1500);
        } else {
          sendJson(r, 400, "{\"ok\":false,\"msg\":\"Update gagal atau ditolak (mesin harus IDLE).\"}");
        }
      },
      [](AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
        if (!authed(r)) return;
        if (index == 0) {
          String err;
          if (!otaWebBegin(r->contentLength(), err)) {
            logf(LOG_ERROR, "Flash web ditolak: %s", err.c_str());
            return;
          }
          uploadOk = false;
        }
        if (!otaBusy()) return;
        if (!otaWebWrite(data, len)) {
          otaWebAbort();
          return;
        }
        if (final) {
          String err;
          if (otaWebEnd(err)) uploadOk = true;
        }
      });

  // ---- captive portal & 404 ----
  server.onNotFound([](AsyncWebServerRequest* r) {
    if (r->url().startsWith("/api/")) {
      sendJson(r, 404, "{\"ok\":false,\"msg\":\"tidak ditemukan\"}");
      return;
    }
    r->redirect("http://192.168.4.1/");
  });
}

// ============================================================================
// WebSocket
// ============================================================================
static void onWsEvent(AsyncWebSocket* srv, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data,
                      size_t len) {
  if (type == WS_EVT_CONNECT) {
    AsyncWebServerRequest* req = (AsyncWebServerRequest*)arg;
    bool ok = req && req->hasHeader("Cookie") && tokenValid(cookieToken(req->header("Cookie")));
    if (!ok || srv->count() > MAX_WS_CLIENTS) {
      client->close();
      return;
    }
    client->text(statusJson());
  }
}

static void wsTask(void*) {
  uint32_t lastSeq = logLastSeq();
  uint32_t lastStatus = 0;
  for (;;) {
    ws.cleanupClients(MAX_WS_CLIENTS);
    if (ws.count() > 0) {
      uint32_t now = millis();
      // log baru
      if (logLastSeq() != lastSeq) {
        String j = logsJson(lastSeq, 40);
        JsonDocument d;
        if (deserializeJson(d, j) == DeserializationError::Ok) lastSeq = d["last"] | lastSeq;
        ws.textAll(j);
      }
      if (now - lastStatus >= 1000) {
        lastStatus = now;
        ws.textAll(statusJson());
      }
    } else {
      lastSeq = logLastSeq();
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void webBegin() {
  setupRoutes();
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
  logf(LOG_INFO, "Dashboard web aktif di port 80");
  xTaskCreatePinnedToCore(wsTask, "ws", 8192, nullptr, 1, nullptr, 0);
}
