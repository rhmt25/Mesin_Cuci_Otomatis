#include "relay.h"
#include "config.h"
#include "logger.h"

static const uint8_t pins[R_COUNT] = {PIN_MOTOR_LEFT, PIN_MOTOR_RIGHT, PIN_WATER_IN,
                                      PIN_WATER_OUT, PIN_SPIN_MOTOR, PIN_PERFUME};
static const char* names[R_COUNT] = {"kiri", "kanan", "isi", "buang", "spin", "pewangi"};
static bool state[R_COUNT];

static uint32_t lastMotorOffMs = 0;
static int8_t lastMotorDir = -1;     // 0 kiri, 1 kanan
static uint32_t motorOnSince = 0;
static uint64_t motorMsTotal = 0;
static portMUX_TYPE relayMux = portMUX_INITIALIZER_UNLOCKED;
static bool faulted = false;

static inline int levelFor(bool on) {
#if RELAY_ACTIVE_LOW
  return on ? LOW : HIGH;
#else
  return on ? HIGH : LOW;
#endif
}

static void writePin(uint8_t pin, bool on) { digitalWrite(pin, levelFor(on)); }

void relayInit() {
  for (int i = 0; i < R_COUNT; i++) {
    writePin(pins[i], false);                 // set "mati" DULU
#if RELAY_OPEN_DRAIN
    pinMode(pins[i], OUTPUT_OPEN_DRAIN);      // HIGH = mengambang
#else
    pinMode(pins[i], OUTPUT);
#endif
    writePin(pins[i], false);
    state[i] = false;
  }
  pinMode(PIN_SENSOR_SRC, OUTPUT);
  digitalWrite(PIN_SENSOR_SRC, HIGH);
}

static uint32_t reverseGapMs() {
  uint32_t g = MOTOR_REVERSE_GAP_MS / TIME_SCALE;
  return g < 8 ? 8 : g;
}

static void updateMotorClock(bool before, bool after) {
  uint32_t now = millis();
  if (!before && after) motorOnSince = now;
  if (before && !after) {
    motorMsTotal += now - motorOnSince;
    lastMotorOffMs = now;
  }
}

// Alasan penolakan (nullptr = diizinkan). Panggil di dalam critical section.
static const char* denyReasonLocked(RelayId id, bool on) {
  if (!on) return nullptr;
  if (faulted) return "relay terkunci karena pelanggaran interlock, restart mesin";
  if (state[id]) return nullptr;
  switch (id) {
    case R_LEFT:
    case R_RIGHT: {
      RelayId other = id == R_LEFT ? R_RIGHT : R_LEFT;
      if (state[other]) return "motor kiri & kanan tidak boleh nyala bersamaan";
      if (state[R_DRAIN]) return "motor cuci tidak boleh nyala saat valve buang aktif";
      if (lastMotorDir >= 0 && lastMotorDir != (id == R_LEFT ? 0 : 1) && millis() - lastMotorOffMs < reverseGapMs())
        return "jeda ganti arah motor belum cukup, coba lagi sebentar";
      return nullptr;
    }
    case R_FILL:
      return state[R_DRAIN] ? "valve isi & buang tidak boleh aktif bersamaan" : nullptr;
    case R_DRAIN:
      if (state[R_FILL]) return "valve isi & buang tidak boleh aktif bersamaan";
      if (state[R_LEFT] || state[R_RIGHT]) return "valve buang ditolak saat motor cuci nyala";
      return nullptr;
    default: return nullptr;
  }
}

const char* relayCheck(RelayId id, bool on) {
  if (id >= R_COUNT) return "relay tidak dikenal";
  portENTER_CRITICAL(&relayMux);
  const char* r = denyReasonLocked(id, on);
  portEXIT_CRITICAL(&relayMux);
  return r;
}

bool relaySet(RelayId id, bool on) {
  if (id >= R_COUNT) return false;
  portENTER_CRITICAL(&relayMux);
  bool motorsBefore = state[R_LEFT] || state[R_RIGHT];
  const char* deny = denyReasonLocked(id, on);
  if (!deny) {
    if (on && (id == R_LEFT || id == R_RIGHT)) {
      // Sabuk & pengaman: paksa pin motor lawan mati secara fisik sebelum menyalakan
      writePin(pins[id == R_LEFT ? R_RIGHT : R_LEFT], false);
    }
    state[id] = on;
    writePin(pins[id], on);
    if (on && (id == R_LEFT || id == R_RIGHT)) lastMotorDir = id == R_LEFT ? 0 : 1;
    updateMotorClock(motorsBefore, state[R_LEFT] || state[R_RIGHT]);
  }
  portEXIT_CRITICAL(&relayMux);
  if (deny) {
    logf(LOG_ERROR, "Interlock menolak relay %s: %s", names[id], deny);
    return false;
  }
  return true;
}

void relayAllOff() {
  portENTER_CRITICAL(&relayMux);
  bool motorsBefore = state[R_LEFT] || state[R_RIGHT];
  for (int i = 0; i < R_COUNT; i++) {
    state[i] = false;
    writePin(pins[i], false);
  }
  updateMotorClock(motorsBefore, false);
  portEXIT_CRITICAL(&relayMux);
}

bool relayIsOn(RelayId id) { return id < R_COUNT && state[id]; }
const char* relayName(RelayId id) { return id < R_COUNT ? names[id] : "?"; }

int relayFromName(const String& name) {
  for (int i = 0; i < R_COUNT; i++)
    if (name.equalsIgnoreCase(names[i])) return i;
  return -1;
}

uint32_t relayMotorSeconds() {
  uint64_t ms = motorMsTotal;
  if (state[R_LEFT] || state[R_RIGHT]) ms += millis() - motorOnSince;
  return (uint32_t)(ms / 1000);
}

bool relayFaulted() { return faulted; }

#ifdef HOST_TEST
// Hanya untuk uji: memaksa status internal agar failsafe bisa dites.
void relayHostForce(RelayId id, bool on) { state[id] = on; }
void relayHostUnfault() { faulted = false; }
#endif

uint8_t relayGuard() {
  uint8_t result = 0;
  const char* why = nullptr;
  portENTER_CRITICAL(&relayMux);
  bool bad = (state[R_LEFT] && state[R_RIGHT]) || (state[R_FILL] && state[R_DRAIN]) ||
             (state[R_DRAIN] && (state[R_LEFT] || state[R_RIGHT]));
  if (bad) {
    for (int i = 0; i < R_COUNT; i++) {
      state[i] = false;
      writePin(pins[i], false);
    }
    updateMotorClock(true, false);
    if (!faulted) {
      faulted = true;
      result = 2;
      why = "PELANGGARAN INTERLOCK: semua relay dimatikan dan dikunci";
    }
  } else if ((state[R_LEFT] || state[R_RIGHT]) && millis() - motorOnSince > MOTOR_MAX_ON_MS) {
    bool motorsBefore = true;
    state[R_LEFT] = state[R_RIGHT] = false;
    writePin(pins[R_LEFT], false);
    writePin(pins[R_RIGHT], false);
    updateMotorClock(motorsBefore, false);
    result = 1;
    why = "Motor cuci menyala terlalu lama, dimatikan paksa";
  }
  portEXIT_CRITICAL(&relayMux);
  if (why) logf(LOG_ERROR, "%s", why);
  return result;
}
