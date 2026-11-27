// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
// Uji logika mesin di PC: jam palsu + model tangki air + stub Arduino.
// Jalankan: bash tools/hosttest/run.sh
#include <functional>
#include <string>
#include <vector>
#include "Arduino.h"
#include "Preferences.h"
#include "logger.h"
#include "machine.h"
#include "proses_store.h"
#include "tahap.h"
#include "backup.h"
#include "net_policy.h"
#include "remote_policy.h"
#include "relay.h"
#include "settings.h"
#include "water_sensor.h"

void relayHostForce(RelayId id, bool on);
void relayHostUnfault();
void machineHostTick();
void machineHostReset();
void machineHostReboot();

// ------------------------------------------------------------------ stub ----
static uint32_t g_ms = 100000;
static bool g_raw = false;
static int g_force = -1;   // >= 0: paksa pembacaan sensor (cipratan)
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
  g_raw = g_force >= 0 ? g_force != 0 : g_level >= 95.0;   // sensor hanya di paling atas: hanya tahu penuh / tidak penuh
}

static uint32_t g_perfumeOnMs = 0;   // total relay pewangi menyala (ms skala)
static long g_violations = 0;   // pelanggaran invarian keselamatan di SEMUA skenario
static void tick() {
  g_ms += TICK_MS;
  model(TICK_MS);
  machineHostTick();
  if (relayIsOn(R_PERFUME)) g_perfumeOnMs += TICK_MS;
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
static void post(uint8_t type, uint8_t arg = 0, uint8_t arg2 = 0) { MachineCmd c; memset(&c, 0, sizeof(c)); c.type = type; c.arg = arg; c.arg2 = arg2; c.finishAt = -1; machinePost(c); }
static bool anyRelayOn() { for (int i = 0; i < R_COUNT; i++) if (relayIsOn((RelayId)i)) return true; return false; }

static void resetWorld() {
  relayHostUnfault();
  Preferences::db().clear();
  prosesStoreHostReset();
  prosesStoreBegin();
  g_perfumeOnMs = 0;
  g_log.clear();
  g_level = 0; g_noWater = false; g_drainBroken = false; g_raw = false; g_force = -1; g_fillOnMs = 0;
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
  post(MC_RUN_RELAYTEST, R_SPIN);
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
  CHECK(total > 70 && bad == 0, "semua vektor allow/deny cocok dengan kebijakan Worker");
  CHECK(!remoteCmdAllowed("") && !remoteCmdAllowed(nullptr), "perintah kosong ditolak");
}

// ======================= uji Proses & Tahap (v3) =======================
static Tahap TH(uint8_t ty) { Tahap t; tahapDefault(ty, t); return t; }
static void saveP(int slot, const char* name, std::vector<Tahap> ts, uint16_t rev = 1) {
  Proses* p = new Proses;
  memset(p, 0, sizeof(*p));
  strlcpy(p->name, name, sizeof(p->name));
  p->rev = rev;
  for (auto& t : ts) p->t[p->count++] = t;
  prosesSave(slot, *p);
  delete p;
}
static void runP(int slot, uint32_t wait = 0, int fin = -1) { machineStartProses(slot, wait, fin); }
static bool deciding() { return st().state == MS_DECIDE; }
static void choose(uint8_t o) { post(MC_DECIDE, o); tick(); }
static int errCount() { int n = 0; for (auto& l : g_log) if (l.level == LOG_ERROR) n++; return n; }
static void fullTank() { g_level = 100; g_raw = true; for (int i = 0; i < 20; i++) tick(); }
static std::string stageNow() { return st().stage; }

static void n1_fullProcess() {
  printf("[N1] Proses lengkap: Isi, Putar, Pewangi 0,5 cup, Buang\n");
  resetWorld();
  Tahap putar = TH(TH_PUTAR); putar.lama = 1;
  Tahap pw = TH(TH_PEWANGI); pw.cup2 = 1;
  Tahap buang = TH(TH_BUANG); buang.lama = 1;
  saveP(0, "Cuci Harian", {TH(TH_ISI), putar, pw, buang});
  runP(0);
  CHECK(runUntil(idle, 120000), "Proses selesai");
  CHECK(count("Proses Cuci Harian selesai", LOG_DONE) == 1, "notifikasi DONE");
  CHECK(errCount() == 0, "tidak ada ERROR/interlock");
  CHECK(!anyRelayOn(), "semua relay mati di akhir");
  CHECK(ST.histCount == 1 && ST.hist[0].result == RES_DONE && std::string(ST.hist[0].name) == "Cuci Harian", "riwayat memuat nama Proses");
  CHECK(C.nFill >= 1, "kalibrasi isi tersimpan");
  CHECK(count("Mengisi air...") == 1, "isi air sekali");
  CHECK(count("Tahap 3/4: Pewangi mulai") == 1 && count("Tahap 4/4: Buang air mulai") == 1, "tiap Tahap mencatat mulai (notifikasi)");
  CHECK(count("Pewangi selesai") == 1, "pewangi masuk sekali");
  printf("  (pompa pewangi menyala %u ms sim = %u dtk nyata)\n", g_perfumeOnMs, g_perfumeOnMs * TIME_SCALE / 1000);
  CHECK(g_perfumeOnMs >= 140 && g_perfumeOnMs <= 220, "0,5 cup = 10 dtk nyata");
}

static void n2_isiGagal() {
  printf("[N2] Isi gagal -> keputusan Ulang / Lanjut / Batal\n");
  resetWorld();
  g_noWater = true;
  Tahap isi = TH(TH_ISI); isi.batas = 900; isi.coba = 1; isi.tunggu = 1;
  saveP(0, "A", {isi, TH(TH_NOTIF)});
  runP(0);
  CHECK(runUntil(deciding, 200000), "berhenti menunggu keputusan");
  CHECK(count("Coba ulang") == 1, "tepat 1 kali coba ulang");
  MachineStatus s = st();
  CHECK(s.decision == DC_ISI_GAGAL && s.decisionOpts == (OP_ULANG | OP_LANJUT | OP_BATAL), "kode ISI_GAGAL dengan pilihan Ulang/Lanjut/Batal");
  CHECK(std::string(s.pauseReason).find("Air tidak terisi") != std::string::npos, "pesan alasan");
  CHECK(!anyRelayOn(), "valve tertutup saat menunggu");
  CHECK(count("Menunggu keputusan", LOG_ERROR) == 1, "notifikasi ERROR (langsung terkirim)");
  choose(OP_BUANG);
  CHECK(deciding() && count("Pilihan tidak tersedia") == 1, "pilihan yang tidak ada ditolak");
  choose(OP_ULANG);
  CHECK(st().state == MS_RUNNING && count("Keputusan: Ulang") == 1, "Ulang memulai Tahap lagi");
  g_noWater = false;
  CHECK(runUntil(idle, 120000), "setelah air ada: Proses selesai");

  resetWorld();
  g_noWater = true;
  saveP(0, "B", {isi, TH(TH_NOTIF)});
  runP(0);
  runUntil(deciding, 200000);
  choose(OP_LANJUT);
  CHECK(runUntil(idle, 5000), "Lanjut melewati Tahap Isi");
  CHECK(count("Keputusan: Lanjut", LOG_WARN) == 1 && count("Tahap Notifikasi", 0) >= 0, "Lanjut tercatat sebagai peringatan");
  CHECK(ST.hist[0].result == RES_DONE, "Proses dianggap selesai");

  resetWorld();
  g_noWater = true;
  saveP(0, "C", {isi, TH(TH_NOTIF)});
  runP(0);
  runUntil(deciding, 200000);
  choose(OP_BATAL);
  CHECK(idle() && ST.hist[0].result == RES_STOPPED && !anyRelayOn(), "Batal menghentikan Proses (riwayat: dihentikan)");
}

static void n3_praCek() {
  printf("[N3] Pra-cek isi: buang / jeda / pakai\n");
  Tahap isi = TH(TH_ISI);
  resetWorld(); fullTank();
  isi.pra = PRA_BUANG;
  saveP(0, "A", {isi});
  runP(0);
  CHECK(runUntil(idle, 200000) && count("membuang dulu") == 1 && count("Mengisi air...") == 1, "pra=buang: buang otomatis lalu isi");

  resetWorld(); fullTank();
  isi.pra = PRA_PAKAI;
  saveP(0, "B", {isi});
  runP(0);
  CHECK(runUntil(idle, 50000) && count("pengisian dilewati") == 1 && count("Mengisi air...") == 0, "pra=pakai: isi dilewati");

  resetWorld(); fullTank();
  isi.pra = PRA_JEDA;
  saveP(0, "C", {isi});
  runP(0);
  CHECK(runUntil(deciding, 50000), "pra=jeda: menunggu keputusan");
  MachineStatus s = st();
  CHECK(s.decision == DC_PRA_PENUH && s.decisionOpts == (OP_BUANG | OP_PAKAI | OP_BATAL), "pilihan Buang lalu isi / Pakai / Batal");
  choose(OP_ULANG);
  CHECK(deciding() && count("Pilihan tidak tersedia") == 1, "Ulang tidak tersedia pada kasus ini");
  choose(OP_BUANG);
  CHECK(runUntil(idle, 200000) && count("Mengisi air...") == 1 && count("Membuang air...") >= 1, "Buang lalu isi: selesai");

  resetWorld(); fullTank();
  saveP(0, "D", {isi, TH(TH_NOTIF)});
  runP(0);
  runUntil(deciding, 50000);
  choose(OP_PAKAI);
  CHECK(runUntil(idle, 5000) && count("Mengisi air...") == 0 && count("pakai air yang ada") == 1, "Pakai air yang ada: lanjut tanpa isi");
}

static void n4_buangGagal() {
  printf("[N4] Buang gagal: Verifikasi Pengurangan Jumlah Air, Tarik Ulang Valve, sesi, keputusan\n");
  resetWorld(); fullTank();
  Tahap b = TH(TH_BUANG); b.lama = 1; b.probe = 10; b.tarik = 1; b.sesi = 2;
  saveP(0, "A", {b, TH(TH_NOTIF)});
  g_drainBroken = true;
  runP(0);
  CHECK(runUntil(deciding, 60000), "menunggu keputusan");
  CHECK(count("Tarik ulang valve") == 2, "1x Tarik Ulang Valve per sesi, 2 sesi");
  CHECK(count("Mengulangi sesi pembuangan") == 1, "sesi diulang 1x");
  MachineStatus s = st();
  CHECK(s.decision == DC_BUANG_GAGAL && s.decisionOpts == (OP_ULANG | OP_LANJUT | OP_BATAL), "kode BUANG_GAGAL, Ulang/Lanjut/Batal");
  CHECK(!anyRelayOn(), "relay mati saat menunggu");
  g_drainBroken = false;
  choose(OP_ULANG);
  CHECK(runUntil(idle, 120000), "valve diperbaiki, Ulang -> selesai");
  CHECK(count("Air sudah turun, pembuangan selesai") == 1, "cek akhir 'tidak penuh' terpenuhi");

  resetWorld(); fullTank();
  saveP(0, "B", {b, TH(TH_NOTIF)});
  g_drainBroken = true;
  runP(0);
  runUntil(deciding, 60000);
  choose(OP_LANJUT);
  CHECK(runUntil(idle, 5000), "Lanjut meski air masih penuh");
  g_drainBroken = false;
}

static void n5_buangNormal() {
  printf("[N5] Buang normal: lama + cek akhir; tarikan pertama gagal ditangani Tarik Ulang Valve\n");
  resetWorld(); fullTank();
  Tahap b = TH(TH_BUANG); b.lama = 2; b.probe = 30;
  saveP(0, "A", {b});
  runP(0);
  CHECK(runUntil(idle, 120000), "selesai");
  CHECK(g_drainOnMs >= minToMs(2) - 100, "valve ditarik selama minimal 'lama' (2 mnt skala)");
  CHECK(count("Tarikan valve berhasil") == 1 && count("Tarik ulang valve") == 0, "probe berhasil tanpa tarik ulang");

  resetWorld(); fullTank();
  g_pullFail = 1;
  saveP(0, "B", {b});
  runP(0);
  CHECK(runUntil(idle, 120000), "selesai walau tarikan pertama gagal");
  CHECK(count("Valve buang belum menarik") == 1 && count("Tarik ulang valve 1/2") == 1, "tarikan pertama gagal -> Tarik Ulang Valve 1/2");
  CHECK(errCount() == 0, "tanpa ERROR");
}

static void n6_cek() {
  printf("[N6] Cek air; Minta tindakan; Notifikasi\n");
  resetWorld();
  Tahap cek = TH(TH_CEK);
  saveP(0, "C", {cek});
  runP(0);
  CHECK(runUntil(deciding, 5000) && st().decision == DC_CEK_GAGAL, "Cek 'harus penuh' pada tangki kosong -> CEK_GAGAL");
  choose(OP_LANJUT);
  resetWorld(); fullTank();
  cek.mode = CEK_TIDAK_PENUH;
  saveP(0, "D", {cek});
  runP(0);
  CHECK(runUntil(deciding, 5000), "Cek 'harus tidak penuh' pada tangki penuh -> keputusan");
  g_level = 0;
  choose(OP_ULANG);
  CHECK(runUntil(idle, 5000), "setelah air turun: Ulang -> lulus");

  resetWorld();
  Tahap tin = TH(TH_TINDAKAN);
  saveP(0, "E", {tin, TH(TH_NOTIF)});
  runP(0);
  CHECK(runUntil(deciding, 2000) && st().decision == DC_TINDAKAN && st().decisionOpts == (OP_LANJUT | OP_BATAL), "Minta tindakan: Lanjut/Batal");
  int notifs = 0;
  for (auto& l : g_log) if (l.level == LOG_NOTIF && l.text.find("Tambahkan deterjen") != std::string::npos) notifs++;
  CHECK(notifs == 1, "pesan Minta tindakan dikirim sebagai LOG_NOTIF (lolos mode Senyap)");
  choose(OP_ULANG);
  CHECK(deciding(), "Ulang tidak tersedia");
  choose(OP_LANJUT);
  CHECK(runUntil(idle, 2000), "Lanjut -> Notifikasi -> selesai");
  int custom = 0;
  for (auto& l : g_log) if (l.level == LOG_NOTIF && l.text.find("Tahap selesai") != std::string::npos) custom++;
  CHECK(custom == 1, "Tahap Notifikasi mengirim pesan sebagai LOG_NOTIF");
}

static void n7_reminder() {
  printf("[N7] Pengingat keputusan tiap 10 menit; Stop/Jeda saat menunggu\n");
  resetWorld();
  saveP(0, "A", {TH(TH_TINDAKAN)});
  runP(0);
  runUntil(deciding, 2000);
  g_log.clear();
  runUntil([] { return false; }, minToMs(25));
  CHECK(count("Masih menunggu keputusan", LOG_ERROR) == 2, "2 pengingat dalam 25 menit");
  post(MC_PAUSE); tick();
  CHECK(deciding(), "Jeda diabaikan saat menunggu keputusan");
  post(MC_RESUME); tick();
  CHECK(deciding() && count("Mesin menunggu keputusan") == 1, "Lanjutkan diarahkan ke pilihan keputusan");
  post(MC_STOP); tick();
  CHECK(idle() && !anyRelayOn(), "Stop mengakhiri Proses yang menunggu");
}

static void n8_soakRendam() {
  printf("[N8] Soak (ulangi, putar, rendam) dan Rendam\n");
  resetWorld();
  Tahap sk = TH(TH_SOAK); sk.ulangi = 2; sk.putar = 1; sk.rendam = 1;
  saveP(0, "A", {sk});
  runP(0);
  uint32_t t0 = g_ms;
  bool motorSeen = false, offDuringRest = false;
  bool done = false;
  for (uint32_t t = 0; t < 60000 && !done; t += TICK_MS) {
    tick();
    if (relayIsOn(R_LEFT) || relayIsOn(R_RIGHT)) motorSeen = true;
    if (std::string(st().detail).find("rendam") != std::string::npos && !anyRelayOn()) offDuringRest = true;
    done = idle();
  }
  CHECK(done && count("Soak ke 2 selesai") == 1 && count("Soak ke") == 2, "2 sesi soak selesai");
  CHECK(motorSeen && offDuringRest, "motor menyala saat putar, semua mati saat rendam");
  printf("  (durasi %u ms, target %u ms)\n", g_ms - t0, (unsigned)minToMs(4));
  CHECK(g_ms - t0 >= minToMs(4) - 100 && g_ms - t0 <= minToMs(4) + 800, "total = 2 x (putar 1 + rendam 1) mnt");

  resetWorld();
  Tahap rd = TH(TH_RENDAM); rd.lama = 1;
  saveP(0, "B", {rd});
  runP(0);
  tick();
  t0 = g_ms;
  bool anyOn = false;
  while (!idle() && g_ms - t0 < 10000) { tick(); if (anyRelayOn()) anyOn = true; }
  CHECK(idle() && !anyOn && g_ms - t0 >= minToMs(1) - 60 && g_ms - t0 <= minToMs(1) + 300, "Rendam 1 mnt dengan semua relay mati");
}

static void n9_tundaDanSelesaiJam() {
  printf("[N9] Tunda mulai & selesai pada jam\n");
  resetWorld();
  Tahap tg = TH(TH_TUNGGU); tg.detik = 1;
  saveP(0, "A", {tg});
  runP(0, 60);   // tunda 60 dtk nyata = 1 dtk skala
  tick(); tick();
  MachineStatus s = st();
  CHECK(s.state == MS_RUNNING && s.stepIndex == 0 && stageNow() == "Menunggu mulai" && !anyRelayOn(), "menunggu mulai (Tahap 0)");
  CHECK(count("Proses ditunda") == 1, "log tunda");
  CHECK(runUntil([] { return st().stepIndex == 1; }, 3000), "mulai setelah waktu tunda");
  CHECK(runUntil(idle, 3000), "selesai");

  resetWorld();
  Tahap tg2 = TH(TH_TUNGGU); tg2.detik = 600;
  saveP(0, "B", {tg2});
  runP(0, 3600);
  runUntil([] { return false; }, 100);
  post(MC_SKIP); tick();
  CHECK(st().stepIndex == 1 && count("Penantian dilewati") == 1, "Lewati saat menunggu mulai memulai Proses sekarang");
  post(MC_STOP); tick();

  resetWorld();
  Tahap pt = TH(TH_PUTAR); pt.lama = 120;
  saveP(0, "C", {pt});
  time_t now = time(nullptr);
  struct tm lt; localtime_r(&now, &lt);
  int fin = (lt.tm_hour * 60 + lt.tm_min + 1) % 1440;
  runP(0, 0, fin);
  tick();
  CHECK(count("Tidak sempat selesai tepat waktu") == 1 && st().stepIndex == 1, "tidak sempat: Proses dimulai sekarang");
  post(MC_STOP); tick();
}

static void n10_stopPause() {
  printf("[N10] Stop saat putar; Jeda/Lanjutkan menjaga sisa waktu\n");
  resetWorld();
  Tahap pt = TH(TH_PUTAR); pt.lama = 5;
  saveP(0, "A", {pt});
  runP(0);
  runUntil([] { return relayIsOn(R_LEFT) || relayIsOn(R_RIGHT); }, 60000);
  CHECK(relayIsOn(R_LEFT) || relayIsOn(R_RIGHT), "motor berputar");
  uint32_t t0 = g_ms;
  post(MC_STOP); tick();
  CHECK(!anyRelayOn() && idle() && g_ms - t0 < 100, "Stop: relay mati & IDLE pada tick berikutnya");
  CHECK(ST.hist[(ST.histHead + 9) % 10].result == RES_STOPPED, "riwayat: dihentikan");

  resetWorld();
  pt.lama = 2;
  saveP(0, "B", {pt});
  runP(0);
  runUntil([] { return std::string(st().detail).find("Agitasi (kiri)") != std::string::npos; }, 60000);
  uint32_t agitStart = g_ms;
  runUntil([] { return false; }, 700);
  post(MC_PAUSE); tick();
  CHECK(paused() && !anyRelayOn(), "dijeda, relay mati");
  uint32_t pausedAt = g_ms;
  runUntil([] { return false; }, 900);
  uint32_t pauseLen = g_ms - pausedAt;
  post(MC_RESUME);
  runUntil(idle, 60000);
  uint32_t agitTotal = g_ms - agitStart - pauseLen;
  printf("  (agitasi efektif %u ms, target %u ms)\n", agitTotal, (unsigned)minToMs(2));
  CHECK(agitTotal > minToMs(2) - 250 && agitTotal < minToMs(2) + 600, "total putar ~2 menit skala (jeda tidak dihitung)");
}

static void n11_resume() {
  printf("[N11] Listrik padam: tawaran pemulihan, rev berbeda / Proses hilang dibatalkan, kunci slot\n");
  resetWorld();
  Tahap pt = TH(TH_PUTAR); pt.lama = 2;
  Tahap b = TH(TH_BUANG); b.lama = 1;
  saveP(0, "Harian", {TH(TH_ISI), pt, b});
  runP(0);
  CHECK(runUntil([] { return stageNow().rfind("Tahap 2/3", 0) == 0; }, 120000), "sedang di Tahap 2");
  tick();
  CHECK(machineSlotInUse(0) && !machineSlotInUse(1), "slot 1 terkunci saat berjalan, slot lain bebas");
  relayAllOff();
  g_level = 100; g_raw = true;
  machineHostReboot();
  g_log.clear();
  tick();
  MachineStatus s = st();
  CHECK(s.state == MS_IDLE && !anyRelayOn() && s.resumePending && s.resumeSlot == 0, "boot: IDLE, relay mati, tawaran pemulihan");
  CHECK(machineSlotInUse(0), "slot terkunci selama pemulihan tertunda");
  printf("  (info: %s)\n", s.resumeInfo);
  post(MC_RESUME);
  CHECK(runUntil([] { return st().state == MS_RUNNING; }, 2000), "Lanjutkan -> berjalan");
  CHECK(st().stepIndex == 2 && count("Mengisi air...") == 0, "lanjut dari Tahap 2 (Isi tidak diulang)");
  CHECK(runUntil(idle, 120000), "selesai");
  CHECK(!st().resumePending && !machineSlotInUse(0), "tawaran & kunci hilang setelah selesai");

  // Proses diubah (rev berbeda) setelah padam
  resetWorld();
  saveP(0, "Harian", {TH(TH_ISI), pt, b}, 1);
  runP(0);
  runUntil([] { return stageNow().rfind("Tahap 2/3", 0) == 0; }, 120000);
  relayAllOff();
  saveP(0, "Harian", {TH(TH_ISI), pt, b}, 2);
  machineHostReboot();
  tick();
  ResumeInfo ri;
  CHECK(!st().resumePending && !resumeLoad(ri), "rev berbeda: pemulihan dibatalkan & data dibersihkan");

  // Proses dihapus
  resetWorld();
  saveP(0, "Harian", {TH(TH_ISI), pt, b}, 1);
  runP(0);
  runUntil([] { return stageNow().rfind("Tahap 2/3", 0) == 0; }, 120000);
  relayAllOff();
  prosesDelete(0);
  machineHostReboot();
  tick();
  CHECK(!st().resumePending && !resumeLoad(ri), "Proses hilang: pemulihan dibatalkan");

  // Batalkan tawaran
  resetWorld();
  saveP(0, "Harian", {TH(TH_ISI), pt, b}, 1);
  runP(0);
  runUntil([] { return stageNow().rfind("Tahap 2/3", 0) == 0; }, 120000);
  relayAllOff();
  machineHostReboot();
  tick();
  CHECK(st().resumePending, "ada tawaran");
  post(MC_CANCEL_RESUME); tick();
  CHECK(!st().resumePending && !resumeLoad(ri) && !machineSlotInUse(0), "Batalkan membersihkan tawaran & kunci");
}

static void n12_fillModes() {
  printf("[N12] Isi: persen setelah kalibrasi; mode waktu berhenti oleh sensor\n");
  resetWorld();
  saveP(0, "A", {TH(TH_ISI)});
  runP(0);
  runUntil(idle, 120000);
  double full = g_fillOnMs;
  CHECK(C.nFill == 1, "kalibrasi dipelajari dari mode sensor");
  g_level = 0; g_raw = false;
  for (int i = 0; i < 20; i++) tick();
  Tahap pc = TH(TH_ISI); pc.mode = ISI_PERSEN; pc.persen = 50;
  saveP(0, "B", {pc});
  g_fillOnMs = 0;
  runP(0);
  runUntil(idle, 120000);
  printf("  (isi penuh %.0f ms, 50%% %.0f ms)\n", full, g_fillOnMs);
  CHECK(g_fillOnMs > full * 0.4 && g_fillOnMs < full * 0.6, "isi ~setengah waktu penuh");

  resetWorld();
  Tahap wk = TH(TH_ISI); wk.mode = ISI_WAKTU; wk.lama = 30;
  saveP(0, "C", {wk});
  runP(0);
  runUntil(idle, 120000);
  CHECK(count("Sensor penuh sebelum waktu habis") == 1, "pengaman meluap aktif");
  CHECK(g_fillOnMs < minToMs(30) / 2, "isi berhenti jauh sebelum 30 menit");

  resetWorld();
  saveP(0, "D", {pc});
  runP(0);
  runUntil(idle, 120000);
  CHECK(count("Belum ada data kalibrasi") == 1, "persen tanpa kalibrasi memakai sensor (dengan peringatan)");
}

static void n13_skipBusy() {
  printf("[N13] Lewati Tahap; Proses kedua ditolak saat bekerja\n");
  resetWorld();
  Tahap tg = TH(TH_TUNGGU); tg.detik = 600;
  saveP(0, "A", {tg, tg, tg});
  saveP(1, "B", {tg});
  runP(0);
  runUntil([] { return st().stepIndex == 1; }, 1000);
  post(MC_SKIP); tick();
  CHECK(st().stepIndex == 2, "Lewati memajukan 1 Tahap");
  runP(1); tick();
  CHECK(count("ditolak", LOG_WARN) >= 1, "Proses kedua ditolak");
  post(MC_STOP); tick();
  runP(5); tick();
  CHECK(count("kosong atau rusak") == 1 && idle(), "slot kosong tidak dijalankan");
}

static void n19_copyJob() {
  printf("[N19] machineCopyJob: salinan pekerjaan untuk /api/job\n");
  resetWorld();
  Proses* c = new Proses; uint8_t idx = 9, slot = 0; bool waiting = true, manual = true, test = true; uint32_t wl = 0;
  CHECK(!machineCopyJob(*c, idx, waiting, wl, slot, manual, test), "idle: tidak aktif");
  Tahap tg = TH(TH_TUNGGU); tg.detik = 5;
  Tahap pt = TH(TH_PUTAR); pt.lama = 1;
  saveP(0, "Dua Tahap", {tg, pt});
  runP(0); tick();
  CHECK(machineCopyJob(*c, idx, waiting, wl, slot, manual, test), "berjalan: aktif");
  CHECK(std::string(c->name) == "Dua Tahap" && c->count == 2 && idx == 0 && !waiting && slot == 0 && !manual && !test, "nama, jumlah, idx 0, slot 0, bukan manual/uji");
  CHECK(c->t[0].type == TH_TUNGGU && c->t[1].type == TH_PUTAR, "urutan Tahap benar");
  post(MC_SKIP); tick();
  CHECK(machineCopyJob(*c, idx, waiting, wl, slot, manual, test) && idx == 1, "idx maju setelah Lewati");
  post(MC_STOP); tick();
  CHECK(!machineCopyJob(*c, idx, waiting, wl, slot, manual, test), "setelah Stop: tidak aktif");
  runP(0, 120); tick();
  CHECK(machineCopyJob(*c, idx, waiting, wl, slot, manual, test) && waiting && wl > 0 && c->count == 2, "menunggu mulai: waiting + sisa tunggu");
  post(MC_STOP); tick();
  Tahap mt = TH(TH_TUNGGU); mt.detik = 3;
  machineRunManual(mt); tick();
  CHECK(machineCopyJob(*c, idx, waiting, wl, slot, manual, test) && manual && !test && c->count == 1 && slot == NO_SLOT, "manual: 1 Tahap tanpa slot");
  post(MC_STOP); tick();
  post(MC_RUN_RELAYSEQ, 1); tick();
  CHECK(machineCopyJob(*c, idx, waiting, wl, slot, manual, test) && test && c->count == R_COUNT && c->t[0].type == TH_UJI_RELAY, "uji relay: 6 langkah, flag test");
  post(MC_STOP); tick();
  delete c;
}

static void n14_manual() {
  printf("[N14] Manual: satu Tahap, Buang manual, relay manual, uji relay\n");
  resetWorld();
  Tahap tg = TH(TH_TUNGGU); tg.detik = 2;
  machineRunManual(tg);
  CHECK(runUntil([] { return st().state == MS_RUNNING; }, 200) && st().manual && st().slot == NO_SLOT, "Manual berjalan tanpa slot");
  CHECK(std::string(st().stage) == "Manual: Tunggu", "label Manual");
  CHECK(runUntil(idle, 2000), "selesai");
  CHECK(ST.histCount == 1 && std::string(ST.hist[0].name) == "Manual: Tunggu", "riwayat 'Manual: Tunggu'");
  ResumeInfo ri;
  CHECK(!resumeLoad(ri), "Manual tidak membuat data pemulihan");

  resetWorld(); fullTank();
  Tahap bm = TH(TH_BUANG_MANUAL); bm.lama = 1;
  g_drainBroken = true;
  machineRunManual(bm);
  CHECK(runUntil(idle, 5000), "Buang manual selesai tanpa sensor");
  CHECK(g_drainOnMs >= minToMs(1) - 60 && count("Air masih penuh setelah pembuangan manual", LOG_WARN) == 1, "valve ditarik selama waktu; masih penuh -> peringatan");
  g_drainBroken = false;

  resetWorld();
  post(MC_RELAY, R_LEFT, 1); tick();
  CHECK(relayIsOn(R_LEFT), "relay kiri menyala manual");
  post(MC_RELAY, R_RIGHT, 1); tick();
  CHECK(!relayIsOn(R_RIGHT) && relayIsOn(R_LEFT), "kanan ditolak saat kiri menyala");
  post(MC_RELAY, R_LEFT, 0); tick();
  CHECK(!relayIsOn(R_LEFT), "kiri dimatikan manual");
  post(MC_RELAY, R_RIGHT, 1);
  runUntil([] { return false; }, 40);
  post(MC_RELAY, R_LEFT, 1); tick();
  CHECK(!(relayIsOn(R_LEFT) && relayIsOn(R_RIGHT)), "kiri tidak ikut menyala (kanan masih ON)");
  uint32_t t0 = g_ms;
  bool off = runUntil([] { return !relayIsOn(R_RIGHT); }, 70000);
  CHECK(off && g_ms - t0 >= 55000 && g_ms - t0 <= 62000, "relay manual otomatis mati ~60 dtk");
  Tahap pt = TH(TH_PUTAR); pt.lama = 5;
  saveP(0, "A", {pt});
  runP(0); tick();
  post(MC_RELAY, R_PERFUME, 1); tick();
  CHECK(!relayIsOn(R_PERFUME), "relay manual cuci ditolak saat tabung cuci bekerja");
  post(MC_RELAY, R_SPIN, 1); tick();
  CHECK(relayIsOn(R_SPIN), "relay spin manual tetap boleh (pengering siap)");
  post(MC_RELAY, R_SPIN, 0); tick();
  post(MC_STOP); tick();
  post(MC_RELAY, R_PERFUME, 1); tick();
  CHECK(relayIsOn(R_PERFUME), "pewangi manual ON");
  runP(0); tick();
  CHECK(!relayIsOn(R_PERFUME), "memulai Proses mematikan relay manual");
  post(MC_STOP); tick();

  resetWorld();
  post(MC_RUN_RELAYTEST, R_SPIN, 1);
  CHECK(runUntil([] { return relayIsOn(R_SPIN); }, 3000), "uji relay spin menyala");
  CHECK(runUntil(idle, 5000) && ST.histCount == 0, "uji relay selesai, tidak masuk riwayat");
}

static void n15_relaySequence() {
  printf("[N15] Kalibrasi relay berurutan otomatis\n");
  resetWorld();
  std::vector<int> order;
  bool prev[R_COUNT] = {false};
  post(MC_RUN_RELAYSEQ, 2);
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
  CHECK(!anyRelayOn() && ST.histCount == 0, "semua relay mati & tidak masuk riwayat");
  post(MC_RUN_RELAYSEQ, 5);
  runUntil([] { return relayIsOn(R_LEFT); }, 5000);
  post(MC_STOP); tick();
  CHECK(!anyRelayOn() && idle(), "Stop menghentikan uji berurutan");
}

static void n16_pumpFailsafe() {
  printf("[N16] Failsafe pompa pewangi 240 dtk\n");
  resetWorld();
  CHECK(relaySet(R_PERFUME, true), "pompa menyala");
  g_ms += PERFUME_MAX_ON_MS + 1000;
  tick();
  CHECK(!relayIsOn(R_PERFUME) && count("Pompa pewangi menyala terlalu lama", LOG_ERROR) == 1, "dimatikan paksa + ERROR");
  CHECK(!relayFaulted(), "tidak mengunci relay lain");
  // di tengah Proses: Proses dibatalkan
  resetWorld();
  Tahap pw = TH(TH_PEWANGI); pw.cup2 = 20;
  saveP(0, "A", {pw});
  runP(0);
  runUntil([] { return relayIsOn(R_PERFUME); }, 500);
  g_ms += PERFUME_MAX_ON_MS + 1000;
  tick();
  CHECK(idle() && !anyRelayOn() && count("pompa pewangi menyala terlalu lama", LOG_ERROR) >= 1, "Proses dibatalkan saat pompa melewati batas");
}

static void n17_migration() {
  printf("[N17] Migrasi v3: variabel global & Custom lama dibersihkan, notif dipertahankan\n");
  resetWorld();
  Preferences pr;
  pr.begin("settings", false);
  pr.clear();
  pr.putUChar("notif", NOTIF_SENYAP);
  pr.putInt("wash", 99);
  pr.putInt("drainProbe", 9999);
  pr.end();
  Preferences cu; cu.begin("custom", false); cu.putBytes("p0", "xx", 2); cu.end();
  Preferences rs; rs.begin("resume", false); rs.putBool("valid", true); rs.putUChar("prog", 3); rs.end();
  settingsLoad();
  Preferences chk; chk.begin("settings", true);
  CHECK(!chk.isKey("wash") && !chk.isKey("drainProbe") && S.notif == NOTIF_SENYAP, "kunci lama hilang, notif dipertahankan");
  Preferences c2; c2.begin("custom", true);
  CHECK(!c2.isKey("p0"), "Custom Proses lama dihapus");
  ResumeInfo ri;
  CHECK(!resumeLoad(ri), "data pemulihan lama dihapus");
  S.notif = NOTIF_RINGKAS; settingsSave(); settingsLoad();
  CHECK(S.notif == NOTIF_RINGKAS, "pengaturan baru tersimpan (migrasi tidak berulang)");
}

static void wipeUserData() {
  settingsDefaults(S);
  while (wifiCount() > 0) wifiRemove(0);
}

static void n18_backupRestore() {
  printf("[N18] Backup & restore v3: bolak-balik, atomik, validasi, impor cadangan v1\n");
  resetWorld();
  wipeUserData();
  S.notif = NOTIF_SENYAP; S.remoteOn = 0;
  wifiAdd("RumahA", "rahasia-A-123");
  wifiAdd("KantorB", "");
  std::string full = backupBuild(true), slim = backupBuild(false);
  CHECK(full.find("rahasia-A-123") != std::string::npos && slim.find("rahasia-A-123") == std::string::npos, "password WiFi hanya ada di cadangan lengkap");
  CHECK(slim.find("RumahA") != std::string::npos && slim.find("KantorB") != std::string::npos, "cadangan memuat SEMUA SSID (bukan salinan penunjuk yang sama)");
  CHECK(full.find("\"version\":3") != std::string::npos && full.find("custom") == std::string::npos, "versi 3 tanpa Custom Proses");
  wipeUserData();
  RestoreReport dry = backupApply(full.c_str(), full.size(), true);
  CHECK(dry.ok && dry.wifiNetworks == 2 && dry.settingsChanged == 2 && dry.oldIgnored == 0, "dry run: ringkasan benar");
  CHECK(S.notif == NOTIF_LENGKAP && wifiCount() == 0, "dry run tidak mengubah apa pun");
  RestoreReport r = backupApply(full.c_str(), full.size(), false);
  WifiCred w0;
  CHECK(r.ok && S.notif == NOTIF_SENYAP && S.remoteOn == 0 && wifiCount() == 2 && wifiGet(0, w0) && std::string(w0.pass) == "rahasia-A-123", "pemulihan persis");
  wifiAdd("BaruC", "pw-baru-c");
  RestoreReport r2 = backupApply(slim.c_str(), slim.size(), false);
  WifiCred x; bool kept = false;
  for (int i = 0; i < wifiCount(); i++) { wifiGet(i, x); if (std::string(x.ssid) == "RumahA") kept = std::string(x.pass) == "rahasia-A-123"; }
  CHECK(r2.ok && wifiCount() == 2 && kept, "tanpa password di berkas: password lama dipertahankan");
  wipeUserData();
  CHECK(backupApply(slim.c_str(), slim.size(), true).wifiNoPassword == 1, "SSID baru berpassword ditandai perlu diisi manual");

  const char* v1 = "{\"app\":\"mesin-cuci\",\"version\":1,\"settings\":{\"wash\":99999,\"soak\":3,\"notif\":1},"
                   "\"custom\":[{\"slot\":1,\"name\":\"X\",\"items\":[{\"t\":\"wash\",\"a\":5}]}]}";
  RestoreReport r3 = backupApply(v1, strlen(v1), false);
  CHECK(r3.ok && r3.oldIgnored == 3 && S.notif == NOTIF_RINGKAS, "cadangan v1: variabel global & Custom diabaikan (3), notif diterapkan");

  resetWorld(); wipeUserData();
  S.notif = NOTIF_RINGKAS;
  const char* bad[] = {
      "", "bukan json", "{\"app\":\"lain\",\"version\":3}", "{\"app\":\"mesin-cuci\",\"version\":2}",
      "{\"app\":\"mesin-cuci\",\"version\":3,\"settings\":{\"notif\":\"abc\"}}",
      "{\"app\":\"mesin-cuci\",\"version\":3,\"wifi\":[{\"ssid\":\"a\"},{\"ssid\":\"b\"},{\"ssid\":\"c\"},{\"ssid\":\"d\"},{\"ssid\":\"e\"},{\"ssid\":\"f\"}]}",
      "{\"app\":\"mesin-cuci\",\"version\":3,\"wifi\":[{\"ssid\":\"\"}]}",
  };
  bool allRejected = true, untouched = true;
  for (const char* js : bad) {
    RestoreReport rr = backupApply(js, strlen(js), false);
    allRejected = allRejected && !rr.ok;
    untouched = untouched && S.notif == NOTIF_RINGKAS && wifiCount() == 0;
  }
  CHECK(allRejected, "berkas rusak/tidak valid ditolak (7 kasus)");
  CHECK(untouched, "penolakan atomik: tidak ada data yang berubah");
  std::string big(RESTORE_MAX_BYTES + 10, ' ');
  CHECK(!backupApply(big.c_str(), big.size(), false).ok, "berkas > 12 KB ditolak");
  const char* clampJs = "{\"app\":\"mesin-cuci\",\"version\":3,\"settings\":{\"notif\":9,\"remoteOn\":0,\"tidakAda\":7}}";
  RestoreReport rc = backupApply(clampJs, strlen(clampJs), false);
  CHECK(rc.ok && rc.clamped == 1 && S.notif == NOTIF_LENGKAP && S.remoteOn == 0 && rc.oldIgnored == 1, "nilai di luar rentang disesuaikan, kunci asing diabaikan");
  wipeUserData();
  for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
    std::string ssid(32, (char)('a' + i)), pw(64, (char)('A' + i));
    wifiAdd(ssid.c_str(), pw.c_str());
  }
  std::string worst = backupBuild(true);
  printf("  (kasus terburuk %zu byte, batas %d)\n", worst.size(), RESTORE_MAX_BYTES);
  CHECK(worst.size() < RESTORE_MAX_BYTES / 2, "cadangan kasus terburuk jauh di bawah batas impor");
  wipeUserData();
  CHECK(backupApply(worst.c_str(), worst.size(), false).ok && wifiCount() == 5, "cadangan kasus terburuk bisa dipulihkan");
  wipeUserData();
}

