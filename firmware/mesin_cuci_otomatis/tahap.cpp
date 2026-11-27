// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#include "tahap.h"
#include <stddef.h>
#include "config.h"

// ============================================================================
// Katalog Tahap: satu tabel yang dipakai bersama oleh bawaan, validasi, JSON, dan meta untuk UI.
// ============================================================================
enum FKind : uint8_t { FK_ENUM, FK_INT, FK_CHOICE, FK_CUP, FK_TEXT };

struct Field {
  uint8_t type;           // TahapType
  const char* key;
  const char* label;
  const char* unit;       // "mnt", "dtk", "" ...
  uint8_t kind;
  uint16_t off;           // offsetof di Tahap
  uint8_t size;           // 1, 2, 4 (angka)
  int32_t mn, mx, def;    // rentang & bawaan (FK_CUP: cup x 2)
  const char* opts;       // FK_ENUM: "a|b|c"; FK_CHOICE: "25|50|75"
  const char* optLabels;  // FK_ENUM: label tampilan, dipisah "|"
  uint8_t whenMask;       // bit per nilai Tahap.mode agar field berlaku (0 = selalu)
  const char* fmt;        // "hms" = tampil HH:MM:SS
};

#define OFF(f) ((uint16_t)offsetof(Tahap, f))
#define CP(str) JsonString((str).c_str())   // salin teks ke dokumen JSON

