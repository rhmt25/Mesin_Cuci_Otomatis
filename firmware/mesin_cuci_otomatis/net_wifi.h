#pragma once
// WiFi multi-SSID non-blocking (STA) + hotspot sendiri (AP) + DNS captive + mDNS + NTP.
#include <Arduino.h>

void netBegin();                 // buat task di Core 0
bool netOnline();                // STA tersambung
String netIp();
String netSsid();
int netRssi();
String netApSsid();
int netApClients();
bool netTimeSynced();
String netTimeString();          // "HH:MM" atau "-"
void netReconnectNow();          // setelah daftar WiFi diubah