static MachineStatus stK() { MachineStatus s; machineGetStatus(s, WS_KERING); return s; }
static bool idleK() { return stK().state == MS_IDLE; }
static void runK(int slot) { machineStartProses(slot, 0, -1, WS_KERING); }
static void postK(uint8_t type, uint8_t arg = 0, uint8_t ws = WS_KERING) {
  MachineCmd c; memset(&c, 0, sizeof(c)); c.type = type; c.arg = arg; c.ws = ws; c.finishAt = -1; machinePost(c);
}
static void saveK(int slot, const char* name, std::initializer_list<Tahap> ts) { saveP(prosesId(WS_KERING, slot), name, ts); }
static void ticks(int n) { for (int i = 0; i < n; i++) tick(); }

static void w1_parallel() {
  printf("[W1] Cuci dan pengering berjalan bersamaan, masing-masing berdiri sendiri\n");
  resetWorld(); fullTank();   // air penuh di tabung cuci tidak menghalangi spin
  Tahap putar = TH(TH_PUTAR); putar.lama = 5;
  Tahap sp = TH(TH_SPIN); sp.lama = 2;
  saveP(0, "Cuci", {putar});
  saveK(0, "Kering", {sp});
  runP(0); runK(0);
  ticks(5);
  CHECK(st().state == MS_RUNNING && stK().state == MS_RUNNING, "dua workspace berjalan");
  CHECK(relayIsOn(R_SPIN), "spin menyala tanpa membaca sensor air");
  CHECK(runUntil([] { return relayIsOn(R_LEFT) && relayIsOn(R_SPIN); }, 5000), "motor cuci dan spin menyala bersamaan");
  post(MC_PAUSE); ticks(2);
  CHECK(st().state == MS_PAUSED && !relayIsOn(R_LEFT) && relayIsOn(R_SPIN), "jeda cuci tidak mematikan spin");
  post(MC_RESUME); ticks(2);
  postK(MC_PAUSE); ticks(2);
  CHECK(stK().state == MS_PAUSED && !relayIsOn(R_SPIN) && st().state == MS_RUNNING, "jeda pengering tidak menjeda cuci");
  postK(MC_RESUME); ticks(2);
  CHECK(relayIsOn(R_SPIN), "pengering lanjut: spin menyala lagi");
  post(MC_STOP); ticks(2);
  CHECK(idle() && stK().state == MS_RUNNING && relayIsOn(R_SPIN) && !relayIsOn(R_LEFT) && !relayIsOn(R_RIGHT), "stop cuci saja");
  CHECK(runUntil(idleK, 20000) && !anyRelayOn(), "pengering selesai sendiri");
  CHECK(count("Pengering: Proses Kering selesai", LOG_DONE) == 1, "log pengering diberi awalan");

  runP(0); runK(0); ticks(5);
  postK(MC_STOP, 0, WS_ALL); ticks(2);
  CHECK(idle() && idleK() && !anyRelayOn(), "Stop semua menghentikan keduanya");
}

