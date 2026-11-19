#include "net_wifi.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include "ping/ping_sock.h"
#include "config.h"
#include "logger.h"
#include "net_policy.h"
#include "settings.h"

static DNSServer dns;
static String apSsid;
static volatile bool online = false;
static volatile bool apActive = false;
static volatile bool reconnectRequested = false;
static bool allSsidFailed = false;

enum WState : uint8_t { W_IDLE, W_TRY, W_BACKOFF };

static WState wstate = W_IDLE;
static int tryIndex = 0;
static uint32_t tryStart = 0;
static uint32_t backoffUntil = 0;
static uint32_t backoffMs = 30000;
static bool ntpStarted = false;
static ApPolicy apPol;
static ConnWatchdog connWd;
static uint32_t lastPingMs = 0;

bool netOnline() { return online; }
String netIp() { return online ? WiFi.localIP().toString() : String(""); }
String netSsid() { return online ? WiFi.SSID() : String(""); }
int netRssi() { return online ? WiFi.RSSI() : 0; }
String netApSsid() { return apSsid; }
bool netApOn() { return apActive; }
int netApClients() { return apActive ? WiFi.softAPgetStationNum() : 0; }
void netReconnectNow() { reconnectRequested = true; }

bool netTimeSynced() { return time(nullptr) > 1700000000; }

String netTimeString() {
  if (!netTimeSynced()) return "-";
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
  return String(buf);
}

// ---------------------------------------------------------------- hotspot ----
static void enableAp() {
  WiFi.enableAP(true);
  IPAddress apIp(192, 168, 4, 1);
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  if (strlen(AP_PASSWORD) >= 8) {
    WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
  } else {
    logf(LOG_WARN, "AP_PASSWORD kurang dari 8 karakter, hotspot terbuka!");
    WiFi.softAP(apSsid.c_str());
  }
  dns.start(53, "*", apIp);   // captive portal hanya saat hotspot nyala
  apActive = true;
  logf(LOG_WARN, "Hotspot aktif (WiFi rumah tidak tersedia): %s, http://192.168.4.1", apSsid.c_str());
}

static void disableAp() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.enableAP(false);
  apActive = false;
  logf(LOG_INFO, "Hotspot dimatikan (WiFi rumah tersambung)");
}

// ------------------------------------------------------------------ ping ----
static volatile int pingResult = 0;   // 0 menunggu, 1 berhasil, -1 gagal
static void onPingOk(esp_ping_handle_t, void*) { pingResult = 1; }
static void onPingTimeout(esp_ping_handle_t, void*) { pingResult = -1; }

// true bila gateway menjawab. Bila sesi ping tidak bisa dibuat, anggap sehat (jangan menyalahkan jaringan).
static bool pingGateway(uint32_t timeoutMs) {
  IPAddress gw = WiFi.gatewayIP();
  if (gw == IPAddress((uint32_t)0)) return false;
  ip_addr_t target;
  IP_ADDR4(&target, gw[0], gw[1], gw[2], gw[3]);
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr = target;
  cfg.count = 1;
  cfg.timeout_ms = timeoutMs;
  cfg.interval_ms = 1000;
  esp_ping_callbacks_t cb = {};
  cb.on_ping_success = onPingOk;
  cb.on_ping_timeout = onPingTimeout;
  esp_ping_handle_t h = nullptr;
  if (esp_ping_new_session(&cfg, &cb, &h) != ESP_OK || !h) return true;
  pingResult = 0;
  esp_ping_start(h);
  uint32_t t0 = millis();
  while (pingResult == 0 && millis() - t0 < timeoutMs + 500) vTaskDelay(pdMS_TO_TICKS(20));
  esp_ping_stop(h);
  esp_ping_delete_session(h);
  return pingResult == 1;
}

// ---------------------------------------------------------------- rotasi ----
static void startRotation() {
  if (wifiCount() == 0) {
    wstate = W_IDLE;
    allSsidFailed = true;   // tidak ada WiFi tersimpan: hotspot jadi satu-satunya jalur akses
    return;
  }
  tryIndex = 0;
  wstate = W_TRY;
  tryStart = 0;   // paksa mulai percobaan pertama
}

static void beginTry() {
  WifiCred cred;
  if (!wifiGet(tryIndex, cred)) return;
  logf(LOG_INFO, "Mencoba WiFi %d/%d: %s", tryIndex + 1, wifiCount(), cred.ssid);
  WiFi.disconnect(false);
  WiFi.begin(cred.ssid, cred.pass);
  tryStart = millis();
}