static const Field FIELDS[] = {
    // ---- Isi air
    {TH_ISI, "mode", "Mode pengisian", "", FK_ENUM, OFF(mode), 1, 0, 2, 0, "sensor|persen|waktu",
     "Sensor (sampai penuh)|Persen (dari waktu isi penuh)|Waktu", 0, ""},
    {TH_ISI, "batas", "Batas waktu isi", "dtk", FK_INT, OFF(batas), 4, 60, 18000, 1800, "", "", 0x03, "hms"},
    {TH_ISI, "persen", "Level", "%", FK_CHOICE, OFF(persen), 1, 25, 75, 50, "25|50|75", "", 0x02, ""},
    {TH_ISI, "lama", "Lama isi", "mnt", FK_INT, OFF(lama), 2, 1, 120, 30, "", "", 0x04, ""},
    {TH_ISI, "coba", "Batas Ulang", "kali", FK_INT, OFF(coba), 1, 0, 10, 3, "", "", 0x03, ""},
    {TH_ISI, "tunggu", "Jeda Percobaan", "mnt", FK_INT, OFF(tunggu), 2, 0, 60, 15, "", "", 0x03, ""},
    {TH_ISI, "pra", "Air Penuh Sebelum Isi", "", FK_ENUM, OFF(pra), 1, 0, 2, 0, "buang|jeda|pakai",
     "Buang dulu otomatis|Jeda dan tanya|Pakai air yang ada", 0, ""},
    // ---- Buang air
    {TH_BUANG, "lama", "Lama buang (setelah air terverifikasi turun)", "mnt", FK_INT, OFF(lama), 2, 1, 60, 5, "", "", 0, ""},
    {TH_BUANG, "probe", "Verifikasi Pengurangan Jumlah Air", "dtk", FK_INT, OFF(probe), 2, 10, 180, 45, "", "", 0, ""},
    {TH_BUANG, "tarik", "Tarik Ulang Valve", "kali", FK_INT, OFF(tarik), 1, 0, 5, 2, "", "", 0, ""},
    {TH_BUANG, "sesi", "Batas ulang sesi buang", "kali", FK_INT, OFF(sesi), 1, 1, 10, 3, "", "", 0, ""},
    // ---- Putar mesin
    {TH_PUTAR, "lama", "Lama putar", "mnt", FK_INT, OFF(lama), 2, 1, 120, 15, "", "", 0, ""},
    {TH_PUTAR, "pola", "Pola putar", "", FK_ENUM, OFF(pola), 1, 0, 2, 0, "normal|gentle|hijab",
     "Normal (7 dtk nyala / 4 jeda)|Gentle (5,3 / 7)|Hijab (3 / 9)", 0, ""},
    // ---- Soak
    {TH_SOAK, "ulangi", "Ulangi", "sesi", FK_INT, OFF(ulangi), 2, 1, 20, 2, "", "", 0, ""},
    {TH_SOAK, "putar", "Waktu putar per sesi", "mnt", FK_INT, OFF(putar), 2, 1, 30, 1, "", "", 0, ""},
    {TH_SOAK, "rendam", "Waktu rendam per sesi", "mnt", FK_INT, OFF(rendam), 2, 1, 120, 4, "", "", 0, ""},
    {TH_SOAK, "pola", "Pola putar", "", FK_ENUM, OFF(pola), 1, 0, 2, 0, "normal|gentle|hijab",
     "Normal (7 dtk nyala / 4 jeda)|Gentle (5,3 / 7)|Hijab (3 / 9)", 0, ""},
    // ---- Rendam
    {TH_RENDAM, "lama", "Lama rendam", "mnt", FK_INT, OFF(lama), 2, 1, 600, 4, "", "", 0, ""},
    // ---- Spin
    {TH_SPIN, "lama", "Lama spin", "mnt", FK_INT, OFF(lama), 2, 1, 60, 10, "", "", 0, ""},
    // ---- Pewangi
    {TH_PEWANGI, "cup", "Jumlah pewangi", "cup", FK_CUP, OFF(cup2), 1, 1, 20, 2, "", "", 0, ""},
    // ---- Tunggu
    {TH_TUNGGU, "detik", "Lama tunggu", "dtk", FK_INT, OFF(detik), 4, 1, 86400, 10, "", "", 0, "hms"},
    // ---- Cek air
    {TH_CEK, "syarat", "Syarat", "", FK_ENUM, OFF(mode), 1, 0, 1, 0, "penuh|tidak_penuh",
     "Air harus penuh|Air harus tidak penuh", 0, ""},
    // ---- Minta tindakan & Notifikasi
    {TH_TINDAKAN, "pesan", "Pesan", "", FK_TEXT, OFF(pesan), PESAN_MAX, 1, PESAN_MAX, 0, "", "", 0, ""},
    {TH_NOTIF, "pesan", "Pesan", "", FK_TEXT, OFF(pesan), PESAN_MAX, 1, PESAN_MAX, 0, "", "", 0, ""},
    // ---- Buang manual (hanya menu Manual)
    {TH_BUANG_MANUAL, "lama", "Lama buang (tanpa sensor)", "mnt", FK_INT, OFF(lama), 2, 1, 60, 5, "", "", 0, ""},
};
static const int NFIELDS = sizeof(FIELDS) / sizeof(FIELDS[0]);

static const char* const KEYS[TH_COUNT] = {"isi", "buang", "putar", "soak", "rendam", "spin", "pewangi", "tunggu", "cek",
                                            "tindakan", "notif", "buang_manual"};
static const char* const LABELS[TH_COUNT] = {"Isi air", "Buang air", "Putar mesin", "Soak", "Rendam", "Spin", "Pewangi",
                                             "Tunggu", "Cek air", "Minta tindakan", "Notifikasi", "Buang manual"};
static const char* const DEFAULT_PESAN[TH_COUNT] = {"", "", "", "", "", "", "", "", "", "Tambahkan deterjen",
                                                    "Tahap selesai", ""};

const char* tahapKey(uint8_t type) { return type < TH_COUNT ? KEYS[type] : "?"; }
const char* tahapLabel(uint8_t type) { return type < TH_COUNT ? LABELS[type] : "?"; }

int tahapFromKey(const String& key) {
  for (int i = 0; i < TH_COUNT; i++)
    if (key == KEYS[i]) return i;
  return -1;
}

