#include "machine.h"
#include <esp_task_wdt.h>
#include <time.h>
#include "config.h"
#include "logger.h"
#include "proses_store.h"
#include "settings.h"
#include "water_sensor.h"

// ============================================================================
// State machine mesin cuci. Berjalan di Core 1, tick tiap TICK_MS, TANPA delay().
// Tidak pernah memanggil fungsi jaringan: hanya menulis log & snapshot status.
// ============================================================================

enum SR : uint8_t { SR_CONTINUE, SR_DONE, SR_DECIDE, SR_FAIL };

static QueueHandle_t cmdQueue = nullptr;
static SemaphoreHandle_t statusMutex = nullptr;
static SemaphoreHandle_t jobMutex = nullptr;   // melindungi job/idx/waitingStart dari pembacaan API (machineCopyJob)
struct JobLock {
  JobLock() { if (jobMutex) xSemaphoreTake(jobMutex, portMAX_DELAY); }
  ~JobLock() { if (jobMutex) xSemaphoreGive(jobMutex); }
};
static MachineStatus snap;
static volatile bool holdFlag = false;

// ---- Proses yang sedang berjalan -----------------------------------------------
static Proses job;                   // salinan Proses (atau satu Tahap untuk Manual/uji)
static uint8_t jobSlot = NO_SLOT;
static uint16_t jobRev = 0;
static bool jobManual = false, jobTest = false;
static uint8_t idx = 0;
static MState mstate = MS_IDLE;
static bool waitingStart = false;    // menunggu jam mulai (tunda / selesai pada jam)
static uint32_t waitLeft = 0;
static uint32_t progStartMs = 0, progStartEpoch = 0, progMotorStart = 0, progTotalEst = 0;
static char pauseReason[72];
static char failMsg[72];
static char detailTxt[56];
static bool resumePending = false;
static ResumeInfo resumeInfo;
static char resumeText[56];
static uint32_t manualSince[R_COUNT];   // kapan relay manual dinyalakan (0 = tidak)

// ---- keputusan menunggu ----------------------------------------------------------
struct Decision {
  uint8_t code;
  uint8_t opts;
  char msg[72];
  uint32_t remindMs;   // kapan terakhir mengingatkan
};
static Decision dec;
#define DECISION_REMIND_MIN 10

// ---- konteks Tahap ---------------------------------------------------------------
struct Run {
  uint8_t phase, sub, attempt;
  uint16_t cnt;
  uint32_t left, elapsed, phaseLeft, onMs, offMs;
};
static Run R;

struct DrainParam {
  uint16_t lama, probe;
  uint8_t tarik, sesi;
};
static DrainParam dp;

enum { D_PULL, D_CHECK, D_UNJAM_WAIT, D_FAILED };
struct DrainCtx {
  uint8_t phase, session, unjam;
  bool probeDone;         // tarikan terbukti berhasil (sensor sudah tidak penuh)
  uint32_t pullElapsed;   // lama valve ditarik pada percobaan ini
  uint32_t left;
};
static DrainCtx D;

enum { F_SETTLE, F_PRECHECK, F_DRAINING, F_FILLING, F_RETRYWAIT };
enum { AG_LEFT_ON, AG_LEFT_OFF, AG_RIGHT_ON, AG_RIGHT_OFF, AG_GAP };

static inline uint32_t stableMs() { return scaledMs(WATER_STABLE_MS); }

