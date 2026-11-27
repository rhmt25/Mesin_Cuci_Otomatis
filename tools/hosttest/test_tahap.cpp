// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
// Uji katalog Tahap, validasi, JSON, estimasi, dan penyimpanan Proses (tanpa perangkat keras).
#include <string>
#include <vector>
#include "Arduino.h"
#include "Preferences.h"
#include "logger.h"
#include "proses_store.h"
#include "settings.h"
#include "tahap.h"

// ------------------------------------------------------------------ stub ----
unsigned long millis() { return 1000; }
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t) { return 0; }
size_t strlcpy(char* dst, const char* src, size_t n) {
  size_t l = strlen(src);
  if (n) { size_t c = l >= n ? n - 1 : l; memcpy(dst, src, c); dst[c] = 0; }
  return l;
}
QueueHandle_t xQueueCreate(unsigned, unsigned item) { auto* q = new HostQueue(); q->item = item; return q; }
int xQueueSend(QueueHandle_t q, const void* item, TickType_t) { q->q.push_back(std::string((const char*)item, q->item)); return pdTRUE; }
int xQueueReceive(QueueHandle_t, void*, TickType_t) { return pdFALSE; }
std::map<std::string, std::string>& Preferences::db() { static std::map<std::string, std::string> d; return d; }
void loggerInit() {}
void logf(LogLevel, const char*, ...) {}
void logStr(LogLevel, const String&) {}
uint32_t logLastSeq() { return 0; }
size_t logRead(uint32_t, LogEntry*, size_t) { return 0; }
const char* logLevelName(uint8_t) { return ""; }
const char* logLevelEmoji(uint8_t) { return ""; }
const char* loggerResetReason() { return "host"; }
String loggerPreviousTrace() { return ""; }

static int checks = 0, failures = 0;
#define CHECK(cond, msg)                                           \
  do {                                                             \
    checks++;                                                      \
    if (!(cond)) { failures++; printf("  GAGAL: %s  [%s:%d]\n", msg, __FILE__, __LINE__); } \
  } while (0)

static bool parseTahap(const std::string& json, Tahap& t, String& err) {
  JsonDocument d;
  if (deserializeJson(d, json) != DeserializationError::Ok) { err = "json"; return false; }
  return tahapFromJson(d.as<JsonObjectConst>(), t, err);
}

static Proses* mk(const char* name, std::initializer_list<uint8_t> types) {
  Proses* p = new Proses;
  memset(p, 0, sizeof(*p));
  strlcpy(p->name, name, sizeof(p->name));
  for (uint8_t ty : types) tahapDefault(ty, p->t[p->count++]);
  return p;
}

// ---------------------------------------------------------------------------
static void t1_defaults() {
  printf("T1 bawaan tiap jenis valid dan sesuai codebase lama\n");
  for (int ty = 0; ty < TH_COUNT; ty++) {
    Tahap t; tahapDefault(ty, t);
    String err;
    CHECK(tahapValidate(t, err, true), (std::string("bawaan valid: ") + tahapKey(ty) + " " + err.c_str()).c_str());
  }
  Tahap t;
  tahapDefault(TH_ISI, t);
  CHECK(t.mode == ISI_SENSOR && t.batas == 1800 && t.lama == 30 && t.coba == 3 && t.tunggu == 15 && t.pra == PRA_BUANG && t.persen == 50, "bawaan Isi");
  tahapDefault(TH_BUANG, t);
  CHECK(t.lama == 5 && t.probe == 45 && t.tarik == 2 && t.sesi == 3, "bawaan Buang");
  tahapDefault(TH_PUTAR, t);
  CHECK(t.lama == 15 && t.pola == POLA_NORMAL, "bawaan Putar");
  tahapDefault(TH_SOAK, t);
  CHECK(t.ulangi == 2 && t.putar == 1 && t.rendam == 4, "bawaan Soak (2 sesi, putar 1, rendam 4)");
  tahapDefault(TH_RENDAM, t); CHECK(t.lama == 4, "bawaan Rendam 4 mnt");
  tahapDefault(TH_SPIN, t); CHECK(t.lama == 10, "bawaan Spin 10 mnt");
  tahapDefault(TH_PEWANGI, t); CHECK(t.cup2 == 2, "bawaan Pewangi 1 cup");
  tahapDefault(TH_TUNGGU, t); CHECK(t.detik == 10, "bawaan Tunggu 10 dtk");
  tahapDefault(TH_CEK, t); CHECK(t.mode == CEK_PENUH, "bawaan Cek penuh");
  tahapDefault(TH_TINDAKAN, t); CHECK(strlen(t.pesan) > 0, "Tindakan punya pesan bawaan");
  tahapDefault(TH_NOTIF, t); CHECK(strlen(t.pesan) > 0, "Notifikasi punya pesan bawaan");
  CHECK(sizeof(Proses) < 4096, "ukuran Proses < 4 KB");
  printf("  sizeof(Tahap)=%zu sizeof(Proses)=%zu\n", sizeof(Tahap), sizeof(Proses));
}

