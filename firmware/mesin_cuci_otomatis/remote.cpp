// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#include "remote.h"
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include "api_core.h"
#include "config.h"
#include "logger.h"
#include "machine.h"
#include "net_wifi.h"
#include "notifier.h"
#include "ota.h"
#include "settings.h"

// Bundel sertifikat CA bawaan core Arduino (memverifikasi sertifikat Cloudflare, tanpa pinning).
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

// Protokol (JSON per pesan WebSocket)
//   ESP->Worker: hello, hb (30 dtk), status (3 dtk saat ada penonton), log, notif, res
//   Worker->ESP: req {id,m,p,q,b}, viewers {n,ttl}
#define HB_INTERVAL_MS      30000
#define STATUS_INTERVAL_MS  3000
#define MAX_INCOMING        8192
#define MAX_VIEWER_TTL_SEC  60

static WebSocketsClient ws;
static volatile bool connected = false;
static bool begun = false;
static char host[96], path[64];
static uint16_t port = 443;
static bool useTls = true;
static char authHeader[140];

static uint32_t viewersUntil = 0;
// ---- ganti password jarak jauh (diminta dari web lokal, dikirim oleh task ini) ----
static String pwPending;               // password menunggu dikirim; dihapus segera setelah terkirim
static volatile bool pwQueued = false;
static volatile RemotePwState pwState = RPW_IDLE;
static char pwMsg[96] = "";
static uint32_t pwSentMs = 0;
#define PW_TIMEOUT_MS 15000

static uint32_t lastHb = 0, lastStatus = 0, lastLogSeq = 0, lastAck = 0;
#define ACK_TIMEOUT_MS 100000   // hb dikirim tiap 30 dtk; tanpa ack selama ini = sambungan dianggap mati
static bool helloPending = false, bootNotified = false, resumeNotified = false;
static uint8_t backoffLevel = 0;
static const uint32_t backoffSteps[] = {5000, 15000, 30000, 60000, 120000, 300000};

bool remoteConfigured() { return strlen(REMOTE_URL) > 8 && strlen(REMOTE_DEVICE_TOKEN) > 0; }
bool remoteConnected() { return connected; }

bool remoteSetPassword(const String& pw, String& err) {
  if (!remoteConfigured() || !S.remoteOn) { err = "Kontrol jarak jauh belum aktif"; return false; }
  if (!connected) { err = "Tidak terhubung ke server jarak jauh. Coba lagi setelah tersambung."; return false; }
  if (pwState == RPW_WAIT) { err = "Masih menunggu jawaban server"; return false; }
  if (pw.length() < 12 || pw.length() > 64) { err = "Password harus 12-64 karakter"; return false; }
  pwPending = pw;
  pwMsg[0] = 0;
  pwState = RPW_WAIT;
  pwSentMs = millis();
  pwQueued = true;
  return true;
}
RemotePwState remotePasswordState(String& msg) {
  msg = pwMsg;
  return pwState;
}

// "wss://host[:port]/path" atau "ws://..." (uji lokal)
static bool parseUrl() {
  String u = REMOTE_URL;
  useTls = u.startsWith("wss://");
  if (!useTls && !u.startsWith("ws://")) return false;
  u.remove(0, useTls ? 6 : 5);
  int slash = u.indexOf('/');
  String hp = slash < 0 ? u : u.substring(0, slash);
  String p = slash < 0 ? String("/") : u.substring(slash);
  int colon = hp.indexOf(':');
  port = useTls ? 443 : 80;
  if (colon > 0) {
    port = (uint16_t)hp.substring(colon + 1).toInt();
    hp = hp.substring(0, colon);
  }
  if (hp.length() == 0 || hp.length() >= sizeof(host) || p.length() >= sizeof(path)) return false;
  strlcpy(host, hp.c_str(), sizeof(host));
  strlcpy(path, p.c_str(), sizeof(path));
  snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s", REMOTE_DEVICE_TOKEN);
  return true;
}

static void sendJsonDoc(JsonDocument& d) {
  String out;
  serializeJson(d, out);
  ws.sendTXT(out);
}