// ---- workspace --------------------------------------------------------------
bool tahapAllowed(uint8_t type, uint8_t ws) {
  if (type >= TH_COUNT) return false;
  if (ws == WS_KERING) return type == TH_SPIN || type == TH_TUNGGU || type == TH_TINDAKAN || type == TH_NOTIF;
  return type != TH_SPIN;
}
const char* wsName(uint8_t ws) { return ws == WS_KERING ? "pengering" : "cuci"; }
int wsFromName(const String& name) {
  if (name.length() == 0 || name.equalsIgnoreCase("cuci")) return WS_CUCI;
  if (name.equalsIgnoreCase("pengering")) return WS_KERING;
  return -1;
}

// Proses cuci buatan firmware lama boleh berisi Spin. Spin kini milik workspace pengering (baju dipindah dengan tangan):
// di akhir Proses menjadi Notifikasi, di tengah menjadi Minta tindakan agar Proses menunggu baju kembali.
// Berkas tersimpan tidak ditulis ulang; konversi terjadi saat dibaca.
static void convertLegacySpin(Proses& p) {
  bool atEnd = true;
  for (int i = (int)p.count - 1; i >= 0; i--) {
    Tahap& t = p.t[i];
    if (t.type == TH_NOTIF) continue;
    if (t.type != TH_SPIN) {
      atEnd = false;
      continue;
    }
    tahapDefault(atEnd ? TH_NOTIF : TH_TINDAKAN, t);
    strlcpy(t.pesan, atEnd ? "Pindahkan baju ke tabung spin" : "Spin dulu, lalu kembalikan baju", sizeof(t.pesan));
  }
}

// ---- akses generik ---------------------------------------------------------
static int32_t getNum(const Tahap& t, const Field& f) {
  const uint8_t* p = (const uint8_t*)&t + f.off;
  switch (f.size) {
    case 1: return *p;
    case 2: { uint16_t v; memcpy(&v, p, 2); return v; }
    default: { uint32_t v; memcpy(&v, p, 4); return (int32_t)v; }
  }
}
static void setNum(Tahap& t, const Field& f, int32_t v) {
  uint8_t* p = (uint8_t*)&t + f.off;
  switch (f.size) {
    case 1: *p = (uint8_t)v; break;
    case 2: { uint16_t x = (uint16_t)v; memcpy(p, &x, 2); break; }
    default: { uint32_t x = (uint32_t)v; memcpy(p, &x, 4); break; }
  }
}

// Posisi pilihan `val` dalam daftar "a|b|c" (-1 bila tidak ada). Sekaligus dipakai mengambil teks ke-idx.
static int optIndex(const char* opts, const String& val) {
  int idx = 0;
  const char* s = opts;
  while (true) {
    const char* e = strchr(s, '|');
    size_t n = e ? (size_t)(e - s) : strlen(s);
    if (n == val.length() && strncmp(s, val.c_str(), n) == 0) return idx;
    if (!e) break;
    s = e + 1;
    idx++;
  }
  return -1;
}
static String optAt(const char* opts, int idx) {
  const char* s = opts;
  for (int i = 0; i < idx; i++) {
    const char* e = strchr(s, '|');
    if (!e) return String("");
    s = e + 1;
  }
  const char* e = strchr(s, '|');
  size_t n = e ? (size_t)(e - s) : strlen(s);
  char buf[64];
  if (n >= sizeof(buf)) n = sizeof(buf) - 1;
  memcpy(buf, s, n);
  buf[n] = 0;
  return String(buf);
}

static bool fieldApplies(const Tahap& t, const Field& f) {
  if (!f.whenMask) return true;
  return (f.whenMask >> (t.mode & 7)) & 1;
}

void tahapDefault(uint8_t type, Tahap& t) {
  memset(&t, 0, sizeof(t));
  t.type = type;
  for (int i = 0; i < NFIELDS; i++) {
    const Field& f = FIELDS[i];
    if (f.type != type) continue;
    if (f.kind == FK_TEXT) continue;
    setNum(t, f, f.def);
  }
  if (type < TH_COUNT) strlcpy(t.pesan, DEFAULT_PESAN[type], sizeof(t.pesan));
}

