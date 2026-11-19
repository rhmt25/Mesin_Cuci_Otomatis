#include "machine.h"
#include <esp_task_wdt.h>
#include <time.h>
#include "config.h"
#include "logger.h"
#include "settings.h"
#include "water_sensor.h"

// ============================================================================
// State machine mesin cuci. Berjalan di Core 1, tick tiap TICK_MS, TANPA delay().
// Tidak pernah memanggil fungsi jaringan: hanya menulis log & snapshot status.
// ============================================================================

enum SR : uint8_t { SR_CONTINUE, SR_DONE, SR_PAUSE, SR_FAIL };

static QueueHandle_t cmdQueue = nullptr;
static SemaphoreHandle_t statusMutex = nullptr;
static MachineStatus snap;
static volatile bool holdFlag = false;

// ---- program yang sedang berjalan -------------------------------------------
static Settings cfg;                 // salinan pengaturan saat program dimulai
static Step prog[MAX_STEPS];
static uint8_t progN = 0, progGroups = 1, waitOffset = 0, idx = 0;
static uint8_t progId = PRG_KUSTOM;
static MState mstate = MS_IDLE;
static uint32_t progStartMs = 0, progStartEpoch = 0, progMotorStart = 0, progTotalEst = 0;
static char pauseReason[72];
static char failMsg[72];
static char detail[56];
static bool resumePending = false;
static ResumeInfo resumeInfo;
static char resumeText[56];
static uint32_t manualSince[R_COUNT];   // kapan relay manual dinyalakan (0 = tidak)

// ---- konteks langkah ----------------------------------------------------------
struct Run {
  uint8_t phase, sub, attempt;
  uint16_t cnt;
  uint32_t left, elapsed, phaseLeft, onMs, offMs;
};
static Run R;

enum { D_PULL, D_CHECK, D_UNJAM_WAIT, D_UNJAM_PULL, D_FAILED };
struct DrainCtx {
  uint8_t phase, session, unjam;
  bool ensure, soft, extraOn;
  uint32_t elapsed, left, extraLeft, emptyAt;
};
static DrainCtx D;

enum { F_SETTLE, F_PRECHECK, F_DRAINING, F_FILLING, F_RETRYWAIT };

enum { AG_LEFT_ON, AG_LEFT_OFF, AG_RIGHT_ON, AG_RIGHT_OFF, AG_GAP };

static inline uint32_t stableMs() { return scaledMs(WATER_STABLE_MS); }

