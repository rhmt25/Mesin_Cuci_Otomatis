#include "commands.h"
#include <time.h>
#include "config.h"
#include "logger.h"
#include "machine.h"
#include "net_wifi.h"
#include "notifier.h"
#include "ota.h"
#include "relay.h"
#include "settings.h"

// ============================================================================
// Util
// ============================================================================
String normalizeCommand(const String& raw) {
  String c = raw;
  c.trim();
  c.toLowerCase();
  if (c.startsWith("/")) c.remove(0, 1);
  int at = c.indexOf('@');
  if (at > 0) c.remove(at);
  return c;
}

static String token(const String& args, int index) {
  int pos = 0, n = 0;
  String a = args;
  a.trim();
  while (pos < (int)a.length()) {
    while (pos < (int)a.length() && a[pos] == ' ') pos++;
    int start = pos;
    while (pos < (int)a.length() && a[pos] != ' ') pos++;
    if (pos > start) {
      if (n == index) return a.substring(start, pos);
      n++;
    }
  }
  return "";
}

static bool parseInt(const String& t, int lo, int hi, int& out) {
  if (t.length() == 0) return false;
  for (unsigned i = 0; i < t.length(); i++)
    if (!isDigit(t[i])) return false;
  long v = t.toInt();
  if (v < lo || v > hi) return false;
  out = (int)v;
  return true;
}

static CmdResult ok(const String& m) { return {true, m}; }
static CmdResult fail(const String& m) { return {false, m}; }

static String hms(int h, int m, int s) { return String(h) + " jam " + m + " menit " + s + " detik"; }

static String fmtDuration(uint32_t sec) {
  if (sec >= 3600) return String(sec / 3600) + "j " + String((sec % 3600) / 60) + "m";
  if (sec >= 60) return String(sec / 60) + "m " + String(sec % 60) + "d";
  return String(sec) + "d";
}

static void scheduleRestart(uint32_t delayMs) {
  xTaskCreate(
      [](void* p) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)(uintptr_t)p));
        ESP.restart();
      },
      "restart", 2048, (void*)(uintptr_t)delayMs, 1, nullptr);
}

// ============================================================================
// Alias & definisi prompt
// ============================================================================
struct Alias {
  const char* from;
  const char* to;
  const char* args;
};
static const Alias aliases[] = {
    {"start", "menu", ""},
    {"cek_variabel", "pengaturan", ""},
    {"set_ssid", "wifi_tambah", ""},
    {"set_password", "wifi_tambah", ""},
    {"hapus_ssid", "wifi_hapus", ""},
    {"hapus_password", "wifi_hapus", ""},
    {"reset_variabel", "reset_semua", ""},
    {"full_proses", "program", "kustom"},
    {"lanjutkan_proses", "lanjutkan", ""},
    {"set_mode_normal", "pola", "normal"},
    {"set_mode_gentle", "pola", "gentle"},
    {"set_mode_hijab", "pola", "hijab"},
    {"set_batas_sesi_pembungan_air", "set_batas_sesi_pembuangan_air", ""},
    {"reset_batas_sesi_pembungan_air", "reset_batas_sesi_pembuangan_air", ""},
    {"mode_waktu_pengisian_air", "level_air", "waktu"},
    {"mode_sensor_pengisian_air", "level_air", "penuh"},
    {"mode_waktu_pembuangan_air", "mode_buang", "waktu"},
    {"mode_sensor_pembuangan_air", "mode_buang", "sensor"},
    {"mulai", "program", "kustom"},
};

struct PromptDef {
  const char* name;
  uint8_t n;
  const char* p[3];
  char joiner;
  bool danger;
};
static const PromptDef promptDefs[] = {
    {"set_delay", 3, {"Kirim jam delay (0-23).", "Kirim menit delay (0-59).", "Kirim detik delay (0-59)."}, ' ', false},
    {"set_batas_waktu_pengisian_air", 3,
     {"Kirim jam batas waktu pengisian air (0-23).", "Kirim menit (0-59).", "Kirim detik (0-59)."}, ' ', false},
    {"set_jumlah_parfume", 1, {"Kirim jumlah parfum dalam cup, kelipatan 0,5 (mis. 0,5 atau 1)."}, ' ', false},
    {"wifi_tambah", 2, {"Kirim nama WiFi (SSID).", "Kirim password WiFi (kirim tanda - bila tanpa password)."}, '|', false},
    {"wifi_hapus", 1, {"Kirim nomor WiFi yang dihapus (lihat /wifi_list)."}, ' ', false},
    {"wifi_urutan", 2, {"Kirim nomor WiFi yang dipindah.", "Kirim nomor posisi tujuan."}, ' ', false},
    {"mulai_nanti", 1, {"Kirim lama tunda sebelum mulai, dalam menit."}, ' ', false},
    {"selesai_jam", 1, {"Kirim jam target selesai, format HH:MM (24 jam), mis. 07:30."}, ' ', false},
    {"level_air", 1, {"Kirim level air: penuh, 25, 50, 75, atau waktu."}, ' ', false},
    {"relay_test", 1, {"Kirim nama relay: kiri, kanan, isi, buang, spin, pewangi."}, ' ', false},
    {"reset_semua", 0, {}, ' ', true},
    {"restart", 0, {}, ' ', true},
    {"kalibrasi_air", 0, {}, ' ', true},
};

