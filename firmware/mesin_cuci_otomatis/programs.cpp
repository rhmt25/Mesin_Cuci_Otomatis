#include "programs.h"

// Nilai awal preset; bebas diubah di sini.
static const Preset presets[] = {
    {"Kustom", 0, 0, 0, 0},          // memakai pengaturan
    {"Normal", POLA_NORMAL, 2, 15, 10},
    {"Gentle", POLA_GENTLE, 2, 12, 8},
    {"Hijab", POLA_HIJAB, 1, 10, 8},
    {"Cepat", POLA_NORMAL, 0, 8, 5},
};

static const char* const names[PRG_COUNT] = {
    "Kustom", "Normal", "Gentle", "Hijab", "Cepat", "Soak", "Wash", "Soak+Wash", "Rinse",
    "Rinse+Pewangi", "Spin", "Isi air", "Buang air", "Pewangi", "Motor", "Uji relay"};
static const char* const cmdNames[PRG_COUNT] = {
    "kustom", "normal", "gentle", "hijab", "cepat", "soak", "wash", "soak_and_wash", "rinse",
    "rinse_parfume", "spin", "water_in", "water_out", "add_parfume", "motor_on", "relay_test"};

const char* programName(uint8_t id) { return id < PRG_COUNT ? names[id] : "?"; }
const char* programCmdName(uint8_t id) { return id < PRG_COUNT ? cmdNames[id] : "?"; }
bool programIsFull(uint8_t id) { return id <= PRG_CEPAT; }

int programFromCmdName(const String& name) {
  for (int i = 0; i < PRG_COUNT; i++)
    if (name.equalsIgnoreCase(cmdNames[i])) return i;
  return -1;
}

const char* stepTypeName(uint8_t type) {
  static const char* n[] = {"Menunggu", "Isi air", "Agitasi", "Soak", "Pewangi",
                            "Buang air", "Spin", "Cek air", "Uji relay"};
  return type < 9 ? n[type] : "?";
}

namespace {
struct Builder {
  Step* out;
  uint8_t n;
  explicit Builder(Step* o) : out(o), n(0) {}
  void add(StepType t, uint32_t a, uint8_t flag, uint8_t grp, uint8_t kind) {
    if (n >= MAX_STEPS) return;
    out[n++] = Step{(uint8_t)t, flag, grp, kind, a};
  }
};
}  // namespace

uint8_t buildProgram(uint8_t id, const Settings& s, uint8_t arg, Step* out, uint8_t& groups) {
  Builder b(out);
  groups = 1;
  uint8_t pola = s.pola;
  int soak = s.soak, wash = s.wash, rinse = s.rinse;

  if (programIsFull(id)) {
    if (id != PRG_KUSTOM) {
      const Preset& p = presets[id];
      pola = p.pola;
      soak = p.soak;
      wash = p.wash;
      rinse = p.rinse;
    }
    uint8_t g = 0;
    if (soak > 0 || wash > 0) {
      b.add(ST_FILL, 0, 0, g, KIND_CUCI);
      if (soak > 0) b.add(ST_SOAK, soak, pola, g, KIND_CUCI);
      if (wash > 0) b.add(ST_AGITATE, wash, pola, g, KIND_CUCI);
      b.add(ST_DRAIN, 0, 0, g, KIND_CUCI);
      g++;
    }
    if (rinse > 0) {
      b.add(ST_FILL, 0, 0, g, KIND_BILAS);
      b.add(ST_AGITATE, rinse, pola, g, KIND_BILAS);
      b.add(ST_DRAIN, 0, 0, g, KIND_BILAS);
      g++;
      b.add(ST_FILL, 0, 0, g, KIND_BILAS_PEWANGI);
      b.add(ST_PERFUME, 0, 0, g, KIND_BILAS_PEWANGI);
      b.add(ST_AGITATE, rinse, pola, g, KIND_BILAS_PEWANGI);
      b.add(ST_DRAIN, 0, 0, g, KIND_BILAS_PEWANGI);
      g++;
    }
    groups = g ? g : 1;
    return b.n;
  }

  switch (id) {
    case PRG_SOAK:
      if (soak > 0) {
        b.add(ST_CHECK, 0, 0, 0, KIND_NONE);
        b.add(ST_SOAK, soak, pola, 0, KIND_NONE);
      }
      break;
    case PRG_WASH:
      if (wash > 0) {
        b.add(ST_FILL, 0, 0, 0, KIND_NONE);
        b.add(ST_AGITATE, wash, pola, 0, KIND_NONE);
        b.add(ST_DRAIN, 0, 0, 0, KIND_NONE);
      }
      break;
    case PRG_SOAKWASH:
      if (soak > 0 && wash > 0) {
        b.add(ST_FILL, 0, 0, 0, KIND_NONE);
        b.add(ST_SOAK, soak, pola, 0, KIND_NONE);
        b.add(ST_AGITATE, wash, pola, 0, KIND_NONE);
        b.add(ST_DRAIN, 0, 0, 0, KIND_NONE);
      }
      break;
    case PRG_RINSE:
      if (rinse > 0) {
        b.add(ST_FILL, 0, 0, 0, KIND_NONE);
        b.add(ST_AGITATE, rinse, pola, 0, KIND_NONE);
        b.add(ST_DRAIN, 0, 0, 0, KIND_NONE);
      }
      break;
    case PRG_RINSEPARFUME:
      if (rinse > 0) {
        b.add(ST_FILL, 0, 0, 0, KIND_NONE);
        b.add(ST_PERFUME, 0, 0, 0, KIND_NONE);
        b.add(ST_AGITATE, rinse, pola, 0, KIND_NONE);
        b.add(ST_DRAIN, 0, 0, 0, KIND_NONE);
      }
      break;
    case PRG_SPIN:
      if (s.spin > 0) b.add(ST_SPIN, s.spin, 0, 0, KIND_NONE);
      break;
    case PRG_WATERIN: b.add(ST_FILL, 0, 0, 0, KIND_NONE); break;
    case PRG_WATEROUT: b.add(ST_DRAIN, 0, 1, 0, KIND_NONE); break;
    case PRG_PARFUME: b.add(ST_PERFUME, 0, 0, 0, KIND_NONE); break;
    case PRG_MOTOR:
      b.add(ST_CHECK, 0, 0, 0, KIND_NONE);
      b.add(ST_AGITATE, 5, pola, 0, KIND_NONE);
      break;
    case PRG_TEST: b.add(ST_TEST, arg, 0, 0, KIND_NONE); break;
    default: break;
  }
  return b.n;
}
