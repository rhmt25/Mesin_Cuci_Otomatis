#pragma once
// Mesin (Core 1): state machine program berbasis millis(), tanpa delay().
#include <Arduino.h>
#include "relay.h"
#include "programs.h"

enum MState : uint8_t { MS_IDLE, MS_RUNNING, MS_PAUSED };

enum MachineCmdType : uint8_t {
  MC_RUN,           // arg = ProgramId, arg2 = argumen program (mis. relay uji)
  MC_STOP,
  MC_PAUSE,
  MC_RESUME,        // lanjutkan dari jeda, atau dari pemulihan listrik padam
  MC_SKIP,
  MC_CANCEL_RESUME, // batalkan tawaran pemulihan listrik padam
  MC_RELAY,         // kontrol relay manual (hanya saat siap): arg = RelayId, arg2 = 1 nyala / 0 mati
};

struct MachineCmd {
  uint8_t type;
  uint8_t arg;
  uint8_t arg2;
};

struct MachineStatus {
  uint8_t state;            // MState
  uint8_t program;          // ProgramId (valid bila state != IDLE)
  uint8_t stepIndex;        // 1-based
  uint8_t stepCount;
  char name[28];            // nama program (untuk Custom: nama yang diberikan pengguna)
  char stage[28];           // mis. "Bilas 2/3"
  char detail[56];          // mis. "Agitasi (kanan)"
  char pauseReason[72];
  uint32_t stepLeftSec;
  uint32_t totalLeftSec;
  uint8_t progressPct;
  uint8_t waterState;       // 0 kosong, 1 penuh, 2 mengukur
  bool waterRaw;
  bool relayFault;          // interlock dilanggar: relay terkunci sampai restart
  bool resumePending;       // listrik sempat padam
  char resumeInfo[56];
  bool relays[R_COUNT];
  uint32_t uptimeSec;
};

void machineBegin();                           // buat task di Core 1
bool machinePost(const MachineCmd& cmd);
void machineGetStatus(MachineStatus& out);
bool machineIsIdle();
void machineHold(bool hold);                   // tahan program baru (saat flash firmware)
String machineStatusLine();                    // ringkas untuk konsol
