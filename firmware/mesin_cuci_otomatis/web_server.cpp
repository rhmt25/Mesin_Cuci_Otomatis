// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#include "web_server.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include "api_core.h"
#include "backup.h"
#include "config.h"
#include "logger.h"
#include "ota.h"
#include "settings.h"
#include "web_ui.h"

// HTTP lokal: login/sesi, halaman, unggah firmware, dan pembungkus tipis ke api_core (dispatcher yang sama
// dipakai jarak jauh lewat remote.cpp).
static AsyncWebServer server(80);

// ============================================================================
// Sesi login (token tersimpan di NVS lewat settings.cpp -> bertahan saat restart)
// ============================================================================
static uint8_t loginFails = 0, loginLevel = 0;
static uint32_t lockUntil = 0;
// Penguncian bertingkat: tiap 5 salah beruntun, kunci makin lama (30 dtk, 1, 2, 5, 15 mnt, 1 jam); berhasil login mereset
static const uint32_t LOCK_STEPS_MS[] = {30000, 60000, 120000, 300000, 900000, 3600000};
static volatile bool uploadOk = false;

static String cookieToken(const String& cookie) {
  int p = cookie.indexOf("sid=");
  if (p < 0) return "";
  int e = cookie.indexOf(';', p);
  return cookie.substring(p + 4, e < 0 ? cookie.length() : e);
}

static String currentToken(AsyncWebServerRequest* r) {
  return r->hasHeader("Cookie") ? cookieToken(r->header("Cookie")) : String("");
}

// "Chrome - Android" dari User-Agent (cukup untuk membedakan perangkat di daftar)
static String deviceNameFromUA(const String& ua) {
  String os = "Perangkat";
  if (ua.indexOf("Android") >= 0) os = "Android";
  else if (ua.indexOf("iPhone") >= 0) os = "iPhone";
  else if (ua.indexOf("iPad") >= 0) os = "iPad";
  else if (ua.indexOf("Windows") >= 0) os = "Windows";
  else if (ua.indexOf("Mac OS") >= 0) os = "Mac";
  else if (ua.indexOf("Linux") >= 0) os = "Linux";
  String br = "Browser";
  if (ua.indexOf("Edg/") >= 0) br = "Edge";
  else if (ua.indexOf("OPR/") >= 0 || ua.indexOf("Opera") >= 0) br = "Opera";
  else if (ua.indexOf("Firefox/") >= 0) br = "Firefox";
  else if (ua.indexOf("Chrome/") >= 0 || ua.indexOf("CriOS") >= 0) br = "Chrome";
  else if (ua.indexOf("Safari/") >= 0) br = "Safari";
  return br + " - " + os;
}

// Cookie perangkat diingat: 400 hari (batas maksimum yang diterima browser), diperbarui tiap dipakai
// sehingga selama perangkat dipakai ia tidak pernah kedaluwarsa. Di server tidak ada kedaluwarsa.
#define REMEMBER_COOKIE_AGE "34560000"

static bool authed(AsyncWebServerRequest* r) {
  if (!r->hasHeader("Cookie")) return false;
  return sessionValid(cookieToken(r->header("Cookie")));
}

#define REQUIRE_AUTH(r)                                                  \
  if (!authed(r)) {                                                      \
    (r)->send(401, "application/json", "{\"ok\":false,\"msg\":\"login\"}"); \
    return;                                                              \
  }

static void sendJson(AsyncWebServerRequest* r, int code, const String& body) {
  r->send(code, "application/json", body);
}

// Balasan + perpanjang cookie bila perangkat diingat
static void sendJsonRefresh(AsyncWebServerRequest* r, int code, const String& body) {
  AsyncWebServerResponse* res = r->beginResponse(code, "application/json", body);
  String tok = currentToken(r);
  if (sessionSlot(tok) >= 0)
    res->addHeader("Set-Cookie", "sid=" + tok + "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" REMEMBER_COOKIE_AGE);
  r->send(res);
}

static void scheduleRestart(uint32_t delayMs) {
  xTaskCreate(
      [](void* p) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)(uintptr_t)p));
        ESP.restart();
      },
      "restart", 2048, (void*)(uintptr_t)delayMs, 1, nullptr);
}

// Jembatan HTTP -> api_core
static void bridge(AsyncWebServerRequest* r, const char* method, const String& body) {
  REQUIRE_AUTH(r);
  ApiResult res = apiHandle(API_LOCAL, method, r->url(), [r](const char* k) {
    return r->hasParam(k) ? r->getParam(k)->value() : String("");
  }, body);
  sendJsonRefresh(r, res.code, res.body);
}