static void t2_roundtrip() {
  printf("T2 JSON bolak-balik untuk semua jenis\n");
  for (int ty = 0; ty < TH_COUNT; ty++) {
    Tahap a; tahapDefault(ty, a);
    JsonDocument d; tahapToJson(a, d.to<JsonObject>());
    std::string s; serializeJson(d, s);
    Tahap b; String err;
    bool ok = parseTahap(s, b, err);
    CHECK(ok, (std::string("parse ") + tahapKey(ty) + " " + s).c_str());
    CHECK(ok && memcmp(&a, &b, sizeof(Tahap)) == 0 || !ok, "struct identik setelah roundtrip");
  }
  Tahap t; String err;
  CHECK(parseTahap("{\"t\":\"isi\",\"mode\":\"waktu\",\"lama\":45,\"pra\":\"jeda\"}", t, err) && t.mode == ISI_WAKTU && t.lama == 45 && t.pra == PRA_JEDA && t.batas == 1800, "kunci yang tidak ada memakai bawaan");
  CHECK(parseTahap("{\"t\":\"pewangi\",\"cup\":0.5}", t, err) && t.cup2 == 1, "pewangi 0,5 cup = cup2 1");
  CHECK(parseTahap("{\"t\":\"pewangi\",\"cup\":10}", t, err) && t.cup2 == 20, "pewangi 10 cup");
  CHECK(parseTahap("{\"t\":\"cek\",\"syarat\":\"tidak_penuh\"}", t, err) && t.mode == CEK_TIDAK_PENUH, "cek tidak_penuh");
  // field yang tidak berlaku untuk mode tidak ikut ditulis
  Tahap w; tahapDefault(TH_ISI, w); w.mode = ISI_WAKTU;
  JsonDocument d; tahapToJson(w, d.to<JsonObject>());
  CHECK(d["batas"].isNull() && !d["lama"].isNull() && d["coba"].isNull(), "mode waktu: hanya lama (tanpa batas/coba)");
}

