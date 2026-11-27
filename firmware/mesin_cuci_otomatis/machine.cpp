// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
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
static SemaphoreHandle_t jobMutex = nullptr;   // melindungi L->job/L->idx/L->waitingStart dari pembacaan API (machineCopyJob)
struct JobLock {
  JobLock() { if (jobMutex) xSemaphoreTake(jobMutex, portMAX_DELAY); }
  ~JobLock() { if (jobMutex) xSemaphoreGive(jobMutex); }
};
static MachineStatus snaps[WS_COUNT];
static volatile bool holdFlag = false;
// Tombol "Cek air": pengamatan yang sedang menunggu hasil
static bool waterCheckOn = false;
static uint32_t waterCheckMs = 0;

// ---- keputusan menunggu ----------------------------------------------------------
struct Decision {
  uint8_t code;
  uint8_t opts;
  char msg[72];
  uint32_t remindMs;   // kapan terakhir mengingatkan
};
#define DECISION_REMIND_MIN 10

// ---- konteks Tahap ---------------------------------------------------------------
struct Run {
  uint8_t phase, sub, attempt;
  uint16_t cnt;
  uint32_t left, elapsed, phaseLeft, onMs, offMs;
};
struct DrainParam {
  uint16_t lama, probe;
  uint8_t tarik, sesi;
};
enum { D_PULL, D_CHECK, D_UNJAM_WAIT, D_FAILED };
struct DrainCtx {
  uint8_t phase, session, unjam;
  bool probeDone;         // tarikan terbukti berhasil (sensor sudah tidak penuh)
  uint32_t pullElapsed;   // lama valve ditarik pada percobaan ini
  uint32_t left;
};
enum { F_SETTLE, F_PRECHECK, F_DRAINING, F_FILLING, F_RETRYWAIT };
enum { AG_LEFT_ON, AG_LEFT_OFF, AG_RIGHT_ON, AG_RIGHT_OFF, AG_GAP };

// ---- Satu workspace (cuci / pengering): semua keadaan pekerjaan yang sedang berjalan ----
struct Lane {
  uint8_t ws = WS_CUCI;
  Proses job = {};              // salinan Proses (atau satu Tahap untuk Manual/uji)
  uint8_t jobSlot = NO_SLOT;
  uint16_t jobRev = 0;
  bool jobManual = false, jobTest = false;
  bool seq = false;             // kalibrasi relay berurutan (memakai semua relay)
  uint8_t idx = 0;
  MState mstate = MS_IDLE;
  bool waitingStart = false;    // menunggu jam mulai (tunda / selesai pada jam)
  uint32_t waitLeft = 0;
  uint32_t progStartMs = 0, progStartEpoch = 0, progMotorStart = 0, progTotalEst = 0;
  char pauseReason[72] = {};
  char failMsg[72] = {};
  char detailTxt[56] = {};
  bool resumePending = false;
  ResumeInfo resumeInfo = {};
  char resumeText[56] = {};
  Decision dec = {};
  Run R = {};
  DrainCtx D = {};
  DrainParam dp = {};
};
static Lane lanes[WS_COUNT];
static Lane* L = &lanes[WS_CUCI];       // workspace yang sedang diproses; HANYA dipakai task mesin
static uint32_t manualSince[R_COUNT];   // kapan relay manual dinyalakan (0 = tidak)

static void useLane(uint8_t ws) {
  if (ws >= WS_COUNT) ws = WS_CUCI;
  L = &lanes[ws];
  L->ws = ws;
}

// Relay milik workspace aktif. Kalibrasi berurutan (di workspace cuci) meminjam motor spin.
static uint8_t laneMask() {
  if (L->ws == WS_KERING) return 1u << R_SPIN;
  uint8_t all = (1u << R_COUNT) - 1;
  return L->seq ? all : (uint8_t)(all & ~(1u << R_SPIN));
}