// {"t":"<type>","fw":"..","d":<status>} tanpa parse ulang (status sudah berupa JSON)
static void sendStatus(const char* type) {
  String out;
  out.reserve(1400);
  out += "{\"t\":\"";
  out += type;
  out += "\",\"fw\":\"" FW_VERSION "\",\"d\":";
  out += apiStatusJson();
  out += "}";
  ws.sendTXT(out);
}

static void sendNotif(const String& text) {
  JsonDocument d;
  d["t"] = "notif";
  d["text"] = text;
  sendJsonDoc(d);
}

static void handleText(const uint8_t* payload, size_t length) {
  if (length == 0 || length > MAX_INCOMING) {
    logf(LOG_DEBUG, "Pesan server diabaikan (panjang %u)", (unsigned)length);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, payload, length) != DeserializationError::Ok) return;
  String t = doc["t"] | "";

  if (t == "setpw_res") {
    strlcpy(pwMsg, doc["msg"] | "", sizeof(pwMsg));
    pwState = (doc["ok"] | false) ? RPW_OK : RPW_FAIL;
    return;
  }

  if (t == "ack") {
    lastAck = millis();
    return;
  }

  if (t == "viewers") {
    uint32_t ttl = doc["ttl"] | 15;
    if (ttl > MAX_VIEWER_TTL_SEC) ttl = MAX_VIEWER_TTL_SEC;
    bool wasViewing = (int32_t)(viewersUntil - millis()) > 0;
    viewersUntil = millis() + ttl * 1000UL;
    if (!wasViewing) {   // penonton baru: kirim status sekarang + beberapa log terakhir sebagai latar
      lastStatus = 0;
      uint32_t last = logLastSeq();
      lastLogSeq = last > 20 ? last - 20 : 0;
    }
    return;
  }

  if (t == "req") {
    uint32_t id = doc["id"] | 0;
    String method = doc["m"] | "";
    String p = doc["p"] | "";
    JsonObjectConst q = doc["q"];
    String body;
    if (!doc["b"].isNull()) serializeJson(doc["b"], body);
    logf(LOG_DEBUG, "RPC %s %s", method.c_str(), p.c_str());
    ApiResult r = apiHandle(API_REMOTE, method.c_str(), p, [&q](const char* k) { JsonVariantConst v = q[k]; return v.isNull() ? String() : v.as<String>(); }, body);
    String out;
    out.reserve(r.body.length() + 48);
    out += "{\"t\":\"res\",\"id\":";
    out += id;
    out += ",\"code\":";
    out += r.code;
    out += ",\"body\":";
    out += r.body.length() ? r.body : String("{}");
    out += "}";
    bool sent = ws.sendTXT(out);
    logf(LOG_DEBUG, "RPC selesai %d, %u B, kirim %s", r.code, (unsigned)out.length(), sent ? "ok" : "GAGAL");
  }
}

static void onEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      connected = true;
      lastAck = millis();
      helloPending = true;
      backoffLevel = 0;
      ws.setReconnectInterval(backoffSteps[0]);
      logf(LOG_INFO, "Terhubung ke server jarak jauh");
      break;
    case WStype_DISCONNECTED:
      if (connected) logf(LOG_WARN, "Koneksi jarak jauh terputus");
      connected = false;
      if (backoffLevel < 5) backoffLevel++;   // 5 dtk -> 15 -> 30 -> 60 -> 120 -> 300
      ws.setReconnectInterval(backoffSteps[backoffLevel]);
      break;
    case WStype_TEXT:
      handleText(payload, length);
      break;
    case WStype_ERROR:
      logf(LOG_WARN, "Jarak jauh: kesalahan WebSocket");
      break;
    default:
      break;
  }
}

// Diagnosis sekali per percobaan: mencatat hasil DNS (alasan kegagalan paling umum di jaringan yang memblokir DNS).
static void diagnoseConnect() {
  IPAddress ip;
  if (!WiFi.hostByName(host, ip)) {
    logf(LOG_WARN, "Jarak jauh: DNS gagal untuk %s", host);
    return;
  }
  logf(LOG_INFO, "Jarak jauh: %s -> %s", host, ip.toString().c_str());
}

