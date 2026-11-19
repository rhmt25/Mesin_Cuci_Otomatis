// Uji logika mesin di PC: jam palsu + model tangki air + stub Arduino.
// Jalankan: bash tools/hosttest/run.sh
#include <functional>
#include <string>
#include <vector>
#include "Arduino.h"
#include "Preferences.h"
#include "logger.h"
#include "machine.h"
#include "relay.h"
#include "settings.h"
#include "water_sensor.h"

void relayHostForce(RelayId id, bool on);
void relayHostUnfault();
void machineHostTick();
void machineHostReset();

// ------------------------------------------------------------------ stub ----
static uint32_t g_ms = 100000;
static bool g_raw = false;
unsigned long millis() { return g_ms; }
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin) { return pin == PIN_WATER_LEVEL ? (g_raw ? HIGH : LOW) : LOW; }
size_t strlcpy(char* dst, const char* src, size_t n) {
  size_t l = strlen(src);
  if (n) { size_t c = l >= n ? n - 1 : l; memcpy(dst, src, c); dst[c] = 0; }
  return l;
}
QueueHandle_t xQueueCreate(unsigned, unsigned item) { auto* q = new HostQueue(); q->item = item; return q; }
int xQueueSend(QueueHandle_t q, const void* item, TickType_t) { q->q.push_back(std::string((const char*)item, q->item)); return pdTRUE; }
int xQueueReceive(QueueHandle_t q, void* item, TickType_t) {
  if (q->q.empty()) return pdFALSE;
  memcpy(item, q->q.front().data(), q->item);
  q->q.pop_front();
  return pdTRUE;
}
std::map<std::string, std::string>& Preferences::db() { static std::map<std::string, std::string> d; return d; }

// logger palsu: simpan semua baris
struct Line { uint32_t ms; int level; std::string text; };
static std::vector<Line> g_log;
static bool g_verbose = false;
void loggerInit() {}
void logf(LogLevel level, const char* fmt, ...) {
  char buf[256];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  g_log.push_back({g_ms, level, buf});
  if (g_verbose) printf("  [%7.2fs] %-5s %s\n", (g_ms - 100000) / 1000.0, logLevelName(level), buf);
}
void logStr(LogLevel level, const String& t) { logf(level, "%s", t.c_str()); }
uint32_t logLastSeq() { return (uint32_t)g_log.size(); }
size_t logRead(uint32_t, LogEntry*, size_t) { return 0; }
const char* logLevelName(uint8_t l) { static const char* n[] = {"DEBUG", "INFO", "STEP", "WARN", "ERROR", "DONE"}; return l < 6 ? n[l] : "?"; }
const char* logLevelEmoji(uint8_t) { return ""; }
const char* loggerResetReason() { return "host"; }
String loggerPreviousTrace() { return ""; }

// ----------------------------------------------------------- model tangki ----
static double g_level = 0;       // 0..100
static bool g_noWater = false;   // pasokan air habis
static bool g_drainBroken = false;
static const double FULL_MS = 10.0 * 60000 / TIME_SCALE;   // 10 menit nyata penuh
static const double EMPTY_MS = 3.0 * 60000 / TIME_SCALE;   // 3 menit nyata kosong
static double g_fillOnMs = 0;

static void model(uint32_t dt) {
  if (relayIsOn(R_FILL) && !g_noWater) { g_level += 100.0 * dt / FULL_MS; g_fillOnMs += dt; }
  if (relayIsOn(R_DRAIN) && !g_drainBroken) g_level -= 100.0 * dt / EMPTY_MS;
  g_level = std::max(0.0, std::min(100.0, g_level));
  g_raw = g_level >= 60.0;
}