static void setDetail(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void setDetail(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(detail, sizeof(detail), fmt, ap);
  va_end(ap);
}

static SR failStep(const char* msg) {
  strlcpy(failMsg, msg, sizeof(failMsg));
  return SR_FAIL;
}

// ============================================================================
// Teks
// ============================================================================
static void stageLabelFor(uint8_t id, const Step& st, uint8_t groups, char* out, size_t n) {
  static const char* kindNames[] = {"", "Cuci", "Bilas", "Bilas+Pewangi", "Tunggu"};
  if (st.type == ST_WAIT) {
    strlcpy(out, "Menunggu mulai", n);
  } else if (programIsFull(id) && st.kind > 0 && st.kind <= 3) {
    snprintf(out, n, "%s %u/%u", kindNames[st.kind], st.grp + 1, groups);
  } else {
    strlcpy(out, programName(id), n);
  }
}

static void stepTitle(const Step& st, char* out, size_t n) {
  switch (st.type) {
    case ST_AGITATE: snprintf(out, n, "Agitasi %lu mnt", (unsigned long)st.a); break;
    case ST_SOAK: snprintf(out, n, "Soak %lu sesi", (unsigned long)st.a); break;
    case ST_SPIN: snprintf(out, n, "Spin %lu mnt", (unsigned long)st.a); break;
    case ST_WAIT: snprintf(out, n, "Menunggu %lu dtk", (unsigned long)st.a); break;
    default: strlcpy(out, stepTypeName(st.type), n); break;
  }
}

// ============================================================================
// Estimasi waktu (dalam ms yang sudah diskalakan)
// ============================================================================
static uint32_t estFillMs() {
  if (C.nFill > 0) return (uint32_t)(C.avgFillMs / TIME_SCALE);
  return minToMs(10);
}
static uint32_t estDrainMs() {
  if (C.nDrain > 0) return (uint32_t)(C.avgDrainMs / TIME_SCALE) + secToMs(cfg.drainExtraSec);
  return minToMs(cfg.drainMode == DRAIN_TIME ? cfg.drainTime : cfg.drainMaxMin);
}
static uint32_t estimateStepMs(const Step& st) {
  switch (st.type) {
    case ST_WAIT: return secToMs(st.a);
    case ST_FILL: return estFillMs() + scaledMs(FILL_SETTLE_MS);
    case ST_AGITATE: return minToMs(st.a);
    case ST_SOAK: return minToMs(st.a * 5);
    case ST_PERFUME: return cfg.parfume > 0 ? scaledMs((uint32_t)(cfg.parfume * 5000) + 3000) : 0;
    case ST_DRAIN: return estDrainMs() + stableMs();
    case ST_SPIN: return minToMs(st.a);
    case ST_CHECK: return stableMs();
    case ST_TEST: return scaledMs(MOTOR_REVERSE_GAP_MS) + secToMs(st.flag ? st.flag : 2);
  }
  return 0;
}

// ============================================================================
// Agitasi (kiri/kanan sesuai pola)
// ============================================================================
static void patternMs(uint8_t pola) {
  uint32_t on, off;
  switch (pola) {
    case POLA_GENTLE: on = 5300; off = 7000; break;
    case POLA_HIJAB: on = 3000; off = 9000; break;
    default: on = 7000; off = 4000; break;
  }
  R.onMs = scaledMs(on);
  R.offMs = scaledMs(off);
}

// Mulai (atau lanjutkan) agitasi dengan sisa waktu R.left. Menunggu jeda ganti arah dulu.
static void agitateStart(uint8_t pola) {
  patternMs(pola);
  R.phase = AG_GAP;
  R.phaseLeft = scaledMs(MOTOR_REVERSE_GAP_MS) + TICK_MS * 2;
  R.cnt = 0;
  setDetail("Agitasi (menyiapkan)");
}

static SR agitateTick(uint32_t dt) {
  if (R.left <= dt) {
    relaySet(R_LEFT, false);
    relaySet(R_RIGHT, false);
    R.left = 0;
    logf(LOG_INFO, "Motor OFF");
    return SR_DONE;
  }
  R.left -= dt;
  if (R.phaseLeft > dt) {
    R.phaseLeft -= dt;
    return SR_CONTINUE;
  }
  switch (R.phase) {
    case AG_GAP:
      if (!relaySet(R_LEFT, true)) return failStep("Motor kiri ditolak interlock");
      logf(LOG_INFO, "Motor ON");
      R.phase = AG_LEFT_ON;
      R.phaseLeft = R.onMs;
      setDetail("Agitasi (kiri)");
      break;
    case AG_LEFT_ON:
      relaySet(R_LEFT, false);
      R.phase = AG_LEFT_OFF;
      R.phaseLeft = R.offMs;
      setDetail("Agitasi (jeda)");
      break;
    case AG_LEFT_OFF:
      if (!relaySet(R_RIGHT, true)) return failStep("Motor kanan ditolak interlock");
      R.phase = AG_RIGHT_ON;
      R.phaseLeft = R.onMs;
      setDetail("Agitasi (kanan)");
      break;
    case AG_RIGHT_ON:
      relaySet(R_RIGHT, false);
      R.phase = AG_RIGHT_OFF;
      R.phaseLeft = R.offMs;
      setDetail("Agitasi (jeda)");
      break;
    case AG_RIGHT_OFF:
      R.cnt++;
      logf(LOG_INFO, "Putaran ke %u", (unsigned)R.cnt);
      if (!relaySet(R_LEFT, true)) return failStep("Motor kiri ditolak interlock");
      R.phase = AG_LEFT_ON;
      R.phaseLeft = R.onMs;
      setDetail("Agitasi (kiri)");
      break;
  }
  return SR_CONTINUE;
}

// ============================================================================
// Pembuangan air (valve ditarik motor). Dipakai langkah DRAIN dan pra-cek FILL.
// ============================================================================
static bool drainPullStart() {
  D.session++;
  if (!relaySet(R_DRAIN, true)) {
    D.phase = D_FAILED;
    return false;
  }
  D.phase = D_PULL;
  D.elapsed = 0;
  D.extraOn = false;
  D.emptyAt = 0;
  int limit = D.soft ? 1 : cfg.drainSessionLimit;
  logf(LOG_INFO, "Membuang air... sesi %d/%d", D.session, limit);
  setDetail("Membuang air (sesi %d/%d)", D.session, limit);
  return true;
}

static void drainBegin(bool ensure, bool soft) {
  memset(&D, 0, sizeof(D));
  D.ensure = ensure;
  D.soft = soft;
  drainPullStart();
}

static SR drainFail() {
  relaySet(R_DRAIN, false);
  if (D.soft) {
    logf(LOG_WARN, "Air masih tersisa setelah pembuangan manual");
    return SR_DONE;
  }
  strlcpy(pauseReason, "Pembuangan gagal, cek valve buang", sizeof(pauseReason));
  return SR_PAUSE;
}

static void drainRelease() {
  relaySet(R_DRAIN, false);
  D.phase = D_CHECK;
  D.left = stableMs() + secToMs(2);
  setDetail("Memeriksa sisa air");
}

static SR drainTick(uint32_t dt) {
  const int sessionLimit = D.soft ? 1 : cfg.drainSessionLimit;
  switch (D.phase) {
    case D_FAILED:
      return failStep("Valve buang ditolak interlock");

    case D_PULL: {
      D.elapsed += dt;
      bool release = false;
      if (cfg.drainMode == DRAIN_SENSOR) {
        if (!D.extraOn && sensorStable(false, stableMs())) {
          D.extraOn = true;
          D.extraLeft = secToMs(cfg.drainExtraSec);
          D.emptyAt = D.elapsed;
          logf(LOG_INFO, "Air habis, valve ditahan %d dtk lagi untuk sisa air", cfg.drainExtraSec);
        }
        if (D.extraOn) {
          if (D.extraLeft <= dt) release = true;
          else D.extraLeft -= dt;
        }
        if (!release && D.elapsed >= minToMs(cfg.drainMaxMin)) {
          release = true;
          logf(LOG_WARN, "Batas waktu buang %d menit tercapai", cfg.drainMaxMin);
        }
      } else if (D.elapsed >= minToMs(cfg.drainTime)) {
        release = true;
      }
      if (release) {
        if (cfg.drainMode == DRAIN_TIME && !D.ensure) {
          relaySet(R_DRAIN, false);
          logf(LOG_INFO, "Pembuangan air selesai berdasarkan waktu");
          return SR_DONE;
        }
        drainRelease();
        logf(LOG_INFO, "Mengecek apakah air sudah benar-benar habis...");
      }
      return SR_CONTINUE;
    }

    case D_CHECK:
      if (sensorStable(false, stableMs())) {
        logf(LOG_INFO, "Air sudah benar-benar habis");
        if (cfg.drainMode == DRAIN_SENSOR && D.session == 1 && D.unjam == 0 && D.emptyAt > 0) {
          float real = (float)D.emptyAt * TIME_SCALE;
          if (C.nDrain >= 2 && real > C.avgDrainMs * ANOMALY_SLOW_FACTOR) {
            logf(LOG_WARN, "Pembuangan lebih lama dari biasanya (valve/pipa tersumbat?)");
          }
          C.avgDrainMs = C.nDrain == 0 ? real : C.avgDrainMs * 0.7f + real * 0.3f;
          if (C.nDrain < 65535) C.nDrain++;
          calibSave();
        }
        return SR_DONE;
      }
      if (D.left > dt) {
        D.left -= dt;
        return SR_CONTINUE;
      }
      logf(LOG_WARN, "Air masih tersisa setelah pembuangan");
      if (D.unjam < cfg.unjamCount) {
        D.unjam++;
        D.phase = D_UNJAM_WAIT;
        D.left = scaledMs(UNJAM_RELEASE_MS);
        logf(LOG_INFO, "Unjam %d/%d: valve dilepas lalu ditarik lagi", D.unjam, cfg.unjamCount);
        setDetail("Unjam valve %d/%d", D.unjam, cfg.unjamCount);
      } else if (D.session < sessionLimit) {
        logf(LOG_INFO, "Mengulangi sesi pembuangan air");
        D.unjam = 0;
        drainPullStart();
      } else {
        return drainFail();
      }
      return SR_CONTINUE;

    case D_UNJAM_WAIT:
      if (D.left > dt) {
        D.left -= dt;
        return SR_CONTINUE;
      }
      if (!relaySet(R_DRAIN, true)) return failStep("Valve buang ditolak interlock");
      D.phase = D_UNJAM_PULL;
      D.left = scaledMs(UNJAM_PULL_MS);
      return SR_CONTINUE;

    case D_UNJAM_PULL:
      if (D.left > dt && !sensorStable(false, stableMs())) {
        D.left -= dt;
        return SR_CONTINUE;
      }
      drainRelease();
      return SR_CONTINUE;
  }
  return SR_CONTINUE;
}

// ============================================================================
// Pengisian air
// ============================================================================
static uint8_t effectiveFillLevel() {
  if (cfg.fillLevel == FILL_PERCENT && C.nFill == 0) return FILL_SENSOR;
  return cfg.fillLevel;
}

static void fillBegin() {
  memset(&R, 0, sizeof(R));
  R.phase = F_SETTLE;
  R.left = scaledMs(FILL_SETTLE_MS);
  if (cfg.fillLevel == FILL_PERCENT && C.nFill == 0)
    logf(LOG_WARN, "Belum ada data kalibrasi untuk level persen, memakai sensor penuh");
  setDetail("Menyiapkan pengisian");
}

static SR fillStartValve() {
  if (!relaySet(R_FILL, true)) return failStep("Valve isi ditolak interlock");
  R.phase = F_FILLING;
  R.elapsed = 0;
  logf(LOG_INFO, "Mengisi air...");
  setDetail("Mengisi air");
  return SR_CONTINUE;
}

static void fillFinished() {
  relaySet(R_FILL, false);
  uint8_t lv = effectiveFillLevel();
  if (lv == FILL_SENSOR && R.attempt == 0) {
    float real = (float)R.elapsed * TIME_SCALE;
    bool suspicious = false;
    if (C.nFill >= 2) {
      if (real > C.avgFillMs * ANOMALY_SLOW_FACTOR) {
        logf(LOG_WARN, "Pengisian lebih lama dari biasanya (tekanan air rendah?)");
      } else if (real < C.avgFillMs * ANOMALY_FAST_FACTOR) {
        logf(LOG_WARN, "Sensor level mencurigakan: air penuh terlalu cepat");
        suspicious = true;
      }
    }
    if (!suspicious) {
      C.avgFillMs = C.nFill == 0 ? real : C.avgFillMs * 0.7f + real * 0.3f;
      if (C.nFill < 65535) C.nFill++;
      calibSave();
    }
  }
  logf(LOG_INFO, "Air sudah penuh!");
}

static SR fillTick(uint32_t dt) {
  switch (R.phase) {
    case F_SETTLE:
      if (R.left > dt) {
        R.left -= dt;
      } else {
        R.phase = F_PRECHECK;
        R.left = stableMs() + secToMs(2);
        setDetail("Memeriksa air kosong");
      }
      return SR_CONTINUE;

    case F_PRECHECK:
      if (sensorStable(false, stableMs())) return fillStartValve();
      if (R.left > dt) {
        R.left -= dt;
        return SR_CONTINUE;
      }
      logf(LOG_INFO, "Air masih terdeteksi sebelum mengisi, membuang dulu");
      drainBegin(true, false);
      R.phase = F_DRAINING;
      return SR_CONTINUE;

    case F_DRAINING: {
      SR r = drainTick(dt);
      if (r == SR_DONE) return fillStartValve();
      return r;
    }

    case F_FILLING: {
      R.elapsed += dt;
      uint8_t lv = effectiveFillLevel();
      bool full = sensorStable(true, stableMs());
      bool done = false;
      uint32_t limit = secToMs(fillLimitSeconds());
      if (lv == FILL_SENSOR) {
        done = full;
      } else {
        uint32_t target = lv == FILL_PERCENT ? (uint32_t)(C.avgFillMs * cfg.fillPercent / 100.0f / TIME_SCALE)
                                             : minToMs(cfg.fillTime);
        if (full) {
          logf(LOG_WARN, "Sensor penuh sebelum waktu habis, pengisian dihentikan");
          done = true;
        } else if (R.elapsed >= target) {
          done = true;
        }
      }
      if (done) {
        fillFinished();
        return SR_DONE;
      }
      bool timedOut = (lv != FILL_TIME) && R.elapsed >= limit;
      if (!timedOut) return SR_CONTINUE;

      relaySet(R_FILL, false);
      if (R.attempt < cfg.fillRetryCount) {
        R.attempt++;
        logf(LOG_WARN, "Air tidak penuh dalam batas waktu. Coba ulang %d/%d dalam %d menit", R.attempt,
             cfg.fillRetryCount, cfg.fillRetryWait);
        R.phase = F_RETRYWAIT;
        R.left = minToMs(cfg.fillRetryWait);
        setDetail("Menunggu air (coba ulang %d/%d)", R.attempt, cfg.fillRetryCount);
        return SR_CONTINUE;
      }
      snprintf(pauseReason, sizeof(pauseReason), "Air tidak terisi setelah %d percobaan", cfg.fillRetryCount + 1);
      return SR_PAUSE;
    }

    case F_RETRYWAIT:
      if (R.left > dt) {
        R.left -= dt;
        return SR_CONTINUE;
      }
      return fillStartValve();
  }
  return SR_CONTINUE;
}

// ============================================================================
// Siklus hidup langkah
// ============================================================================
static void publishStatus();

static void stepInit() {
  const Step& st = prog[idx];
  memset(&R, 0, sizeof(R));
  switch (st.type) {
    case ST_WAIT: R.left = secToMs(st.a); setDetail("Menunggu jam mulai"); break;
    case ST_FILL: fillBegin(); break;
    case ST_AGITATE:
      R.left = minToMs(st.a);
      agitateStart(st.flag);
      break;
    case ST_SOAK:
      R.sub = 0;
      R.left = minToMs(1);
      agitateStart(st.flag);
      break;
    case ST_PERFUME:
      if (cfg.parfume <= 0) {
        logf(LOG_INFO, "Pewangi tidak dimasukkan!");
        R.phase = 1;
      } else {
        if (relaySet(R_PERFUME, true)) logf(LOG_INFO, "Memasukkan pewangi...");
        R.left = scaledMs((uint32_t)(cfg.parfume * 5000.0f) + 3000);
      }
      setDetail("Memasukkan pewangi");
      break;
    case ST_DRAIN: drainBegin(true, st.flag == 1); break;
    case ST_SPIN:
      R.left = minToMs(st.a);
      relaySet(R_SPIN, true);
      setDetail("Spin");
      break;
    case ST_CHECK:
      R.left = stableMs() + secToMs(2);
      setDetail("Memeriksa air");
      break;
    case ST_TEST:
      R.phase = 0;   // jeda dulu (juga memberi jeda ganti arah motor), lalu menyala
      R.left = scaledMs(MOTOR_REVERSE_GAP_MS) + TICK_MS * 2;
      setDetail("Uji relay %s (siap)", relayName((RelayId)st.a));
      break;
  }
}

static void stepBegin() {
  const Step& st = prog[idx];
  char stage[28], title[32];
  stageLabelFor(progId, st, progGroups, stage, sizeof(stage));
  stepTitle(st, title, sizeof(title));
  logf(LOG_STEP, "%s: %s mulai", stage, title);
  if (idx >= waitOffset && !programIsTest(progId)) resumeSave(progId, idx - waitOffset);
  stepInit();
}

// Setelah dilanjutkan dari jeda: nyalakan lagi keluaran tanpa mengulang langkah.
static void stepEngage() {
  const Step& st = prog[idx];
  switch (st.type) {
    case ST_AGITATE: agitateStart(st.flag); break;
    case ST_SOAK:
      if (R.sub == 0) agitateStart(st.flag);
      break;
    case ST_PERFUME:
      if (R.phase == 0 && R.left > 0) relaySet(R_PERFUME, true);
      break;
    case ST_SPIN: relaySet(R_SPIN, true); break;
    case ST_TEST:
      R.phase = 0;
      R.left = scaledMs(MOTOR_REVERSE_GAP_MS) + TICK_MS * 2;
      break;
    case ST_FILL: fillBegin(); break;
    case ST_DRAIN: drainBegin(true, st.flag == 1); break;
    default: break;
  }
}

static void endProgram(CycleResult res, const char* why) {
  relayAllOff();
  uint32_t durSec = (millis() - progStartMs) / 1000;
  uint32_t motorDelta = relayMotorSeconds() - progMotorStart;
  if (!programIsTest(progId)) {
    statsRecord(progId, progStartEpoch, durSec * TIME_SCALE, res);
    if (motorDelta) statsAddMotorSec(motorDelta);
    resumeClear();
  }
  mstate = MS_IDLE;
  detail[0] = 0;
  pauseReason[0] = 0;
  if (res == RES_DONE) logf(LOG_DONE, "Proses %s selesai", programName(progId));
  else if (res == RES_STOPPED) logf(LOG_WARN, "Proses %s dihentikan%s%s", programName(progId), why ? ": " : "", why ? why : "");
  else logf(LOG_ERROR, "Proses %s dibatalkan: %s", programName(progId), why ? why : "kesalahan");
}

static void stepFinished() {
  idx++;
  if (idx >= progN) {
    endProgram(RES_DONE, nullptr);
    return;
  }
  stepBegin();
}

static void doPause(const char* reason, bool automatic) {
  relayAllOff();
  mstate = MS_PAUSED;
  strlcpy(pauseReason, reason, sizeof(pauseReason));
  if (automatic) logf(LOG_ERROR, "Proses dijeda: %s. Kirim /lanjutkan untuk mencoba lagi", reason);
  else logf(LOG_WARN, "Proses dijeda oleh pengguna");
}

static void doResume() {
  mstate = MS_RUNNING;
  pauseReason[0] = 0;
  char title[32];
  stepTitle(prog[idx], title, sizeof(title));
  logf(LOG_STEP, "Melanjutkan: %s", title);
  stepEngage();
}

// Hitung berapa detik harus menunggu sebelum mulai (delay + "selesai jam")
static uint32_t computeWaitSeconds(uint32_t estTotalMs) {
  uint32_t wait = (uint32_t)cfg.dH * 3600UL + (uint32_t)cfg.dM * 60UL + (uint32_t)cfg.dS;
  if (cfg.finishAt >= 0) {
    time_t t = time(nullptr);
    if (t < 1700000000) {
      logf(LOG_WARN, "Jam belum sinkron, 'selesai jam' diabaikan");
    } else {
      struct tm lt;
      localtime_r(&t, &lt);
      int nowSec = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
      int diff = cfg.finishAt * 60 - nowSec;
      if (diff <= 0) diff += 86400;
      int est = (int)((uint64_t)estTotalMs * TIME_SCALE / 1000);
      if (diff - est > 0) {
        wait = diff - est;
      } else {
        logf(LOG_WARN, "Tidak sempat selesai tepat waktu, proses dimulai sekarang");
      }
    }
    S.finishAt = -1;   // sekali pakai
    settingsSave();
  }
  return wait;
}

static void startProgram(uint8_t id, uint8_t arg, int resumeIdx) {
  cfg = S;
  uint8_t groups = 1;
  uint8_t n = buildProgram(id, cfg, arg, prog, groups);
  if (n == 0) {
    logf(LOG_WARN, "%s tidak dilakukan (waktu/sesi diatur 0)", programName(id));
    return;
  }
  if (resumeIdx >= n) {
    logf(LOG_ERROR, "Data pemulihan tidak cocok dengan program");
    resumeClear();
    return;
  }
  progId = id;
  progGroups = groups;
  progN = n;
  waitOffset = 0;
  progTotalEst = 0;
  for (uint8_t i = 0; i < n; i++) progTotalEst += estimateStepMs(prog[i]);

  if (resumeIdx < 0 && !programIsTest(id)) {
    uint32_t waitSec = computeWaitSeconds(progTotalEst);
    if (waitSec > 0 && progN < MAX_STEPS) {
      memmove(prog + 1, prog, progN * sizeof(Step));
      prog[0] = Step{ST_WAIT, 0, 0, KIND_TUNGGU, waitSec};
      progN++;
      waitOffset = 1;
      progTotalEst += secToMs(waitSec);
      logf(LOG_INFO, "Proses ditunda %lu menit %lu detik", (unsigned long)(waitSec / 60), (unsigned long)(waitSec % 60));
    }
  }
  if (!programIsTest(id)) resumePending = false;
  idx = resumeIdx >= 0 ? (uint8_t)resumeIdx : 0;
  mstate = MS_RUNNING;
  progStartMs = millis();
  time_t now = time(nullptr);
  progStartEpoch = now > 1700000000 ? (uint32_t)now : 0;
  progMotorStart = relayMotorSeconds();
  pauseReason[0] = 0;
  logf(LOG_STEP, "Program %s %s (%u langkah)", programName(id), resumeIdx >= 0 ? "dilanjutkan" : "dimulai", progN);
  stepBegin();
}

// ============================================================================
// Eksekusi tick langkah
// ============================================================================
static SR tickStep(uint32_t dt) {
  const Step& st = prog[idx];
  switch (st.type) {
    case ST_WAIT:
      if (R.left <= dt) return SR_DONE;
      R.left -= dt;
      return SR_CONTINUE;
    case ST_FILL: return fillTick(dt);
    case ST_AGITATE: return agitateTick(dt);
    case ST_SOAK: {
      if (R.sub == 0) {
        SR r = agitateTick(dt);
        if (r == SR_DONE) {
          R.sub = 1;
          R.left = minToMs(4);
          setDetail("Soak: rendam (sesi %u/%lu)", R.attempt + 1, (unsigned long)st.a);
        }
        return r == SR_DONE ? SR_CONTINUE : r;
      }
      if (R.left > dt) {
        R.left -= dt;
        return SR_CONTINUE;
      }
      R.sub = 0;
      R.attempt++;   // jumlah sesi selesai
      logf(LOG_INFO, "Soak ke %u selesai", R.attempt);
      if (R.attempt >= st.a) return SR_DONE;
      R.left = minToMs(1);
      agitateStart(st.flag);
      return SR_CONTINUE;
    }
    case ST_PERFUME:
      if (R.phase == 1) return SR_DONE;
      if (R.left <= dt) {
        relaySet(R_PERFUME, false);
        logf(LOG_INFO, "Pewangi selesai dimasukkan!");
        return SR_DONE;
      }
      R.left -= dt;
      return SR_CONTINUE;
    case ST_DRAIN: return drainTick(dt);
    case ST_SPIN:
      if (R.left <= dt) {
        relaySet(R_SPIN, false);
        return SR_DONE;
      }
      R.left -= dt;
      return SR_CONTINUE;
    case ST_CHECK:
      if (sensorStable(true, stableMs())) return SR_DONE;
      if (R.left <= dt) return failStep("Air belum mencukupi!");
      R.left -= dt;
      return SR_CONTINUE;
    case ST_TEST:
      if (R.left > dt) {
        R.left -= dt;
        return SR_CONTINUE;
      }
      if (R.phase == 0) {
        if (!relaySet((RelayId)st.a, true)) return failStep("Relay ditolak interlock saat uji");
        logf(LOG_INFO, "Uji relay %s: ON", relayName((RelayId)st.a));
        setDetail("Uji relay %s (menyala)", relayName((RelayId)st.a));
        R.phase = 1;
        R.left = secToMs(st.flag ? st.flag : 2);
        return SR_CONTINUE;
      }
      relaySet((RelayId)st.a, false);
      logf(LOG_INFO, "Uji relay %s: OFF", relayName((RelayId)st.a));
      return SR_DONE;
  }
  return SR_DONE;
}

// ============================================================================
// Perintah
// ============================================================================
static void processCmd(const MachineCmd& c) {
  switch (c.type) {
    case MC_RUN:
      if (mstate != MS_IDLE || holdFlag) {
        logf(LOG_WARN, "Perintah ditolak: mesin sedang %s", holdFlag ? "memperbarui firmware" : "bekerja");
        break;
      }
      relayAllOff();   // batalkan kontrol relay manual yang mungkin masih menyala
      startProgram(c.arg, c.arg2, -1);
      break;
    case MC_STOP:
      relayAllOff();
      if (mstate != MS_IDLE) {
        endProgram(RES_STOPPED, nullptr);
      } else if (resumePending) {
        resumePending = false;
        resumeClear();
        logf(LOG_INFO, "Pemulihan proses dibatalkan");
      }
      break;
    case MC_PAUSE:
      if (mstate == MS_RUNNING) doPause("", false);
      else logf(LOG_WARN, "Tidak ada proses yang berjalan");
      break;
    case MC_RESUME:
      if (mstate == MS_PAUSED) {
        doResume();
      } else if (mstate == MS_IDLE && resumePending && !holdFlag) {
        startProgram(resumeInfo.program, 0, resumeInfo.stepIndex);
      } else {
        logf(LOG_WARN, "Tidak ada proses yang sedang dijeda");
      }
      break;
    case MC_SKIP:
      if (mstate == MS_IDLE) {
        logf(LOG_WARN, "Tidak ada proses yang berjalan");
      } else {
        relayAllOff();
        logf(LOG_STEP, "Tahap dilewati");
        mstate = MS_RUNNING;
        pauseReason[0] = 0;
        stepFinished();
      }
      break;
    case MC_RELAY:
      if (c.arg >= R_COUNT) break;
      if (mstate != MS_IDLE || holdFlag) {
        logf(LOG_WARN, "Kontrol relay manual hanya saat mesin siap");
      } else if (c.arg2) {
        if (relaySet((RelayId)c.arg, true))
          logf(LOG_INFO, "Relay %s ON (manual, otomatis mati %d dtk)", relayName((RelayId)c.arg), MANUAL_MAX_ON_MS / 1000);
      } else {
        relaySet((RelayId)c.arg, false);
        logf(LOG_INFO, "Relay %s OFF (manual)", relayName((RelayId)c.arg));
      }
      break;
    case MC_CANCEL_RESUME:
      if (resumePending) {
        resumePending = false;
        resumeClear();
        logf(LOG_INFO, "Pemulihan proses dibatalkan");
      }
      break;
  }
}

// ============================================================================
// Snapshot status
// ============================================================================
static uint32_t currentStepLeftMs() {
  const Step& st = prog[idx];
  switch (st.type) {
    case ST_WAIT: case ST_AGITATE: case ST_SPIN: case ST_CHECK:
      return R.left;
    case ST_TEST:
      return R.left + (R.phase == 0 ? secToMs(st.flag ? st.flag : 2) : 0);
    case ST_PERFUME:
      return R.phase == 1 ? 0 : R.left;
    case ST_SOAK: {
      uint32_t doneSessions = R.attempt;
      uint32_t remainingAfter = st.a > doneSessions + 1 ? st.a - doneSessions - 1 : 0;
      return R.left + (R.sub == 0 ? minToMs(4) : 0) + remainingAfter * minToMs(5);
    }
    case ST_FILL: {
      uint32_t est = estimateStepMs(st);
      return est > R.elapsed ? est - R.elapsed : 0;
    }
    case ST_DRAIN: {
      uint32_t est = estimateStepMs(st);
      return est > D.elapsed ? est - D.elapsed : 0;
    }
  }
  return 0;
}

static void publishStatus() {
  MachineStatus s;
  memset(&s, 0, sizeof(s));
  s.state = mstate;
  s.uptimeSec = millis() / 1000;
  s.waterRaw = sensorRaw();
  s.waterState = sensorStable(true, stableMs()) ? 1 : sensorStable(false, stableMs()) ? 0 : 2;
  s.resumePending = resumePending;
  s.relayFault = relayFaulted();
  strlcpy(s.resumeInfo, resumeText, sizeof(s.resumeInfo));
  for (int i = 0; i < R_COUNT; i++) s.relays[i] = relayIsOn((RelayId)i);
  if (mstate != MS_IDLE) {
    s.program = progId;
    s.stepIndex = idx + 1;
    s.stepCount = progN;
    stageLabelFor(progId, prog[idx], progGroups, s.stage, sizeof(s.stage));
    strlcpy(s.detail, detail, sizeof(s.detail));
    strlcpy(s.pauseReason, pauseReason, sizeof(s.pauseReason));
    uint32_t cur = currentStepLeftMs();
    uint32_t total = cur;
    for (uint8_t i = idx + 1; i < progN; i++) total += estimateStepMs(prog[i]);
    s.stepLeftSec = cur / 1000;
    s.totalLeftSec = total / 1000;
    if (progTotalEst > 0) {
      uint32_t pct = 100 - (uint32_t)((uint64_t)min(total, progTotalEst) * 100 / progTotalEst);
      s.progressPct = pct > 99 ? 99 : pct;
    }
  }
  if (xSemaphoreTake(statusMutex, 0) == pdTRUE) {
    snap = s;
    xSemaphoreGive(statusMutex);
  }
}

// ============================================================================
// Task & API publik
// ============================================================================
// Satu iterasi loop mesin (dipakai task asli dan harness uji di host).
static void machineLoopOnce(uint32_t& lastTick) {
  sensorUpdate();

  MachineCmd c;
  while (xQueueReceive(cmdQueue, &c, 0) == pdTRUE) processCmd(c);

  uint32_t now = millis();
  uint32_t dt = now - lastTick;
  lastTick = now;

  // Failsafe relay (interlock dilanggar / motor menyala terlalu lama)
  uint8_t guard = relayGuard();
  if (guard && mstate != MS_IDLE) endProgram(RES_ERROR, guard == 2 ? "pelanggaran interlock relay" : "motor menyala terlalu lama");

  // Relay manual otomatis mati setelah MANUAL_MAX_ON_MS (hanya saat IDLE)
  for (int i = 0; i < R_COUNT; i++) {
    if (mstate != MS_IDLE || !relayIsOn((RelayId)i)) {
      manualSince[i] = 0;
    } else if (manualSince[i] == 0) {
      manualSince[i] = now ? now : 1;
    } else if (now - manualSince[i] >= MANUAL_MAX_ON_MS) {
      relaySet((RelayId)i, false);
      manualSince[i] = 0;
      logf(LOG_WARN, "Relay %s otomatis dimatikan (batas waktu manual)", relayName((RelayId)i));
    }
  }
  if (mstate == MS_RUNNING) {
    SR r = tickStep(dt);
    switch (r) {
      case SR_DONE: stepFinished(); break;
      case SR_PAUSE: doPause(pauseReason, true); break;
      case SR_FAIL: endProgram(RES_ERROR, failMsg); break;
      default: break;
    }
  }
  publishStatus();
}

#ifdef HOST_TEST
static uint32_t hostLastTick = 0;
void machineHostTick() { machineLoopOnce(hostLastTick); }
void machineHostReset() {
  mstate = MS_IDLE;
  resumePending = false;
  idx = progN = 0;
  memset(&R, 0, sizeof(R));
  memset(&D, 0, sizeof(D));
  detail[0] = pauseReason[0] = failMsg[0] = 0;
  hostLastTick = millis();
}
#endif

static void machineTask(void*) {
  esp_task_wdt_add(nullptr);
  TickType_t lastWake = xTaskGetTickCount();
  uint32_t lastTick = millis();
  for (;;) {
    esp_task_wdt_reset();
    machineLoopOnce(lastTick);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(TICK_MS));
  }
}