static void onConnected() {
  WiFi.setSleep(false);
  online = true;
  allSsidFailed = false;
  backoffMs = 30000;
  wstate = W_IDLE;
  connWdInit(connWd);
  lastPingMs = millis();
  logf(LOG_INFO, "WiFi tersambung: %s (%s) %d dBm", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
       WiFi.RSSI());
  if (!ntpStarted) {
    configTzTime(NTP_TZ, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    ntpStarted = true;
  }
}

static void restartWifi() {
  logf(LOG_WARN, "Reconnect berulang tidak berhasil, WiFi dimulai ulang");
  online = false;
  if (apActive) {
    dns.stop();
    apActive = false;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  vTaskDelay(pdMS_TO_TICKS(500));
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  apPolicyInit(apPol);
  startRotation();
}

static void netTask(void*) {
  for (;;) {
    if (apActive) dns.processNextRequest();

    if (reconnectRequested) {
      reconnectRequested = false;
      online = false;
      WiFi.disconnect(false);
      startRotation();
    }

    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected) {
      if (!online) onConnected();

      // Watchdog konektivitas: ping gateway tiap 30 dtk; link "zombie" (CONNECTED tapi tanpa paket) dipulihkan.
      if (millis() - lastPingMs >= 30000) {
        lastPingMs = millis();
        bool ok = pingGateway(2000);
        bool wasArmed = connWd.armed;
        ConnAction act = connWdReport(connWd, ok);
        if (!wasArmed && connWd.armed) logf(LOG_DEBUG, "Watchdog konektivitas aktif (gateway menjawab ping)");
        if (act == CONN_RECONNECT) {
          logf(LOG_WARN, "Gateway tidak menjawab, menyambung ulang WiFi");
          online = false;
          WiFi.disconnect(false);
          startRotation();
        } else if (act == CONN_RESTART_WIFI) {
          restartWifi();
        }
      }
    } else {
      if (online) {
        online = false;
        logf(LOG_WARN, "WiFi terputus, mencari jaringan...");
        startRotation();
      }
      uint32_t now = millis();
      switch (wstate) {
        case W_IDLE:
          if (wifiCount() > 0) startRotation();
          break;
        case W_TRY:
          if (tryStart == 0 || now - tryStart >= WIFI_TRY_TIMEOUT_MS) {
            if (tryStart != 0) {
              logf(LOG_DEBUG, "Gagal tersambung ke WiFi %d", tryIndex + 1);
              tryIndex++;
            }
            if (tryIndex >= wifiCount()) {
              // Semua SSID gagal satu putaran: buka hotspot, lalu tunggu dengan backoff.
              allSsidFailed = true;
              uint32_t wait = backoffMs;
              if (WiFi.softAPgetStationNum() > 0) wait = min<uint32_t>(wait * 3, 300000);
              logf(LOG_WARN, "Tidak ada WiFi tersambung, coba lagi %lu dtk", (unsigned long)(wait / 1000));
              backoffUntil = now + wait;
              backoffMs = min<uint32_t>(backoffMs * 2, 300000);
              WiFi.disconnect(false);
              wstate = W_BACKOFF;
            } else {
              beginTry();
            }
          }
          break;
        case W_BACKOFF:
          if ((int32_t)(now - backoffUntil) >= 0) startRotation();
          break;
      }
    }

    bool wantAp = apPolicyStep(apPol, millis(), connected, allSsidFailed, apActive ? WiFi.softAPgetStationNum() : 0);
    if (wantAp && !apActive) enableAp();
    else if (!wantAp && apActive) disableAp();

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void netBegin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);   // hotspot TIDAK nyala saat boot; hanya bila semua SSID gagal
  WiFi.setSleep(false);  // matikan power-save WiFi: tanpa ini dashboard lambat/putus-putus
  WiFi.setHostname(DEVICE_HOSTNAME);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssid[24];
  snprintf(ssid, sizeof(ssid), "MesinCuci-%02X%02X", mac[4], mac[5]);
  apSsid = ssid;

  if (MDNS.begin(DEVICE_HOSTNAME)) MDNS.addService("http", "tcp", 80);

  apPolicyInit(apPol);
  connWdInit(connWd);
  startRotation();
  xTaskCreatePinnedToCore(netTask, "net", 5120, nullptr, 2, nullptr, 0);
}
