#pragma once
// Penyimpanan NVS: pengaturan, daftar WiFi, kalibrasi, resume, statistik, auth.
#include <Arduino.h>
#include "config.h"

enum Pola : uint8_t { POLA_NORMAL, POLA_GENTLE, POLA_HIJAB };
enum FillLevel : uint8_t { FILL_SENSOR, FILL_PERCENT, FILL_TIME };
enum DrainMode : uint8_t { DRAIN_SENSOR, DRAIN_TIME };
enum NotifMode : uint8_t { NOTIF_LENGKAP, NOTIF_RINGKAS, NOTIF_SENYAP };

struct Settings {
  uint8_t pola;
  int soak;          // sesi
  int wash;          // menit
  int rinse;         // menit
  float parfume;     // cup (kelipatan 0,5)
  int spin;          // menit
  uint8_t fillLevel;
  uint8_t fillPercent;     // 25 / 50 / 75
  int fillTime;            // menit (mode waktu)
  int flH, flM, flS;       // batas waktu isi
  int fillRetryCount;
  int fillRetryWait;       // menit
  uint8_t drainMode;
  int drainTime;           // menit (mode waktu)
  int drainSessionLimit;
  int drainExtraSec;
  int drainMaxMin;
  int unjamCount;
  int dH, dM, dS;          // delay sebelum mulai
  int finishAt;            // menit-dalam-hari target selesai, -1 = tidak ada (sekali pakai)
  uint8_t notif;
};

extern Settings S;

void settingsDefaults(Settings& s);
void settingsLoad();
void settingsSave();
void settingsReset();
uint32_t fillLimitSeconds();

// --- WiFi ---
struct WifiCred {
  char ssid[33];
  char pass[65];
};
int wifiCount();
bool wifiGet(int index, WifiCred& out);
bool wifiAdd(const String& ssid, const String& pass);   // false bila penuh/kosong
bool wifiRemove(int index);
bool wifiMove(int from, int to);
void wifiStoreInit();   // migrasi + default dari secrets.h

// --- Kalibrasi ---
struct Calib {
  float avgFillMs;     // waktu nyata (tidak diskalakan)
  float avgDrainMs;
  uint16_t nFill;
  uint16_t nDrain;
};
extern Calib C;
void calibLoad();
void calibSave();
void calibReset();

// --- Resume listrik padam ---
struct ResumeInfo {
  bool valid;
  uint8_t program;
  uint8_t stepIndex;
  uint32_t savedAt;   // epoch (0 bila tidak diketahui)
};
void resumeSave(uint8_t program, uint8_t stepIndex);
bool resumeLoad(ResumeInfo& out);
void resumeClear();

// --- Statistik & riwayat ---
enum CycleResult : uint8_t { RES_DONE, RES_STOPPED, RES_ERROR };
struct HistoryEntry {
  uint32_t startEpoch;
  uint32_t durSec;
  uint8_t program;
  uint8_t result;
};
struct Stats {
  uint32_t totalCycles;
  uint32_t motorSec;
  uint8_t histCount;
  uint8_t histHead;
  HistoryEntry hist[10];
};
extern Stats ST;
void statsLoad();
void statsRecord(uint8_t program, uint32_t startEpoch, uint32_t durSec, uint8_t result);
void statsAddMotorSec(uint32_t sec);

// --- Auth web ---
bool authCheck(const String& password);
void authSet(const String& password);   // juga mencabut semua sesi login

// Sesi login tersimpan di NVS (bertahan saat restart / flash ulang non-erase)
#define MAX_SESSIONS 8
String sessionCreate();                  // buat token baru (yang tertua digantikan)
bool sessionValid(const String& token);
void sessionRevoke(const String& token);
void sessionRevokeAll();