static void startClient() {
  if (!parseUrl()) {
    logf(LOG_ERROR, "REMOTE_URL tidak valid (harus wss://host/path)");
    return;
  }
  diagnoseConnect();
  ws.setExtraHeaders(authHeader);   // setAuthorization() mengirim skema Basic; Worker meminta Bearer
  if (useTls) ws.beginSslWithBundle(host, port, path, x509_crt_bundle_start, "");
  else ws.begin(host, port, path, "");
  ws.onEvent(onEvent);
  ws.setReconnectInterval(backoffSteps[0]);
  // Ping WebSocket bawaan pustaka tidak dijawab Worker (sambungan putus tiap ±80 dtk): pakai heartbeat aplikasi hb -> ack
  begun = true;
  logf(LOG_INFO, "Menyambung ke server jarak jauh (%s)", host);
}

static void stopClient() {
  ws.disconnect();
  begun = false;
  connected = false;
  notifierSetOnline(false);
}

static void pump() {
  uint32_t now = millis();
  if (helloPending) {
    helloPending = false;
    sendStatus("hello");
    lastHb = now;
    if (!bootNotified) {
      bootNotified = true;
      sendNotif(String("\xF0\x9F\xA7\xBA Mesin Cuci online (v") + FW_VERSION + ")\nWiFi: " + netSsid() + " \xE2\x80\xA2 IP " +
                netIp() + "\nReset terakhir: " + loggerResetReason());
    }
  }
  if (pwQueued) {   // kirim password baru ke Worker, lalu hapus dari memori
    pwQueued = false;
    JsonDocument d;
    d["t"] = "setpw";
    d["id"] = 1;
    d["pw"] = pwPending;
    sendJsonDoc(d);
    pwPending = "";
    pwPending.reserve(0);
  }
  if (pwState == RPW_WAIT && now - pwSentMs > PW_TIMEOUT_MS) {
    strlcpy(pwMsg, "Server tidak menjawab", sizeof(pwMsg));
    pwState = RPW_FAIL;
  }
  if (now - lastHb >= HB_INTERVAL_MS) {
    lastHb = now;
    sendStatus("hb");
  }
  bool viewing = (int32_t)(viewersUntil - now) > 0;
  if (viewing && now - lastStatus >= STATUS_INTERVAL_MS) {
    lastStatus = now;
    sendStatus("status");
  }
  if (viewing && logLastSeq() > lastLogSeq) {
    LogEntry chunk[8];
    size_t n = logRead(lastLogSeq, chunk, 8);
    if (n) {
      JsonDocument d;
      d["t"] = "log";
      JsonArray arr = d["entries"].to<JsonArray>();
      for (size_t i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["t"] = chunk[i].ms;
        o["e"] = chunk[i].epoch;
        o["l"] = chunk[i].level;
        o["m"] = chunk[i].text;
      }
      sendJsonDoc(d);
      lastLogSeq = chunk[n - 1].seq;
    }
  }
  String msg;
  if (notifierNextMessage(msg)) sendNotif(msg);

  MachineStatus ms;
  machineGetStatus(ms);
  if (ms.resumePending && !resumeNotified) {
    resumeNotified = true;
    sendNotif(String("\xE2\x9A\xA1 Listrik sempat padam saat ") + ms.resumeInfo +
              ".\nMesin tidak lanjut otomatis. Buka dashboard untuk Lanjutkan atau Batalkan.");
  }
  if (!ms.resumePending) resumeNotified = false;
}

static void remoteTask(void*) {
  notifierInit();
  for (;;) {
    bool want = S.remoteOn && remoteConfigured() && netOnline() && netTimeSynced() && !otaBusy();   // TLS butuh jam yang benar
    if (want && !begun) startClient();
    else if (!want && begun) stopClient();

    notifierPoll();
    if (begun) {
      ws.loop();
      notifierSetOnline(connected);
      if (connected) {
        pump();
        if (millis() - lastAck > ACK_TIMEOUT_MS) {   // server tidak menjawab: paksa sambung ulang
          logf(LOG_WARN, "Server jarak jauh tidak menjawab, menyambung ulang");
          lastAck = millis();
          ws.disconnect();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(begun ? 20 : 250));
  }
}

void remoteBegin() {
  if (!remoteConfigured()) logf(LOG_INFO, "Kontrol jarak jauh belum dikonfigurasi (REMOTE_URL/REMOTE_DEVICE_TOKEN di secrets.h)");
  xTaskCreatePinnedToCore(remoteTask, "remote", 8192, nullptr, 1, nullptr, 0);
}