static void w2_locks() {
  printf("[W2] Kunci relay per workspace\n");
  resetWorld();
  Tahap sp = TH(TH_SPIN); sp.lama = 2;
  saveK(0, "K", {sp});
  runK(0); ticks(3);
  post(MC_RELAY, R_PERFUME, 1); ticks(2);
  CHECK(relayIsOn(R_PERFUME) && stK().state == MS_RUNNING, "relay manual cuci boleh saat pengering bekerja");
  post(MC_RELAY, R_SPIN, 0); ticks(2);
  CHECK(relayIsOn(R_SPIN), "relay spin manual ditolak saat pengering bekerja");
  post(MC_RUN_RELAYSEQ, 1); ticks(3);
  CHECK(idle(), "kalibrasi berurutan ditolak saat pengering bekerja");
  postK(MC_STOP); ticks(2);
  post(MC_RUN_RELAYSEQ, 1); ticks(3);
  runK(0); ticks(3);
  CHECK(st().relaySeq && idleK(), "pengering ditolak selama kalibrasi berurutan");
  post(MC_STOP); ticks(2);
  CHECK(!anyRelayOn(), "semua mati");
}

static void w3_resume() {
  printf("[W3] Listrik padam: kedua workspace menawarkan pemulihan sendiri\n");
  resetWorld();
  Tahap r = TH(TH_RENDAM); r.lama = 5;
  Tahap sp = TH(TH_SPIN); sp.lama = 5;
  saveP(0, "Cuci", {r});
  saveK(0, "Kering", {sp});
  runP(0); runK(0); ticks(5);
  relayAllOff();
  machineHostReboot(); ticks(2);
  CHECK(st().resumePending && stK().resumePending && idle() && idleK() && !anyRelayOn(), "dua tawaran, tidak lanjut otomatis");
  CHECK(machineSlotInUse(0) && machineSlotInUse(prosesId(WS_KERING, 0)), "kedua slot terkunci");
  postK(MC_RESUME); ticks(3);
  CHECK(stK().state == MS_RUNNING && relayIsOn(R_SPIN) && st().resumePending, "lanjutkan pengering saja");
  post(MC_CANCEL_RESUME); ticks(2);
  ResumeInfo ri;
  CHECK(!st().resumePending && !resumeLoad(ri, WS_CUCI) && resumeLoad(ri, WS_KERING), "batalkan cuci tidak menyentuh data pengering");
  postK(MC_STOP); ticks(2);
}