// ---- pengaturan bilangan bulat (tabel) --------------------------------------
struct IntSetting {
  const char* setCmd;
  const char* resetCmd;
  int* ptr;
  int lo, hi;
  const char* label;
  const char* unit;
  const char* prompt;
};
static const IntSetting intSettings[] = {
    {"set_soak_session", "reset_soak_session", &S.soak, 0, 1440, "Soak", "sesi", "Kirim jumlah sesi soak (0-1440)."},
    {"set_wash_time", "reset_wash_time", &S.wash, 0, 1440, "Wash", "menit", "Kirim waktu wash dalam menit (0-1440)."},
    {"set_rinse_time", "reset_rinse_time", &S.rinse, 0, 1440, "Rinse", "menit", "Kirim waktu rinse dalam menit (0-1440)."},
    {"set_spin_time", "reset_spin_time", &S.spin, 0, 1440, "Spin", "menit", "Kirim waktu spin dalam menit (0-1440)."},
    {"set_waktu_pengisian_air", "reset_waktu_pengisian_air", &S.fillTime, 1, 1440, "Waktu pengisian air", "menit",
     "Kirim waktu pengisian air dalam menit (lebih dari 0)."},
    {"set_waktu_pembuangan_air", "reset_waktu_pembuangan_air", &S.drainTime, 1, 1440, "Waktu pembuangan air", "menit",
     "Kirim waktu pembuangan air dalam menit (lebih dari 0)."},
    {"set_batas_sesi_pembuangan_air", "reset_batas_sesi_pembuangan_air", &S.drainSessionLimit, 1, 1440,
     "Batas sesi pembuangan air", "sesi", "Kirim batas sesi pembuangan air sebelum jeda (1-1440)."},
    {"set_coba_ulang_isi", "reset_coba_ulang_isi", &S.fillRetryCount, 0, 20, "Coba ulang pengisian", "kali",
     "Kirim jumlah coba ulang bila air habis (0-20)."},
    {"set_tunggu_coba_ulang", "reset_tunggu_coba_ulang", &S.fillRetryWait, 0, 240, "Tunggu sebelum coba ulang", "menit",
     "Kirim lama tunggu antar coba ulang dalam menit (0-240)."},
    {"set_unjam", "reset_unjam", &S.unjamCount, 0, 10, "Unjam valve buang", "kali",
     "Kirim jumlah percobaan unjam valve (0-10)."},
    {"set_probe_buang", "reset_probe_buang", &S.drainProbeSec, 5, 600, "Probe tarikan valve buang", "detik",
     "Kirim lama probe tarikan valve buang dalam detik (5-600)."},
};
static const size_t nIntSettings = sizeof(intSettings) / sizeof(intSettings[0]);

static String resolveAlias(const String& c, String& fixedArgs) {
  fixedArgs = "";
  for (const Alias& a : aliases) {
    if (c == a.from) {
      fixedArgs = a.args;
      return a.to;
    }
  }
  return c;
}

bool cmdInfo(const String& command, CmdInfo& out) {
  String c = normalizeCommand(command);
  out.name = resolveAlias(c, out.fixedArgs);
  out.nPrompts = 0;
  out.joiner = ' ';
  out.danger = false;
  for (const PromptDef& d : promptDefs) {
    if (out.name == d.name) {
      out.nPrompts = d.n;
      for (int i = 0; i < d.n; i++) out.prompts[i] = d.p[i];
      out.joiner = d.joiner;
      out.danger = d.danger;
      return true;
    }
  }
  for (size_t i = 0; i < nIntSettings; i++) {
    if (out.name == intSettings[i].setCmd) {
      out.nPrompts = 1;
      out.prompts[0] = intSettings[i].prompt;
      return true;
    }
  }
  return true;
}

// ============================================================================
// Teks laporan
// ============================================================================
static String fillLevelText() {
  switch (S.fillLevel) {
    case FILL_PERCENT: return "Persen " + String(S.fillPercent) + "%" + (C.nFill == 0 ? " (belum terkalibrasi, memakai sensor)" : "");
    case FILL_TIME: return "Waktu " + String(S.fillTime) + " menit";
    default: return "Penuh (sensor)";
  }
}