static void getRoute(const char* path) {
  server.on(path, HTTP_GET, [](AsyncWebServerRequest* r) { bridge(r, "GET", ""); });
}
static void deleteRoute(const char* path) {
  server.on(path, HTTP_DELETE, [](AsyncWebServerRequest* r) { bridge(r, "DELETE", ""); });
}
static void postJsonRoute(const char* path) {
  server.addHandler(new AsyncCallbackJsonWebHandler(path, [](AsyncWebServerRequest* r, JsonVariant& json) {
    String body;
    serializeJson(json, body);
    bridge(r, "POST", body);
  }));
}

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
    int32_t lockLeft = (int32_t)(lockUntil - millis());
    if (lockUntil != 0 && lockLeft > 0) {
      sendJson(r, 429, String("{\"ok\":false,\"msg\":\"Terlalu banyak percobaan. Coba lagi dalam ") + (lockLeft / 1000 + 1) + " detik.\"}");
      return;
    }
    String pw = json["password"] | "";
    if (!authCheck(pw)) {
      if (++loginFails >= 5) {
        uint32_t ms = LOCK_STEPS_MS[loginLevel];
        if (loginLevel < sizeof(LOCK_STEPS_MS) / sizeof(LOCK_STEPS_MS[0]) - 1) loginLevel++;
        lockUntil = millis() + ms;
        if (lockUntil == 0) lockUntil = 1;
        loginFails = 0;
        logf(LOG_WARN, "Login web gagal berulang: dikunci %lu detik", (unsigned long)(ms / 1000));
      }
      sendJson(r, 401, "{\"ok\":false,\"msg\":\"Password salah\"}");
      return;
    }
    loginFails = 0;
    loginLevel = 0;
    bool remember = json["remember"] | false;
    String ua = r->hasHeader("User-Agent") ? r->header("User-Agent") : String("");
    String token = sessionCreate(remember, deviceNameFromUA(ua));
    AsyncWebServerResponse* res = r->beginResponse(200, "application/json", "{\"ok\":true}");
    // "Ingat perangkat": cookie bertahan terus (diperpanjang tiap dipakai). Tanpa itu, hilang saat browser ditutup.
    String cookie = "sid=" + token + "; Path=/; HttpOnly; SameSite=Lax";
    if (remember) cookie += "; Max-Age=" REMEMBER_COOKIE_AGE;
    res->addHeader("Set-Cookie", cookie);
    r->send(res);
  }));

  server.on("/api/logout", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (r->hasHeader("Cookie")) sessionRevoke(cookieToken(r->header("Cookie")));
    AsyncWebServerResponse* res = r->beginResponse(200, "application/json", "{\"ok\":true}");
    res->addHeader("Set-Cookie", "sid=; Path=/; Max-Age=0");
    r->send(res);
  });

  // ---- API (dispatcher bersama api_core) ----
  for (const char* p : {"/api/status", "/api/info", "/api/poll", "/api/settings", "/api/logs", "/api/history",
                        "/api/tahap/meta", "/api/job", "/api/proses", "/api/proses/export", "/api/wifi", "/api/console", "/api/update/info"})
    getRoute(p);
  for (const char* p : {"/api/proses", "/api/wifi"}) deleteRoute(p);
  for (const char* p : {"/api/settings", "/api/proses", "/api/proses/import", "/api/password", "/api/run", "/api/decide", "/api/cmd", "/api/wifi", "/api/console"})
    postJsonRoute(p);

  // ---- cadangkan & pulihkan (khusus lokal) ----
  server.on("/api/backup", HTTP_GET, [](AsyncWebServerRequest* r) {
    REQUIRE_AUTH(r);
    ApiResult res = apiHandle(API_LOCAL, "GET", "/api/backup", [r](const char* k) {
      return r->hasParam(k) ? r->getParam(k)->value() : String("");
    }, "");
    AsyncWebServerResponse* resp = r->beginResponse(res.code, "application/json", res.body);
    if (res.code == 200) resp->addHeader("Content-Disposition", "attachment; filename=\"mesin-cuci-backup.json\"");
    r->send(resp);
  });
  {
    auto* h = new AsyncCallbackJsonWebHandler("/api/restore", [](AsyncWebServerRequest* r, JsonVariant& json) {
      String body;
      serializeJson(json, body);
      bridge(r, "POST", body);
    });
    h->setMaxContentLength(RESTORE_MAX_BYTES + 512);
    server.addHandler(h);
  }

  // ---- perangkat yang diingat (khusus lokal: butuh cookie) ----
  server.on("/api/devices", HTTP_GET, [](AsyncWebServerRequest* r) {
    REQUIRE_AUTH(r);
    DeviceInfo list[MAX_DEVICES];
    String tok = currentToken(r);
    int n = deviceList(list, MAX_DEVICES, tok);
    JsonDocument d;
    JsonArray arr = d["devices"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject o = arr.add<JsonObject>();
      o["id"] = list[i].slot;
      o["name"] = list[i].name;
      o["created"] = list[i].created;
      o["lastSeen"] = list[i].lastSeen;
      o["current"] = list[i].current;
    }
    d["max"] = MAX_DEVICES;
    d["temporary"] = sessionSlot(tok) < 0;   // sesi ini tidak diingat
    String out;
    serializeJson(d, out);
    sendJson(r, 200, out);
  });
  server.on("/api/devices", HTTP_DELETE, [](AsyncWebServerRequest* r) {
    REQUIRE_AUTH(r);
    String tok = currentToken(r);
    if (r->hasParam("others")) {
      int n = deviceRemoveOthers(tok);
      sendJson(r, 200, String("{\"ok\":true,\"msg\":\"") + n + " perangkat lain dihapus\"}");
      return;
    }
    int id = r->hasParam("id") ? r->getParam("id")->value().toInt() : -1;
    int mine = sessionSlot(tok);
    if (id < 0 || !deviceRemove((uint8_t)id)) {
      sendJson(r, 404, "{\"ok\":false,\"msg\":\"Perangkat tidak ditemukan\"}");
      return;
    }
    sendJson(r, 200, String("{\"ok\":true,\"self\":") + (id == mine ? "true" : "false") + ",\"msg\":\"Perangkat dihapus\"}");
  });

  // ---- flash firmware (khusus lokal) ----
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

void webBegin() {
  setupRoutes();
  server.begin();
  logf(LOG_INFO, "Dashboard web aktif di port 80");
}