// Paksa sensor terbaca `level` selama `detik` (nyata), lalu kembali ke model tangki
static void splash(bool level, uint32_t detik) {
  g_force = level;
  for (uint32_t t = 0; t < secToMs(detik); t += TICK_MS) tick();
  g_force = -1;
}

static void s1_cipratan() {
  printf("[S1] Konfirmasi sensor air: cipratan diabaikan, lamanya bisa diatur\n");
  resetWorld();
  CHECK(S.airDetik == 10, "bawaan 10 detik");
  S.airDetik = 25; settingsSave(); S.airDetik = 0; settingsLoad();
  CHECK(S.airDetik == 25, "lama konfirmasi tersimpan");
  S.airDetik = 200; settingsSave(); settingsLoad();
  CHECK(S.airDetik == 10, "nilai di luar 1-60 kembali ke bawaan");

  resetWorld();
  saveP(0, "Isi", {TH(TH_ISI)});
  runP(0);
  CHECK(runUntil([] { return relayIsOn(R_FILL); }, 5000), "mengisi");
  splash(true, 6); ticks(3);
  CHECK(relayIsOn(R_FILL), "cipratan 6 dtk saat mengisi: valve tetap terbuka");
  splash(true, 14);
  CHECK(!relayIsOn(R_FILL), "terdeteksi terus lebih dari 10 dtk: pengisian selesai");

  resetWorld();
  S.airDetik = 3;
  saveP(0, "Isi", {TH(TH_ISI)});
  runP(0);
  runUntil([] { return relayIsOn(R_FILL); }, 5000);
  splash(true, 6);
  CHECK(!relayIsOn(R_FILL), "diatur 3 dtk: 6 dtk sudah dianggap penuh");

  resetWorld();
  saveP(0, "Isi", {TH(TH_ISI)});
  runP(0);
  ticks(8);
  splash(true, 6);
  CHECK(runUntil([] { return relayIsOn(R_FILL); }, 5000) && count("membuang dulu") == 0, "cipratan saat pra-cek tidak memicu buang");

  resetWorld();
  Tahap cek = TH(TH_CEK);
  saveP(0, "Cek", {cek});
  runP(0);
  splash(true, 6); ticks(3);
  CHECK(st().state == MS_RUNNING, "Cek 'penuh': cipratan tidak meloloskan");
  CHECK(runUntil(deciding, 5000), "tetap kosong -> keputusan");

  resetWorld(); fullTank();
  cek.mode = CEK_TIDAK_PENUH;
  saveP(0, "Cek", {cek});
  runP(0);
  splash(false, 6); ticks(3);
  CHECK(st().state == MS_RUNNING, "Cek 'tidak penuh': sensor lepas sesaat tidak meloloskan");

  resetWorld(); fullTank();
  g_pullFail = 1;
  Tahap b = TH(TH_BUANG); b.lama = 1;
  saveP(0, "Buang", {b});
  runP(0);
  ticks(10);
  splash(false, 6); ticks(3);
  CHECK(count("Tarikan valve berhasil") == 0, "sensor lepas sesaat tidak dianggap tarikan berhasil");
}

int main(int argc, char** argv) {
  g_verbose = argc > 1;
  n1_fullProcess();
  n2_isiGagal();
  n3_praCek();
  n4_buangGagal();
  n5_buangNormal();
  n6_cek();
  w1_parallel();
  w2_locks();
  w3_resume();
  n7_reminder();
  n8_soakRendam();
  n9_tundaDanSelesaiJam();
  n10_stopPause();
  n11_resume();
  n12_fillModes();
  n13_skipBusy();
  n14_manual();
  n19_copyJob();
  n15_relaySequence();
  n16_pumpFailsafe();
  t9_interlock();
  t13_failsafe();
  t14_devices();
  t15_relayCheck();
  n17_migration();
  t24_connWatchdog();
  t25_apPolicy();
  t26_remotePolicy();
  n18_backupRestore();
  s1_cipratan();
  checks++;
  if (g_violations != 0) { failures++; printf("  GAGAL: %ld tick melanggar invarian keselamatan\n", g_violations); }
  else printf("  ok: 0 pelanggaran invarian (kiri+kanan, isi+buang, buang+motor) di seluruh skenario\n");
  printf("\n%d pemeriksaan, %d gagal\n", checks, failures);
  return failures ? 1 : 0;
}
