#include "net_wifi.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include "config.h"
#include "logger.h"
#include "settings.h"
#include "ota.h"

static DNSServer dns;
static String apSsid;
static volatile bool online = false;
static volatile bool reconnectRequested = false;

enum WState : uint8_t { W_IDLE, W_TRY, W_BACKOFF };

static WState wstate = W_IDLE;
static int tryIndex = 0;
static uint32_t tryStart = 0;
static uint32_t backoffUntil = 0;
static uint32_t backoffMs = 30000;
static bool ntpStarted = false;

bool netOnline() { return online; }
String netIp() { return online ? WiFi.localIP().toString() : String(""); }
String netSsid() { return online ? WiFi.SSID() : String(""); }
int netRssi() { return online ? WiFi.RSSI() : 0; }
String netApSsid() { return apSsid; }
int netApClients() { return WiFi.softAPgetStationNum(); }
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

static void startRotation() {
  if (wifiCount() == 0) {
    wstate = W_IDLE;
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
  online = true;
  backoffMs = 30000;
  wstate = W_IDLE;
  logf(LOG_INFO, "WiFi tersambung: %s (%s) %d dBm", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
       WiFi.RSSI());
  if (!ntpStarted) {
    configTzTime(NTP_TZ, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    ntpStarted = true;
  }
}

static void netTask(void*) {
  for (;;) {
    dns.processNextRequest();
    otaNetLoop();

    if (reconnectRequested) {
      reconnectRequested = false;
      online = false;
      WiFi.disconnect(false);
      startRotation();
    }

    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected) {
      if (!online) onConnected();
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
              // Semua gagal: tunggu dengan backoff. Pelan saat ada klien hotspot.
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
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void netBegin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssid[24];
  snprintf(ssid, sizeof(ssid), "MesinCuci-%02X%02X", mac[4], mac[5]);
  apSsid = ssid;

  IPAddress apIp(192, 168, 4, 1);
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  if (strlen(AP_PASSWORD) >= 8) {
    WiFi.softAP(ssid, AP_PASSWORD);
  } else {
    logf(LOG_WARN, "AP_PASSWORD kurang dari 8 karakter, hotspot terbuka!");
    WiFi.softAP(ssid);
  }
  dns.start(53, "*", apIp);   // captive portal
  logf(LOG_INFO, "Hotspot aktif: %s (http://192.168.4.1)", ssid);

  otaNetBegin();   // ArduinoOTA + mDNS mesincuci.local
  MDNS.addService("http", "tcp", 80);

  startRotation();
  xTaskCreatePinnedToCore(netTask, "net", 5120, nullptr, 2, nullptr, 0);
}