static void t3_ranges() {
  printf("T3 batas rentang setiap variabel (dari meta)\n");
  String metaS = tahapMetaJson();
  JsonDocument meta;
  CHECK(deserializeJson(meta, metaS.c_str()) == DeserializationError::Ok, "meta JSON valid");
  JsonArray types = meta["types"];
  CHECK(types.size() == TH_COUNT, "meta memuat semua jenis");
  int intFields = 0;
  for (JsonObject ty : types) {
    std::string key = ty["t"].as<const char*>();
    for (JsonObject f : ty["fields"].as<JsonArray>()) {
      std::string k = f["k"].as<const char*>(), kind = f["kind"].as<const char*>();
      if (kind != "int") continue;
      intFields++;
      long mn = f["min"], mx = f["max"];
      Tahap t; String err;
      auto tryv = [&](long v) {
        char buf[200];
        // untuk mode-bergantung, set mode yang membuat field berlaku (validasi tetap menyeluruh)
        snprintf(buf, sizeof(buf), "{\"t\":\"%s\",\"%s\":%ld}", key.c_str(), k.c_str(), v);
        return parseTahap(buf, t, err);
      };
      CHECK(tryv(mn), (key + "." + k + " min diterima").c_str());
      CHECK(tryv(mx), (key + "." + k + " max diterima").c_str());
      CHECK(!tryv(mn - 1), (key + "." + k + " min-1 ditolak").c_str());
      CHECK(!tryv(mx + 1), (key + "." + k + " max+1 ditolak").c_str());
    }
  }
  CHECK(intFields >= 16, "semua field angka teruji");
  Tahap t; String err;
  CHECK(!parseTahap("{\"t\":\"isi\",\"mode\":\"lain\"}", t, err), "mode isi tidak dikenal ditolak");
  CHECK(!parseTahap("{\"t\":\"isi\",\"persen\":40}", t, err), "persen 40 ditolak");
  CHECK(parseTahap("{\"t\":\"isi\",\"persen\":25}", t, err) && parseTahap("{\"t\":\"isi\",\"persen\":75}", t, err), "persen 25 & 75 diterima");
  CHECK(!parseTahap("{\"t\":\"pewangi\",\"cup\":0.3}", t, err), "cup 0,3 ditolak");
  CHECK(!parseTahap("{\"t\":\"pewangi\",\"cup\":0}", t, err), "cup 0 ditolak");
  CHECK(!parseTahap("{\"t\":\"pewangi\",\"cup\":10.5}", t, err), "cup 10,5 ditolak");
  CHECK(!parseTahap("{\"t\":\"putar\",\"pola\":\"zigzag\"}", t, err), "pola tidak dikenal ditolak");
  CHECK(!parseTahap("{\"t\":\"tidakada\"}", t, err), "jenis tidak dikenal ditolak");
  CHECK(!parseTahap("{\"t\":\"putar\",\"lama\":\"abc\"}", t, err), "angka berupa teks ditolak");
  CHECK(!parseTahap("{\"t\":\"tindakan\",\"pesan\":\"\"}", t, err), "pesan kosong ditolak");
  CHECK(!parseTahap("{\"t\":\"notif\",\"pesan\":\"   \"}", t, err), "pesan spasi ditolak");
  std::string p40(40, 'a'), p41(41, 'a');
  CHECK(parseTahap("{\"t\":\"notif\",\"pesan\":\"" + p40 + "\"}", t, err), "pesan 40 karakter diterima");
  CHECK(!parseTahap("{\"t\":\"notif\",\"pesan\":\"" + p41 + "\"}", t, err), "pesan 41 karakter ditolak");
  CHECK(parseTahap("{\"t\":\"buang_manual\",\"lama\":5}", t, err), "buang_manual dikenal di tahapFromJson (menu Manual)");
}