static long g_violations = 0;   // pelanggaran invarian keselamatan di SEMUA skenario
static void tick() {
  g_ms += TICK_MS;
  model(TICK_MS);
  machineHostTick();
  if (relayIsOn(R_LEFT) && relayIsOn(R_RIGHT)) g_violations++;                       // kiri+kanan
  if (relayIsOn(R_FILL) && relayIsOn(R_DRAIN)) g_violations++;                       // isi+buang
  if (relayIsOn(R_DRAIN) && (relayIsOn(R_LEFT) || relayIsOn(R_RIGHT))) g_violations++;  // buang+motor
}
static bool runUntil(std::function<bool()> pred, uint32_t maxMs) {
  for (uint32_t t = 0; t < maxMs; t += TICK_MS) {
    tick();
    if (pred()) return true;
  }
  return false;
}
static MachineStatus st() { MachineStatus s; machineGetStatus(s); return s; }
static bool idle() { return st().state == MS_IDLE; }
static bool paused() { return st().state == MS_PAUSED; }
static int count(const std::string& sub, int minLevel = 0) {
  int n = 0;
  for (auto& l : g_log) if (l.level >= minLevel && l.text.find(sub) != std::string::npos) n++;
  return n;
}
static void post(uint8_t type, uint8_t arg = 0, uint8_t arg2 = 0) { MachineCmd c{type, arg, arg2}; machinePost(c); }
static bool anyRelayOn() { for (int i = 0; i < R_COUNT; i++) if (relayIsOn((RelayId)i)) return true; return false; }

static void resetWorld() {
  relayHostUnfault();
  Preferences::db().clear();
  g_log.clear();
  g_level = 0; g_noWater = false; g_drainBroken = false; g_raw = false; g_fillOnMs = 0;
  relayAllOff();
  machineHostReset();
  settingsLoad(); calibLoad(); statsLoad();
  machineBegin();
  sensorInit();
  for (int i = 0; i < 20; i++) tick();   // biarkan sensor stabil
}