// ---- validasi ---------------------------------------------------------------
static String rangeMsg(const Field& f) {
  String m = String(f.label) + " harus ";
  if (f.kind == FK_CHOICE) return m + "salah satu dari " + f.opts;
  if (f.kind == FK_ENUM) return m + "salah satu dari " + f.opts;
  if (f.kind == FK_CUP) return m + "0,5 - 10 cup (kelipatan 0,5)";
  if (f.kind == FK_TEXT) return m + "diisi (1-" + String((int)f.mx) + " karakter)";
  return m + String((int)f.mn) + " - " + String((int)f.mx) + (f.unit[0] ? String(" ") + f.unit : String(""));
}

bool tahapValidate(const Tahap& t, String& err, bool allowManualOnly) {
  if (t.type >= TH_COUNT) {
    err = "Jenis Tahap tidak dikenal";
    return false;
  }
  if (t.type == TH_BUANG_MANUAL && !allowManualOnly) {
    err = "Buang manual hanya untuk menu Manual";
    return false;
  }
  for (int i = 0; i < NFIELDS; i++) {
    const Field& f = FIELDS[i];
    if (f.type != t.type) continue;
    if (f.kind == FK_TEXT) {
      size_t n = strnlen(t.pesan, sizeof(t.pesan));
      bool blank = true;
      for (size_t k = 0; k < n; k++)
        if (t.pesan[k] != ' ') blank = false;
      if (n == 0 || blank || n > PESAN_MAX) {
        err = tahapLabel(t.type) + String(": ") + rangeMsg(f);
        return false;
      }
      continue;
    }
    int32_t v = getNum(t, f);
    bool ok = v >= f.mn && v <= f.mx;
    if (ok && f.kind == FK_CHOICE) ok = optIndex(f.opts, String((int)v)) >= 0;
    if (!ok) {
      err = tahapLabel(t.type) + String(": ") + rangeMsg(f);
      return false;
    }
  }
  return true;
}

// ---- JSON ------------------------------------------------------------------
bool tahapFromJson(JsonObjectConst o, Tahap& t, String& err) {
  String key = o["t"] | "";
  int type = tahapFromKey(key);
  if (type < 0) {
    err = "Jenis Tahap tidak dikenal: " + key;
    return false;
  }
  tahapDefault((uint8_t)type, t);
  for (int i = 0; i < NFIELDS; i++) {
    const Field& f = FIELDS[i];
    if (f.type != type) continue;
    JsonVariantConst v = o[f.key];
    if (v.isNull()) continue;
    if (f.kind == FK_TEXT) {
      if (!v.is<const char*>()) {
        err = tahapLabel(type) + String(": ") + rangeMsg(f);
        return false;
      }
      strlcpy(t.pesan, v.as<const char*>(), sizeof(t.pesan));
      if (strlen(v.as<const char*>()) > PESAN_MAX) {
        err = tahapLabel(type) + String(": ") + rangeMsg(f);
        return false;
      }
    } else if (f.kind == FK_ENUM) {
      int idx = -1;
      if (v.is<const char*>()) idx = optIndex(f.opts, String(v.as<const char*>()));
      else if (v.is<int>()) idx = v.as<int>();
      if (idx < f.mn || idx > f.mx) {
        err = tahapLabel(type) + String(": ") + rangeMsg(f);
        return false;
      }
      setNum(t, f, idx);
    } else if (f.kind == FK_CUP) {
      if (!v.is<float>() && !v.is<int>()) {
        err = tahapLabel(type) + String(": ") + rangeMsg(f);
        return false;
      }
      float cup = v.as<float>();
      float c2 = cup * 2.0f;
      if (c2 < 0.0f || c2 > 255.0f || fabsf(c2 - roundf(c2)) > 0.001f) {
        err = tahapLabel(type) + String(": ") + rangeMsg(f);
        return false;
      }
      setNum(t, f, (int32_t)roundf(c2));
    } else {
      if (!v.is<long>() && !v.is<int>() && !v.is<float>()) {
        err = tahapLabel(type) + String(": ") + rangeMsg(f);
        return false;
      }
      double d = v.as<double>();
      if (d < -2147483647.0 || d > 4294967295.0) {
        err = tahapLabel(type) + String(": ") + rangeMsg(f);
        return false;
      }
      setNum(t, f, (int32_t)(int64_t)d);
    }
  }
  return tahapValidate(t, err, true);
}