static void t4_proses() {
  printf("T4 validasi Proses & peringatan\n");
  Proses* p = mk("Cuci Harian", {TH_ISI, TH_PUTAR, TH_BUANG});
  String err;
  CHECK(prosesValidate(*p, err), "Proses normal valid");
  String w[8];
  CHECK(prosesWarnings(*p, w, 8) == 0, "urutan normal tanpa peringatan");
  strlcpy(p->name, "", sizeof(p->name));
  CHECK(!prosesValidate(*p, err), "nama kosong ditolak");
  strlcpy(p->name, "                           ", sizeof(p->name));
  CHECK(!prosesValidate(*p, err), "nama spasi ditolak");
  std::string n27(27, 'x'), n28(28, 'x');
  strlcpy(p->name, n27.c_str(), sizeof(p->name));
  CHECK(prosesValidate(*p, err), "nama 27 karakter diterima");
  p->count = 0;
  CHECK(!prosesValidate(*p, err), "0 Tahap ditolak");
  delete p;

  p = mk("Besar", {});
  for (int i = 0; i < PROSES_MAX_TAHAP; i++) tahapDefault(TH_TUNGGU, p->t[p->count++]);
  CHECK(prosesValidate(*p, err), "40 Tahap diterima");
  p->count = PROSES_MAX_TAHAP + 1 > 255 ? 255 : p->count;   // 41 tidak muat array: uji lewat JSON di bawah
  delete p;

  {
    JsonDocument d; d["name"] = "X"; JsonArray a = d["tahap"].to<JsonArray>();
    for (int i = 0; i < PROSES_MAX_TAHAP + 1; i++) { JsonObject o = a.add<JsonObject>(); o["t"] = "tunggu"; }
    Proses q;
    CHECK(!prosesFromJson(d.as<JsonVariantConst>(), q, err), "41 Tahap ditolak (JSON)");
    JsonDocument d2; d2["name"] = "X"; d2["tahap"].to<JsonArray>();
    CHECK(!prosesFromJson(d2.as<JsonVariantConst>(), q, err), "tahap kosong ditolak");
    JsonDocument d3; d3["name"] = "X"; JsonArray a3 = d3["tahap"].to<JsonArray>(); a3.add<JsonObject>()["t"] = "buang_manual";
    CHECK(!prosesFromJson(d3.as<JsonVariantConst>(), q, err), "buang_manual ditolak di dalam Proses");
    JsonDocument d4; d4["name"] = "X"; JsonArray a4 = d4["tahap"].to<JsonArray>(); JsonObject o4 = a4.add<JsonObject>(); o4["t"] = "putar"; o4["lama"] = 0;
    CHECK(!prosesFromJson(d4.as<JsonVariantConst>(), q, err) && err.length() > 0, "rentang salah ditolak dengan pesan");
    printf("  contoh pesan: %s\n", err.c_str());
  }

  // peringatan
  p = mk("A", {TH_PUTAR, TH_ISI});
  CHECK(prosesWarnings(*p, w, 8) >= 2, "Putar sebelum Isi + tidak diakhiri Buang");
  delete p;
  p = mk("C", {TH_ISI, TH_PUTAR, TH_BUANG});
  CHECK(prosesWarnings(*p, w, 8) == 0, "diakhiri Buang: aman");
  delete p;
  p = mk("D", {TH_ISI, TH_PUTAR, TH_BUANG, TH_NOTIF, TH_TINDAKAN});
  CHECK(prosesWarnings(*p, w, 8) == 0, "Notifikasi/Tindakan di akhir tidak dihitung");
  delete p;
  p = mk("E", {TH_PEWANGI, TH_TUNGGU});
  CHECK(prosesWarnings(*p, w, 8) == 1, "Pewangi tanpa Isi (tanpa Isi sama sekali -> tidak peringatan ujung)");
  delete p;
}

static void t5_estimate() {
  printf("T5 estimasi durasi\n");
  Tahap t;
  tahapDefault(TH_SOAK, t); CHECK(tahapEstimateSec(t) == 600, "Soak 2x(1+4) = 10 mnt");
  tahapDefault(TH_PEWANGI, t); CHECK(tahapEstimateSec(t) == 20, "Pewangi 1 cup = 20 dtk");
  t.cup2 = 1; CHECK(tahapEstimateSec(t) == 10, "Pewangi 0,5 cup = 10 dtk");
  tahapDefault(TH_PUTAR, t); CHECK(tahapEstimateSec(t) == 900, "Putar 15 mnt");
  tahapDefault(TH_TUNGGU, t); CHECK(tahapEstimateSec(t) == 10, "Tunggu 10 dtk");
  tahapDefault(TH_NOTIF, t); CHECK(tahapEstimateSec(t) == 0, "Notifikasi 0 dtk");
  tahapDefault(TH_ISI, t);
  C.nFill = 0;
  uint32_t noCal = tahapEstimateSec(t);
  CHECK(noCal >= 600, "Isi tanpa kalibrasi >= 10 mnt");
  C.nFill = 3; C.avgFillMs = 300000.0f;
  CHECK(tahapEstimateSec(t) < noCal, "Isi memakai kalibrasi");
  t.mode = ISI_PERSEN; t.persen = 50;
  uint32_t half = tahapEstimateSec(t);
  t.persen = 75;
  CHECK(tahapEstimateSec(t) > half, "persen lebih besar = lebih lama");
  t.mode = ISI_WAKTU; t.lama = 7;
  CHECK(tahapEstimateSec(t) >= 420 && tahapEstimateSec(t) < 480, "Isi mode waktu = lama");
  Proses* p = mk("E", {TH_ISI, TH_PUTAR, TH_BUANG});
  CHECK(prosesEstimateSec(*p) == tahapEstimateSec(p->t[0]) + tahapEstimateSec(p->t[1]) + tahapEstimateSec(p->t[2]), "total = jumlah Tahap");
  delete p;
  Tahap r; tahapDefault(TH_SOAK, r);
  CHECK(tahapRingkas(r).length() > 5, "ringkasan teks ada");
}