static void setDetail(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void setDetail(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(detailTxt, sizeof(detailTxt), fmt, ap);
  va_end(ap);
}

static SR failStep(const char* msg) {
  strlcpy(failMsg, msg, sizeof(failMsg));
  return SR_FAIL;
}

// ============================================================================
// Keputusan menunggu
// ============================================================================
const char* decisionCodeName(uint8_t code) {
  static const char* n[] = {"", "isi_gagal", "pra_penuh", "buang_gagal", "spin_penuh", "cek_gagal", "tindakan"};
  return code < 7 ? n[code] : "";
}
const char* decisionOptName(uint8_t opt) {
  switch (opt) {
    case OP_ULANG: return "ulang";
    case OP_LANJUT: return "lanjut";
    case OP_BATAL: return "batal";
    case OP_BUANG: return "buang";
    case OP_PAKAI: return "pakai";
  }
  return "";
}
const char* decisionOptLabel(uint8_t opt) {
  switch (opt) {
    case OP_ULANG: return "Ulang";
    case OP_LANJUT: return "Lanjut";
    case OP_BATAL: return "Batal";
    case OP_BUANG: return "Buang lalu isi";
    case OP_PAKAI: return "Pakai air yang ada";
  }
  return "";
}
uint8_t decisionOptFromName(const String& name) {
  for (uint8_t b = 1; b <= OP_PAKAI; b <<= 1)
    if (name == decisionOptName(b)) return b;
  return 0;
}

static String optsText(uint8_t opts) {
  String t;
  for (uint8_t b = 1; b <= OP_PAKAI; b <<= 1) {
    if (!(opts & b)) continue;
    if (t.length()) t += " / ";
    t += decisionOptLabel(b);
  }
  return t;
}

static SR decide(uint8_t code, uint8_t opts, const char* msg) {
  dec.code = code;
  dec.opts = opts;
  strlcpy(dec.msg, msg, sizeof(dec.msg));
  return SR_DECIDE;
}

// ============================================================================
// Teks
// ============================================================================
static void stageLabel(char* out, size_t n) {
  if (waitingStart) {
    strlcpy(out, "Menunggu mulai", n);
    return;
  }
  const Tahap& t = job.t[idx];
  if (jobTest) {
    strlcpy(out, jobManual ? "Uji relay" : "Kalibrasi relay", n);
  } else if (jobManual) {
    snprintf(out, n, "Manual: %s", tahapLabel(t.type));
  } else {
    snprintf(out, n, "Tahap %u/%u: %s", (unsigned)idx + 1, (unsigned)job.count, tahapLabel(t.type));
  }
}

static String stepTitle(const Tahap& t) {
  if (t.type == TH_UJI_RELAY) return String("Uji relay ") + relayName((RelayId)t.mode);
  return tahapRingkas(t);
}
static String stepParams(const Tahap& t) {
  if (t.type == TH_UJI_RELAY) return String(relayName((RelayId)t.mode));
  return tahapParams(t);
}

// ============================================================================
// Estimasi waktu (ms yang sudah diskalakan)
// ============================================================================
static uint32_t estimateStepMs(const Tahap& t) {
  if (t.type == TH_UJI_RELAY) return scaledMs(MOTOR_REVERSE_GAP_MS) + secToMs(t.detik ? t.detik : 2);
  return secToMs(tahapEstimateSec(t));
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
// Pembuangan air (valve ditarik motor). Dipakai Tahap Buang dan pra-cek Isi.
// ============================================================================
// Sensor level hanya ada di paling atas: ia hanya tahu "penuh" atau "tidak penuh".
// Tarikan valve dibuktikan berhasil bila air turun dari posisi penuh dalam `probe` detik (Verifikasi Pengurangan
// Jumlah Air). Bila tidak, valve dilepas lalu ditarik lagi (Tarik Ulang Valve). Setelah `lama` menit, air tidak boleh
// masih penuh (cek akhir, selalu); bila masih penuh, sesi diulang sampai `sesi` kali.
static void drainParamFrom(const Tahap& t) {
  dp.lama = t.lama;
  dp.probe = t.probe;
  dp.tarik = t.tarik;
  dp.sesi = t.sesi;
}
static void drainParamDefault() {
  Tahap t;
  tahapDefault(TH_BUANG, t);
  drainParamFrom(t);
}

static bool drainPullStart() {
  if (!relaySet(R_DRAIN, true)) {
    D.phase = D_FAILED;
    return false;
  }
  D.phase = D_PULL;
  D.pullElapsed = 0;
  // Bila air sudah tidak penuh sejak awal (mis. isi sebagian), keberhasilan tarikan tidak bisa dibuktikan.
  D.probeDone = !sensorRaw();
  logf(LOG_INFO, "Membuang air... sesi %d/%d", D.session, (int)dp.sesi);
  setDetail("Membuang air (sesi %d/%d)", D.session, (int)dp.sesi);
  return true;
}

static void drainBegin() {
  memset(&D, 0, sizeof(D));
  D.session = 1;
  drainPullStart();
}

static SR drainFail() {
  relaySet(R_DRAIN, false);
  return decide(DC_BUANG_GAGAL, OP_ULANG | OP_LANJUT | OP_BATAL, "Pembuangan gagal, cek modul tarik valve buang");
}

static void drainRelease() {
  relaySet(R_DRAIN, false);
  D.phase = D_CHECK;
  D.left = stableMs() + secToMs(2);
  setDetail("Memeriksa air sudah turun");
  logf(LOG_INFO, "Mengecek apakah air sudah turun dari posisi penuh...");
}

// Sesi gagal (air masih penuh): ulangi sesi sampai batas, lalu keputusan.
static SR drainSessionFailed() {
  if (D.session < dp.sesi) {
    D.session++;
    D.unjam = 0;
    logf(LOG_INFO, "Mengulangi sesi pembuangan air");
    drainPullStart();
    return SR_CONTINUE;
  }
  return drainFail();
}

static SR drainTick(uint32_t dt) {
  switch (D.phase) {
    case D_FAILED:
      return failStep("Valve buang ditolak interlock");

    case D_PULL:
      D.pullElapsed += dt;
      if (!D.probeDone) {
        if (sensorStable(false, stableMs())) {
          D.probeDone = true;
          logf(LOG_INFO, "Tarikan valve berhasil: air mulai turun");
        } else if (D.pullElapsed >= secToMs(dp.probe)) {
          relaySet(R_DRAIN, false);
          logf(LOG_WARN, "Valve buang belum menarik (air masih penuh setelah %d dtk)", (int)dp.probe);
          if (D.unjam < dp.tarik) {
            D.unjam++;
            D.phase = D_UNJAM_WAIT;
            D.left = scaledMs(UNJAM_RELEASE_MS);
            logf(LOG_INFO, "Tarik ulang valve %d/%d: valve dilepas lalu ditarik lagi", D.unjam, (int)dp.tarik);
            setDetail("Tarik ulang valve %d/%d", D.unjam, (int)dp.tarik);
            return SR_CONTINUE;
          }
          return drainSessionFailed();
        }
      }
      if (D.probeDone && D.pullElapsed >= minToMs(dp.lama)) drainRelease();
      return SR_CONTINUE;

    case D_UNJAM_WAIT:
      if (D.left > dt) {
        D.left -= dt;
        return SR_CONTINUE;
      }
      if (!relaySet(R_DRAIN, true)) return failStep("Valve buang ditolak interlock");
      D.phase = D_PULL;
      D.pullElapsed = 0;
      D.probeDone = !sensorRaw();
      return SR_CONTINUE;

    case D_CHECK:
      if (sensorStable(false, stableMs())) {
        logf(LOG_INFO, "Air sudah turun, pembuangan selesai");
        return SR_DONE;
      }
      if (D.left > dt) {
        D.left -= dt;
        return SR_CONTINUE;
      }
      logf(LOG_WARN, "Air masih penuh setelah pembuangan");
      return drainSessionFailed();
  }
  return SR_CONTINUE;
}

// ============================================================================
// Pengisian air
// ============================================================================
static uint8_t effectiveFillMode(const Tahap& t) {
  if (t.mode == ISI_PERSEN && C.nFill == 0) return ISI_SENSOR;
  return t.mode;
}

static void fillBegin(const Tahap& t) {
  memset(&R, 0, sizeof(R));
  R.phase = F_SETTLE;
  R.left = scaledMs(FILL_SETTLE_MS);
  if (t.mode == ISI_PERSEN && C.nFill == 0)
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

static void fillFinished(const Tahap& t) {
  relaySet(R_FILL, false);
  if (effectiveFillMode(t) == ISI_SENSOR && R.attempt == 0) {
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

static void fillDrainFirst() {
  drainParamDefault();
  drainBegin();
  R.phase = F_DRAINING;
}

static SR fillTick(const Tahap& t, uint32_t dt) {
  switch (R.phase) {
    case F_SETTLE:
      if (R.left > dt) {
        R.left -= dt;
      } else {
        R.phase = F_PRECHECK;
        R.left = stableMs() + secToMs(2);
        setDetail("Memeriksa air belum penuh");
      }
      return SR_CONTINUE;

    case F_PRECHECK:
      if (sensorStable(false, stableMs())) return fillStartValve();
      // Sudah jelas penuh (atau tidak stabil terlalu lama): terapkan pilihan pada Tahap
      if ((sensorRaw() && sensorSteadyMs() >= secToMs(2)) || R.left <= dt) {
        switch (t.pra) {
          case PRA_JEDA:
            return decide(DC_PRA_PENUH, OP_BUANG | OP_PAKAI | OP_BATAL, "Air sudah penuh sebelum mengisi");
          case PRA_PAKAI:
            logf(LOG_INFO, "Air sudah penuh, pengisian dilewati");
            return SR_DONE;
          default:
            logf(LOG_INFO, "Air masih penuh sebelum mengisi, membuang dulu");
            fillDrainFirst();
            return SR_CONTINUE;
        }
      }
      R.left -= dt;
      return SR_CONTINUE;

    case F_DRAINING: {
      SR r = drainTick(dt);
      if (r == SR_DONE) return fillStartValve();
      return r;
    }

    case F_FILLING: {
      R.elapsed += dt;
      uint8_t lv = effectiveFillMode(t);
      bool full = sensorStable(true, stableMs());
      bool done = false;
      if (lv == ISI_SENSOR) {
        done = full;
      } else {
        uint32_t target = lv == ISI_PERSEN ? (uint32_t)(C.avgFillMs * t.persen / 100.0f / TIME_SCALE) : minToMs(t.lama);
        if (full) {
          logf(LOG_WARN, "Sensor penuh sebelum waktu habis, pengisian dihentikan");
          done = true;
        } else if (R.elapsed >= target) {
          done = true;
        }
      }
      if (done) {
        fillFinished(t);
        return SR_DONE;
      }
      bool timedOut = (lv != ISI_WAKTU) && R.elapsed >= secToMs(t.batas);
      if (!timedOut) return SR_CONTINUE;

      relaySet(R_FILL, false);
      if (R.attempt < t.coba) {
        R.attempt++;
        logf(LOG_WARN, "Air tidak penuh dalam batas waktu. Coba ulang %d/%d dalam %d menit", R.attempt, (int)t.coba,
             (int)t.tunggu);
        R.phase = F_RETRYWAIT;
        R.left = minToMs(t.tunggu);
        setDetail("Menunggu air (coba ulang %d/%d)", R.attempt, (int)t.coba);
        return SR_CONTINUE;
      }
      char msg[72];
      snprintf(msg, sizeof(msg), "Air tidak terisi setelah %d percobaan", (int)t.coba + 1);
      return decide(DC_ISI_GAGAL, OP_ULANG | OP_LANJUT | OP_BATAL, msg);
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
// Siklus hidup Tahap
// ============================================================================
static void publishStatus();

static void stepInit() {
  const Tahap& t = job.t[idx];
  memset(&R, 0, sizeof(R));
  switch (t.type) {
    case TH_ISI: fillBegin(t); break;
    case TH_BUANG:
      drainParamFrom(t);
      drainBegin();
      break;
    case TH_PUTAR:
      R.left = minToMs(t.lama);
      agitateStart(t.pola);
      break;
    case TH_SOAK:
      R.sub = 0;
      R.left = minToMs(t.putar);
      agitateStart(t.pola);
      break;
    case TH_RENDAM:
      R.left = minToMs(t.lama);
      setDetail("Merendam");
      break;
    case TH_SPIN:
      R.phase = 0;   // periksa dulu air sudah turun
      R.left = stableMs() + secToMs(2);
      setDetail("Memeriksa air sudah turun");
      break;
    case TH_PEWANGI:
      R.left = secToMs((uint32_t)t.cup2 * 10);   // 0,5 cup = 10 dtk
      if (relaySet(R_PERFUME, true)) logf(LOG_INFO, "Memasukkan pewangi...");
      setDetail("Memasukkan pewangi");
      break;
    case TH_TUNGGU:
      R.left = secToMs(t.detik);
      setDetail("Menunggu");
      break;
    case TH_CEK:
      R.left = stableMs() + secToMs(2);
      setDetail("Memeriksa air");
      break;
    case TH_TINDAKAN:
      setDetail("Menunggu tindakan");
      break;
    case TH_NOTIF:
      logf(LOG_NOTIF, "%s", t.pesan);
      setDetail("Mengirim notifikasi");
      break;
    case TH_BUANG_MANUAL:
      R.left = minToMs(t.lama);
      if (!relaySet(R_DRAIN, true)) R.phase = 9;   // gagal: dilaporkan di tick
      setDetail("Membuang air (waktu %u mnt)", (unsigned)t.lama);
      break;
    case TH_UJI_RELAY:
      R.phase = 0;   // jeda dulu (juga memberi jeda ganti arah motor), lalu menyala
      R.left = scaledMs(MOTOR_REVERSE_GAP_MS) + TICK_MS * 2;
      setDetail("Uji relay %s (siap)", relayName((RelayId)t.mode));
      break;
  }
}

static void stepBegin() {
  const Tahap& t = job.t[idx];
  char stage[40];
  stageLabel(stage, sizeof(stage));
  String params = stepParams(t);
  logf(LOG_STEP, "%s mulai%s%s", stage, params.length() ? ": " : "", params.c_str());
  if (!jobTest && !jobManual) resumeSave(jobSlot, idx, jobRev);
  stepInit();
}

// Setelah dilanjutkan dari jeda: nyalakan lagi keluaran tanpa mengulang Tahap.
static void stepEngage() {
  const Tahap& t = job.t[idx];
  switch (t.type) {
    case TH_PUTAR: agitateStart(t.pola); break;
    case TH_SOAK:
      if (R.sub == 0) agitateStart(t.pola);
      break;
    case TH_PEWANGI:
      if (R.left > 0) relaySet(R_PERFUME, true);
      break;
    case TH_SPIN:
      if (R.phase == 1) relaySet(R_SPIN, true);
      break;
    case TH_UJI_RELAY:
      R.phase = 0;
      R.left = scaledMs(MOTOR_REVERSE_GAP_MS) + TICK_MS * 2;
      break;
    case TH_ISI: fillBegin(t); break;
    case TH_BUANG:
      drainParamFrom(t);
      drainBegin();
      break;
    case TH_BUANG_MANUAL:
      if (R.left > 0) relaySet(R_DRAIN, true);
      break;
    default: break;
  }
}

static void endProgram(CycleResult res, const char* why) {
  relayAllOff();
  uint32_t durSec = (millis() - progStartMs) / 1000;
  uint32_t motorDelta = relayMotorSeconds() - progMotorStart;
  if (!jobTest) {
    statsRecord(job.name, progStartEpoch, durSec * TIME_SCALE, res);
    if (motorDelta) statsAddMotorSec(motorDelta);
    if (!jobManual) resumeClear();
  }
  mstate = MS_IDLE;
  waitingStart = false;
  dec.code = DC_NONE;
  detailTxt[0] = 0;
  pauseReason[0] = 0;
  if (res == RES_DONE) logf(LOG_DONE, "Proses %s selesai", job.name);
  else if (res == RES_STOPPED) logf(LOG_WARN, "Proses %s dihentikan%s%s", job.name, why ? ": " : "", why ? why : "");
  else logf(LOG_ERROR, "Proses %s dibatalkan: %s", job.name, why ? why : "kesalahan");
}

static void stepFinished() {
  { JobLock jl; idx++; }
  if (idx >= job.count) {
    endProgram(RES_DONE, nullptr);
    return;
  }
  stepBegin();
}

static void doPause() {
  relayAllOff();
  mstate = MS_PAUSED;
  pauseReason[0] = 0;
  logf(LOG_WARN, "Proses dijeda oleh pengguna");
}

static void doResume() {
  mstate = MS_RUNNING;
  pauseReason[0] = 0;
  logf(LOG_STEP, "Melanjutkan: %s", stepTitle(job.t[idx]).c_str());
  stepEngage();
}

static void enterDecide() {
  relayAllOff();
  mstate = MS_DECIDE;
  dec.remindMs = millis();
  strlcpy(pauseReason, dec.msg, sizeof(pauseReason));
  String opts = optsText(dec.opts);
  if (dec.code == DC_TINDAKAN) logf(LOG_NOTIF, "%s. Buka dashboard: %s", dec.msg, opts.c_str());
  else logf(LOG_ERROR, "Menunggu keputusan: %s. Buka dashboard: %s", dec.msg, opts.c_str());
}

static void applyDecision(uint8_t choice) {
  const Tahap& t = job.t[idx];
  uint8_t code = dec.code;
  char msg[72];
  strlcpy(msg, dec.msg, sizeof(msg));
  dec.code = DC_NONE;
  pauseReason[0] = 0;
  mstate = MS_RUNNING;
  switch (choice) {
    case OP_BATAL:
      logf(LOG_WARN, "Keputusan: Batal");
      endProgram(RES_STOPPED, "dibatalkan oleh pengguna");
      return;
    case OP_ULANG:
      logf(LOG_STEP, "Keputusan: Ulang Tahap");
      stepInit();
      return;
    case OP_BUANG:
      logf(LOG_INFO, "Keputusan: buang air dulu lalu isi");
      memset(&R, 0, sizeof(R));
      fillDrainFirst();
      return;
    case OP_PAKAI:
      logf(LOG_INFO, "Keputusan: pakai air yang ada, pengisian dilewati");
      stepFinished();
      return;
    case OP_LANJUT:
      logf(LOG_WARN, "Keputusan: Lanjut meski %s", msg);
      if (code == DC_BUANG_GAGAL && t.type == TH_ISI) {   // pembuangan pra-isi gagal: lanjut ke pengisian
        SR r = fillStartValve();
        if (r == SR_FAIL) endProgram(RES_ERROR, failMsg);
      } else if (code == DC_SPIN_PENUH) {                  // spin walau air masih penuh
        if (relaySet(R_SPIN, true)) {
          R.phase = 1;
          R.left = minToMs(t.lama);
          setDetail("Spin");
        } else {
          endProgram(RES_ERROR, "Relay spin ditolak interlock");
        }
      } else {
        stepFinished();
      }
      return;
  }
}

// Hitung berapa detik harus menunggu sebelum mulai (tunda + "selesai pada jam")
static uint32_t computeWaitSeconds(uint32_t estTotalMs, uint32_t waitSec, int finishAt) {
  uint32_t wait = waitSec;
  if (finishAt >= 0) {
    time_t t = time(nullptr);
    if (t < 1700000000) {
      logf(LOG_WARN, "Jam belum sinkron, 'selesai jam' diabaikan");
    } else {
      struct tm lt;
      localtime_r(&t, &lt);
      int nowSec = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
      int diff = finishAt * 60 - nowSec;
      if (diff <= 0) diff += 86400;
      int est = (int)((uint64_t)estTotalMs * TIME_SCALE / 1000);
      if (diff - est > 0) {
        wait = diff - est;
      } else {
        logf(LOG_WARN, "Tidak sempat selesai tepat waktu, Proses dimulai sekarang");
      }
    }
  }
  return wait;
}

static void commonStart(int resumeIdx, uint32_t waitSec) {
  progTotalEst = 0;
  for (uint8_t i = 0; i < job.count; i++) progTotalEst += estimateStepMs(job.t[i]);
  resumePending = false;
  idx = resumeIdx >= 0 ? (uint8_t)resumeIdx : 0;
  mstate = MS_RUNNING;
  dec.code = DC_NONE;
  progStartMs = millis();
  time_t now = time(nullptr);
  progStartEpoch = now > 1700000000 ? (uint32_t)now : 0;
  progMotorStart = relayMotorSeconds();
  pauseReason[0] = 0;
  waitingStart = false;
  if (waitSec > 0) {
    waitingStart = true;
    waitLeft = secToMs(waitSec);
    progTotalEst += waitLeft;
    setDetail("Menunggu jam mulai");
    logf(LOG_INFO, "Proses ditunda %lu menit %lu detik", (unsigned long)(waitSec / 60), (unsigned long)(waitSec % 60));
  }
}

static void startProses(uint8_t slot, uint32_t waitSec, int finishAt, int resumeIdx) {
  JobLock jl;
  if (slot >= PROSES_SLOTS || !prosesLoad(slot, job)) {
    logf(LOG_WARN, "Proses slot %u kosong atau rusak", (unsigned)slot + 1);
    memset(&job, 0, sizeof(job));
    return;
  }
  if (resumeIdx >= job.count || (resumeIdx >= 0 && job.rev != resumeInfo.rev)) {
    logf(LOG_ERROR, "Data pemulihan tidak cocok dengan Proses");
    resumeClear();
    resumePending = false;
    return;
  }
  jobSlot = slot;
  jobRev = job.rev;
  jobManual = jobTest = false;
  uint32_t est = 0;
  for (uint8_t i = 0; i < job.count; i++) est += estimateStepMs(job.t[i]);
  uint32_t wait = resumeIdx < 0 ? computeWaitSeconds(est, waitSec, finishAt) : 0;
  commonStart(resumeIdx, wait);
  logf(LOG_STEP, "Proses %s %s (%u Tahap)", job.name, resumeIdx >= 0 ? "dilanjutkan" : "dimulai", (unsigned)job.count);
  if (!waitingStart) stepBegin();
}

static void startSingle(const Tahap& t, const char* name, bool test) {
  JobLock jl;
  memset(&job, 0, sizeof(job));
  strlcpy(job.name, name, sizeof(job.name));
  job.t[0] = t;
  job.count = 1;
  jobSlot = NO_SLOT;
  jobRev = 0;
  jobManual = true;
  jobTest = test;
  if (resumePending) {   // memulai kerja baru menggugurkan tawaran pemulihan
    resumePending = false;
    resumeClear();
  }
  commonStart(-1, 0);
  logf(LOG_STEP, "Proses %s dimulai (1 Tahap)", job.name);
  stepBegin();
}

static void startRelaySeq(uint8_t secs) {
  JobLock jl;
  memset(&job, 0, sizeof(job));
  strlcpy(job.name, "Kalibrasi relay", sizeof(job.name));
  for (uint8_t r = 0; r < R_COUNT; r++) {
    Tahap& t = job.t[job.count++];
    memset(&t, 0, sizeof(t));
    t.type = TH_UJI_RELAY;
    t.mode = r;
    t.detik = secs ? secs : 3;
  }
  jobSlot = NO_SLOT;
  jobRev = 0;
  jobManual = false;
  jobTest = true;
  if (resumePending) {
    resumePending = false;
    resumeClear();
  }
  commonStart(-1, 0);
  logf(LOG_STEP, "Proses %s dimulai (%u Tahap)", job.name, (unsigned)job.count);
  stepBegin();
}

// ============================================================================
// Eksekusi tick Tahap
// ============================================================================
static SR tickStep(uint32_t dt) {
  const Tahap& t = job.t[idx];
  switch (t.type) {
    case TH_ISI: return fillTick(t, dt);
    case TH_BUANG: return drainTick(dt);
    case TH_PUTAR: return agitateTick(dt);
    case TH_SOAK: {
      if (R.sub == 0) {
        SR r = agitateTick(dt);
        if (r == SR_DONE) {
          R.sub = 1;
          R.left = minToMs(t.rendam);
          setDetail("Soak: rendam (sesi %u/%u)", (unsigned)R.attempt + 1, (unsigned)t.ulangi);
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
      if (R.attempt >= t.ulangi) return SR_DONE;
      R.left = minToMs(t.putar);
      agitateStart(t.pola);
      return SR_CONTINUE;
    }
    case TH_RENDAM:
    case TH_TUNGGU:
      if (R.left <= dt) return SR_DONE;
      R.left -= dt;
      return SR_CONTINUE;
    case TH_SPIN:
      if (R.phase == 0) {   // sensor harus menunjukkan air sudah turun sebelum spin
        if (sensorStable(false, stableMs())) {
          if (!relaySet(R_SPIN, true)) return failStep("Relay spin ditolak interlock");
          R.phase = 1;
          R.left = minToMs(t.lama);
          setDetail("Spin");
          return SR_CONTINUE;
        }
        if (R.left <= dt) return decide(DC_SPIN_PENUH, OP_ULANG | OP_LANJUT | OP_BATAL, "Air masih penuh sebelum spin");
        R.left -= dt;
        return SR_CONTINUE;
      }
      if (R.left <= dt) {
        relaySet(R_SPIN, false);
        return SR_DONE;
      }
      R.left -= dt;
      return SR_CONTINUE;
    case TH_PEWANGI:
      if (R.left <= dt) {
        relaySet(R_PERFUME, false);
        logf(LOG_INFO, "Pewangi selesai dimasukkan!");
        return SR_DONE;
      }
      R.left -= dt;
      return SR_CONTINUE;
    case TH_CEK: {
      bool want = t.mode == CEK_PENUH;
      if (sensorStable(want, stableMs())) return SR_DONE;
      if (R.left <= dt) {
        return decide(DC_CEK_GAGAL, OP_ULANG | OP_LANJUT | OP_BATAL,
                      want ? "Air belum mencukupi (belum penuh)" : "Air masih penuh");
      }
      R.left -= dt;
      return SR_CONTINUE;
    }
    case TH_TINDAKAN:
      return decide(DC_TINDAKAN, OP_LANJUT | OP_BATAL, t.pesan);
    case TH_NOTIF:
      return SR_DONE;
    case TH_BUANG_MANUAL:
      if (R.phase == 9) return failStep("Valve buang ditolak interlock");
      if (R.left <= dt) {
        relaySet(R_DRAIN, false);
        if (sensorRaw()) logf(LOG_WARN, "Air masih penuh setelah pembuangan manual");
        return SR_DONE;
      }
      R.left -= dt;
      return SR_CONTINUE;
    case TH_UJI_RELAY:
      if (R.left > dt) {
        R.left -= dt;
        return SR_CONTINUE;
      }
      if (R.phase == 0) {
        if (!relaySet((RelayId)t.mode, true)) return failStep("Relay ditolak interlock saat uji");
        logf(LOG_INFO, "Uji relay %s: ON", relayName((RelayId)t.mode));
        setDetail("Uji relay %s (menyala)", relayName((RelayId)t.mode));
        R.phase = 1;
        R.left = secToMs(t.detik ? t.detik : 2);
        return SR_CONTINUE;
      }
      relaySet((RelayId)t.mode, false);
      logf(LOG_INFO, "Uji relay %s: OFF", relayName((RelayId)t.mode));
      return SR_DONE;
  }
  return SR_DONE;
}

// ============================================================================
// Perintah
// ============================================================================
static bool rejectIfBusy() {
  if (mstate != MS_IDLE || holdFlag) {
    logf(LOG_WARN, "Perintah ditolak: mesin sedang %s", holdFlag ? "memperbarui firmware" : "bekerja");
    return true;
  }
  return false;
}

static void processCmd(const MachineCmd& c) {
  switch (c.type) {
    case MC_RUN:
      if (rejectIfBusy()) break;
      relayAllOff();   // batalkan kontrol relay manual yang mungkin masih menyala
      if (resumePending) {
        resumePending = false;
        resumeClear();
      }
      startProses(c.arg, c.waitSec, c.finishAt, -1);
      break;
    case MC_RUN_MANUAL: {
      if (rejectIfBusy()) break;
      relayAllOff();
      char name[28];
      snprintf(name, sizeof(name), "Manual: %s", tahapLabel(c.tahap.type));
      startSingle(c.tahap, name, false);
      break;
    }
    case MC_RUN_RELAYTEST: {
      if (rejectIfBusy() || c.arg >= R_COUNT) break;
      relayAllOff();
      Tahap t;
      memset(&t, 0, sizeof(t));
      t.type = TH_UJI_RELAY;
      t.mode = c.arg;
      t.detik = c.arg2 ? c.arg2 : 2;
      char name[28];
      snprintf(name, sizeof(name), "Uji relay %s", relayName((RelayId)c.arg));
      startSingle(t, name, true);
      break;
    }
    case MC_RUN_RELAYSEQ:
      if (rejectIfBusy()) break;
      relayAllOff();
      startRelaySeq(c.arg);
      break;
    case MC_STOP:
      relayAllOff();
      if (mstate != MS_IDLE) {
        endProgram(RES_STOPPED, nullptr);
      } else if (resumePending) {
        resumePending = false;
        resumeClear();
        logf(LOG_INFO, "Pemulihan Proses dibatalkan");
      }
      break;
    case MC_PAUSE:
      if (mstate == MS_RUNNING) doPause();
      else logf(LOG_WARN, "Tidak ada Proses yang berjalan");
      break;
    case MC_RESUME:
      if (mstate == MS_PAUSED) {
        doResume();
      } else if (mstate == MS_DECIDE) {
        logf(LOG_WARN, "Mesin menunggu keputusan: pilih %s", optsText(dec.opts).c_str());
      } else if (mstate == MS_IDLE && resumePending && !holdFlag) {
        relayAllOff();
        startProses(resumeInfo.slot, 0, -1, resumeInfo.stepIndex);
      } else {
        logf(LOG_WARN, "Tidak ada Proses yang sedang dijeda");
      }
      break;
    case MC_SKIP:
      if (mstate == MS_IDLE) {
        logf(LOG_WARN, "Tidak ada Proses yang berjalan");
      } else if (waitingStart) {
        waitingStart = false;
        logf(LOG_STEP, "Penantian dilewati, Proses dimulai sekarang");
        stepBegin();
      } else {
        relayAllOff();
        logf(LOG_STEP, "Tahap dilewati");
        mstate = MS_RUNNING;
        dec.code = DC_NONE;
        pauseReason[0] = 0;
        stepFinished();
      }
      break;
    case MC_DECIDE:
      if (mstate != MS_DECIDE) {
        logf(LOG_WARN, "Tidak ada keputusan yang menunggu");
      } else if (!(dec.opts & c.arg) || !c.arg) {
        logf(LOG_WARN, "Pilihan tidak tersedia untuk keputusan ini");
      } else {
        applyDecision(c.arg);
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
        logf(LOG_INFO, "Pemulihan Proses dibatalkan");
      }
      break;
  }
}

// ============================================================================
// Snapshot status
// ============================================================================
static uint32_t currentStepLeftMs() {
  if (waitingStart) return waitLeft;
  const Tahap& t = job.t[idx];
  switch (t.type) {
    case TH_RENDAM: case TH_TUNGGU: case TH_PUTAR: case TH_CEK:
      return R.left;
    case TH_SPIN:
      return R.left + (R.phase == 0 ? minToMs(t.lama) : 0);
    case TH_UJI_RELAY:
      return R.left + (R.phase == 0 ? secToMs(t.detik ? t.detik : 2) : 0);
    case TH_PEWANGI: case TH_BUANG_MANUAL:
      return R.left;
    case TH_SOAK: {
      uint32_t doneSessions = R.attempt;
      uint32_t remainingAfter = t.ulangi > doneSessions + 1 ? t.ulangi - doneSessions - 1 : 0;
      return R.left + (R.sub == 0 ? minToMs(t.rendam) : 0) + remainingAfter * minToMs(t.putar + t.rendam);
    }
    case TH_ISI: {
      uint32_t est = estimateStepMs(t);
      return est > R.elapsed ? est - R.elapsed : 0;
    }
    case TH_BUANG: {
      uint32_t est = estimateStepMs(t);
      return est > D.pullElapsed ? est - D.pullElapsed : 0;
    }
  }
  return 0;
}

static void publishStatus() {
  MachineStatus s;
  memset(&s, 0, sizeof(s));
  s.state = mstate;
  s.slot = NO_SLOT;
  s.resumeSlot = NO_SLOT;
  s.uptimeSec = millis() / 1000;
  s.waterRaw = sensorRaw();
  s.waterState = sensorStable(true, stableMs()) ? 1 : sensorStable(false, stableMs()) ? 0 : 2;
  s.resumePending = resumePending;
  if (resumePending) s.resumeSlot = resumeInfo.slot;
  s.relayFault = relayFaulted();
  strlcpy(s.resumeInfo, resumeText, sizeof(s.resumeInfo));
  for (int i = 0; i < R_COUNT; i++) s.relays[i] = relayIsOn((RelayId)i);
  if (mstate != MS_IDLE) {
    s.slot = jobManual || jobTest ? NO_SLOT : jobSlot;
    s.manual = jobManual || jobTest;
    strlcpy(s.name, job.name, sizeof(s.name));
    s.stepIndex = waitingStart ? 0 : idx + 1;
    s.stepCount = job.count;
    stageLabel(s.stage, sizeof(s.stage));
    if (!waitingStart) strlcpy(s.tahap, job.t[idx].type == TH_UJI_RELAY ? "uji" : tahapKey(job.t[idx].type), sizeof(s.tahap));
    strlcpy(s.detail, detailTxt, sizeof(s.detail));
    strlcpy(s.pauseReason, pauseReason, sizeof(s.pauseReason));
    if (mstate == MS_DECIDE) {
      s.decision = dec.code;
      s.decisionOpts = dec.opts;
    }
    uint32_t cur = currentStepLeftMs();
    uint32_t total = cur;
    for (uint8_t i = waitingStart ? 0 : idx + 1; i < job.count; i++) total += estimateStepMs(job.t[i]);
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

  // Failsafe relay (interlock dilanggar / motor atau pompa menyala terlalu lama)
  uint8_t guard = relayGuard();
  if (guard && mstate != MS_IDLE)
    endProgram(RES_ERROR, guard == 2   ? "pelanggaran interlock relay"
                          : guard == 3 ? "pompa pewangi menyala terlalu lama"
                                       : "motor menyala terlalu lama");

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

  if (mstate == MS_RUNNING && waitingStart) {
    if (waitLeft <= dt) {
      waitingStart = false;
      logf(LOG_STEP, "Waktu mulai tiba");
      stepBegin();
    } else {
      waitLeft -= dt;
    }
  } else if (mstate == MS_RUNNING) {
    SR r = tickStep(dt);
    switch (r) {
      case SR_DONE: stepFinished(); break;
      case SR_DECIDE: enterDecide(); break;
      case SR_FAIL: endProgram(RES_ERROR, failMsg); break;
      default: break;
    }
  } else if (mstate == MS_DECIDE && now - dec.remindMs >= minToMs(DECISION_REMIND_MIN)) {
    dec.remindMs = now;
    logf(LOG_ERROR, "Masih menunggu keputusan: %s. Buka dashboard: %s", dec.msg, optsText(dec.opts).c_str());
  }
  publishStatus();
}

#ifdef HOST_TEST
static uint32_t hostLastTick = 0;
void machineHostTick() { machineLoopOnce(hostLastTick); }
void machineHostReset() {
  mstate = MS_IDLE;
  resumePending = false;
  waitingStart = false;
  idx = 0;
  memset(&job, 0, sizeof(job));
  jobSlot = NO_SLOT;
  jobManual = jobTest = false;
  dec.code = DC_NONE;
  memset(&R, 0, sizeof(R));
  memset(&D, 0, sizeof(D));
  detailTxt[0] = pauseReason[0] = failMsg[0] = 0;
  hostLastTick = millis();
}
// Meniru boot setelah listrik padam: baca ulang data pemulihan dari penyimpanan.
void machineHostReboot() {
  machineHostReset();
  resumeText[0] = 0;
  if (resumeLoad(resumeInfo)) {
    if (resumeInfo.slot < PROSES_SLOTS && prosesLoad(resumeInfo.slot, job) && job.rev == resumeInfo.rev &&
        resumeInfo.stepIndex < job.count) {
      snprintf(resumeText, sizeof(resumeText), "%s \xE2\x80\xA2 Tahap %u: %s", job.name, (unsigned)resumeInfo.stepIndex + 1,
               tahapLabel(job.t[resumeInfo.stepIndex].type));
      resumePending = true;
      logf(LOG_WARN, "Listrik sempat padam saat %s", resumeText);
    } else {
      resumeClear();
    }
  }
  memset(&job, 0, sizeof(job));
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
  jobMutex = xSemaphoreCreateMutex();
  memset(&snap, 0, sizeof(snap));
  memset(&job, 0, sizeof(job));

  // Pemulihan listrik padam: hanya menawarkan, tidak pernah lanjut otomatis.
  // Dibatalkan bila Proses sudah berubah (rev berbeda) atau hilang.
  if (resumeLoad(resumeInfo)) {
    if (resumeInfo.slot < PROSES_SLOTS && prosesLoad(resumeInfo.slot, job) && job.rev == resumeInfo.rev &&
        resumeInfo.stepIndex < job.count) {
      snprintf(resumeText, sizeof(resumeText), "%s \xE2\x80\xA2 Tahap %u: %s", job.name, (unsigned)resumeInfo.stepIndex + 1,
               tahapLabel(job.t[resumeInfo.stepIndex].type));
      resumePending = true;
      logf(LOG_WARN, "Listrik sempat padam saat %s", resumeText);
    } else {
      resumeClear();
    }
    memset(&job, 0, sizeof(job));
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

bool machineStartProses(uint8_t slot, uint32_t waitSec, int finishAtMin) {
  MachineCmd c;
  memset(&c, 0, sizeof(c));
  c.type = MC_RUN;
  c.arg = slot;
  c.waitSec = waitSec;
  c.finishAt = (int16_t)finishAtMin;
  return machinePost(c);
}

bool machineRunManual(const Tahap& t) {
  MachineCmd c;
  memset(&c, 0, sizeof(c));
  c.type = MC_RUN_MANUAL;
  c.finishAt = -1;
  c.tahap = t;
  return machinePost(c);
}

void machineGetStatus(MachineStatus& out) {
  if (xSemaphoreTake(statusMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    out = snap;
    xSemaphoreGive(statusMutex);
  } else {
    out = snap;
  }
}

bool machineCopyJob(Proses& out, uint8_t& idxOut, bool& waiting, uint32_t& waitLeftMs, uint8_t& slot, bool& manual, bool& test) {
  if (jobMutex && xSemaphoreTake(jobMutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  bool active = mstate != MS_IDLE && job.count > 0;
  if (active) {
    out = job;
    idxOut = idx;
    waiting = waitingStart;
    waitLeftMs = waitLeft;
    slot = jobSlot;
    manual = jobManual;
    test = jobTest;
  }
  if (jobMutex) xSemaphoreGive(jobMutex);
  return active;
}

bool machineIsIdle() {
  MachineStatus s;
  machineGetStatus(s);
  return s.state == MS_IDLE;
}

bool machineSlotInUse(uint8_t slot) {
  MachineStatus s;
  machineGetStatus(s);
  if (s.state != MS_IDLE && s.slot == slot) return true;
  return s.resumePending && s.resumeSlot == slot;
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
    if (s.state == MS_DECIDE) out = "\xE2\x9D\x93 MENUNGGU KEPUTUSAN \xE2\x80\xA2 " + String(s.pauseReason);
  }
  out += "\n\xF0\x9F\x92\xA7 Air: ";
  out += waterTxt[s.waterState];
  return out;
}