void tahapToJson(const Tahap& t, JsonObject o) {
  o["t"] = tahapKey(t.type);
  for (int i = 0; i < NFIELDS; i++) {
    const Field& f = FIELDS[i];
    if (f.type != t.type || !fieldApplies(t, f)) continue;
    switch (f.kind) {
      case FK_TEXT: o[f.key] = JsonString((const char*)t.pesan); break;
      case FK_ENUM: o[f.key] = CP(optAt(f.opts, (int)getNum(t, f))); break;
      case FK_CUP: o[f.key] = getNum(t, f) / 2.0f; break;
      default: o[f.key] = getNum(t, f); break;
    }
  }
}

static bool nameOk(const char* n, String& err) {
  size_t len = strlen(n);
  bool blank = true;
  for (size_t i = 0; i < len; i++) {
    if ((uint8_t)n[i] < 32) {
      err = "Nama Proses tidak boleh berisi karakter kontrol";
      return false;
    }
    if (n[i] != ' ') blank = false;
  }
  if (len == 0 || blank || len > PROSES_NAME_MAX) {
    err = "Nama Proses harus 1-" + String(PROSES_NAME_MAX) + " karakter";
    return false;
  }
  return true;
}

bool prosesFromJson(JsonVariantConst v, Proses& p, String& err, bool keepRev, uint8_t ws) {
  memset(&p, 0, sizeof(p));
  p.ws = ws;
  if (!v.is<JsonObjectConst>()) {
    err = "Format Proses tidak valid";
    return false;
  }
  JsonObjectConst o = v.as<JsonObjectConst>();
  const char* nm = o["name"] | "";
  if (strlen(nm) > PROSES_NAME_MAX) {
    err = "Nama Proses harus 1-" + String(PROSES_NAME_MAX) + " karakter";
    return false;
  }
  strlcpy(p.name, nm, sizeof(p.name));
  if (keepRev) p.rev = (uint16_t)(o["rev"] | 0);
  JsonArrayConst arr = o["tahap"];
  if (arr.isNull() || arr.size() < 1 || arr.size() > PROSES_MAX_TAHAP) {
    err = "Proses harus berisi 1-" + String(PROSES_MAX_TAHAP) + " Tahap";
    return false;
  }
  uint8_t n = 0;
  for (JsonObjectConst it : arr) {
    String e;
    if (!tahapFromJson(it, p.t[n], e)) {
      err = "Tahap " + String((int)n + 1) + ": " + e;
      return false;
    }
    n++;
  }
  p.count = n;
  if (ws == WS_CUCI) convertLegacySpin(p);
  return prosesValidate(p, err);
}

void prosesToJson(const Proses& p, JsonObject o) {
  o["v"] = 1;
  o["name"] = JsonString((const char*)p.name);
  o["rev"] = p.rev;
  if (p.ws == WS_KERING) o["jenis"] = wsName(p.ws);
  JsonArray arr = o["tahap"].to<JsonArray>();
  for (uint8_t i = 0; i < p.count; i++) tahapToJson(p.t[i], arr.add<JsonObject>());
}

bool prosesValidate(const Proses& p, String& err) {
  if (!nameOk(p.name, err)) return false;
  if (p.count < 1 || p.count > PROSES_MAX_TAHAP) {
    err = "Proses harus berisi 1-" + String(PROSES_MAX_TAHAP) + " Tahap";
    return false;
  }
  for (uint8_t i = 0; i < p.count; i++) {
    String e;
    if (!tahapAllowed(p.t[i].type, p.ws)) {
      err = "Tahap " + String((int)i + 1) + ": " + tahapLabel(p.t[i].type) + " tidak tersedia di Proses " + wsName(p.ws);
      return false;
    }
    if (!tahapValidate(p.t[i], e, false)) {
      err = "Tahap " + String((int)i + 1) + ": " + e;
      return false;
    }
  }
  return true;
}