static void t6_store() {
  printf("T6 penyimpanan Proses (atomik, rusak, hapus)\n");
  prosesStoreHostReset();
  CHECK(prosesStoreBegin(), "mount");
  Proses* p = mk("Cuci Harian", {TH_ISI, TH_SOAK, TH_PUTAR, TH_BUANG, TH_RENDAM, TH_PEWANGI});
  p->rev = 3;
  CHECK(prosesSlotState(0) == SLOT_EMPTY, "slot kosong di awal");
  CHECK(prosesSave(0, *p), "simpan");
  CHECK(prosesSlotState(0) == SLOT_OK, "slot OK setelah simpan");
  Proses* q = new Proses;
  CHECK(prosesLoad(0, *q) && strcmp(q->name, "Cuci Harian") == 0 && q->count == 6 && q->rev == 3, "muat kembali (nama, jumlah, rev)");
  CHECK(memcmp(&p->t[1], &q->t[1], sizeof(Tahap)) == 0, "Tahap identik setelah simpan-muat");
  // gagal tulis: berkas lama tetap utuh
  prosesStoreHostFailWrites(true);
  strlcpy(p->name, "Diubah", sizeof(p->name));
  CHECK(!prosesSave(0, *p), "gagal tulis dilaporkan");
  prosesStoreHostFailWrites(false);
  CHECK(prosesLoad(0, *q) && strcmp(q->name, "Cuci Harian") == 0, "berkas lama utuh setelah gagal tulis");
  // rusak
  prosesStoreHostPut(1, "{rusak");
  CHECK(prosesSlotState(1) == SLOT_BAD, "berkas rusak -> SLOT_BAD");
  CHECK(!prosesLoad(1, *q), "berkas rusak tidak termuat");
  prosesStoreHostPut(2, "{\"v\":1,\"name\":\"X\",\"tahap\":[{\"t\":\"putar\",\"lama\":999}]}");
  CHECK(prosesSlotState(2) == SLOT_BAD, "berkas valid JSON tetapi nilai di luar rentang -> SLOT_BAD");
  CHECK(prosesDelete(1) && prosesSlotState(1) == SLOT_EMPTY, "hapus berkas rusak");
  CHECK(prosesDelete(0) && prosesSlotState(0) == SLOT_EMPTY, "hapus");
  CHECK(!prosesLoad(PROSES_SLOTS, *q) && prosesSlotState(PROSES_SLOTS) == SLOT_EMPTY, "slot di luar rentang aman");
  // ukuran kasus terburuk: 40 Tahap Isi dengan semua variabel
  Proses* big = mk("Terburuk", {});
  for (int i = 0; i < PROSES_MAX_TAHAP; i++) { tahapDefault(TH_ISI, big->t[big->count]); big->t[big->count].mode = ISI_SENSOR; big->count++; }
  CHECK(prosesSave(3, *big), "40 Tahap Isi tersimpan");
  size_t sz = prosesStoreHostGet(3).length();
  printf("  ukuran berkas 40 Tahap Isi = %zu B\n", sz);
  CHECK(sz < 6000, "berkas terburuk < 6 KB");
  CHECK(prosesLoad(3, *q) && q->count == 40, "40 Tahap termuat");
  delete p; delete q; delete big;
}

