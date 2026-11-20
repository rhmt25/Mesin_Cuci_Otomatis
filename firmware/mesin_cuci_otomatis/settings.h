#pragma once
// Penyimpanan NVS: pengaturan, daftar WiFi, kalibrasi, resume, statistik, auth.
#include <Arduino.h>
#include "config.h"

enum Pola : uint8_t { POLA_NORMAL, POLA_GENTLE, POLA_HIJAB };
enum NotifMode : uint8_t { NOTIF_LENGKAP, NOTIF_RINGKAS, NOTIF_SENYAP };

// Pengaturan global yang tersisa. Semua variabel proses (air, putar, pewangi, ...) kini ada di tiap Tahap (tahap.h).
struct Settings {
  uint8_t notif;
  uint8_t remoteOn;        // kontrol jarak jauh aktif (bila REMOTE_URL terisi)
};

extern Settings S;

void settingsDefaults(Settings& s);
void settingsSanitize(Settings& s);   // jepit nilai ke rentang valid
void settingsLoad();
void settingsSave();
void settingsReset();

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
  uint16_t nFill;
};
extern Calib C;
void calibLoad();
void calibSave();
void calibReset();

// --- Resume listrik padam ---
struct ResumeInfo {
  bool valid;
  uint8_t slot;       // slot Proses (0..9)
  uint8_t stepIndex;  // indeks Tahap yang sedang berjalan
  uint16_t rev;       // revisi Proses saat dimulai; berbeda = Proses sudah diubah, pemulihan dibatalkan
  uint32_t savedAt;   // epoch (0 bila tidak diketahui)
};
void resumeSave(uint8_t slot, uint8_t stepIndex, uint16_t rev);
bool resumeLoad(ResumeInfo& out);
void resumeClear();

// --- Statistik & riwayat ---
enum CycleResult : uint8_t { RES_DONE, RES_STOPPED, RES_ERROR };
struct HistoryEntry {
  uint32_t startEpoch;
  uint32_t durSec;
  char name[24];      // nama Proses ("Manual: Isi air" untuk menu Manual)
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
void statsRecord(const char* name, uint32_t startEpoch, uint32_t durSec, uint8_t result);
void statsAddMotorSec(uint32_t sec);

// --- Auth web ---
bool authCheck(const String& password);
void authSet(const String& password);   // juga mencabut semua sesi login

// --- Perangkat login yang diingat (maks 5, selamanya sampai dihapus) ---
#define MAX_DEVICES 5
struct DeviceInfo {
  uint8_t slot;
  char name[32];
  uint32_t created;    // epoch, 0 bila jam belum sinkron
  uint32_t lastSeen;
  bool current;
};
// remember=true -> disimpan di NVS sebagai "perangkat diingat" (bila penuh, yang terlama dihapus).
// remember=false -> sesi sementara (RAM, hilang saat restart).
String sessionCreate(bool remember, const String& deviceName);
bool sessionValid(const String& token);
int sessionSlot(const String& token);            // slot perangkat diingat, -1 bila sementara/tidak ada
void sessionRevoke(const String& token);
void sessionRevokeAll();
int deviceList(DeviceInfo* out, int maxCount, const String& currentToken);
bool deviceRemove(uint8_t slot);
int deviceRemoveOthers(const String& currentToken);   // jumlah yang dihapus