int prosesWarnings(const Proses& p, String* out, int maxOut) {
  int n = 0;
  if (p.ws == WS_KERING) return 0;   // peringatan hanya soal urutan air
  auto add = [&](const String& s) {
    if (n < maxOut) out[n] = s;
    n++;
  };
  bool sawIsi = false, anyIsi = false;
  for (uint8_t i = 0; i < p.count; i++) {
    const Tahap& t = p.t[i];
    String at = "Tahap " + String((int)i + 1) + " (" + tahapLabel(t.type) + "): ";
    switch (t.type) {
      case TH_ISI: sawIsi = anyIsi = true; break;
      case TH_PUTAR: case TH_RENDAM: case TH_SOAK: case TH_PEWANGI:
        if (!sawIsi) add(at + "berjalan sebelum ada Isi air.");
        break;
    }
  }
  if (anyIsi) {
    int last = -1;
    for (int i = p.count - 1; i >= 0; i--)
      if (p.t[i].type != TH_NOTIF && p.t[i].type != TH_TINDAKAN) { last = i; break; }
    if (last >= 0 && p.t[last].type != TH_BUANG) add("Proses tidak diakhiri Buang air.");
  }
  return n;
}

// ---- estimasi (detik nyata) -----------------------------------------------------
uint32_t tahapEstimateSec(const Tahap& t) {
  switch (t.type) {
    case TH_ISI: {
      uint32_t base;
      if (t.mode == ISI_WAKTU) base = (uint32_t)t.lama * 60;
      else if (C.nFill > 0) base = (uint32_t)(C.avgFillMs / 1000.0f * (t.mode == ISI_PERSEN ? t.persen / 100.0f : 1.0f));
      else base = 600;
      return base + FILL_SETTLE_MS / 1000 + S.airDetik + 2;
    }
    case TH_BUANG: return (uint32_t)t.lama * 60 + S.airDetik + 2;
    case TH_PUTAR: case TH_RENDAM: case TH_BUANG_MANUAL: return (uint32_t)t.lama * 60;
    case TH_SPIN: return (uint32_t)t.lama * 60;
    case TH_SOAK: return (uint32_t)t.ulangi * ((uint32_t)t.putar + t.rendam) * 60;
    case TH_PEWANGI: return (uint32_t)t.cup2 * 10;
    case TH_TUNGGU: return t.detik;
    case TH_CEK: return S.airDetik + 2;
    default: return 0;
  }
}

uint32_t prosesEstimateSec(const Proses& p) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < p.count; i++) total += tahapEstimateSec(p.t[i]);
  return total;
}