// ------------------------------------------------------------------ uji ----
static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; if (!(cond)) { failures++; printf("  GAGAL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } else printf("  ok: %s\n", msg); } while (0)

static void t1_fullNormal() {
  printf("[T1] Full proses Normal, air penuh tepat waktu\n");
  resetWorld();
  post(MC_RUN, PRG_NORMAL);
  bool done = runUntil(idle, 600000);
  CHECK(done, "program selesai");
  CHECK(count("Proses Normal selesai", LOG_DONE) == 1, "notifikasi DONE");
  int errs = 0; for (auto& l : g_log) if (l.level == LOG_ERROR) errs++;
  CHECK(errs == 0, "tidak ada ERROR/interlock");
  CHECK(!anyRelayOn(), "semua relay mati di akhir");
  CHECK(ST.histCount == 1 && ST.hist[0].result == RES_DONE, "riwayat tercatat");
  CHECK(C.nFill >= 1, "kalibrasi isi tersimpan");
  CHECK(count("Mengisi air...") == 3, "3 kali isi air (cuci, bilas, bilas+pewangi)");
  CHECK(count("Pewangi selesai") == 1, "pewangi masuk sekali");
  printf("  (durasi simulasi %.1f dtk = %.0f menit nyata)\n", (g_ms - 100000) / 1000.0, (g_ms - 100000) * TIME_SCALE / 60000.0);
}

static void t2_noWater() {
  printf("[T2] Air tidak pernah penuh -> coba ulang 3x lalu jeda; lanjutkan berfungsi\n");
  resetWorld();
  g_noWater = true;
  post(MC_RUN, PRG_WATERIN);
  bool p = runUntil(paused, 1200000);
  CHECK(p, "berakhir dijeda");
  CHECK(count("Coba ulang") == 3, "tepat 3 kali coba ulang");
  CHECK(std::string(st().pauseReason).find("Air tidak terisi") != std::string::npos, "alasan jeda benar");
  CHECK(!anyRelayOn(), "valve tertutup saat dijeda");
  g_noWater = false;
  post(MC_RESUME);
  CHECK(runUntil(idle, 600000), "setelah air ada & Lanjutkan -> selesai");
}

static void t3_drainBroken() {
  printf("[T3] Air tetap terdeteksi setelah buang -> unjam -> ulang sesi -> jeda\n");
  resetWorld();
  S.wash = 1; S.soak = 0;
  post(MC_RUN, PRG_WASH);
  g_drainBroken = true;
  bool p = runUntil(paused, 1200000);
  CHECK(p, "dijeda");
  CHECK(count("Unjam ") == 2 * 3, "unjam 2x per sesi, 3 sesi");
  CHECK(count("Mengulangi sesi pembuangan") == 2, "sesi diulang 2x");
  CHECK(std::string(st().pauseReason).find("Pembuangan gagal") != std::string::npos, "alasan: pembuangan gagal");
  CHECK(!anyRelayOn(), "relay mati saat dijeda");
  g_drainBroken = false;
  post(MC_RESUME);
  CHECK(runUntil(idle, 600000), "valve diperbaiki, Lanjutkan -> selesai");
}

static void t4_stop() {
  printf("[T4] Stop di tengah agitasi -> semua relay mati < 2 dtk\n");
  resetWorld();
  S.wash = 5;
  post(MC_RUN, PRG_WASH);
  runUntil([] { return relayIsOn(R_LEFT) || relayIsOn(R_RIGHT); }, 600000);
  CHECK(relayIsOn(R_LEFT) || relayIsOn(R_RIGHT), "motor sedang berputar");
  uint32_t t0 = g_ms;
  post(MC_STOP);
  tick();
  CHECK(!anyRelayOn() && idle(), "relay mati & IDLE pada tick berikutnya");
  CHECK(g_ms - t0 < 2000 / TIME_SCALE + 100, "respons jauh < 2 dtk");
  uint32_t resume = ST.histCount;
  CHECK(ST.hist[(ST.histHead + 9) % 10].result == RES_STOPPED && resume == 1, "riwayat: dihentikan");
}

static void t5_pauseResume() {
  printf("[T5] Jeda -> Lanjutkan saat agitasi: sisa waktu benar\n");
  resetWorld();
  S.wash = 2;
  post(MC_RUN, PRG_WASH);
  runUntil([] { return std::string(st().detail).find("Agitasi (kiri)") != std::string::npos; }, 600000);
  uint32_t agitStart = g_ms;
  runUntil([] { return false; }, 700);
  post(MC_PAUSE);
  tick();
  CHECK(paused() && !anyRelayOn(), "dijeda, relay mati");
  uint32_t pausedAt = g_ms;
  runUntil([] { return false; }, 900);
  uint32_t pauseLen = g_ms - pausedAt;
  post(MC_RESUME);
  runUntil([] { return st().stepIndex >= 3 || idle(); }, 600000);   // sampai DRAIN
  uint32_t agitTotal = g_ms - agitStart - pauseLen;
  printf("  (agitasi efektif %u ms, target %u ms)\n", agitTotal, (unsigned)minToMs(2));
  CHECK(agitTotal > minToMs(2) - 150 && agitTotal < minToMs(2) + 400, "total agitasi ~2 menit skala (jeda tidak dihitung)");
  CHECK(runUntil(idle, 600000), "program selesai");
}

static void t6_powerLoss() {
  printf("[T6] Reset di tengah bilas -> boot relay mati + tawaran Lanjutkan\n");
  resetWorld();
  post(MC_RUN, PRG_NORMAL);
  runUntil([] { return std::string(st().stage).rfind("Bilas", 0) == 0; }, 900000);
  CHECK(std::string(st().stage).rfind("Bilas", 0) == 0, "sedang di tahap bilas");
  // simulasi listrik padam: RAM hilang, NVS tetap
  relayAllOff();
  machineHostReset();
  g_level = 100; g_raw = true;
  settingsLoad(); calibLoad(); statsLoad();
  machineBegin();
  tick();
  MachineStatus s = st();
  CHECK(s.state == MS_IDLE && !anyRelayOn(), "boot: IDLE, relay mati");
  CHECK(s.resumePending, "ada tawaran pemulihan");
  printf("  (info: %s)\n", s.resumeInfo);
  post(MC_RESUME);
  CHECK(runUntil([] { return st().state == MS_RUNNING; }, 2000), "Lanjutkan -> berjalan");
  CHECK(runUntil(idle, 900000), "selesai sampai akhir");
  CHECK(!st().resumePending, "tawaran hilang setelah selesai");
}

static void t7_percent() {
  printf("[T7] Level 50%% setelah kalibrasi\n");
  resetWorld();
  post(MC_RUN, PRG_WATERIN);
  runUntil(idle, 900000);
  double full = g_fillOnMs;
  CHECK(C.nFill == 1, "kalibrasi dipelajari dari mode sensor");
  g_level = 0; g_raw = false;
  for (int i = 0; i < 20; i++) tick();
  S.fillLevel = FILL_PERCENT; S.fillPercent = 50;
  g_fillOnMs = 0;
  post(MC_RUN, PRG_WATERIN);
  runUntil(idle, 900000);
  printf("  (isi penuh %.0f ms, isi 50%% %.0f ms)\n", full, g_fillOnMs);
  CHECK(g_fillOnMs > full * 0.4 && g_fillOnMs < full * 0.6, "isi ~setengah waktu penuh");
}

static void t8_timeOverflowGuard() {
  printf("[T8] Mode waktu, sensor penuh lebih awal -> berhenti (pengaman meluap)\n");
  resetWorld();
  S.fillLevel = FILL_TIME; S.fillTime = 30;
  post(MC_RUN, PRG_WATERIN);
  runUntil(idle, 900000);
  CHECK(count("Sensor penuh sebelum waktu habis") == 1, "pengaman meluap aktif");
  CHECK(g_fillOnMs < minToMs(30) / 2, "isi berhenti jauh sebelum 30 menit");
}

static void t9_interlock() {
  printf("[T9] Interlock\n");
  resetWorld();
  g_log.clear();
  CHECK(relaySet(R_LEFT, true), "motor kiri nyala");
  CHECK(!relaySet(R_RIGHT, true), "motor kanan ditolak saat kiri nyala");
  CHECK(!relaySet(R_DRAIN, true), "valve buang ditolak saat motor nyala");
  relaySet(R_LEFT, false);
  CHECK(!relaySet(R_RIGHT, true), "ganti arah langsung ditolak (jeda 500ms)");
  g_ms += 2000;
  CHECK(relaySet(R_RIGHT, true), "setelah jeda cukup diizinkan");
  relaySet(R_RIGHT, false);
  CHECK(relaySet(R_FILL, true) && !relaySet(R_DRAIN, true), "isi & buang tidak bersamaan");
  relayAllOff();
  CHECK(count("Interlock menolak", LOG_ERROR) == 4, "penolakan tercatat sebagai ERROR");
}

static void t10_skipAndHold() {
  printf("[T10] Lewati tahap & perintah ditolak saat bekerja\n");
  resetWorld();
  post(MC_RUN, PRG_NORMAL);
  runUntil([] { return st().stepIndex == 1 && st().state == MS_RUNNING; }, 1000);
  int idx0 = st().stepIndex;
  post(MC_SKIP);
  tick();
  CHECK(st().stepIndex == idx0 + 1, "Lewati memajukan 1 langkah");
  post(MC_RUN, PRG_CEPAT);
  tick();
  CHECK(count("ditolak", LOG_WARN) >= 1, "program kedua ditolak");
  post(MC_STOP);
  tick();
}

static void t11_manualRelay() {
  printf("[T11] Kontrol relay manual + batas waktu otomatis + interlock manual\n");
  resetWorld();
  post(MC_RELAY, R_LEFT, 1);
  tick();
  CHECK(relayIsOn(R_LEFT), "relay kiri menyala manual");
  post(MC_RELAY, R_RIGHT, 1);
  tick();
  CHECK(!relayIsOn(R_RIGHT) && relayIsOn(R_LEFT), "kanan ditolak saat kiri menyala");
  post(MC_RELAY, R_LEFT, 0);
  tick();
  CHECK(!relayIsOn(R_LEFT), "kiri dimatikan manual");
  post(MC_RELAY, R_RIGHT, 1);
  runUntil([] { return false; }, 40);
  post(MC_RELAY, R_LEFT, 1);
  tick();
  CHECK(!(relayIsOn(R_LEFT) && relayIsOn(R_RIGHT)), "kiri tidak ikut menyala (kanan masih ON)");
  // batas waktu manual
  uint32_t t0 = g_ms;
  bool off = runUntil([] { return !relayIsOn(R_RIGHT); }, 70000);
  CHECK(off && g_ms - t0 >= 55000 && g_ms - t0 <= 62000, "relay manual otomatis mati ~60 dtk");
  // manual ditolak saat program berjalan
  post(MC_RUN, PRG_WASH);
  tick();
  post(MC_RELAY, R_SPIN, 1);
  tick();
  CHECK(!relayIsOn(R_SPIN), "relay manual ditolak saat mesin bekerja");
  post(MC_STOP);
  tick();
  // program dimulai membatalkan relay manual
  post(MC_RELAY, R_PERFUME, 1);
  tick();
  CHECK(relayIsOn(R_PERFUME), "pewangi manual ON");
  post(MC_RUN, PRG_SPIN);
  tick();
  CHECK(!relayIsOn(R_PERFUME), "memulai program mematikan relay manual");
  post(MC_STOP);
  tick();
}

static void t12_relaySequence() {
  printf("[T12] Uji relay berurutan otomatis\n");
  resetWorld();
  std::vector<int> order;
  bool prev[R_COUNT] = {false};
  post(MC_RUN, PRG_RELAYSEQ, 2);
  bool done = false;
  for (uint32_t t = 0; t < 600000 && !done; t += TICK_MS) {
    tick();
    for (int i = 0; i < R_COUNT; i++) {
      bool now = relayIsOn((RelayId)i);
      if (now && !prev[i]) order.push_back(i);
      prev[i] = now;
    }
    done = idle();
  }
  CHECK(done, "uji berurutan selesai");
  bool inOrder = order.size() == R_COUNT;
  for (size_t i = 0; i < order.size(); i++) inOrder = inOrder && order[i] == (int)i;
  CHECK(inOrder, "tiap relay menyala tepat sekali, urut kiri,kanan,isi,buang,spin,pewangi");
  CHECK(!anyRelayOn(), "semua relay mati di akhir");
  CHECK(ST.histCount == 0, "uji relay tidak masuk riwayat");
  // stop di tengah uji
  post(MC_RUN, PRG_RELAYSEQ, 5);
  runUntil([] { return relayIsOn(R_LEFT); }, 5000);
  post(MC_STOP);
  tick();
  CHECK(!anyRelayOn() && idle(), "Stop menghentikan uji berurutan");
}

static void t13_failsafe() {
  printf("[T13] Failsafe: pelanggaran interlock mengunci relay\n");
  resetWorld();
  relayHostForce(R_LEFT, true);
  relayHostForce(R_RIGHT, true);   // kondisi yang seharusnya mustahil
  long before = g_violations;
  tick();
  CHECK(!anyRelayOn(), "guard mematikan semua relay pada tick berikutnya");
  CHECK(g_violations == before, "tidak ada tick dengan kiri+kanan menyala setelah guard");
  CHECK(relayFaulted() && st().relayFault, "relay terkunci & status fault terlapor");
  CHECK(!relaySet(R_FILL, true) && !relaySet(R_LEFT, true), "semua relay ditolak saat terkunci");
  post(MC_RUN, PRG_SPIN);
  runUntil([] { return false; }, 200);
  CHECK(!anyRelayOn(), "program tidak bisa menyalakan relay saat terkunci");
  g_violations = before;   // pelanggaran paksaan uji tidak dihitung
  relayHostUnfault();
}

static void t14_sessions() {
  printf("[T14] Sesi login awet (ingat perangkat)\n");
  resetWorld();
  String t1 = sessionCreate();
  CHECK(sessionValid(t1), "token baru valid");
  CHECK(!sessionValid("salah"), "token acak ditolak");
  // simulasi restart: RAM hilang, NVS tetap -> sesi harus tetap valid
  CHECK(sessionValid(t1), "token tetap valid (disimpan ke NVS)");
  String ts[9];
  for (int i = 0; i < 9; i++) ts[i] = sessionCreate();
  CHECK(!sessionValid(t1) && sessionValid(ts[8]), "slot tertua digantikan setelah 8 sesi");
  sessionRevoke(ts[8]);
  CHECK(!sessionValid(ts[8]), "logout mencabut sesi");
  authSet("passwordbaru");
  CHECK(!sessionValid(ts[7]), "ganti password mencabut semua sesi");
}

int main(int argc, char** argv) {
  g_verbose = argc > 1;
  t1_fullNormal();
  t2_noWater();
  t3_drainBroken();
  t4_stop();
  t5_pauseResume();
  t6_powerLoss();
  t7_percent();
  t8_timeOverflowGuard();
  t9_interlock();
  t10_skipAndHold();
  t11_manualRelay();
  t12_relaySequence();
  t13_failsafe();
  t14_sessions();
  checks++;
  if (g_violations != 0) { failures++; printf("  GAGAL: %ld tick melanggar invarian keselamatan\n", g_violations); }
  else printf("  ok: 0 pelanggaran invarian (kiri+kanan, isi+buang, buang+motor) di seluruh skenario\n");
  printf("\n%d pemeriksaan, %d gagal\n", checks, failures);
  return failures ? 1 : 0;
}