static String polaText() {
  static const char* n[] = {"normal", "gentle", "hijab"};
  return S.pola < 3 ? n[S.pola] : "?";
}

String airText() {
  String t = "\xF0\x9F\x92\xA7 Pengaturan air\n";
  t += "Level isi: " + fillLevelText() + "\n";
  t += "Batas waktu isi: " + hms(S.flH, S.flM, S.flS) + "\n";
  t += "Coba ulang isi: " + String(S.fillRetryCount) + "x, tunggu " + S.fillRetryWait + " menit\n";
  if (S.drainMode == DRAIN_SENSOR)
    t += "Mode buang: sensor (probe " + String(S.drainProbeSec) + " dtk, tarik total " + S.drainTime + " menit)\n";
  else
    t += "Mode buang: waktu " + String(S.drainTime) + " menit (tanpa probe)\n";
  static const char* pre[] = {"buang dulu", "jeda", "pakai air yang ada"};
  t += String("Bila air penuh sebelum mengisi: ") + pre[S.fillPrecheck < 3 ? S.fillPrecheck : 0] + "\n";
  t += "Batas sesi buang: " + String(S.drainSessionLimit) + " \xE2\x80\xA2 Unjam: " + S.unjamCount + "x\n";
  if (C.nFill > 0) t += "Rata-rata isi penuh: " + fmtDuration((uint32_t)(C.avgFillMs / 1000)) + " (" + C.nFill + " sampel)\n";
  t.trim();
  return t;
}

String settingsText() {
  String t = "\xE2\x9A\x99 Pengaturan\n";
  t += "Pola: " + polaText() + "\n";
  t += "Soak: " + String(S.soak) + " sesi\n";
  t += "Wash: " + String(S.wash) + " menit\n";
  t += "Rinse: " + String(S.rinse) + " menit\n";
  t += "Pewangi: " + String(S.parfume, 1) + " cup\n";
  t += "Spin: " + String(S.spin) + " menit\n";
  t += "Delay mulai: " + hms(S.dH, S.dM, S.dS) + "\n";
  if (S.finishAt >= 0) {
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", S.finishAt / 60, S.finishAt % 60);
    t += String("Selesai jam: ") + b + " (sekali pakai)\n";
  }
  t += "Notifikasi: " + String(notifModeName(S.notif)) + "\n\n";
  t += airText();
  return t;
}

String wifiListText() {
  String t = "\xF0\x9F\x93\xB6 Daftar WiFi (" + String(wifiCount()) + "/" + String(MAX_WIFI_NETWORKS) + ")\n";
  String active = netSsid();
  for (int i = 0; i < wifiCount(); i++) {
    WifiCred c;
    wifiGet(i, c);
    t += String(i + 1) + ". " + c.ssid + (c.pass[0] ? "  \xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2" : "  (terbuka)");
    if (active == c.ssid) t += "  \xE2\x9C\x85 tersambung";
    t += "\n";
  }
  if (wifiCount() == 0) t += "(kosong)\n";
  t += netApOn() ? "Hotspot: " + netApSsid() + " AKTIF\n" : String("Hotspot: nonaktif (menyala otomatis bila semua WiFi gagal)\n");
  t.trim();
  return t;
}

String statusText() {
  MachineStatus s;
  machineGetStatus(s);
  static const char* waterTxt[] = {"kosong", "penuh", "mengukur"};
  String t = "\xF0\x9F\x93\x8A ";
  if (s.state == MS_IDLE) {
    t += "Siap";
    if (s.resumePending) t += " \xE2\x80\xA2 \xE2\x9A\xA1 menunggu keputusan pemulihan (" + String(s.resumeInfo) + ")";
  } else if (s.state == MS_PAUSED) {
    t += String("\xE2\x8F\xB8 DIJEDA \xE2\x80\xA2 ") + s.stage + " \xE2\x80\xA2 " + (s.pauseReason[0] ? s.pauseReason : "oleh pengguna");
  } else {
    t += String(s.stage) + " \xE2\x80\xA2 " + s.detail;
    t += " \xE2\x80\xA2 sisa tahap " + fmtDuration(s.stepLeftSec);
    t += " \xE2\x80\xA2 total sisa \xC2\xB1 " + fmtDuration(s.totalLeftSec) + " (" + s.progressPct + "%)";
  }
  t += "\n\xF0\x9F\x92\xA7 Air: " + String(waterTxt[s.waterState]) + " \xE2\x80\xA2 Level: " + fillLevelText() + " \xE2\x80\xA2 Pola: " + polaText();
  t += "\n";
  if (netOnline()) t += "\xF0\x9F\x93\xB6 " + netSsid() + " (" + netRssi() + " dBm)";
  else t += "\xF0\x9F\x93\xB4 WiFi offline";
  if (s.relayFault) t += "\n\xE2\x9D\x8C RELAY TERKUNCI (pelanggaran interlock). Restart mesin.";
  t += String(" \xE2\x80\xA2 \xF0\x9F\x94\x94 ") + notifModeName(S.notif) + " \xE2\x80\xA2 \xE2\x8F\xB1 Uptime " + fmtDuration(s.uptimeSec);
  return t;
}

