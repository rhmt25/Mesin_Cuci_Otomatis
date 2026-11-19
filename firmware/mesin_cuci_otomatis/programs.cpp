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
    "Rinse+Pewangi", "Spin", "Isi air", "Buang air", "Pewangi", "Motor", "Uji relay", "Kalibrasi relay", "Custom"};
static const char* const cmdNames[PRG_COUNT] = {
    "kustom", "normal", "gentle", "hijab", "cepat", "soak", "wash", "soak_and_wash", "rinse",
    "rinse_parfume", "spin", "water_in", "water_out", "add_parfume", "motor_on", "relay_test", "kalibrasi_relay", "custom"};

const char* programName(uint8_t id) { return id < PRG_COUNT ? names[id] : "?"; }
const char* programCmdName(uint8_t id) { return id < PRG_COUNT ? cmdNames[id] : "?"; }
bool programIsFull(uint8_t id) { return id <= PRG_CEPAT; }
bool programIsTest(uint8_t id) { return id == PRG_TEST || id == PRG_RELAYSEQ; }

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


// ============================================================================
// Custom Proses
// ============================================================================
static const char* const customNames[CI_COUNT] = {"fill", "agitate", "soak", "perfume", "drain", "spin", "wait",
                                                  "wash", "soakwash", "rinse", "rinseperfume"};
const char* customTypeName(uint8_t t) { return t < CI_COUNT ? customNames[t] : "?"; }
int customTypeFromName(const String& name) {
  for (int i = 0; i < CI_COUNT; i++)
    if (name.equalsIgnoreCase(customNames[i])) return i;
  return -1;
}

static uint8_t itemSteps(uint8_t type) {
  switch (type) {
    case CI_WASH: case CI_RINSE: return 3;
    case CI_SOAKWASH: case CI_RINSEPERFUME: return 4;
    default: return 1;
  }
}

uint8_t customStepCount(const CustomProc& p) {
  int n = 0;
  for (int i = 0; i < p.count && i < CUSTOM_MAX_ITEMS; i++) n += itemSteps(p.items[i].type);
  return n > 255 ? 255 : n;
}

bool customValidate(const CustomProc& p, String& err) {
  if (p.name[0] == 0) { err = "Nama proses wajib diisi."; return false; }
  if (p.count == 0) { err = "Tambahkan minimal satu langkah."; return false; }
  if (p.count > CUSTOM_MAX_ITEMS) { err = String("Maksimal ") + CUSTOM_MAX_ITEMS + " langkah."; return false; }
  for (int i = 0; i < p.count; i++) {
    const CustomItem& it = p.items[i];
    String at = String("Langkah ") + (i + 1) + ": ";
    if (it.type >= CI_COUNT) { err = at + "jenis tidak dikenal."; return false; }
    if (it.pola > 3) { err = at + "pola tidak valid."; return false; }
    bool needA = it.type != CI_FILL && it.type != CI_PERFUME && it.type != CI_DRAIN;
    if (needA && (it.a < 1 || it.a > 1440)) { err = at + "nilai harus 1-1440."; return false; }
    if (it.type == CI_SOAKWASH && (it.b < 1 || it.b > 1440)) { err = at + "menit cuci harus 1-1440."; return false; }
    bool cups = it.type == CI_PERFUME || it.type == CI_RINSEPERFUME;
    if (cups && it.cups2 > 40) { err = at + "pewangi maksimal 20 cup."; return false; }
  }
  if (customStepCount(p) > MAX_STEPS - 1) { err = "Terlalu panjang: langkah mesin melebihi batas."; return false; }
  return true;
}

