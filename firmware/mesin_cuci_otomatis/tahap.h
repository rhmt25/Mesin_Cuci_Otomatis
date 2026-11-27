// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#pragma once
// Tahap = satu aksi mesin terkecil dengan variabelnya sendiri. Proses = urutan Tahap buatan pemilik.
// Katalog (jenis, variabel, rentang, bawaan) ada di sini dan diekspos ke UI lewat tahapMetaJson().
#include <Arduino.h>
#include <ArduinoJson.h>
#include "settings.h"   // Pola (POLA_NORMAL/GENTLE/HIJAB) dan kalibrasi

enum TahapType : uint8_t {
  TH_ISI, TH_BUANG, TH_PUTAR, TH_SOAK, TH_RENDAM, TH_SPIN, TH_PEWANGI, TH_TUNGGU, TH_CEK, TH_TINDAKAN, TH_NOTIF,
  TH_BUANG_MANUAL,   // hanya untuk menu Manual (valve ditarik selama waktu tertentu, tanpa sensor)
  TH_COUNT
};

enum IsiMode : uint8_t { ISI_SENSOR, ISI_PERSEN, ISI_WAKTU };
enum IsiPra : uint8_t { PRA_BUANG, PRA_JEDA, PRA_PAKAI };     // bila air sudah penuh sebelum mengisi
enum CekSyarat : uint8_t { CEK_PENUH, CEK_TIDAK_PENUH };

#define PESAN_MAX 40

// Dua workspace yang berjalan bersamaan: tabung cuci dan tabung pengering (spin). Masing-masing punya Proses sendiri.
enum Ws : uint8_t { WS_CUCI, WS_KERING, WS_COUNT };
#define WS_ALL 0xFF   // perintah Stop untuk kedua workspace

struct Tahap {
  uint8_t type;
  uint8_t mode;     // Isi: IsiMode; Cek: CekSyarat
  uint8_t pra;      // Isi: IsiPra
  uint8_t pola;     // Putar, Soak
  uint8_t persen;   // Isi (mode persen): 25 / 50 / 75
  uint8_t coba;     // Isi: coba ulang
  uint8_t tarik;    // Buang: Tarik Ulang Valve
  uint8_t sesi;     // Buang: batas ulang sesi
  uint8_t cup2;     // Pewangi: jumlah cup x 2
  uint16_t lama;    // menit (Isi-waktu, Buang, Putar, Rendam, Spin, Buang manual)
  uint16_t tunggu;  // Isi: menit antar coba ulang
  uint16_t probe;   // Buang: detik (Verifikasi Pengurangan Jumlah Air)
  uint16_t ulangi;  // Soak: jumlah sesi
  uint16_t putar;   // Soak: menit putar per sesi
  uint16_t rendam;  // Soak: menit rendam per sesi
  uint32_t batas;   // Isi: batas waktu (detik)
  uint32_t detik;   // Tunggu: detik
  char pesan[PESAN_MAX + 1];   // Minta tindakan / Notifikasi
};

#define PROSES_SLOTS 10                              // per workspace
#define PROSES_TOTAL (PROSES_SLOTS * 2)             // id penyimpanan: 0-9 cuci, 10-19 pengering
inline uint8_t prosesId(uint8_t ws, uint8_t slot) { return ws * PROSES_SLOTS + slot; }
#define PROSES_MAX_TAHAP 40
#define PROSES_NAME_MAX 27

struct Proses {
  char name[PROSES_NAME_MAX + 1];
  uint16_t rev;     // naik tiap simpan (untuk membatalkan pemulihan bila Proses berubah)
  uint8_t count;
  uint8_t ws;       // Ws
  Tahap t[PROSES_MAX_TAHAP];
};

const char* tahapKey(uint8_t type);                  // "isi", "buang", ...
const char* tahapLabel(uint8_t type);                // "Isi air", "Buang air", ...
int tahapFromKey(const String& key);                 // -1 bila tidak dikenal
bool tahapAllowed(uint8_t type, uint8_t ws);         // Tahap tersedia di workspace ini?
const char* wsName(uint8_t ws);                      // "cuci" / "pengering"
int wsFromName(const String& name);                  // "" = cuci; -1 bila tidak dikenal
void tahapDefault(uint8_t type, Tahap& t);

// JSON <-> struct. fromJson mulai dari nilai bawaan jenisnya, lalu menimpa kunci yang ada, lalu validasi.
bool tahapFromJson(JsonObjectConst o, Tahap& t, String& err);
void tahapToJson(const Tahap& t, JsonObject o);
bool tahapValidate(const Tahap& t, String& err, bool allowManualOnly = false);

// Proses: {"v":1,"name":"..","rev":N,"tahap":[{...}]}
// ws menentukan Tahap yang diterima. Tahap Spin pada Proses cuci lama diubah otomatis (lihat tahap.cpp).
bool prosesFromJson(JsonVariantConst v, Proses& p, String& err, bool keepRev = false, uint8_t ws = WS_CUCI);
void prosesToJson(const Proses& p, JsonObject o);
bool prosesValidate(const Proses& p, String& err);
int prosesWarnings(const Proses& p, String* out, int maxOut);   // peringatan (boleh disimpan setelah konfirmasi)

// Estimasi (detik NYATA, belum diskalakan TIME_SCALE)
uint32_t tahapEstimateSec(const Tahap& t);
uint32_t prosesEstimateSec(const Proses& p);

String tahapRingkas(const Tahap& t);                 // mis. "Isi air · sensor, batas 00:30:00"
String tahapParams(const Tahap& t);                  // bagian setelah nama: "sensor, batas 00:30:00" ("" bila tidak ada)
String tahapMetaJson();                              // katalog untuk UI