String historyText() {
  String t = "\xF0\x9F\x93\x9C Riwayat siklus terakhir\n";
  static const char* res[] = {"selesai", "dihentikan", "error"};
  if (ST.histCount == 0) return t + "(belum ada)";
  for (int i = 0; i < ST.histCount; i++) {
    int pos = (ST.histHead - 1 - i + 20) % 10;
    const HistoryEntry& e = ST.hist[pos];
    String when = "-";
    if (e.startEpoch) {
      time_t tt = e.startEpoch;
      struct tm lt;
      localtime_r(&tt, &lt);
      char b[20];
      strftime(b, sizeof(b), "%d/%m %H:%M", &lt);
      when = b;
    }
    t += "\xE2\x80\xA2 " + String(programName(e.program)) + " \xE2\x80\x94 " + when + " \xE2\x80\x94 " + fmtDuration(e.durSec) +
         " \xE2\x80\x94 " + res[e.result < 3 ? e.result : 2] + "\n";
  }
  t.trim();
  return t;
}

String statsText() {
  String t = "\xF0\x9F\x93\x88 Statistik\n";
  t += "Total siklus selesai: " + String(ST.totalCycles) + "\n";
  t += "Total motor cuci menyala: " + String((ST.motorSec + relayMotorSeconds()) / 3600.0f, 1) + " jam\n";
  t += "Rata-rata isi penuh: " + (C.nFill ? fmtDuration((uint32_t)(C.avgFillMs / 1000)) : String("-"));
  return t;
}

String helpText() {
  return "Perintah utama:\n"
         "program normal|gentle|hijab|cepat|kustom\n"
         "soak, wash, soak_and_wash, rinse, rinse_parfume, spin\n"
         "water_in, water_out, add_parfume, motor_on\n"
         "stop, jeda, lanjutkan, lewati, batalkan, status, cek_air\n"
         "pengaturan, riwayat, statistik, wifi_list, wifi_tambah, wifi_hapus, wifi_urutan\n"
         "pola <normal|gentle|hijab>, level_air <penuh|25|50|75|waktu [menit]>, mode_buang <sensor|waktu>\n"
         "set_soak_session, set_wash_time, set_rinse_time, set_jumlah_parfume, set_spin_time, set_delay\n"
         "mulai_nanti <menit>, selesai_jam <HH:MM>, notif <lengkap|ringkas|senyap>\n"
         "kalibrasi_air, reset_semua, restart, info\n"
         "relay <kiri|kanan|isi|buang|spin|pewangi> <on|off>, relay semua off, kalibrasi_relay [detik]\n"
         "set_probe_buang <detik>, pra_cek_isi <buang|jeda|pakai>\n"
         "custom [nomor] (proses buatan sendiri, disusun di dashboard web)\n"
         "Konsol: heap, tasks, relay_test <nama>, log, serial debug on|off";
}

// ============================================================================
// Eksekusi
// ============================================================================
// Teks alasan mesin tidak siap (kosong bila siap)
static String busyReason() {
  MachineStatus st;
  machineGetStatus(st);
  if (otaBusy()) return "Mesin sedang memperbarui firmware.";
  if (st.state == MS_RUNNING)
    return String("Ada proses berjalan: ") + st.name + " (" + st.stage + "). Hentikan dulu dengan STOP.";
  if (st.state == MS_PAUSED)
    return String("Ada proses yang dijeda: ") + st.name + " (" + st.stage + "). Lanjutkan atau hentikan dulu.";
  return "";
}

static CmdResult runProgram(uint8_t id, uint8_t arg) {
  String busy = busyReason();
  if (busy.length()) return fail("Ditolak. " + busy);
  MachineCmd c = {MC_RUN, id, arg};
  if (!machinePost(c)) return fail("Antrean perintah mesin penuh, coba lagi.");
  return ok(String("\xE2\x96\xB6 Program ") + programName(id) + " dikirim ke mesin.");
}

static CmdResult postSimple(uint8_t type, const char* okMsg) {
  MachineCmd c = {type, 0, 0};
  if (!machinePost(c)) return fail("Antrean perintah mesin penuh, coba lagi.");
  return ok(okMsg);
}

static CmdResult setTriple(const String& args, int& a, int& b, int& c, bool nonZero) {
  int h, m, s;
  if (!parseInt(token(args, 0), 0, 23, h) || !parseInt(token(args, 1), 0, 59, m) || !parseInt(token(args, 2), 0, 59, s))
    return fail("Nilai tidak valid. Jam harus 0-23, menit dan detik 0-59.");
  if (nonZero && h == 0 && m == 0 && s == 0) return fail("Nilai tidak boleh 0.");
  a = h; b = m; c = s;
  settingsSave();
  return ok("Tersimpan: " + hms(h, m, s));
}