static uint8_t buildCustom(const CustomProc& p, const Settings& s, Step* out, uint8_t& groups) {
  Builder b(out);
  for (int i = 0; i < p.count; i++) {
    const CustomItem& it = p.items[i];
    uint8_t g = (uint8_t)i;
    uint8_t pola = it.pola > 2 ? s.pola : it.pola;
    uint8_t cups = it.cups2 ? it.cups2 : PERFUME_FROM_SETTINGS;
    switch (it.type) {
      case CI_FILL: b.add(ST_FILL, 0, 0, g, KIND_NONE); break;
      case CI_AGITATE: b.add(ST_AGITATE, it.a, pola, g, KIND_NONE); break;
      case CI_SOAK: b.add(ST_SOAK, it.a, pola, g, KIND_NONE); break;
      case CI_PERFUME: b.add(ST_PERFUME, 0, cups, g, KIND_NONE); break;
      case CI_DRAIN: b.add(ST_DRAIN, 0, 0, g, KIND_NONE); break;
      case CI_SPIN: b.add(ST_SPIN, it.a, 0, g, KIND_NONE); break;
      case CI_WAIT: b.add(ST_WAIT, (uint32_t)it.a * 60UL, 0, g, KIND_NONE); break;
      case CI_WASH:
        b.add(ST_FILL, 0, 0, g, KIND_NONE);
        b.add(ST_AGITATE, it.a, pola, g, KIND_NONE);
        b.add(ST_DRAIN, 0, 0, g, KIND_NONE);
        break;
      case CI_SOAKWASH:
        b.add(ST_FILL, 0, 0, g, KIND_NONE);
        b.add(ST_SOAK, it.a, pola, g, KIND_NONE);
        b.add(ST_AGITATE, it.b, pola, g, KIND_NONE);
        b.add(ST_DRAIN, 0, 0, g, KIND_NONE);
        break;
      case CI_RINSE:
        b.add(ST_FILL, 0, 0, g, KIND_NONE);
        b.add(ST_AGITATE, it.a, pola, g, KIND_NONE);
        b.add(ST_DRAIN, 0, 0, g, KIND_NONE);
        break;
      case CI_RINSEPERFUME:
        b.add(ST_FILL, 0, 0, g, KIND_NONE);
        b.add(ST_PERFUME, 0, cups, g, KIND_NONE);
        b.add(ST_AGITATE, it.a, pola, g, KIND_NONE);
        b.add(ST_DRAIN, 0, 0, g, KIND_NONE);
        break;
    }
  }
  groups = p.count ? p.count : 1;
  return b.n;
}

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
      b.add(ST_PERFUME, 0, PERFUME_FROM_SETTINGS, g, KIND_BILAS_PEWANGI);
      b.add(ST_AGITATE, rinse, pola, g, KIND_BILAS_PEWANGI);
      b.add(ST_DRAIN, 0, 0, g, KIND_BILAS_PEWANGI);
      g++;
    }
    groups = g ? g : 1;
    return b.n;
  }

  if (id == PRG_CUSTOM) {
    CustomProc cp;
    if (!customLoad(arg, cp)) return 0;
    return buildCustom(cp, s, out, groups);
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
        b.add(ST_PERFUME, 0, PERFUME_FROM_SETTINGS, 0, KIND_NONE);
        b.add(ST_AGITATE, rinse, pola, 0, KIND_NONE);
        b.add(ST_DRAIN, 0, 0, 0, KIND_NONE);
      }
      break;
    case PRG_SPIN:
      if (s.spin > 0) b.add(ST_SPIN, s.spin, 0, 0, KIND_NONE);
      break;
    case PRG_WATERIN: b.add(ST_FILL, 0, 0, 0, KIND_NONE); break;
    case PRG_WATEROUT: b.add(ST_DRAIN, 0, 1, 0, KIND_NONE); break;
    case PRG_PARFUME: b.add(ST_PERFUME, 0, PERFUME_FROM_SETTINGS, 0, KIND_NONE); break;
    case PRG_MOTOR:
      b.add(ST_CHECK, 0, 0, 0, KIND_NONE);
      b.add(ST_AGITATE, 5, pola, 0, KIND_NONE);
      break;
    case PRG_TEST: b.add(ST_TEST, arg, 0, 0, KIND_NONE); break;
    case PRG_RELAYSEQ: {
      uint8_t secs = arg ? arg : 3;
      for (uint8_t r = 0; r < R_COUNT; r++) b.add(ST_TEST, r, secs, 0, KIND_NONE);
      break;
    }
    default: break;
  }
  return b.n;
}