static void t7_workspace() {
  printf("T7 workspace: Tahap tersedia & konversi Spin lama\n");
  CHECK(tahapAllowed(TH_SPIN, WS_KERING) && !tahapAllowed(TH_SPIN, WS_CUCI), "Spin hanya di pengering");
  CHECK(tahapAllowed(TH_TUNGGU, WS_KERING) && tahapAllowed(TH_TINDAKAN, WS_KERING) && tahapAllowed(TH_NOTIF, WS_KERING), "Tunggu/Tindakan/Notifikasi di pengering");
  CHECK(!tahapAllowed(TH_RENDAM, WS_KERING) && !tahapAllowed(TH_ISI, WS_KERING) && !tahapAllowed(TH_CEK, WS_KERING), "Rendam/Isi/Cek tidak di pengering");
  Proses* q = new Proses;
  prosesStoreHostReset();
  prosesStoreHostPut(0, "{\"v\":1,\"name\":\"Lama\",\"rev\":4,\"tahap\":[{\"t\":\"buang\"},{\"t\":\"spin\",\"lama\":3},{\"t\":\"isi\"},{\"t\":\"buang\"},{\"t\":\"spin\"},{\"t\":\"notif\"}]}");
  CHECK(prosesSlotState(0) == SLOT_OK && prosesLoad(0, *q) && q->count == 6 && q->rev == 4, "berkas lama berisi Spin tetap terbaca");
  CHECK(q->t[1].type == TH_TINDAKAN && strcmp(q->t[1].pesan, "Spin dulu, lalu kembalikan baju") == 0, "Spin di tengah -> Minta tindakan");
  CHECK(q->t[4].type == TH_NOTIF && strcmp(q->t[4].pesan, "Pindahkan baju ke tabung spin") == 0, "Spin di akhir -> Notifikasi");
  CHECK(std::string(prosesStoreHostGet(0).c_str()).find("spin\"") != std::string::npos, "berkas tersimpan tidak ditulis ulang");
  uint8_t k = prosesId(WS_KERING, 0);
  Proses* p = mk("Kering", {TH_SPIN, TH_TUNGGU});
  p->ws = WS_KERING;
  String err, w[4];
  CHECK(prosesValidate(*p, err) && prosesWarnings(*p, w, 4) == 0, "Proses pengering valid tanpa peringatan");
  CHECK(prosesSave(k, *p) && prosesLoad(k, *q) && q->ws == WS_KERING && q->t[0].type == TH_SPIN, "simpan & muat slot pengering");
  CHECK(prosesSlotState(0) == SLOT_OK && strcmp(q->name, "Kering") == 0, "slot cuci 1 dan pengering 1 terpisah");
  prosesStoreHostPut(prosesId(WS_KERING, 1), "{\"v\":1,\"name\":\"X\",\"tahap\":[{\"t\":\"isi\"}]}");
  CHECK(prosesSlotState(prosesId(WS_KERING, 1)) == SLOT_BAD, "Isi air ditolak di Proses pengering");
  delete p; delete q;
}

int main(int argc, char** argv) {
  if (argc > 1 && strcmp(argv[1], "--meta") == 0) {   // keluarkan katalog untuk fixture uji UI
    printf("%s", tahapMetaJson().c_str());
    return 0;
  }
  t1_defaults();
  t2_roundtrip();
  t3_ranges();
  t4_proses();
  t5_estimate();
  t6_store();
  t7_workspace();
  printf("\n%d pemeriksaan, %d gagal\n", checks, failures);
  return failures ? 1 : 0;
}