static bool parseHalf(const String& text, float& out) {
  String t = text;
  t.replace(',', '.');
  if (t.length() == 0) return false;
  int dots = 0;
  for (unsigned i = 0; i < t.length(); i++) {
    if (t[i] == '.') dots++;
    else if (!isDigit(t[i])) return false;
  }
  if (dots > 1) return false;
  float v = t.toFloat();
  if (v < 0 || v > 1440 || (int)(v * 2) != v * 2) return false;
  out = v;
  return true;
}

CmdResult executeCommand(const String& command, const String& argsIn, CmdSource src) {
  String c = normalizeCommand(command);
  String fixed;
  String name = resolveAlias(c, fixed);
  String args = argsIn;
  args.trim();
  if (args.length() == 0) args = fixed;

  if (name == "set_tambahan_buang" || name == "reset_tambahan_buang" || name == "set_maks_buang" || name == "reset_maks_buang")
    return fail("Pengaturan ini tidak dipakai lagi: sensor hanya di atas, jadi pembuangan memakai probe tarikan "
                "(set_probe_buang) dan total waktu tarik (set_waktu_pembuangan_air).");
  if (name == "pra_cek_isi") {
    String a = token(args, 0);
    a.toLowerCase();
    if (a == "buang" || a == "0") S.fillPrecheck = PRECHECK_DRAIN;
    else if (a == "jeda" || a == "1") S.fillPrecheck = PRECHECK_PAUSE;
    else if (a == "pakai" || a == "2") S.fillPrecheck = PRECHECK_USE;
    else return fail("Pilihan: buang (buang dulu), jeda (tunggu keputusan), pakai (lewati pengisian).");
    settingsSave();
    return ok(String("Bila air penuh sebelum mengisi: ") + (S.fillPrecheck == PRECHECK_DRAIN ? "buang dulu" : S.fillPrecheck == PRECHECK_PAUSE ? "jeda" : "pakai air yang ada") + ".");
  }

  // ---- tabel pengaturan bilangan bulat ----
  for (size_t i = 0; i < nIntSettings; i++) {
    const IntSetting& d = intSettings[i];
    if (name == d.setCmd) {
      int v;
      if (!parseInt(token(args, 0), d.lo, d.hi, v))
        return fail(String("Nilai tidak valid. Masukkan bilangan bulat ") + d.lo + "-" + d.hi + ".");
      *d.ptr = v;
      settingsSave();
      return ok(String(d.label) + " disimpan: " + v + " " + d.unit);
    }
    if (name == d.resetCmd) {
      Settings def;
      settingsDefaults(def);
      int offset = (int)((char*)d.ptr - (char*)&S);
      *d.ptr = *(int*)((char*)&def + offset);
      settingsSave();
      return ok(String(d.label) + " direset: " + *d.ptr + " " + d.unit);
    }
  }

  // ---- program & kontrol mesin ----
  if (name == "program") {
    int id = programFromCmdName(token(args, 0));
    if (id < 0 || id > PRG_CEPAT) return fail("Program tidak dikenal. Pilihan: kustom, normal, gentle, hijab, cepat.");
    return runProgram(id, 0);
  }
  if (name == "soak" || name == "wash" || name == "soak_and_wash" || name == "rinse" || name == "rinse_parfume" ||
      name == "spin" || name == "water_in" || name == "water_out" || name == "add_parfume" || name == "motor_on") {
    return runProgram(programFromCmdName(name), 0);
  }
  if (name == "relay_test") {
    int r = relayFromName(token(args, 0));
    if (r < 0) return fail("Relay tidak dikenal. Pilihan: kiri, kanan, isi, buang, spin, pewangi.");
    return runProgram(PRG_TEST, r);
  }
  if (name == "custom") {
    String a = token(args, 0);
    if (a.length() == 0) {
      String t = "\xF0\x9F\x97\x82 Custom Proses (maks 5):\n";
      int n = 0;
      for (int i = 0; i < CUSTOM_SLOTS; i++) {
        CustomProc p;
        if (!customLoad(i, p)) continue;
        t += String(i + 1) + ". " + p.name + " (" + p.count + " langkah)\n";
        n++;
      }
      if (!n) t += "(belum ada, buat di dashboard web tab Custom)\n";
      else t += "Jalankan: /custom <nomor>";
      return ok(t);
    }
    int slot;
    if (!parseInt(a, 1, CUSTOM_SLOTS, slot)) return fail("Nomor slot 1-5. Ketik /custom untuk daftar.");
    CustomProc p;
    if (!customLoad(slot - 1, p)) return fail("Slot " + String(slot) + " kosong.");
    return runProgram(PRG_CUSTOM, slot - 1);
  }
  if (name == "kalibrasi_relay") {
    int secs = 3;
    if (args.length() && !parseInt(token(args, 0), 1, 30, secs)) return fail("Lama nyala per relay: 1-30 detik.");
    return runProgram(PRG_RELAYSEQ, secs);
  }
  if (name == "relay") {
    String who = token(args, 0);
    String mode = token(args, 1);
    who.toLowerCase();
    mode.toLowerCase();
    bool on = mode == "on" || mode == "nyala" || mode == "1";
    bool off = mode == "off" || mode == "mati" || mode == "0";
    if (!on && !off) return fail("Format: relay <kiri|kanan|isi|buang|spin|pewangi|semua> <on|off>");
    if (who == "semua") {
      if (on) return fail("Semua relay tidak boleh dinyalakan sekaligus. Gunakan kalibrasi_relay.");
      return postSimple(MC_STOP, "Semua relay dimatikan.");
    }
    int r = relayFromName(who);
    if (r < 0) return fail("Relay tidak dikenal. Pilihan: kiri, kanan, isi, buang, spin, pewangi, semua.");
    String busy = busyReason();
    if (busy.length()) return fail("Ditolak: kontrol relay manual hanya saat tidak ada proses. " + busy);
    if (on) {
      const char* why = relayCheck((RelayId)r, true);
      if (why) {
        String msg = String("Ditolak: relay ") + who + " tidak dinyalakan. ";
        if (r == R_LEFT && relayIsOn(R_RIGHT)) msg += "Motor kanan sedang aktif; matikan motor kanan dulu.";
        else if (r == R_RIGHT && relayIsOn(R_LEFT)) msg += "Motor kiri sedang aktif; matikan motor kiri dulu.";
        else if (r == R_FILL && relayIsOn(R_DRAIN)) msg += "Valve buang sedang aktif; matikan dulu.";
        else if (r == R_DRAIN && relayIsOn(R_FILL)) msg += "Valve isi sedang aktif; matikan dulu.";
        else if (r == R_DRAIN && (relayIsOn(R_LEFT) || relayIsOn(R_RIGHT))) msg += "Motor cuci sedang aktif; matikan dulu.";
        else msg += String(why[0] >= 'a' && why[0] <= 'z' ? (char)(why[0] - 32) : why[0]) + String(why + 1) + ".";
        return fail(msg);
      }
    }
    MachineCmd c = {MC_RELAY, (uint8_t)r, (uint8_t)(on ? 1 : 0)};
    if (!machinePost(c)) return fail("Antrean perintah mesin penuh, coba lagi.");
    if (!on) return ok(String("Relay ") + who + " dimatikan.");
    return ok(String("Relay ") + who + " dinyalakan (otomatis mati 60 detik).");
  }
  if (name == "stop") return postSimple(MC_STOP, "\xE2\x8F\xB9 Perintah STOP dikirim. Semua relay dimatikan.");
  if (name == "jeda") return postSimple(MC_PAUSE, "\xE2\x8F\xB8 Perintah jeda dikirim.");
  if (name == "lanjutkan") return postSimple(MC_RESUME, "\xE2\x96\xB6 Perintah lanjutkan dikirim.");
  if (name == "lewati") return postSimple(MC_SKIP, "\xE2\x8F\xAD Tahap dilewati.");
  if (name == "batalkan") return postSimple(MC_CANCEL_RESUME, "Pemulihan proses dibatalkan.");

  if (name == "cek_air") {
    MachineStatus s;
    machineGetStatus(s);
    static const char* t[] = {"Air belum mencukupi (kosong stabil).", "Air sudah mencukupi (penuh stabil).",
                              "Level air belum stabil (sedang mengukur)."};
    return ok(t[s.waterState]);
  }

  // ---- laporan ----
  if (name == "status") return ok(statusText());
  if (name == "pengaturan") return ok(settingsText());
  if (name == "riwayat") return ok(historyText());
  if (name == "statistik") return ok(statsText());
  if (name == "wifi_list") return ok(wifiListText());
  if (name == "menu") return ok(statusText());
  if (name == "bantuan" || name == "help") return ok(helpText());
  if (name == "info") {
    String t = String("Firmware v") + FW_VERSION + " (" + __DATE__ + ")\n";
    t += "Heap bebas: " + String(ESP.getFreeHeap() / 1024) + " KB (min " + String(ESP.getMinFreeHeap() / 1024) + " KB)\n";
    t += String("Alasan reset terakhir: ") + loggerResetReason() + "\n";
    t += netApOn() ? "Hotspot: " + netApSsid() + " AKTIF (192.168.4.1)\n" : String("Hotspot: nonaktif (menyala otomatis bila semua WiFi gagal)\n");
    t += "WiFi: " + (netOnline() ? netSsid() + " " + netIp() : String("offline")) + "\n";
    t += "Jam: " + netTimeString() + "\nDashboard: http://" + DEVICE_HOSTNAME + ".local";
    return ok(t);
  }

  // ---- pengaturan khusus ----
  if (name == "pola") {
    String p = token(args, 0);
    p.toLowerCase();
    if (p == "normal") S.pola = POLA_NORMAL;
    else if (p == "gentle") S.pola = POLA_GENTLE;
    else if (p == "hijab") S.pola = POLA_HIJAB;
    else return fail("Pola tidak dikenal. Pilihan: normal, gentle, hijab.");
    settingsSave();
    return ok("Pola " + p + " digunakan.");
  }
  if (name == "set_jumlah_parfume") {
    float v;
    if (!parseHalf(token(args, 0), v)) return fail("Nilai tidak valid. Gunakan kelipatan 0,5 (mis. 0,5 atau 1).");
    S.parfume = v;
    settingsSave();
    return ok("Jumlah parfum disimpan. Pompa: " + String(v, 1) + " cup x 5 detik + 3 detik awal.");
  }
  if (name == "reset_jumlah_parfume") {
    S.parfume = 1.0f;
    settingsSave();
    return ok("Jumlah parfum direset menjadi 1 cup.");
  }
  if (name == "set_delay") {
    CmdResult r = setTriple(args, S.dH, S.dM, S.dS, false);
    return r;
  }
  if (name == "reset_delay") {
    S.dH = S.dM = S.dS = 0;
    S.finishAt = -1;
    settingsSave();
    return ok("Delay direset menjadi 0.");
  }
  if (name == "set_batas_waktu_pengisian_air") return setTriple(args, S.flH, S.flM, S.flS, true);
  if (name == "reset_batas_waktu_pengisian_air") {
    S.flH = 0; S.flM = 30; S.flS = 0;
    settingsSave();
    return ok("Batas waktu pengisian direset menjadi 0 jam 30 menit 0 detik.");
  }
  if (name == "mulai_nanti") {
    int m;
    if (!parseInt(token(args, 0), 0, 1439, m)) return fail("Masukkan lama tunda dalam menit (0-1439).");
    S.dH = m / 60; S.dM = m % 60; S.dS = 0;
    S.finishAt = -1;
    settingsSave();
    return ok(m == 0 ? "Proses akan dimulai langsung." : "Proses berikutnya ditunda " + hms(S.dH, S.dM, 0) + ".");
  }
  if (name == "selesai_jam") {
    String a = token(args, 0);
    if (a == "batal" || a == "-") {
      S.finishAt = -1;
      settingsSave();
      return ok("Jadwal selesai jam dibatalkan.");
    }
    if (!netTimeSynced()) return fail("Jam belum tersinkron (butuh internet). Gunakan /mulai_nanti.");
    int h, m;
    String hh = a, mm = token(args, 1);
    int colon = a.indexOf(':');
    if (colon > 0) { hh = a.substring(0, colon); mm = a.substring(colon + 1); }
    if (!parseInt(hh, 0, 23, h) || !parseInt(mm.length() ? mm : "0", 0, 59, m)) return fail("Format jam tidak valid. Contoh: 07:30");
    S.finishAt = h * 60 + m;
    S.dH = S.dM = S.dS = 0;
    settingsSave();
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", h, m);
    return ok(String("Proses berikutnya akan dijadwalkan selesai jam ") + b + ".");
  }
  if (name == "level_air") {
    String a = token(args, 0);
    a.toLowerCase();
    if (a == "penuh" || a == "sensor") {
      S.fillLevel = FILL_SENSOR;
    } else if (a == "25" || a == "50" || a == "75") {
      S.fillLevel = FILL_PERCENT;
      S.fillPercent = a.toInt();
    } else if (a == "waktu" || a == "menit") {
      String n = token(args, 1);
      if (n.length()) {
        int v;
        if (!parseInt(n, 1, 1440, v)) return fail("Waktu isi tidak valid (1-1440 menit).");
        S.fillTime = v;
      }
      S.fillLevel = FILL_TIME;
    } else {
      return fail("Level tidak dikenal. Pilihan: penuh, 25, 50, 75, waktu [menit].");
    }
    settingsSave();
    return ok("Level air: " + fillLevelText());
  }
  if (name == "mode_buang") {
    String a = token(args, 0);
    a.toLowerCase();
    if (a == "sensor") S.drainMode = DRAIN_SENSOR;
    else if (a == "waktu") S.drainMode = DRAIN_TIME;
    else return fail("Pilihan: sensor atau waktu.");
    settingsSave();
    return ok(S.drainMode == DRAIN_SENSOR ? "Pembuangan air memakai sensor." : "Pembuangan air memakai waktu.");
  }
  if (name == "notif") {
    String a = token(args, 0);
    a.toLowerCase();
    if (a == "lengkap") S.notif = NOTIF_LENGKAP;
    else if (a == "ringkas") S.notif = NOTIF_RINGKAS;
    else if (a == "senyap") S.notif = NOTIF_SENYAP;
    else return fail("Pilihan: lengkap, ringkas, senyap.");
    settingsSave();
    return ok(String("Mode notifikasi: ") + notifModeName(S.notif) + ". (Log web tetap lengkap.)");
  }
  if (name == "reset_semua") {
    settingsReset();
    return ok("Semua pengaturan direset ke default (daftar WiFi & notifikasi dipertahankan).");
  }
  if (name == "kalibrasi_air") {
    calibReset();
    return ok("Data kalibrasi waktu isi/buang direset.");
  }

  // ---- WiFi ----
  if (name == "wifi_tambah") {
    String a = args;
    int bar = a.indexOf('|');
    String ssid = bar >= 0 ? a.substring(0, bar) : a;
    String pass = bar >= 0 ? a.substring(bar + 1) : "";
    ssid.trim();
    if (pass == "-") pass = "";
    if (ssid.length() == 0) return fail("SSID tidak boleh kosong.");
    if (!wifiAdd(ssid, pass)) return fail("Gagal: daftar penuh (maks " + String(MAX_WIFI_NETWORKS) + ") atau data tidak valid.");
    netReconnectNow();
    return ok("WiFi \"" + ssid + "\" disimpan. Mencoba tersambung...");
  }
  if (name == "wifi_hapus") {
    int n;
    if (!parseInt(token(args, 0), 1, MAX_WIFI_NETWORKS, n) || !wifiRemove(n - 1)) return fail("Nomor WiFi tidak valid (lihat /wifi_list).");
    netReconnectNow();
    return ok("WiFi nomor " + String(n) + " dihapus.");
  }
  if (name == "wifi_urutan") {
    int a, b;
    if (!parseInt(token(args, 0), 1, MAX_WIFI_NETWORKS, a) || !parseInt(token(args, 1), 1, MAX_WIFI_NETWORKS, b) ||
        !wifiMove(a - 1, b - 1))
      return fail("Nomor tidak valid (lihat /wifi_list).");
    return ok(wifiListText());
  }

  // ---- sistem ----
  if (name == "restart") {
    MachineStatus s;
    machineGetStatus(s);
    if (s.state != MS_IDLE) return fail("Mesin sedang bekerja. Hentikan proses dulu.");
    scheduleRestart(1000);
    return ok("Restart dalam 1 detik...");
  }
  if (name == "web_password") {
    if (src != SRC_WEB) return fail("Hanya bisa diganti dari dashboard web.");
    if (args.length() < 6) return fail("Password minimal 6 karakter.");
    authSet(args);
    return ok("Password web diganti.");
  }

  return fail("Perintah tidak dikenal: /" + c + ". Ketik /bantuan.");
}

