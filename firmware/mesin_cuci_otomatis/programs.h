#pragma once
// Definisi program (data): urutan langkah + preset.
#include <Arduino.h>
#include "settings.h"
#include "relay.h"

enum ProgramId : uint8_t {
  PRG_KUSTOM, PRG_NORMAL, PRG_GENTLE, PRG_HIJAB, PRG_CEPAT,
  PRG_SOAK, PRG_WASH, PRG_SOAKWASH, PRG_RINSE, PRG_RINSEPARFUME,
  PRG_SPIN, PRG_WATERIN, PRG_WATEROUT, PRG_PARFUME, PRG_MOTOR, PRG_TEST, PRG_RELAYSEQ,
  PRG_COUNT
};

enum StepType : uint8_t {
  ST_WAIT,     // menunggu mulai (a = detik)
  ST_FILL,     // isi air sesuai level
  ST_AGITATE,  // putar kiri/kanan (a = menit, flag = pola)
  ST_SOAK,     // sesi agitasi 1 mnt + rendam 4 mnt (a = jumlah sesi, flag = pola)
  ST_PERFUME,  // pompa pewangi
  ST_DRAIN,    // buang air (flag = 1: lunak, gagal tidak menjeda)
  ST_SPIN,     // motor spin (a = menit)
  ST_CHECK,    // pastikan air cukup
  ST_TEST      // uji relay (a = RelayId, flag = detik menyala, 0 = 2 dtk)
};

enum StageKind : uint8_t { KIND_NONE, KIND_CUCI, KIND_BILAS, KIND_BILAS_PEWANGI, KIND_TUNGGU };

struct Step {
  uint8_t type;
  uint8_t flag;
  uint8_t grp;    // nomor tahap (0..)
  uint8_t kind;   // StageKind
  uint32_t a;
};

#define MAX_STEPS 16

struct Preset {
  const char* name;
  uint8_t pola;
  int soak, wash, rinse;
};

const char* programName(uint8_t id);                 // nama tampilan
const char* programCmdName(uint8_t id);              // nama perintah
int programFromCmdName(const String& name);          // -1 bila tidak dikenal
bool programIsFull(uint8_t id);
bool programIsTest(uint8_t id);                      // uji relay: tanpa riwayat/pemulihan                      // Kustom/Normal/Gentle/Hijab/Cepat

// Susun langkah program. Mengembalikan jumlah langkah; groups = jumlah tahap.
uint8_t buildProgram(uint8_t id, const Settings& s, uint8_t arg, Step* out, uint8_t& groups);
const char* stepTypeName(uint8_t type);
