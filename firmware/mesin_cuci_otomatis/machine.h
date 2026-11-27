// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#pragma once
// Mesin (Core 1): state machine Proses berbasis millis(), tanpa delay().
// Proses = urutan Tahap (tahap.h). Mesin menjalankan salinan Proses di RAM; tidak membaca berkas saat berjalan.
#include <Arduino.h>
#include "relay.h"
#include "tahap.h"

enum MState : uint8_t { MS_IDLE, MS_RUNNING, MS_PAUSED, MS_DECIDE };

// Keputusan menunggu: mesin berhenti dan tidak pernah lanjut sendiri.
enum DecCode : uint8_t { DC_NONE, DC_ISI_GAGAL, DC_PRA_PENUH, DC_BUANG_GAGAL, DC_CEK_GAGAL, DC_TINDAKAN };
enum DecOpt : uint8_t { OP_ULANG = 1, OP_LANJUT = 2, OP_BATAL = 4, OP_BUANG = 8, OP_PAKAI = 16 };

const char* decisionCodeName(uint8_t code);       // "isi_gagal", ...
const char* decisionOptName(uint8_t opt);         // "ulang", "lanjut", "batal", "buang", "pakai"
const char* decisionOptLabel(uint8_t opt);        // label tampilan
uint8_t decisionOptFromName(const String& name);  // 0 bila tidak dikenal

#define TH_UJI_RELAY 100   // internal: uji/kalibrasi relay (mode = RelayId, detik = lama menyala)
#define NO_SLOT 255

enum MachineCmdType : uint8_t {
  MC_RUN,            // arg = slot Proses; waitSec = tunda; finishAt = selesai pada jam (menit dalam hari, -1 tidak ada)
  MC_RUN_MANUAL,     // jalankan satu Tahap (menu Manual): tahap
  MC_RUN_RELAYTEST,  // arg = RelayId, arg2 = detik (0 = 2 dtk)
  MC_RUN_RELAYSEQ,   // kalibrasi relay: arg = detik per relay
  MC_STOP,
  MC_PAUSE,
  MC_RESUME,         // lanjutkan dari jeda, atau dari pemulihan listrik padam
  MC_SKIP,
  MC_CANCEL_RESUME,  // batalkan tawaran pemulihan listrik padam
  MC_RELAY,          // kontrol relay manual (hanya saat siap): arg = RelayId, arg2 = 1 nyala / 0 mati
  MC_DECIDE,         // arg = DecOpt
};

struct MachineCmd {
  uint8_t type;
  uint8_t arg;
  uint8_t arg2;
  uint8_t ws;        // Ws sasaran (WS_ALL hanya untuk MC_STOP); uji relay & relay manual mengikuti pemilik relay
  uint32_t waitSec;
  int16_t finishAt;
  Tahap tahap;
};

struct MachineStatus {
  uint8_t ws;               // workspace snapshot ini
  bool relaySeq;            // kalibrasi relay berurutan sedang memakai semua relay
  uint8_t state;            // MState
  uint8_t slot;             // slot Proses yang berjalan (NO_SLOT untuk Manual/uji)
  bool manual;
  uint8_t stepIndex;        // 1-based (0 = menunggu mulai)
  uint8_t stepCount;
  char name[28];            // nama Proses
  char stage[40];           // mis. "Tahap 2/5: Putar mesin"
  char tahap[16];           // kunci jenis Tahap ("isi", "putar", ...)
  char detail[56];          // mis. "Agitasi (kanan)"
  char pauseReason[72];     // alasan jeda / pesan keputusan
  uint8_t decision;         // DecCode (DC_NONE bila tidak menunggu)
  uint8_t decisionOpts;     // bitmask DecOpt
  uint32_t stepLeftSec;
  uint32_t totalLeftSec;
  uint8_t progressPct;
  uint8_t waterState;       // 0 kosong, 1 penuh, 2 mengukur
  bool waterRaw;
  bool relayFault;          // interlock dilanggar: relay terkunci sampai restart
  bool resumePending;       // listrik sempat padam
  uint8_t resumeSlot;       // NO_SLOT bila tidak ada
  char resumeInfo[56];
  bool relays[R_COUNT];
  uint32_t uptimeSec;
};

void machineBegin();                           // buat task di Core 1
bool machinePost(const MachineCmd& cmd);
bool machineStartProses(uint8_t slot, uint32_t waitSec = 0, int finishAtMin = -1, uint8_t ws = WS_CUCI);
bool machineRunManual(const Tahap& t, uint8_t ws = WS_CUCI);
void machineGetStatus(MachineStatus& out, uint8_t ws = WS_CUCI);   // air, relay, dan uptime sama di kedua workspace
uint8_t machineRelayWs(uint8_t relay);          // workspace pemilik relay (spin = pengering)
// Salinan pekerjaan yang sedang berjalan untuk API (/api/job). false bila idle atau mutex sibuk.
bool machineCopyJob(Proses& out, uint8_t& idx, bool& waiting, uint32_t& waitLeftMs, uint8_t& slot, bool& manual, bool& test,
                    uint8_t ws = WS_CUCI);
bool machineIsIdle();                          // kedua workspace idle
bool machineIsIdle(uint8_t ws);
bool machineSlotInUse(uint8_t id);             // id = prosesId(ws, slot): berjalan/menunggu/dijeda atau pemulihan tertunda
void machineHold(bool hold);                   // tahan Proses baru (saat flash firmware)
String machineStatusLine();                    // ringkas untuk konsol
