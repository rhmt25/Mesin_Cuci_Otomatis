// Uji logika mesin di PC: jam palsu + model tangki air + stub Arduino.
// Jalankan: bash tools/hosttest/run.sh
#include <functional>
#include <string>
#include <vector>
#include "Arduino.h"
#include "Preferences.h"
#include "logger.h"
#include "machine.h"
#include "net_policy.h"
#include "remote_policy.h"
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
static int g_pullFail = 0;        // jumlah tarikan awal yang "gagal": valve ON tetapi air tidak keluar
static bool g_drainPrev = false, g_pullEffective = true;
static double g_drainOnMs = 0;     // total relay buang menyala
static bool g_noWater = false;   // pasokan air habis
static bool g_drainBroken = false;
static const double FULL_MS = 10.0 * 60000 / TIME_SCALE;   // 10 menit nyata penuh
static const double EMPTY_MS = 3.0 * 60000 / TIME_SCALE;   // 3 menit nyata kosong
static double g_fillOnMs = 0;

static void model(uint32_t dt) {
  if (relayIsOn(R_FILL) && !g_noWater) { g_level += 100.0 * dt / FULL_MS; g_fillOnMs += dt; }
  bool drainOn = relayIsOn(R_DRAIN);
  if (drainOn && !g_drainPrev) {   // tarikan baru: mungkin gagal (modul tarik kadang tidak menarik)
    if (g_pullFail > 0) { g_pullFail--; g_pullEffective = false; } else g_pullEffective = true;
  }
  g_drainPrev = drainOn;
  if (drainOn) g_drainOnMs += dt;
  if (drainOn && g_pullEffective && !g_drainBroken) g_level -= 100.0 * dt / EMPTY_MS;
  g_level = std::max(0.0, std::min(100.0, g_level));
  g_raw = g_level >= 95.0;   // sensor hanya di paling atas: hanya tahu penuh / tidak penuh
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
  g_pullFail = 0; g_drainPrev = false; g_pullEffective = true; g_drainOnMs = 0;
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

static void t14_devices() {
  printf("[T14] Perangkat diingat: maks 5, terlama dihapus, bisa dikelola\n");
  resetWorld();
  String first = sessionCreate(true, "HP Android");
  CHECK(sessionValid(first) && sessionSlot(first) >= 0, "perangkat diingat valid");
  String tmp = sessionCreate(false, "Tamu");
  CHECK(sessionValid(tmp) && sessionSlot(tmp) < 0, "sesi sementara valid tapi tidak masuk daftar");
  String ts[6];
  for (int i = 0; i < 5; i++) ts[i] = sessionCreate(true, String("Perangkat ") + (i + 2));
  DeviceInfo list[MAX_DEVICES];
  int n = deviceList(list, MAX_DEVICES, ts[4]);
  CHECK(n == 5, "daftar tetap 5 (batas)");
  CHECK(!sessionValid(first), "perangkat terlama otomatis terhapus saat ke-6 masuk");
  CHECK(sessionValid(ts[0]) && sessionValid(ts[4]), "perangkat lain masih valid");
  bool curOk = false;
  for (int i = 0; i < n; i++) if (list[i].current) curOk = std::string(list[i].name) == "Perangkat 6";
  CHECK(curOk, "perangkat saat ini ditandai");
  int victim = -1;
  for (int i = 0; i < n; i++) if (!list[i].current) { victim = list[i].slot; break; }
  CHECK(victim >= 0 && deviceRemove(victim) && deviceList(list, MAX_DEVICES, ts[4]) == 4, "hapus satu perangkat");
  int removed = deviceRemoveOthers(ts[4]);
  CHECK(removed == 3 && sessionValid(ts[4]), "hapus semua selain perangkat ini");
  sessionRevoke(ts[4]);
  CHECK(!sessionValid(ts[4]), "logout mencabut sesi");
  String again = sessionCreate(true, "X");
  authSet("passwordbaru");
  CHECK(!sessionValid(again) && !sessionValid(tmp), "ganti password mencabut semua sesi");
}

static void t15_relayCheck() {
  printf("[T15] relayCheck: alasan penolakan tanpa mengubah status\n");
  resetWorld();
  CHECK(relayCheck(R_LEFT, true) == nullptr, "kiri boleh saat semua mati");
  relaySet(R_LEFT, true);
  const char* why = relayCheck(R_RIGHT, true);
  CHECK(why != nullptr && std::string(why).find("bersamaan") != std::string::npos, "kanan ditolak saat kiri aktif, dengan alasan");
  CHECK(!relayIsOn(R_RIGHT), "relayCheck tidak menyalakan apa pun");
  CHECK(relayCheck(R_DRAIN, true) != nullptr, "buang ditolak saat motor aktif");
  CHECK(relayCheck(R_RIGHT, false) == nullptr, "mematikan selalu boleh");
  relayAllOff();
}

static CustomProc makeCustom(const char* name, std::vector<CustomItem> items) {
  CustomProc p;
  memset(&p, 0, sizeof(p));
  strlcpy(p.name, name, sizeof(p.name));
  p.count = (uint8_t)items.size();
  for (size_t i = 0; i < items.size(); i++) p.items[i] = items[i];
  return p;
}

static void t16_custom() {
  printf("[T16] Custom Proses: simpan, validasi, jalankan, urutan bebas\n");
  resetWorld();
  // rinse 10 menit lalu rinse lagi 5 menit, lalu spin 2 menit
  CustomItem r10{CI_RINSE, 3, 10, 0, 0, 0}, r5{CI_RINSE, 3, 5, 0, 0, 0}, sp{CI_SPIN, 3, 2, 0, 0, 0};
  CustomProc p = makeCustom("Bilas dua kali", {r10, r5, sp});
  String err;
  CHECK(customValidate(p, err), "proses valid");
  CHECK(customStepCount(p) == 7, "3+3+1 = 7 langkah mesin");
  CHECK(customSave(2, p), "tersimpan di slot 3");
  CustomProc back;
  CHECK(customLoad(2, back) && std::string(back.name) == "Bilas dua kali" && back.count == 3 && back.items[1].a == 5, "dibaca kembali utuh");
  CHECK(!customLoad(0, back), "slot kosong tidak terbaca");

  CustomProc bad = makeCustom("", {r10});
  CHECK(!customValidate(bad, err), "nama kosong ditolak");
  CustomProc bad2 = makeCustom("x", {});
  CHECK(!customValidate(bad2, err), "tanpa langkah ditolak");
  CustomItem zero{CI_WASH, 3, 0, 0, 0, 0};
  CHECK(!customValidate(makeCustom("x", {zero}), err) && err.length() > 0, "menit 0 ditolak dengan pesan");
  std::vector<CustomItem> many(24, CustomItem{CI_RINSEPERFUME, 3, 5, 0, 2, 0});
  CHECK(customValidate(makeCustom("x", many), err) && customStepCount(makeCustom("x", many)) == 96, "24 blok terbesar (96 langkah mesin) masih muat");
  CustomProc over = makeCustom("x", many);
  over.count = 25;
  CHECK(!customValidate(over, err), "lebih dari 24 langkah ditolak");

  g_log.clear();
  post(MC_RUN, PRG_CUSTOM, 2);
  CHECK(runUntil([] { return st().state == MS_RUNNING; }, 1000), "mulai");
  CHECK(std::string(st().name) == "Bilas dua kali", "status memakai nama custom");
  bool done = runUntil(idle, 900000);
  CHECK(done, "selesai");
  CHECK(count("Mengisi air...") == 2, "dua kali bilas = dua kali isi air");
  CHECK(count("Putaran ke") >= 1 && count("Spin") >= 0, "ada agitasi");
  CHECK(count("Proses Bilas dua kali selesai", LOG_DONE) == 1, "DONE memakai nama custom");

  // urutan bebas + pewangi per langkah + tunggu
  CustomItem pf{CI_RINSEPERFUME, 0, 3, 0, 4, 0};   // 2 cup
  CustomItem wt{CI_WAIT, 3, 1, 0, 0, 0};
  CHECK(customSave(0, makeCustom("Campur", {wt, pf})), "simpan proses kedua");
  g_log.clear();
  post(MC_RUN, PRG_CUSTOM, 0);
  runUntil(idle, 900000);
  CHECK(count("Memasukkan pewangi") == 1, "pewangi dimasukkan di bilas+pewangi");
  CHECK(count("Menunggu") >= 1, "langkah tunggu berjalan");

  // slot kosong
  g_log.clear();
  post(MC_RUN, PRG_CUSTOM, 4);
  tick();
  CHECK(idle() && count("kosong atau tidak ditemukan", LOG_WARN) == 1, "slot kosong ditolak dengan pesan");

  // listrik padam di tengah custom -> pulih dengan slot yang benar
  post(MC_RUN, PRG_CUSTOM, 2);
  runUntil([] { return st().stepIndex >= 4; }, 900000);
  relayAllOff(); machineHostReset(); g_level = 100; g_raw = true;
  settingsLoad(); calibLoad(); statsLoad(); machineBegin(); tick();
  CHECK(st().resumePending, "tawaran pulih untuk custom");
  post(MC_RESUME);
  CHECK(runUntil([] { return st().state == MS_RUNNING; }, 2000) && std::string(st().name) == "Bilas dua kali", "lanjut custom yang sama");
  CHECK(runUntil(idle, 900000), "custom pulih sampai selesai");
  customDelete(2); customDelete(0);
  CHECK(!customLoad(2, back), "hapus slot");
}


static void t17_drainNormal() {
  printf("[T17] Pembuangan normal: tarik sampai total waktu, lalu cek air sudah turun\n");
  resetWorld();
  S.wash = 1; S.soak = 0;
  post(MC_RUN, PRG_WASH);
  CHECK(runUntil(idle, 900000), "selesai");
  printf("  (relay buang menyala %.0f ms, target %u ms)\n", g_drainOnMs, (unsigned)minToMs(S.drainTime));
  CHECK(g_drainOnMs > minToMs(S.drainTime) - 100 && g_drainOnMs < minToMs(S.drainTime) + 400, "relay buang menyala ~total waktu buang");
  CHECK(count("Tarikan valve berhasil") == 1, "tarikan dibuktikan berhasil lewat sensor");
  CHECK(count("Valve buang belum menarik", LOG_WARN) == 0 && count("Unjam") == 0, "tanpa unjam");
}

static void t18_firstPullFails() {
  printf("[T18] Tarikan pertama gagal -> lepas, tarik lagi (unjam) -> berhasil\n");
  resetWorld();
  S.wash = 1; S.soak = 0;
  g_pullFail = 1;
  post(MC_RUN, PRG_WASH);
  CHECK(runUntil(idle, 900000), "selesai tanpa jeda");
  CHECK(count("Valve buang belum menarik", LOG_WARN) == 1, "kegagalan tarikan terdeteksi sekali");
  CHECK(count("Unjam 1/2") == 1 && count("Unjam 2/2") == 0, "unjam sekali lalu berhasil");
  CHECK(count("Mengulangi sesi") == 0 && count("Proses Wash selesai", LOG_DONE) == 1, "tidak perlu ulang sesi, proses selesai");
  CHECK(!st().pauseReason[0], "tidak pernah dijeda");
  printf("  (relay buang menyala total %.0f ms)\n", g_drainOnMs);
  CHECK(g_drainOnMs > minToMs(S.drainTime) && g_drainOnMs < minToMs(S.drainTime) + secToMs(S.drainProbeSec) + 500, "total = probe gagal + waktu buang penuh");
}

static void t19_drainTimeMode() {
  printf("[T19] Mode waktu: tanpa probe, tarik total waktu lalu cek akhir\n");
  resetWorld();
  S.wash = 1; S.soak = 0; S.drainMode = DRAIN_TIME;
  post(MC_RUN, PRG_WASH);
  CHECK(runUntil(idle, 900000), "selesai");
  CHECK(count("Tarikan valve berhasil") == 0 && count("belum menarik") == 0, "tidak memakai probe");
  CHECK(g_drainOnMs > minToMs(S.drainTime) - 100 && g_drainOnMs < minToMs(S.drainTime) + 400, "tarik selama waktu buang");
  // mode waktu + valve rusak total -> cek akhir masih penuh -> ulang sesi -> jeda
  resetWorld();
  S.wash = 1; S.soak = 0; S.drainMode = DRAIN_TIME;
  post(MC_RUN, PRG_WASH);
  g_drainBroken = true;
  CHECK(runUntil(paused, 1800000) && count("Mengulangi sesi") == 2, "valve rusak: 3 sesi lalu jeda");
}

static void t20_fillPrecheck() {
  printf("[T20] Pra-cek isi saat air masih penuh: buang / jeda / pakai\n");
  // buang (default)
  resetWorld();
  g_level = 100; g_raw = true;
  for (int i = 0; i < 20; i++) tick();
  CHECK(S.fillPrecheck == PRECHECK_DRAIN, "default = buang dulu");
  post(MC_RUN, PRG_WATERIN);
  CHECK(runUntil(idle, 900000), "selesai");
  CHECK(count("membuang dulu") == 1 && count("Mengisi air...") == 1 && g_drainOnMs > 0, "membuang dulu lalu mengisi");
  // jeda
  resetWorld();
  S.fillPrecheck = PRECHECK_PAUSE;
  g_level = 100; g_raw = true;
  for (int i = 0; i < 20; i++) tick();
  post(MC_RUN, PRG_WATERIN);
  CHECK(runUntil(paused, 60000), "dijeda");
  CHECK(std::string(st().pauseReason).find("Air masih penuh sebelum mengisi") != std::string::npos, "alasan jeda jelas");
  CHECK(!relayIsOn(R_FILL) && !relayIsOn(R_DRAIN) && g_drainOnMs == 0, "tidak ada relay yang digerakkan otomatis");
  post(MC_SKIP);
  tick();
  CHECK(idle(), "Lewati -> proses selesai");
  // pakai
  resetWorld();
  S.fillPrecheck = PRECHECK_USE;
  g_level = 100; g_raw = true;
  for (int i = 0; i < 20; i++) tick();
  post(MC_RUN, PRG_WATERIN);
  CHECK(runUntil(idle, 60000), "selesai cepat");
  CHECK(count("pengisian dilewati") == 1 && g_fillOnMs == 0 && g_drainOnMs == 0, "pengisian dan pembuangan dilewati");
}

static void t21_manualDrainSoft() {
  printf("[T21] Buang manual: gagal tidak menjeda proses\n");
  resetWorld();
  g_level = 100; g_raw = true;
  for (int i = 0; i < 20; i++) tick();
  g_drainBroken = true;
  post(MC_RUN, PRG_WATEROUT);
  CHECK(runUntil(idle, 900000), "selesai tanpa jeda");
  CHECK(count("Air masih penuh setelah pembuangan manual", LOG_WARN) == 1, "peringatan dicatat");
}

static void t22_partialFill() {
  printf("[T22] Isi sebagian (sensor tidak penuh sejak awal): buang tetap berjalan sesuai waktu\n");
  resetWorld();
  S.fillLevel = FILL_TIME; S.fillTime = 3; S.wash = 1; S.soak = 0;
  post(MC_RUN, PRG_WASH);
  CHECK(runUntil(idle, 900000), "selesai");
  CHECK(count("Tarikan valve berhasil") == 0, "keberhasilan tarikan tidak bisa dibuktikan (sensor tidak penuh), dilewati");
  CHECK(g_drainOnMs > minToMs(S.drainTime) - 100, "tetap menarik selama total waktu buang");
}

static void t23_settingsMigration() {
  printf("[T23] Migrasi NVS: kunci pembuangan lama dihapus, pengaturan baru valid\n");
  resetWorld();
  Preferences pr;
  pr.begin("settings", false);
  pr.putInt("drainExtra", 60);
  pr.putInt("drainMax", 5);
  pr.putInt("drainProbe", 9999);   // di luar rentang -> dibersihkan
  pr.end();
  settingsLoad();
  Preferences chk;
  chk.begin("settings", true);
  CHECK(!chk.isKey("drainExtra") && !chk.isKey("drainMax"), "kunci lama dihapus");
  CHECK(S.drainProbeSec == 45, "probe di luar rentang kembali ke default 45");
  S.drainProbeSec = 30; S.fillPrecheck = PRECHECK_PAUSE;
  settingsSave(); settingsLoad();
  CHECK(S.drainProbeSec == 30 && S.fillPrecheck == PRECHECK_PAUSE, "probe & pra-cek tersimpan");
  S.drainProbeSec = 2; settingsSave(); settingsLoad();
  CHECK(S.drainProbeSec == 45, "probe < 5 dtk ditolak (default)");
}

static void t24_connWatchdog() {
  printf("[T24] Watchdog konektivitas: link zombie dipulihkan, router yang memblokir ping tidak memicu apa pun\n");
  ConnWatchdog w; connWdInit(w);
  bool noAct = true;
  for (int i = 0; i < 10; i++) noAct = noAct && connWdReport(w, false) == CONN_NONE;
  CHECK(noAct && !w.armed, "gateway tidak pernah menjawab ping: watchdog tidak aktif (tidak ada reconnect palsu)");
  connWdInit(w);
  CHECK(connWdReport(w, true) == CONN_NONE && w.armed, "ping berhasil -> aktif");
  CHECK(connWdReport(w, false) == CONN_NONE && connWdReport(w, false) == CONN_NONE, "2 gagal berturut-turut belum bertindak");
  CHECK(connWdReport(w, false) == CONN_RECONNECT, "gagal ke-3 -> reconnect");
  connWdReport(w, false); connWdReport(w, false);
  CHECK(connWdReport(w, false) == CONN_RECONNECT, "3 gagal lagi -> reconnect ke-2");
  connWdReport(w, false); connWdReport(w, false);
  CHECK(connWdReport(w, false) == CONN_RESTART_WIFI, "3 reconnect tanpa hasil -> WiFi dimulai ulang");
  connWdInit(w); connWdReport(w, true);
  connWdReport(w, false); connWdReport(w, false);
  CHECK(connWdReport(w, true) == CONN_NONE, "ping berhasil di tengah mereset hitungan");
  CHECK(connWdReport(w, false) == CONN_NONE && connWdReport(w, false) == CONN_NONE, "hitungan mulai dari nol lagi");
}

static void t25_apPolicy() {
  printf("[T25] Hotspot hanya bila semua SSID gagal; mati lagi setelah WiFi pulih\n");
  ApPolicy p; apPolicyInit(p);
  CHECK(!apPolicyStep(p, 1000, true, false, 0), "WiFi tersambung sejak awal: hotspot mati");
  CHECK(!apPolicyStep(p, 60000, true, false, 0), "tetap mati");
  apPolicyInit(p);
  CHECK(!apPolicyStep(p, 1000, false, false, 0), "WiFi belum tersambung tetapi belum semua gagal: hotspot mati");
  CHECK(!apPolicyStep(p, 20000, false, false, 0), "masih mencoba SSID: mati");
  CHECK(apPolicyStep(p, 40000, false, true, 0), "semua SSID gagal: hotspot nyala");
  CHECK(apPolicyStep(p, 90000, false, false, 0), "tetap nyala selama WiFi belum tersambung (walau flag gagal dihapus)");
  CHECK(apPolicyStep(p, 100000, true, false, 1), "WiFi pulih tetapi ada klien di hotspot: tetap nyala");
  CHECK(apPolicyStep(p, 200000, true, false, 1), "klien masih ada: tetap nyala");
  CHECK(apPolicyStep(p, 250000, true, false, 0), "klien pergi, belum 2 menit: tetap nyala");
  CHECK(!apPolicyStep(p, 250000 + AP_IDLE_OFF_MS + 10, true, false, 0), "2 menit tanpa klien setelah WiFi pulih: mati");
  CHECK(!apPolicyStep(p, 900000, true, false, 0), "tetap mati");
  // tidak ada SSID tersimpan
  apPolicyInit(p);
  CHECK(apPolicyStep(p, 500, false, true, 0), "tanpa SSID tersimpan: hotspot langsung nyala");
  // WiFi pulih tanpa klien -> mati setelah 2 menit
  apPolicyInit(p);
  apPolicyStep(p, 0, false, true, 0);
  CHECK(apPolicyStep(p, 10000, true, false, 0), "pulih: belum 2 menit, nyala");
  CHECK(!apPolicyStep(p, 10000 + AP_IDLE_OFF_MS, true, false, 0), "pulih tanpa klien: mati setelah 2 menit");
  // putus lagi -> hotspot hanya bila semua gagal lagi
  CHECK(!apPolicyStep(p, 1000000, false, false, 0), "putus sesaat tanpa semua gagal: hotspot tidak nyala");
}

static void t26_remotePolicy() {
  printf("[T26] Allowlist jarak jauh identik dengan Worker (vektor bersama)\n");
  FILE* f = fopen("cloud/test/allow-vectors.txt", "r");
  CHECK(f != nullptr, "berkas vektor ditemukan");
  if (!f) return;
  char line[256];
  int total = 0, bad = 0;
  while (fgets(line, sizeof(line), f)) {
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (!n || line[0] == '#') continue;
    char kind[16], verdict[8], a[64] = "", b[128] = "";
    int got = sscanf(line, "%15s %7s %63s %127s", kind, verdict, a, b);
    if (got < 2) continue;
    bool expect = strcmp(verdict, "allow") == 0;
    bool actual;
    if (strcmp(kind, "cmd") == 0) actual = remoteCmdAllowed(a);
    else if (strcmp(kind, "apicmd") == 0) actual = remoteApiAllowed("POST", "/api/cmd", a);
    else if (strcmp(kind, "api") == 0) actual = remoteApiAllowed(a, b, nullptr);
    else continue;
    total++;
    if (actual != expect) { bad++; printf("  tidak cocok: %s\n", line); }
  }
  fclose(f);
  printf("  (%d vektor)\n", total);
  CHECK(total > 80 && bad == 0, "semua vektor allow/deny cocok dengan kebijakan Worker");
  CHECK(!remoteCmdAllowed("") && !remoteCmdAllowed(nullptr), "perintah kosong ditolak");
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
  t14_devices();
  t15_relayCheck();
  t16_custom();
  t17_drainNormal();
  t18_firstPullFails();
  t19_drainTimeMode();
  t20_fillPrecheck();
  t21_manualDrainSoft();
  t22_partialFill();
  t23_settingsMigration();
  t24_connWatchdog();
  t25_apPolicy();
  t26_remotePolicy();
  checks++;
  if (g_violations != 0) { failures++; printf("  GAGAL: %ld tick melanggar invarian keselamatan\n", g_violations); }
  else printf("  ok: 0 pelanggaran invarian (kiri+kanan, isi+buang, buang+motor) di seluruh skenario\n");
  printf("\n%d pemeriksaan, %d gagal\n", checks, failures);
  return failures ? 1 : 0;
}
