#pragma once
// Flash firmware: web (upload .bin) atau USB; rollback ke partisi sebelumnya.
#include <Arduino.h>

bool otaBusy();            // sedang menulis firmware (koneksi jarak jauh dijeda)
bool otaWebBegin(size_t totalSize, String& err);
bool otaWebWrite(uint8_t* data, size_t len);
bool otaWebEnd(String& err);
void otaWebAbort();
uint8_t otaProgress();     // 0..100

String otaInfoJson();      // versi aktif, partisi, apakah rollback tersedia
bool otaRollback(String& err);
void otaMarkValidIfHealthy();   // tandai firmware valid setelah ±30 dtk berjalan