void machineBegin() {
  cmdQueue = xQueueCreate(8, sizeof(MachineCmd));
  statusMutex = xSemaphoreCreateMutex();
  memset(&snap, 0, sizeof(snap));
  cfg = S;

  // Pemulihan listrik padam: hanya menawarkan, tidak pernah lanjut otomatis.
  if (resumeLoad(resumeInfo)) {
    Step tmp[MAX_STEPS];
    uint8_t groups = 1;
    uint8_t n = buildProgram(resumeInfo.program, S, 0, tmp, groups);
    if (resumeInfo.program < PRG_COUNT && resumeInfo.stepIndex < n) {
      char stage[28];
      stageLabelFor(resumeInfo.program, tmp[resumeInfo.stepIndex], groups, stage, sizeof(stage));
      snprintf(resumeText, sizeof(resumeText), "%s", stage);
      resumePending = true;
      logf(LOG_WARN, "Listrik sempat padam saat %s", stage);
    } else {
      resumeClear();
    }
  }

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t wdt = {};
  wdt.timeout_ms = WATCHDOG_SEC * 1000;
  wdt.idle_core_mask = 0;
  wdt.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
#else
  esp_task_wdt_init(WATCHDOG_SEC, true);
#endif
  xTaskCreatePinnedToCore(machineTask, "machine", 6144, nullptr, 3, nullptr, 1);
}

bool machinePost(const MachineCmd& cmd) {
  if (!cmdQueue) return false;
  return xQueueSend(cmdQueue, &cmd, 0) == pdTRUE;
}

void machineGetStatus(MachineStatus& out) {
  if (xSemaphoreTake(statusMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    out = snap;
    xSemaphoreGive(statusMutex);
  } else {
    out = snap;
  }
}

bool machineIsIdle() {
  MachineStatus s;
  machineGetStatus(s);
  return s.state == MS_IDLE;
}

void machineHold(bool hold) { holdFlag = hold; }

String machineStatusLine() {
  MachineStatus s;
  machineGetStatus(s);
  static const char* waterTxt[] = {"kosong", "penuh", "mengukur"};
  String out;
  if (s.state == MS_IDLE) {
    out = "Siap";
  } else {
    out = String(s.stage) + " \xE2\x80\xA2 " + s.detail;
    if (s.state == MS_PAUSED) out = "\xE2\x8F\xB8 DIJEDA \xE2\x80\xA2 " + String(s.pauseReason[0] ? s.pauseReason : "oleh pengguna");
  }
  out += "\n\xF0\x9F\x92\xA7 Air: ";
  out += waterTxt[s.waterState];
  return out;
}