// Log mesin: pesan dari workspace pengering diberi awalan agar jelas di web dan notifikasi.
static void lanef(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void lanef(LogLevel level, const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (L->ws == WS_KERING) logStr(level, String("Pengering: ") + buf);
  else logStr(level, String(buf));
}

// Sensor baru dipercaya setelah nilainya tidak berubah selama waktu konfirmasi (cipratan sesaat diabaikan).
static inline uint32_t stableMs() { return secToMs(S.airDetik); }
// Batas menunggu pemeriksaan: cukup longgar agar satu-dua cipratan tidak langsung dianggap gagal.
static inline uint32_t checkWindowMs() { return 3 * stableMs() + secToMs(2); }
// Berhenti menunggu: batas habis, atau sudah jelas kebalikannya selama waktu konfirmasi.
static inline bool checkGaveUp(bool opposite, uint32_t left, uint32_t dt) {
  return left <= dt || (left <= 2 * stableMs() && sensorStable(opposite, stableMs()));
}

static void setDetail(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void setDetail(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(L->detailTxt, sizeof(L->detailTxt), fmt, ap);
  va_end(ap);
}

static SR failStep(const char* msg) {
  strlcpy(L->failMsg, msg, sizeof(L->failMsg));
  return SR_FAIL;
}

// ============================================================================
// Keputusan menunggu
// ============================================================================
const char* decisionCodeName(uint8_t code) {
  static const char* n[] = {"", "isi_gagal", "pra_penuh", "buang_gagal", "cek_gagal", "tindakan"};
  return code < 6 ? n[code] : "";
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
  L->dec.code = code;
  L->dec.opts = opts;
  strlcpy(L->dec.msg, msg, sizeof(L->dec.msg));
  return SR_DECIDE;
}

// ============================================================================
// Teks
// ============================================================================
static void stageLabel(char* out, size_t n) {
  if (L->waitingStart) {
    strlcpy(out, "Menunggu mulai", n);
    return;
  }
  const Tahap& t = L->job.t[L->idx];
  if (L->jobTest) {
    strlcpy(out, L->jobManual ? "Uji relay" : "Kalibrasi relay", n);
  } else if (L->jobManual) {
    snprintf(out, n, "Manual: %s", tahapLabel(t.type));
  } else {
    snprintf(out, n, "Tahap %u/%u: %s", (unsigned)L->idx + 1, (unsigned)L->job.count, tahapLabel(t.type));
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
  L->R.onMs = scaledMs(on);
  L->R.offMs = scaledMs(off);
}

// Mulai (atau lanjutkan) agitasi dengan sisa waktu L->R.left. Menunggu jeda ganti arah dulu.
static void agitateStart(uint8_t pola) {
  patternMs(pola);
  L->R.phase = AG_GAP;
  L->R.phaseLeft = scaledMs(MOTOR_REVERSE_GAP_MS) + TICK_MS * 2;
  L->R.cnt = 0;
  setDetail("Agitasi (menyiapkan)");
}

static SR agitateTick(uint32_t dt) {
  if (L->R.left <= dt) {
    relaySet(R_LEFT, false);
    relaySet(R_RIGHT, false);
    L->R.left = 0;
    lanef(LOG_INFO, "Motor OFF");
    return SR_DONE;
  }
  L->R.left -= dt;
  if (L->R.phaseLeft > dt) {
    L->R.phaseLeft -= dt;
    return SR_CONTINUE;
  }
  switch (L->R.phase) {
    case AG_GAP:
      if (!relaySet(R_LEFT, true)) return failStep("Motor kiri ditolak interlock");
      lanef(LOG_INFO, "Motor ON");
      L->R.phase = AG_LEFT_ON;
      L->R.phaseLeft = L->R.onMs;
      setDetail("Agitasi (kiri)");
      break;
    case AG_LEFT_ON:
      relaySet(R_LEFT, false);
      L->R.phase = AG_LEFT_OFF;
      L->R.phaseLeft = L->R.offMs;
      setDetail("Agitasi (jeda)");
      break;
    case AG_LEFT_OFF:
      if (!relaySet(R_RIGHT, true)) return failStep("Motor kanan ditolak interlock");
      L->R.phase = AG_RIGHT_ON;
      L->R.phaseLeft = L->R.onMs;
      setDetail("Agitasi (kanan)");
      break;
    case AG_RIGHT_ON:
      relaySet(R_RIGHT, false);
      L->R.phase = AG_RIGHT_OFF;
      L->R.phaseLeft = L->R.offMs;
      setDetail("Agitasi (jeda)");
      break;
    case AG_RIGHT_OFF:
      L->R.cnt++;
      lanef(LOG_INFO, "Putaran ke %u", (unsigned)L->R.cnt);
      if (!relaySet(R_LEFT, true)) return failStep("Motor kiri ditolak interlock");
      L->R.phase = AG_LEFT_ON;
      L->R.phaseLeft = L->R.onMs;
      setDetail("Agitasi (kiri)");
      break;
  }
  return SR_CONTINUE;
}

// ============================================================================
// Cek air: hasil baru diberikan setelah sensor diamati selama waktu konfirmasi sejak diminta
// ============================================================================
static bool waterSettled() { return sensorStable(true, stableMs()) || sensorStable(false, stableMs()); }
static void reportWater() {
  const char* r = sensorStable(true, stableMs())    ? "PENUH"
                  : sensorStable(false, stableMs()) ? "TIDAK penuh"
                                                    : "belum stabil (sensor berubah-ubah)";
  logf(LOG_NOTIF, "Hasil cek air: %s", r);
}
static void waterCheckTick(uint32_t dt) {
  if (!waterCheckOn) return;
  waterCheckMs += dt;
  if ((waterCheckMs >= stableMs() && waterSettled()) || waterCheckMs >= checkWindowMs()) {
    waterCheckOn = false;
    reportWater();
  }
}

// ============================================================================
// Pembuangan air (valve ditarik motor). Dipakai Tahap Buang dan pra-cek Isi.
// ============================================================================
// Sensor level hanya ada di paling atas: ia hanya tahu "penuh" atau "tidak penuh".
// Tarikan valve dibuktikan berhasil bila air turun dari posisi penuh dalam `probe` detik (Verifikasi Pengurangan
// Jumlah Air). Bila tidak, valve dilepas lalu ditarik lagi (Tarik Ulang Valve). Setelah `lama` menit, air tidak boleh
// masih penuh (cek akhir, selalu); bila masih penuh, sesi diulang sampai `sesi` kali.
static void drainParamFrom(const Tahap& t) {
  L->dp.lama = t.lama;
  L->dp.probe = t.probe;
  L->dp.tarik = t.tarik;
  L->dp.sesi = t.sesi;
}
static void drainParamDefault() {
  Tahap t;
  tahapDefault(TH_BUANG, t);
  drainParamFrom(t);
}

static bool drainPullStart() {
  if (!relaySet(R_DRAIN, true)) {
    L->D.phase = D_FAILED;
    return false;
  }
  L->D.phase = D_PULL;
  L->D.pullElapsed = 0;
  // Bila air sudah tidak penuh sejak awal (mis. isi sebagian), keberhasilan tarikan tidak bisa dibuktikan.
  L->D.probeDone = sensorStable(false, stableMs());
  lanef(LOG_INFO, "Membuang air... sesi %d/%d", L->D.session, (int)L->dp.sesi);
  setDetail("Membuang air (sesi %d/%d)", L->D.session, (int)L->dp.sesi);
  return true;
}

static void drainBegin() {
  memset(&L->D, 0, sizeof(L->D));
  L->D.session = 1;
  drainPullStart();
}

static SR drainFail() {
  relaySet(R_DRAIN, false);
  return decide(DC_BUANG_GAGAL, OP_ULANG | OP_LANJUT | OP_BATAL, "Pembuangan gagal, cek modul tarik valve buang");
}

static void drainRelease() {
  relaySet(R_DRAIN, false);
  L->D.phase = D_CHECK;
  L->D.left = checkWindowMs();
  setDetail("Memeriksa air sudah turun");
  lanef(LOG_INFO, "Mengecek apakah air sudah turun dari posisi penuh...");
}

// Sesi gagal (air masih penuh): ulangi sesi sampai batas, lalu keputusan.
static SR drainSessionFailed() {
  if (L->D.session < L->dp.sesi) {
    L->D.session++;
    L->D.unjam = 0;
    lanef(LOG_INFO, "Mengulangi sesi pembuangan air");
    drainPullStart();
    return SR_CONTINUE;
  }
  return drainFail();
}

static SR drainTick(uint32_t dt) {
  switch (L->D.phase) {
    case D_FAILED:
      return failStep("Valve buang ditolak interlock");

    case D_PULL:
      L->D.pullElapsed += dt;
      if (!L->D.probeDone) {
        if (sensorStable(false, stableMs())) {
          L->D.probeDone = true;
          lanef(LOG_INFO, "Tarikan valve berhasil: air mulai turun");
        } else if (L->D.pullElapsed >= secToMs(L->dp.probe) &&
                   (sensorRaw() || L->D.pullElapsed >= secToMs(L->dp.probe) + stableMs())) {
          // air yang baru turun tepat di batas diberi waktu konfirmasi
          relaySet(R_DRAIN, false);
          lanef(LOG_WARN, "Valve buang belum menarik (air masih penuh setelah %d dtk)", (int)L->dp.probe);
          if (L->D.unjam < L->dp.tarik) {
            L->D.unjam++;
            L->D.phase = D_UNJAM_WAIT;
            L->D.left = scaledMs(UNJAM_RELEASE_MS);
            lanef(LOG_INFO, "Tarik ulang valve %d/%d: valve dilepas lalu ditarik lagi", L->D.unjam, (int)L->dp.tarik);
            setDetail("Tarik ulang valve %d/%d", L->D.unjam, (int)L->dp.tarik);
            return SR_CONTINUE;
          }
          return drainSessionFailed();
        }
      }
      if (L->D.probeDone && L->D.pullElapsed >= minToMs(L->dp.lama)) drainRelease();
      return SR_CONTINUE;

    case D_UNJAM_WAIT:
      if (L->D.left > dt) {
        L->D.left -= dt;
        return SR_CONTINUE;
      }
      if (!relaySet(R_DRAIN, true)) return failStep("Valve buang ditolak interlock");
      L->D.phase = D_PULL;
      L->D.pullElapsed = 0;
      L->D.probeDone = sensorStable(false, stableMs());
      return SR_CONTINUE;

    case D_CHECK:
      if (sensorStable(false, stableMs())) {
        lanef(LOG_INFO, "Air sudah turun, pembuangan selesai");
        return SR_DONE;
      }
      if (!checkGaveUp(true, L->D.left, dt)) {
        L->D.left -= dt;
        return SR_CONTINUE;
      }
      lanef(LOG_WARN, "Air masih penuh setelah pembuangan");
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
  memset(&L->R, 0, sizeof(L->R));
  L->R.phase = F_SETTLE;
  L->R.left = scaledMs(FILL_SETTLE_MS);
  if (t.mode == ISI_PERSEN && C.nFill == 0)
    lanef(LOG_WARN, "Belum ada data kalibrasi untuk level persen, memakai sensor penuh");
  setDetail("Menyiapkan pengisian");
}

static SR fillStartValve() {
  if (!relaySet(R_FILL, true)) return failStep("Valve isi ditolak interlock");
  L->R.phase = F_FILLING;
  L->R.elapsed = 0;
  lanef(LOG_INFO, "Mengisi air...");
  setDetail("Mengisi air");
  return SR_CONTINUE;
}

static void fillFinished(const Tahap& t) {
  relaySet(R_FILL, false);
  if (effectiveFillMode(t) == ISI_SENSOR && L->R.attempt == 0) {
    float real = (float)L->R.elapsed * TIME_SCALE;
    bool suspicious = false;
    if (C.nFill >= 2) {
      if (real > C.avgFillMs * ANOMALY_SLOW_FACTOR) {
        lanef(LOG_WARN, "Pengisian lebih lama dari biasanya (tekanan air rendah?)");
      } else if (real < C.avgFillMs * ANOMALY_FAST_FACTOR) {
        lanef(LOG_WARN, "Sensor level mencurigakan: air penuh terlalu cepat");
        suspicious = true;
      }
    }
    if (!suspicious) {
      C.avgFillMs = C.nFill == 0 ? real : C.avgFillMs * 0.7f + real * 0.3f;
      if (C.nFill < 65535) C.nFill++;
      calibSave();
    }
  }
  lanef(LOG_INFO, "Air sudah penuh!");
}

static void fillDrainFirst() {
  drainParamDefault();
  drainBegin();
  L->R.phase = F_DRAINING;
}

static SR fillTick(const Tahap& t, uint32_t dt) {
  switch (L->R.phase) {
    case F_SETTLE:
      if (L->R.left > dt) {
        L->R.left -= dt;
      } else {
        L->R.phase = F_PRECHECK;
        L->R.left = checkWindowMs();
        setDetail("Memeriksa air belum penuh");
      }
      return SR_CONTINUE;

    case F_PRECHECK:
      if (sensorStable(false, stableMs())) return fillStartValve();
      // Sudah jelas penuh (atau tidak stabil terlalu lama): terapkan pilihan pada Tahap
      if (sensorStable(true, stableMs()) || L->R.left <= dt) {
        switch (t.pra) {
          case PRA_JEDA:
            return decide(DC_PRA_PENUH, OP_BUANG | OP_PAKAI | OP_BATAL, "Air sudah penuh sebelum mengisi");
          case PRA_PAKAI:
            lanef(LOG_INFO, "Air sudah penuh, pengisian dilewati");
            return SR_DONE;
          default:
            lanef(LOG_INFO, "Air masih penuh sebelum mengisi, membuang dulu");
            fillDrainFirst();
            return SR_CONTINUE;
        }
      }
      L->R.left -= dt;
      return SR_CONTINUE;

    case F_DRAINING: {
      SR r = drainTick(dt);
      if (r == SR_DONE) return fillStartValve();
      return r;
    }

    case F_FILLING: {
      L->R.elapsed += dt;
      uint8_t lv = effectiveFillMode(t);
      bool full = sensorStable(true, stableMs());
      bool done = false;
      if (lv == ISI_SENSOR) {
        done = full;
      } else {
        uint32_t target = lv == ISI_PERSEN ? (uint32_t)(C.avgFillMs * t.persen / 100.0f / TIME_SCALE) : minToMs(t.lama);
        if (full) {
          lanef(LOG_WARN, "Sensor penuh sebelum waktu habis, pengisian dihentikan");
          done = true;
        } else if (L->R.elapsed >= target) {
          done = true;
        }
      }
      if (done) {
        fillFinished(t);
        return SR_DONE;
      }
      // sensor yang baru kena air tepat di batas diberi waktu konfirmasi
      bool timedOut = (lv != ISI_WAKTU) && L->R.elapsed >= secToMs(t.batas) &&
                      (!sensorRaw() || L->R.elapsed >= secToMs(t.batas) + stableMs());
      if (!timedOut) return SR_CONTINUE;

      relaySet(R_FILL, false);
      if (L->R.attempt < t.coba) {
        L->R.attempt++;
        lanef(LOG_WARN, "Air tidak penuh dalam batas waktu. Coba ulang %d/%d dalam %d menit", L->R.attempt, (int)t.coba,
             (int)t.tunggu);
        L->R.phase = F_RETRYWAIT;
        L->R.left = minToMs(t.tunggu);
        setDetail("Menunggu air (coba ulang %d/%d)", L->R.attempt, (int)t.coba);
        return SR_CONTINUE;
      }
      char msg[72];
      snprintf(msg, sizeof(msg), "Air tidak terisi setelah %d percobaan", (int)t.coba + 1);
      return decide(DC_ISI_GAGAL, OP_ULANG | OP_LANJUT | OP_BATAL, msg);
    }

    case F_RETRYWAIT:
      if (L->R.left > dt) {
        L->R.left -= dt;
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
  const Tahap& t = L->job.t[L->idx];
  memset(&L->R, 0, sizeof(L->R));
  switch (t.type) {
    case TH_ISI: fillBegin(t); break;
    case TH_BUANG:
      drainParamFrom(t);
      drainBegin();
      break;
    case TH_PUTAR:
      L->R.left = minToMs(t.lama);
      agitateStart(t.pola);
      break;
    case TH_SOAK:
      L->R.sub = 0;
      L->R.left = minToMs(t.putar);
      agitateStart(t.pola);
      break;
    case TH_RENDAM:
      L->R.left = minToMs(t.lama);
      setDetail("Merendam");
      break;
    case TH_SPIN:
      L->R.left = minToMs(t.lama);
      if (!relaySet(R_SPIN, true)) L->R.phase = 9;   // gagal: dilaporkan di tick
      setDetail("Spin");
      break;
    case TH_PEWANGI:
      L->R.left = secToMs((uint32_t)t.cup2 * 10);   // 0,5 cup = 10 dtk
      if (relaySet(R_PERFUME, true)) lanef(LOG_INFO, "Memasukkan pewangi...");
      setDetail("Memasukkan pewangi");
      break;
    case TH_TUNGGU:
      L->R.left = secToMs(t.detik);
      setDetail("Menunggu");
      break;
    case TH_CEK:
      L->R.left = checkWindowMs();
      setDetail("Memeriksa air");
      break;
    case TH_TINDAKAN:
      setDetail("Menunggu tindakan");
      break;
    case TH_NOTIF:
      lanef(LOG_NOTIF, "%s", t.pesan);
      setDetail("Mengirim notifikasi");
      break;
    case TH_BUANG_MANUAL:
      L->R.left = minToMs(t.lama);
      if (!relaySet(R_DRAIN, true)) L->R.phase = 9;   // gagal: dilaporkan di tick
      setDetail("Membuang air (waktu %u mnt)", (unsigned)t.lama);
      break;
    case TH_UJI_RELAY:
      L->R.phase = 0;   // jeda dulu (juga memberi jeda ganti arah motor), lalu menyala
      L->R.left = scaledMs(MOTOR_REVERSE_GAP_MS) + TICK_MS * 2;
      setDetail("Uji relay %s (siap)", relayName((RelayId)t.mode));
      break;
  }
}

static void stepBegin() {
  const Tahap& t = L->job.t[L->idx];
  char stage[40];
  stageLabel(stage, sizeof(stage));
  String params = stepParams(t);
  lanef(LOG_STEP, "%s mulai%s%s", stage, params.length() ? ": " : "", params.c_str());
  if (!L->jobTest && !L->jobManual) resumeSave(L->jobSlot, L->idx, L->jobRev, L->ws);
  stepInit();
}

// Setelah dilanjutkan dari jeda: nyalakan lagi keluaran tanpa mengulang Tahap.
static void stepEngage() {
  const Tahap& t = L->job.t[L->idx];
  switch (t.type) {
    case TH_PUTAR: agitateStart(t.pola); break;
    case TH_SOAK:
      if (L->R.sub == 0) agitateStart(t.pola);
      break;
    case TH_PEWANGI:
      if (L->R.left > 0) relaySet(R_PERFUME, true);
      break;
    case TH_SPIN:
      if (L->R.left > 0) relaySet(R_SPIN, true);
      break;
    case TH_UJI_RELAY:
      L->R.phase = 0;
      L->R.left = scaledMs(MOTOR_REVERSE_GAP_MS) + TICK_MS * 2;
      break;
    case TH_ISI: fillBegin(t); break;
    case TH_BUANG:
      drainParamFrom(t);
      drainBegin();
      break;
    case TH_BUANG_MANUAL:
      if (L->R.left > 0) relaySet(R_DRAIN, true);
      break;
    default: break;
  }
}

static void endProgram(CycleResult res, const char* why) {
  relayOffMask(laneMask());
  uint32_t durSec = (millis() - L->progStartMs) / 1000;
  uint32_t motorDelta = L->ws == WS_CUCI ? relayMotorSeconds() - L->progMotorStart : 0;   // motor cuci saja
  if (!L->jobTest) {
    statsRecord(L->job.name, L->progStartEpoch, durSec * TIME_SCALE, res);
    if (motorDelta) statsAddMotorSec(motorDelta);
    if (!L->jobManual) resumeClear(L->ws);
  }
  L->mstate = MS_IDLE;
  L->seq = false;
  L->waitingStart = false;
  L->dec.code = DC_NONE;
  L->detailTxt[0] = 0;
  L->pauseReason[0] = 0;
  if (res == RES_DONE) lanef(LOG_DONE, "Proses %s selesai", L->job.name);
  else if (res == RES_STOPPED) lanef(LOG_WARN, "Proses %s dihentikan%s%s", L->job.name, why ? ": " : "", why ? why : "");
  else lanef(LOG_ERROR, "Proses %s dibatalkan: %s", L->job.name, why ? why : "kesalahan");
}

static void stepFinished() {
  { JobLock jl; L->idx++; }
  if (L->idx >= L->job.count) {
    endProgram(RES_DONE, nullptr);
    return;
  }
  stepBegin();
}

static void doPause() {
  relayOffMask(laneMask());
  L->mstate = MS_PAUSED;
  L->pauseReason[0] = 0;
  lanef(LOG_WARN, "Proses dijeda oleh pengguna");
}

static void doResume() {
  L->mstate = MS_RUNNING;
  L->pauseReason[0] = 0;
  lanef(LOG_STEP, "Melanjutkan: %s", stepTitle(L->job.t[L->idx]).c_str());
  stepEngage();
}

static void enterDecide() {
  relayOffMask(laneMask());
  L->mstate = MS_DECIDE;
  L->dec.remindMs = millis();
  strlcpy(L->pauseReason, L->dec.msg, sizeof(L->pauseReason));
  String opts = optsText(L->dec.opts);
  if (L->dec.code == DC_TINDAKAN) lanef(LOG_NOTIF, "%s. Buka dashboard: %s", L->dec.msg, opts.c_str());
  else lanef(LOG_ERROR, "Menunggu keputusan: %s. Buka dashboard: %s", L->dec.msg, opts.c_str());
}

static void applyDecision(uint8_t choice) {
  const Tahap& t = L->job.t[L->idx];
  uint8_t code = L->dec.code;
  char msg[72];
  strlcpy(msg, L->dec.msg, sizeof(msg));
  L->dec.code = DC_NONE;
  L->pauseReason[0] = 0;
  L->mstate = MS_RUNNING;
  switch (choice) {
    case OP_BATAL:
      lanef(LOG_WARN, "Keputusan: Batal");
      endProgram(RES_STOPPED, "dibatalkan oleh pengguna");
      return;
    case OP_ULANG:
      lanef(LOG_STEP, "Keputusan: Ulang Tahap");
      stepInit();
      return;
    case OP_BUANG:
      lanef(LOG_INFO, "Keputusan: buang air dulu lalu isi");
      memset(&L->R, 0, sizeof(L->R));
      fillDrainFirst();
      return;
    case OP_PAKAI:
      lanef(LOG_INFO, "Keputusan: pakai air yang ada, pengisian dilewati");
      stepFinished();
      return;
    case OP_LANJUT:
      lanef(LOG_WARN, "Keputusan: Lanjut meski %s", msg);
      if (code == DC_BUANG_GAGAL && t.type == TH_ISI) {   // pembuangan pra-isi gagal: lanjut ke pengisian
        SR r = fillStartValve();
        if (r == SR_FAIL) endProgram(RES_ERROR, L->failMsg);
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
      lanef(LOG_WARN, "Jam belum sinkron, 'selesai jam' diabaikan");
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
        lanef(LOG_WARN, "Tidak sempat selesai tepat waktu, Proses dimulai sekarang");
      }
    }
  }
  return wait;
}

static void commonStart(int resumeIdx, uint32_t waitSec) {
  L->progTotalEst = 0;
  for (uint8_t i = 0; i < L->job.count; i++) L->progTotalEst += estimateStepMs(L->job.t[i]);
  L->resumePending = false;
  L->idx = resumeIdx >= 0 ? (uint8_t)resumeIdx : 0;
  L->mstate = MS_RUNNING;
  L->dec.code = DC_NONE;
  L->progStartMs = millis();
  time_t now = time(nullptr);
  L->progStartEpoch = now > 1700000000 ? (uint32_t)now : 0;
  L->progMotorStart = relayMotorSeconds();
  L->pauseReason[0] = 0;
  L->waitingStart = false;
  if (waitSec > 0) {
    L->waitingStart = true;
    L->waitLeft = secToMs(waitSec);
    L->progTotalEst += L->waitLeft;
    setDetail("Menunggu jam mulai");
    lanef(LOG_INFO, "Proses ditunda %lu menit %lu detik", (unsigned long)(waitSec / 60), (unsigned long)(waitSec % 60));
  }
}

static void startProses(uint8_t slot, uint32_t waitSec, int finishAt, int resumeIdx) {
  JobLock jl;
  if (slot >= PROSES_SLOTS || !prosesLoad(prosesId(L->ws, slot), L->job)) {
    lanef(LOG_WARN, "Proses slot %u kosong atau rusak", (unsigned)slot + 1);
    memset(&L->job, 0, sizeof(L->job));
    return;
  }
  if (resumeIdx >= L->job.count || (resumeIdx >= 0 && L->job.rev != L->resumeInfo.rev)) {
    lanef(LOG_ERROR, "Data pemulihan tidak cocok dengan Proses");
    resumeClear(L->ws);
    L->resumePending = false;
    return;
  }
  L->jobSlot = slot;
  L->jobRev = L->job.rev;
  L->jobManual = L->jobTest = L->seq = false;
  uint32_t est = 0;
  for (uint8_t i = 0; i < L->job.count; i++) est += estimateStepMs(L->job.t[i]);
  uint32_t wait = resumeIdx < 0 ? computeWaitSeconds(est, waitSec, finishAt) : 0;
  commonStart(resumeIdx, wait);
  lanef(LOG_STEP, "Proses %s %s (%u Tahap)", L->job.name, resumeIdx >= 0 ? "dilanjutkan" : "dimulai", (unsigned)L->job.count);
  if (!L->waitingStart) stepBegin();
}

static void startSingle(const Tahap& t, const char* name, bool test) {
  JobLock jl;
  memset(&L->job, 0, sizeof(L->job));
  strlcpy(L->job.name, name, sizeof(L->job.name));
  L->job.t[0] = t;
  L->job.count = 1;
  L->jobSlot = NO_SLOT;
  L->jobRev = 0;
  L->jobManual = true;
  L->jobTest = test;
  L->seq = false;
  if (L->resumePending) {   // memulai kerja baru menggugurkan tawaran pemulihan
    L->resumePending = false;
    resumeClear(L->ws);
  }
  commonStart(-1, 0);
  lanef(LOG_STEP, "Proses %s dimulai (1 Tahap)", L->job.name);
  stepBegin();
}

static void startRelaySeq(uint8_t secs) {
  JobLock jl;
  memset(&L->job, 0, sizeof(L->job));
  strlcpy(L->job.name, "Kalibrasi relay", sizeof(L->job.name));
  for (uint8_t r = 0; r < R_COUNT; r++) {
    Tahap& t = L->job.t[L->job.count++];
    memset(&t, 0, sizeof(t));
    t.type = TH_UJI_RELAY;
    t.mode = r;
    t.detik = secs ? secs : 3;
  }
  L->jobSlot = NO_SLOT;
  L->jobRev = 0;
  L->jobManual = false;
  L->jobTest = true;
  L->seq = true;
  if (L->resumePending) {
    L->resumePending = false;
    resumeClear(L->ws);
  }
  commonStart(-1, 0);
  lanef(LOG_STEP, "Proses %s dimulai (%u Tahap)", L->job.name, (unsigned)L->job.count);
  stepBegin();
}

// ============================================================================
// Eksekusi tick Tahap
// ============================================================================
static SR tickStep(uint32_t dt) {
  const Tahap& t = L->job.t[L->idx];
  switch (t.type) {
    case TH_ISI: return fillTick(t, dt);
    case TH_BUANG: return drainTick(dt);
    case TH_PUTAR: return agitateTick(dt);
    case TH_SOAK: {
      if (L->R.sub == 0) {
        SR r = agitateTick(dt);
        if (r == SR_DONE) {
          L->R.sub = 1;
          L->R.left = minToMs(t.rendam);
          setDetail("Soak: rendam (sesi %u/%u)", (unsigned)L->R.attempt + 1, (unsigned)t.ulangi);
        }
        return r == SR_DONE ? SR_CONTINUE : r;
      }
      if (L->R.left > dt) {
        L->R.left -= dt;
        return SR_CONTINUE;
      }
      L->R.sub = 0;
      L->R.attempt++;   // jumlah sesi selesai
      lanef(LOG_INFO, "Soak ke %u selesai", L->R.attempt);
      if (L->R.attempt >= t.ulangi) return SR_DONE;
      L->R.left = minToMs(t.putar);
      agitateStart(t.pola);
      return SR_CONTINUE;
    }
    case TH_RENDAM:
    case TH_TUNGGU:
      if (L->R.left <= dt) return SR_DONE;
      L->R.left -= dt;
      return SR_CONTINUE;
    case TH_SPIN:
      if (L->R.phase == 9) return failStep("Relay spin ditolak interlock");
      if (L->R.left <= dt) {
        relaySet(R_SPIN, false);
        return SR_DONE;
      }
      L->R.left -= dt;
      return SR_CONTINUE;
    case TH_PEWANGI:
      if (L->R.left <= dt) {
        relaySet(R_PERFUME, false);
        lanef(LOG_INFO, "Pewangi selesai dimasukkan!");
        return SR_DONE;
      }
      L->R.left -= dt;
      return SR_CONTINUE;
    case TH_CEK: {
      bool want = t.mode == CEK_PENUH;
      bool observed = checkWindowMs() - L->R.left >= stableMs();   // diamati sejak Tahap dimulai
      if (L->jobManual) {   // menu Manual: cukup laporkan keadaan air, apa pun syaratnya
        if ((observed && waterSettled()) || L->R.left <= dt) {
          reportWater();
          return SR_DONE;
        }
        L->R.left -= dt;
        return SR_CONTINUE;
      }
      if (observed && sensorStable(want, stableMs())) {
        lanef(LOG_INFO, "Cek air lulus: %s", want ? "penuh" : "tidak penuh");
        return SR_DONE;
      }
      if (checkGaveUp(!want, L->R.left, dt)) {
        return decide(DC_CEK_GAGAL, OP_ULANG | OP_LANJUT | OP_BATAL,
                      want ? "Air belum mencukupi (belum penuh)" : "Air masih penuh");
      }
      L->R.left -= dt;
      return SR_CONTINUE;
    }
    case TH_TINDAKAN:
      return decide(DC_TINDAKAN, OP_LANJUT | OP_BATAL, t.pesan);
    case TH_NOTIF:
      return SR_DONE;
    case TH_BUANG_MANUAL:
      if (L->R.phase == 9) return failStep("Valve buang ditolak interlock");
      if (L->R.left <= dt) {
        relaySet(R_DRAIN, false);
        if (sensorStable(true, stableMs())) lanef(LOG_WARN, "Air masih penuh setelah pembuangan manual");
        return SR_DONE;
      }
      L->R.left -= dt;
      return SR_CONTINUE;
    case TH_UJI_RELAY:
      if (L->R.left > dt) {
        L->R.left -= dt;
        return SR_CONTINUE;
      }
      if (L->R.phase == 0) {
        if (!relaySet((RelayId)t.mode, true)) return failStep("Relay ditolak interlock saat uji");
        lanef(LOG_INFO, "Uji relay %s: ON", relayName((RelayId)t.mode));
        setDetail("Uji relay %s (menyala)", relayName((RelayId)t.mode));
        L->R.phase = 1;
        L->R.left = secToMs(t.detik ? t.detik : 2);
        return SR_CONTINUE;
      }
      relaySet((RelayId)t.mode, false);
      lanef(LOG_INFO, "Uji relay %s: OFF", relayName((RelayId)t.mode));
      return SR_DONE;
  }
  return SR_DONE;
}

// ============================================================================
// Perintah
// ============================================================================
// Kalibrasi berurutan memakai motor spin juga, jadi workspace pengering ikut terkunci.
static bool seqRunning() { return lanes[WS_CUCI].mstate != MS_IDLE && lanes[WS_CUCI].seq; }

static bool rejectIfBusy() {
  if (L->mstate != MS_IDLE || holdFlag || (L->ws == WS_KERING && seqRunning())) {
    lanef(LOG_WARN, "Perintah ditolak: mesin sedang %s", holdFlag ? "memperbarui firmware" : "bekerja");
    return true;
  }
  return false;
}

static void stopLane() {
  relayOffMask(laneMask());   // juga mematikan relay manual milik workspace ini
  if (L->mstate != MS_IDLE) {
    endProgram(RES_STOPPED, nullptr);
  } else if (L->resumePending) {
    L->resumePending = false;
    resumeClear(L->ws);
    lanef(LOG_INFO, "Pemulihan Proses dibatalkan");
  }
}

uint8_t machineRelayWs(uint8_t relay) { return relay == R_SPIN ? WS_KERING : WS_CUCI; }

static void processCmd(const MachineCmd& c) {
  uint8_t ws = c.ws;
  if (c.type == MC_RUN_RELAYTEST || c.type == MC_RELAY) ws = machineRelayWs(c.arg);
  if (c.type == MC_RUN_RELAYSEQ) ws = WS_CUCI;
  if (c.type == MC_STOP && ws == WS_ALL) {
    relayAllOff();
    for (uint8_t w = 0; w < WS_COUNT; w++) {
      useLane(w);
      stopLane();
    }
    return;
  }
  if (ws >= WS_COUNT) return;
  useLane(ws);
  switch (c.type) {
    case MC_RUN:
      if (rejectIfBusy()) break;
      relayOffMask(laneMask());   // batalkan kontrol relay manual yang mungkin masih menyala
      if (L->resumePending) {
        L->resumePending = false;
        resumeClear(L->ws);
      }
      startProses(c.arg, c.waitSec, c.finishAt, -1);
      break;
    case MC_RUN_MANUAL: {
      if (rejectIfBusy() || !tahapAllowed(c.tahap.type, ws)) break;
      relayOffMask(laneMask());
      char name[28];
      snprintf(name, sizeof(name), "Manual: %s", tahapLabel(c.tahap.type));
      startSingle(c.tahap, name, false);
      break;
    }
    case MC_RUN_RELAYTEST: {
      if (rejectIfBusy() || c.arg >= R_COUNT) break;
      relayOffMask(laneMask());
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
      if (lanes[WS_KERING].mstate != MS_IDLE) {
        lanef(LOG_WARN, "Perintah ditolak: pengering sedang bekerja");
        break;
      }
      relayAllOff();
      startRelaySeq(c.arg);
      break;
    case MC_STOP:
      stopLane();
      break;
    case MC_PAUSE:
      if (L->mstate == MS_RUNNING) doPause();
      else lanef(LOG_WARN, "Tidak ada Proses yang berjalan");
      break;
    case MC_RESUME:
      if (L->mstate == MS_PAUSED) {
        doResume();
      } else if (L->mstate == MS_DECIDE) {
        lanef(LOG_WARN, "Mesin menunggu keputusan: pilih %s", optsText(L->dec.opts).c_str());
      } else if (L->mstate == MS_IDLE && L->resumePending && !holdFlag && !(ws == WS_KERING && seqRunning())) {
        relayOffMask(laneMask());
        startProses(L->resumeInfo.slot, 0, -1, L->resumeInfo.stepIndex);
      } else {
        lanef(LOG_WARN, "Tidak ada Proses yang sedang dijeda");
      }
      break;
    case MC_SKIP:
      if (L->mstate == MS_IDLE) {
        lanef(LOG_WARN, "Tidak ada Proses yang berjalan");
      } else if (L->waitingStart) {
        L->waitingStart = false;
        lanef(LOG_STEP, "Penantian dilewati, Proses dimulai sekarang");
        stepBegin();
      } else {
        relayOffMask(laneMask());
        lanef(LOG_STEP, "Tahap dilewati");
        L->mstate = MS_RUNNING;
        L->dec.code = DC_NONE;
        L->pauseReason[0] = 0;
        stepFinished();
      }
      break;
    case MC_DECIDE:
      if (L->mstate != MS_DECIDE) {
        lanef(LOG_WARN, "Tidak ada keputusan yang menunggu");
      } else if (!(L->dec.opts & c.arg) || !c.arg) {
        lanef(LOG_WARN, "Pilihan tidak tersedia untuk keputusan ini");
      } else {
        applyDecision(c.arg);
      }
      break;
    case MC_CHECK_WATER:
      if (!waterCheckOn) {
        waterCheckOn = true;
        waterCheckMs = 0;
      }
      break;
    case MC_RELAY:
      if (c.arg >= R_COUNT) break;
      if (L->mstate != MS_IDLE || holdFlag || seqRunning()) {
        lanef(LOG_WARN, "Kontrol relay manual hanya saat mesin siap");
      } else if (c.arg2) {
        if (relaySet((RelayId)c.arg, true))
          lanef(LOG_INFO, "Relay %s ON (manual, otomatis mati %d dtk)", relayName((RelayId)c.arg), MANUAL_MAX_ON_MS / 1000);
      } else {
        relaySet((RelayId)c.arg, false);
        lanef(LOG_INFO, "Relay %s OFF (manual)", relayName((RelayId)c.arg));
      }
      break;
    case MC_CANCEL_RESUME:
      if (L->resumePending) {
        L->resumePending = false;
        resumeClear(L->ws);
        lanef(LOG_INFO, "Pemulihan Proses dibatalkan");
      }
      break;
  }
}

// ============================================================================
// Snapshot status
// ============================================================================
static uint32_t currentStepLeftMs() {
  if (L->waitingStart) return L->waitLeft;
  const Tahap& t = L->job.t[L->idx];
  switch (t.type) {
    case TH_RENDAM: case TH_TUNGGU: case TH_PUTAR: case TH_SPIN:
      return L->R.left;
    case TH_CEK: {   // tampilkan hitung mundur waktu konfirmasi, bukan batas terlama
      uint32_t waited = checkWindowMs() - L->R.left;
      return waited < stableMs() ? stableMs() - waited : L->R.left;
    }
    case TH_UJI_RELAY:
      return L->R.left + (L->R.phase == 0 ? secToMs(t.detik ? t.detik : 2) : 0);
    case TH_PEWANGI: case TH_BUANG_MANUAL:
      return L->R.left;
    case TH_SOAK: {
      uint32_t doneSessions = L->R.attempt;
      uint32_t remainingAfter = t.ulangi > doneSessions + 1 ? t.ulangi - doneSessions - 1 : 0;
      return L->R.left + (L->R.sub == 0 ? minToMs(t.rendam) : 0) + remainingAfter * minToMs(t.putar + t.rendam);
    }
    case TH_ISI: {
      uint32_t est = estimateStepMs(t);
      return est > L->R.elapsed ? est - L->R.elapsed : 0;
    }
    case TH_BUANG: {
      uint32_t est = estimateStepMs(t);
      return est > L->D.pullElapsed ? est - L->D.pullElapsed : 0;
    }
  }
  return 0;
}

static void publishStatus() {
  MachineStatus s;
  memset(&s, 0, sizeof(s));
  s.ws = L->ws;
  s.relaySeq = L->mstate != MS_IDLE && L->seq;
  s.state = L->mstate;
  s.slot = NO_SLOT;
  s.resumeSlot = NO_SLOT;
  s.uptimeSec = millis() / 1000;
  s.waterRaw = sensorRaw();
  s.waterState = sensorStable(true, stableMs()) ? 1 : sensorStable(false, stableMs()) ? 0 : 2;
  s.resumePending = L->resumePending;
  if (L->resumePending) s.resumeSlot = L->resumeInfo.slot;
  s.relayFault = relayFaulted();
  strlcpy(s.resumeInfo, L->resumeText, sizeof(s.resumeInfo));
  for (int i = 0; i < R_COUNT; i++) s.relays[i] = relayIsOn((RelayId)i);
  if (L->mstate != MS_IDLE) {
    s.slot = L->jobManual || L->jobTest ? NO_SLOT : L->jobSlot;
    s.manual = L->jobManual || L->jobTest;
    strlcpy(s.name, L->job.name, sizeof(s.name));
    s.stepIndex = L->waitingStart ? 0 : L->idx + 1;
    s.stepCount = L->job.count;
    stageLabel(s.stage, sizeof(s.stage));
    if (!L->waitingStart) strlcpy(s.tahap, L->job.t[L->idx].type == TH_UJI_RELAY ? "uji" : tahapKey(L->job.t[L->idx].type), sizeof(s.tahap));
    strlcpy(s.detail, L->detailTxt, sizeof(s.detail));
    strlcpy(s.pauseReason, L->pauseReason, sizeof(s.pauseReason));
    if (L->mstate == MS_DECIDE) {
      s.decision = L->dec.code;
      s.decisionOpts = L->dec.opts;
    }
    uint32_t cur = currentStepLeftMs();
    uint32_t total = cur;
    for (uint8_t i = L->waitingStart ? 0 : L->idx + 1; i < L->job.count; i++) total += estimateStepMs(L->job.t[i]);
    s.stepLeftSec = cur / 1000;
    s.totalLeftSec = total / 1000;
    if (L->progTotalEst > 0) {
      uint32_t pct = 100 - (uint32_t)((uint64_t)min(total, L->progTotalEst) * 100 / L->progTotalEst);
      s.progressPct = pct > 99 ? 99 : pct;
    }
  }
  if (xSemaphoreTake(statusMutex, 0) == pdTRUE) {
    snaps[L->ws] = s;
    xSemaphoreGive(statusMutex);
  }
}

// ============================================================================
// Task & API publik
// ============================================================================
// Satu langkah workspace aktif
static void laneTick(uint32_t now, uint32_t dt) {
  if (L->mstate == MS_RUNNING && L->waitingStart) {
    if (L->waitLeft <= dt) {
      L->waitingStart = false;
      lanef(LOG_STEP, "Waktu mulai tiba");
      stepBegin();
    } else {
      L->waitLeft -= dt;
    }
  } else if (L->mstate == MS_RUNNING) {
    SR r = tickStep(dt);
    switch (r) {
      case SR_DONE: stepFinished(); break;
      case SR_DECIDE: enterDecide(); break;
      case SR_FAIL: endProgram(RES_ERROR, L->failMsg); break;
      default: break;
    }
  } else if (L->mstate == MS_DECIDE && now - L->dec.remindMs >= minToMs(DECISION_REMIND_MIN)) {
    L->dec.remindMs = now;
    lanef(LOG_ERROR, "Masih menunggu keputusan: %s. Buka dashboard: %s", L->dec.msg, optsText(L->dec.opts).c_str());
  }
}

// Satu iterasi loop mesin (dipakai task asli dan harness uji di host).
static void machineLoopOnce(uint32_t& lastTick) {
  sensorUpdate();

  MachineCmd c;
  while (xQueueReceive(cmdQueue, &c, 0) == pdTRUE) processCmd(c);

  uint32_t now = millis();
  uint32_t dt = now - lastTick;
  lastTick = now;

  // Failsafe relay: pelanggaran interlock menghentikan keduanya, selain itu hanya workspace pemilik relay
  uint8_t guard = relayGuard();
  for (uint8_t w = 0; guard && w < WS_COUNT; w++) {
    useLane(w);
    bool hit = guard == 2 || (guard == 4 ? (w == WS_KERING || L->seq) : w == WS_CUCI);
    if (hit && L->mstate != MS_IDLE)
      endProgram(RES_ERROR, guard == 2   ? "pelanggaran interlock relay"
                            : guard == 3 ? "pompa pewangi menyala terlalu lama"
                                         : "motor menyala terlalu lama");
  }

  waterCheckTick(dt);

  // Relay manual otomatis mati setelah MANUAL_MAX_ON_MS (hanya saat workspace pemiliknya IDLE)
  for (int i = 0; i < R_COUNT; i++) {
    useLane(seqRunning() ? (uint8_t)WS_CUCI : machineRelayWs(i));
    if (L->mstate != MS_IDLE || !relayIsOn((RelayId)i)) {
      manualSince[i] = 0;
    } else if (manualSince[i] == 0) {
      manualSince[i] = now ? now : 1;
    } else if (now - manualSince[i] >= MANUAL_MAX_ON_MS) {
      relaySet((RelayId)i, false);
      manualSince[i] = 0;
      lanef(LOG_WARN, "Relay %s otomatis dimatikan (batas waktu manual)", relayName((RelayId)i));
    }
  }

  for (uint8_t w = 0; w < WS_COUNT; w++) {
    useLane(w);
    laneTick(now, dt);
  }
  for (uint8_t w = 0; w < WS_COUNT; w++) {   // setelah semua tick: relay di kedua snapshot konsisten
    useLane(w);
    publishStatus();
  }
}

// Pemulihan listrik padam: hanya menawarkan, tidak pernah lanjut otomatis.
// Dibatalkan bila Proses sudah berubah (rev berbeda) atau hilang.
static void loadResume() {
  for (uint8_t w = 0; w < WS_COUNT; w++) {
    useLane(w);
    if (!resumeLoad(L->resumeInfo, w)) continue;
    const ResumeInfo& ri = L->resumeInfo;
    if (ri.slot < PROSES_SLOTS && prosesLoad(prosesId(w, ri.slot), L->job) && L->job.rev == ri.rev &&
        ri.stepIndex < L->job.count) {
      snprintf(L->resumeText, sizeof(L->resumeText), "%s \xE2\x80\xA2 Tahap %u: %s", L->job.name, (unsigned)ri.stepIndex + 1,
               tahapLabel(L->job.t[ri.stepIndex].type));
      L->resumePending = true;
      lanef(LOG_WARN, "Listrik sempat padam saat %s", L->resumeText);
    } else {
      resumeClear(w);
    }
    memset(&L->job, 0, sizeof(L->job));
  }
}

#ifdef HOST_TEST
static uint32_t hostLastTick = 0;
void machineHostTick() { machineLoopOnce(hostLastTick); }
void machineHostReset() {
  waterCheckOn = false;
  for (uint8_t w = 0; w < WS_COUNT; w++) {
    lanes[w] = Lane();
    useLane(w);
  }
  memset(manualSince, 0, sizeof(manualSince));
  hostLastTick = millis();
}
// Meniru boot setelah listrik padam: baca ulang data pemulihan dari penyimpanan.
void machineHostReboot() {
  machineHostReset();
  loadResume();
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
  memset(snaps, 0, sizeof(snaps));
  for (uint8_t w = 0; w < WS_COUNT; w++) {
    useLane(w);
    snaps[w].ws = w;
    snaps[w].slot = snaps[w].resumeSlot = NO_SLOT;
  }
  loadResume();

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

bool machineStartProses(uint8_t slot, uint32_t waitSec, int finishAtMin, uint8_t ws) {
  MachineCmd c;
  memset(&c, 0, sizeof(c));
  c.type = MC_RUN;
  c.arg = slot;
  c.ws = ws;
  c.waitSec = waitSec;
  c.finishAt = (int16_t)finishAtMin;
  return machinePost(c);
}

bool machineRunManual(const Tahap& t, uint8_t ws) {
  MachineCmd c;
  memset(&c, 0, sizeof(c));
  c.type = MC_RUN_MANUAL;
  c.ws = ws;
  c.finishAt = -1;
  c.tahap = t;
  return machinePost(c);
}

void machineGetStatus(MachineStatus& out, uint8_t ws) {
  if (ws >= WS_COUNT) ws = WS_CUCI;
  if (xSemaphoreTake(statusMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    out = snaps[ws];
    xSemaphoreGive(statusMutex);
  } else {
    out = snaps[ws];
  }
}

// Dipanggil dari task lain: baca lanes[] langsung, jangan sentuh L
bool machineCopyJob(Proses& out, uint8_t& idxOut, bool& waiting, uint32_t& waitLeftMs, uint8_t& slot, bool& manual, bool& test,
                    uint8_t ws) {
  if (ws >= WS_COUNT) return false;
  if (jobMutex && xSemaphoreTake(jobMutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  const Lane& l = lanes[ws];
  bool active = l.mstate != MS_IDLE && l.job.count > 0;
  if (active) {
    out = l.job;
    idxOut = l.idx;
    waiting = l.waitingStart;
    waitLeftMs = l.waitLeft;
    slot = l.jobSlot;
    manual = l.jobManual;
    test = l.jobTest;
  }
  if (jobMutex) xSemaphoreGive(jobMutex);
  return active;
}

bool machineIsIdle(uint8_t ws) {
  MachineStatus s;
  machineGetStatus(s, ws);
  return s.state == MS_IDLE;
}

bool machineIsIdle() { return machineIsIdle(WS_CUCI) && machineIsIdle(WS_KERING); }

bool machineSlotInUse(uint8_t id) {
  if (id >= PROSES_TOTAL) return false;
  MachineStatus s;
  machineGetStatus(s, id / PROSES_SLOTS);
  uint8_t slot = id % PROSES_SLOTS;
  if (s.state != MS_IDLE && s.slot == slot) return true;
  return s.resumePending && s.resumeSlot == slot;
}

void machineHold(bool hold) { holdFlag = hold; }

String machineStatusLine() {
  static const char* waterTxt[] = {"kosong", "penuh", "mengukur"};
  String out;
  MachineStatus s;
  for (uint8_t w = 0; w < WS_COUNT; w++) {
    machineGetStatus(s, w);
    out += w == WS_CUCI ? "Cuci: " : "\nPengering: ";
    if (s.state == MS_IDLE) out += "Siap";
    else if (s.state == MS_PAUSED) out += "\xE2\x8F\xB8 DIJEDA \xE2\x80\xA2 " + String(s.pauseReason[0] ? s.pauseReason : "oleh pengguna");
    else if (s.state == MS_DECIDE) out += "\xE2\x9D\x93 MENUNGGU KEPUTUSAN \xE2\x80\xA2 " + String(s.pauseReason);
    else out += String(s.stage) + " \xE2\x80\xA2 " + s.detail;
  }
  out += "\n\xF0\x9F\x92\xA7 Air: ";
  out += waterTxt[s.waterState];
  return out;
}