static String hms(uint32_t s) {
  char b[16];
  snprintf(b, sizeof(b), "%02u:%02u:%02u", (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
  return String(b);
}

String tahapRingkas(const Tahap& t) {
  String s = tahapLabel(t.type);
  auto polaName = [](uint8_t p) { return p == POLA_GENTLE ? "gentle" : p == POLA_HIJAB ? "hijab" : "normal"; };
  switch (t.type) {
    case TH_ISI:
      if (t.mode == ISI_WAKTU) s += " · " + String((int)t.lama) + " mnt";
      else if (t.mode == ISI_PERSEN) s += " · " + String((int)t.persen) + "%, batas " + hms(t.batas);
      else s += " · sensor, batas " + hms(t.batas);
      break;
    case TH_BUANG: s += " · " + String((int)t.lama) + " mnt, " + String((int)t.sesi) + " sesi"; break;
    case TH_PUTAR: s += " · " + String((int)t.lama) + " mnt, " + polaName(t.pola); break;
    case TH_SOAK:
      s += " · " + String((int)t.ulangi) + "x (putar " + String((int)t.putar) + " + rendam " + String((int)t.rendam) + " mnt)";
      break;
    case TH_RENDAM: case TH_SPIN: case TH_BUANG_MANUAL: s += " · " + String((int)t.lama) + " mnt"; break;
    case TH_PEWANGI: {
      char b[16];
      snprintf(b, sizeof(b), " · %.1f cup", t.cup2 / 2.0f);
      s += b;
      break;
    }
    case TH_TUNGGU: s += " · " + hms(t.detik); break;
    case TH_CEK: s += t.mode == CEK_PENUH ? " · harus penuh" : " · harus tidak penuh"; break;
    case TH_TINDAKAN: case TH_NOTIF: s += " · " + String(t.pesan); break;
  }
  return s;
}

String tahapParams(const Tahap& t) {
  String r = tahapRingkas(t);
  const char* sep = strstr(r.c_str(), " Â· ");   // " · "
  return sep ? String(sep + 4) : String("");
}

String tahapMetaJson() {
  JsonDocument d;
  JsonArray types = d["types"].to<JsonArray>();
  for (int ty = 0; ty < TH_COUNT; ty++) {
    JsonObject o = types.add<JsonObject>();
    o["t"] = KEYS[ty];
    o["label"] = LABELS[ty];
    o["manual"] = ty == TH_BUANG_MANUAL;
    JsonArray wsArr = o["ws"].to<JsonArray>();
    for (uint8_t w = 0; w < WS_COUNT; w++)
      if (tahapAllowed(ty, w)) wsArr.add(wsName(w));
    JsonArray fs = o["fields"].to<JsonArray>();
    for (int i = 0; i < NFIELDS; i++) {
      const Field& f = FIELDS[i];
      if (f.type != ty) continue;
      JsonObject fo = fs.add<JsonObject>();
      fo["k"] = f.key;
      fo["label"] = f.label;
      if (f.unit[0]) fo["unit"] = f.unit;
      if (f.fmt[0]) fo["fmt"] = f.fmt;
      if (f.whenMask) {
        // daftar nilai mode (nama) yang membuat field ini berlaku
        JsonArray w = fo["when"].to<JsonArray>();
        const Field* modeF = nullptr;
        for (int k = 0; k < NFIELDS; k++)
          if (FIELDS[k].type == ty && strcmp(FIELDS[k].key, "mode") == 0) modeF = &FIELDS[k];
        for (int m = 0; m < 8 && modeF; m++)
          if ((f.whenMask >> m) & 1) w.add(CP(optAt(modeF->opts, m)));
      }
      switch (f.kind) {
        case FK_ENUM: {
          fo["kind"] = "enum";
          JsonArray opts = fo["opts"].to<JsonArray>();
          JsonArray lab = fo["optLabels"].to<JsonArray>();
          for (int m = (int)f.mn; m <= (int)f.mx; m++) {
            opts.add(CP(optAt(f.opts, m)));
            lab.add(CP(optAt(f.optLabels, m)));
          }
          fo["def"] = CP(optAt(f.opts, f.def));
          break;
        }
        case FK_CHOICE: {
          fo["kind"] = "choice";
          JsonArray opts = fo["opts"].to<JsonArray>();
          for (int m = 0; m < 8; m++) {
            String v = optAt(f.opts, m);
            if (v.length() == 0) break;
            opts.add(atoi(v.c_str()));
          }
          fo["def"] = f.def;
          break;
        }
        case FK_CUP:
          fo["kind"] = "cup";
          fo["min"] = 0.5;
          fo["max"] = 10;
          fo["step"] = 0.5;
          fo["def"] = f.def / 2.0f;
          break;
        case FK_TEXT:
          fo["kind"] = "text";
          fo["max"] = f.mx;
          fo["def"] = DEFAULT_PESAN[ty];
          break;
        default:
          fo["kind"] = "int";
          fo["min"] = f.mn;
          fo["max"] = f.mx;
          fo["def"] = f.def;
          break;
      }
    }
  }
  String out;
  serializeJson(d, out);
  return out;
}