// ============================================================================
// Konsol (Serial USB & web): "status", "stop", "heap", "tasks", "relay_test kiri", ...
// ============================================================================
String consoleExec(const String& lineIn, CmdSource src) {
  String line = lineIn;
  line.trim();
  if (line.length() == 0) return "";
  int sp = line.indexOf(' ');
  String cmd = sp > 0 ? line.substring(0, sp) : line;
  String args = sp > 0 ? line.substring(sp + 1) : "";
  cmd = normalizeCommand(cmd);

  String out;
  if (cmd == "heap") {
    out = "Heap bebas " + String(ESP.getFreeHeap()) + " B, minimum " + String(ESP.getMinFreeHeap()) + " B, blok terbesar " +
          String(ESP.getMaxAllocHeap()) + " B";
  } else if (cmd == "tasks") {
    out = "Tasks: " + String(uxTaskGetNumberOfTasks()) + ", uptime " + fmtDuration(millis() / 1000);
  } else if (cmd == "log") {
    out = "Seq log terakhir: " + String(logLastSeq()) + ". Jejak boot sebelumnya:\n" + loggerPreviousTrace();
  } else if (cmd == "serial") {
    String a = token(args, 0), b = token(args, 1);
    if (a == "debug" && (b == "on" || b == "off")) {
      loggerSetSerialDebug(b == "on");
      out = String("Log DEBUG ke serial USB: ") + (b == "on" ? "aktif (tidak disimpan)" : "mati");
    } else {
      out = "Format: serial debug on|off";
    }
  } else if (cmd == "reboot") {
    out = executeCommand("restart", "", src).msg;
  } else {
    CmdResult r = executeCommand(cmd, args, src);
    out = r.msg;
  }
  return out;
}
