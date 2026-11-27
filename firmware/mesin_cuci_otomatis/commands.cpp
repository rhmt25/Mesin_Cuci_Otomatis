// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#include "commands.h"
#include <time.h>
#include "config.h"
#include "logger.h"
#include "machine.h"
#include "net_wifi.h"
#include "notifier.h"
#include "ota.h"
#include "proses_store.h"
#include "remote.h"
#include "remote_policy.h"
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
// Teks laporan
// ============================================================================
String settingsText() {
  String t = "\xE2\x9A\x99 Pengaturan\n";
  t += "Notifikasi: " + String(notifModeName(S.notif)) + "\n";
  t += String("Kontrol jarak jauh: ") + (!remoteConfigured() ? "belum dikonfigurasi" : S.remoteOn ? "aktif" : "nonaktif") + "\n";
  if (C.nFill > 0) t += "Rata-rata isi penuh: " + fmtDuration((uint32_t)(C.avgFillMs / 1000)) + " (" + C.nFill + " sampel)\n";
  else t += "Kalibrasi isi: belum ada (mode persen memakai sensor)\n";
  t += "Variabel air, putar, dan pewangi diatur per Tahap di tab Proses.";
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

String prosesListText() {
  Proses* p = new Proses;
  if (!p) return "Memori rendah.";
  String t;
  for (uint8_t ws = 0; ws < WS_COUNT; ws++) {
    t += "\xF0\x9F\x97\x82 Proses " + String(wsName(ws)) + " (maks " + String(PROSES_SLOTS) + "):\n";
    int n = 0;
    for (uint8_t i = 0; i < PROSES_SLOTS; i++) {
      uint8_t id = prosesId(ws, i);
      SlotState st = prosesSlotState(id);
      if (st == SLOT_EMPTY) continue;
      n++;
      if (st == SLOT_BAD) {
        t += String(i + 1) + ". (berkas rusak)\n";
      } else if (prosesLoad(id, *p)) {
        t += String(i + 1) + ". " + p->name + " (" + String((int)p->count) + " Tahap, \xC2\xB1" + fmtDuration(prosesEstimateSec(*p)) + ")\n";
      }
    }
    if (!n) t += "(belum ada)\n";
  }
  delete p;
  t += "Jalankan: proses [pengering] <nomor>";
  return t;
}

// Satu baris keadaan workspace
static String laneText(const MachineStatus& s) {
  if (s.state == MS_IDLE) {
    String t = "Siap";
    if (s.resumePending) t += " \xE2\x80\xA2 \xE2\x9A\xA1 menunggu keputusan pemulihan (" + String(s.resumeInfo) + ")";
    return t;
  }
  if (s.state == MS_PAUSED) return String("\xE2\x8F\xB8 DIJEDA \xE2\x80\xA2 ") + s.name + " \xE2\x80\xA2 " + s.stage;
  if (s.state == MS_DECIDE) return String("\xE2\x9D\x93 MENUNGGU KEPUTUSAN \xE2\x80\xA2 ") + s.name + " \xE2\x80\xA2 " + s.pauseReason;
  String t = String(s.name) + " \xE2\x80\xA2 " + s.stage + " \xE2\x80\xA2 " + s.detail;
  t += " \xE2\x80\xA2 sisa Tahap " + fmtDuration(s.stepLeftSec);
  t += " \xE2\x80\xA2 total sisa \xC2\xB1 " + fmtDuration(s.totalLeftSec) + " (" + s.progressPct + "%)";
  return t;
}

String statusText() {
  MachineStatus s;
  machineGetStatus(s);
  static const char* waterTxt[] = {"kosong", "penuh", "mengukur"};
  String t = "\xF0\x9F\x93\x8A Cuci: " + laneText(s);
  {
    MachineStatus k;
    machineGetStatus(k, WS_KERING);
    t += "\nPengering: " + laneText(k);
  }
  t += "\n\xF0\x9F\x92\xA7 Air: " + String(waterTxt[s.waterState]) + "\n";
  if (netOnline()) t += "\xF0\x9F\x93\xB6 " + netSsid() + " (" + netRssi() + " dBm)";
  else t += "\xF0\x9F\x93\xB4 WiFi offline";
  if (s.relayFault) t += "\n\xE2\x9D\x8C RELAY TERKUNCI (pelanggaran interlock). Restart mesin.";
  t += String(" \xE2\x80\xA2 \xF0\x9F\x94\x94 ") + notifModeName(S.notif) + " \xE2\x80\xA2 \xE2\x8F\xB1 Uptime " + fmtDuration(s.uptimeSec);
  return t;
}

String historyText() {
  String t = "\xF0\x9F\x93\x9C Riwayat terakhir\n";
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
    t += "\xE2\x80\xA2 " + String(e.name) + " \xE2\x80\x94 " + when + " \xE2\x80\x94 " + fmtDuration(e.durSec) + " \xE2\x80\x94 " +
         res[e.result < 3 ? e.result : 2] + "\n";
  }
  t.trim();
  return t;
}

String statsText() {
  String t = "\xF0\x9F\x93\x88 Statistik\n";
  t += "Total Proses selesai: " + String(ST.totalCycles) + "\n";
  t += "Total motor cuci menyala: " + String((ST.motorSec + relayMotorSeconds()) / 3600.0f, 1) + " jam\n";
  t += "Rata-rata isi penuh: " + (C.nFill ? fmtDuration((uint32_t)(C.avgFillMs / 1000)) : String("-"));
  return t;
}

String helpText() {
  return "Perintah utama:\n"
         "proses [pengering] [nomor] [tunda HH:MM:SS | selesai HH:MM]  (tanpa nomor: daftar Proses)\n"
         "pilih <ulang|lanjut|batal|buang|pakai> [pengering]  (jawab keputusan menunggu)\n"
         "stop [cuci|pengering]  (tanpa pilihan: hentikan keduanya)\n"
         "jeda, lanjutkan, lewati, batalkan [pengering]; status, cek_air\n"
         "pengaturan, riwayat, statistik, wifi_list, wifi_tambah, wifi_hapus, wifi_urutan\n"
         "notif <lengkap|ringkas|senyap>, remote [on|off], kalibrasi_air, reset_semua, restart, info\n"
         "relay <kiri|kanan|isi|buang|spin|pewangi> <on|off>, relay semua off, kalibrasi_relay [detik]\n"
         "buang_manual <menit>  (buang air hanya dengan waktu, tanpa sensor)\n"
         "Tahap dan variabelnya diatur di dashboard web (tab Proses dan Manual).\n"
         "Konsol: heap, tasks, relay_test <nama>, log, serial debug on|off";
}

// ============================================================================
// Eksekusi
// ============================================================================
// Teks alasan workspace tidak siap (kosong bila siap)
static String busyReason(uint8_t ws) {
  MachineStatus st;
  if (otaBusy()) return "Mesin sedang memperbarui firmware.";
  if (ws == WS_KERING) {   // kalibrasi relay berurutan meminjam motor spin
    machineGetStatus(st, WS_CUCI);
    if (st.relaySeq) return "Kalibrasi relay sedang berjalan. Tunggu selesai atau STOP.";
  }
  machineGetStatus(st, ws);
  String who = ws == WS_KERING ? "Pengering: " : "";
  if (st.state == MS_RUNNING)
    return who + "Ada Proses berjalan: " + st.name + " (" + st.stage + "). Hentikan dulu dengan STOP.";
  if (st.state == MS_PAUSED)
    return who + "Ada Proses yang dijeda: " + st.name + " (" + st.stage + "). Lanjutkan atau hentikan dulu.";
  if (st.state == MS_DECIDE)
    return who + "Mesin menunggu keputusan: " + st.name + " (" + st.pauseReason + "). Jawab atau hentikan dulu.";
  return "";
}

static CmdResult postSimple(uint8_t type, const char* okMsg, uint8_t ws = WS_CUCI) {
  MachineCmd c;
  memset(&c, 0, sizeof(c));
  c.type = type;
  c.ws = ws;
  c.finishAt = -1;
  if (!machinePost(c)) return fail("Antrean perintah mesin penuh, coba lagi.");
  return ok(okMsg);
}

CmdResult commandRunProses(int slot, uint32_t waitSec, int finishAtMin, uint8_t ws) {
  if (slot < 1 || slot > PROSES_SLOTS) return fail(String("Nomor Proses 1-") + PROSES_SLOTS + ". Ketik proses untuk daftar.");
  String busy = busyReason(ws);
  if (busy.length()) return fail("Ditolak. " + busy);
  if (finishAtMin >= 0 && !netTimeSynced()) return fail("Jam belum tersinkron (butuh internet). Gunakan tunda.");
  SlotState st = prosesSlotState(prosesId(ws, slot - 1));
  if (st == SLOT_EMPTY) return fail("Proses " + String(slot) + " kosong.");
  if (st == SLOT_BAD) return fail("Proses " + String(slot) + " rusak. Hapus atau impor ulang.");
  if (!machineStartProses(slot - 1, waitSec, finishAtMin, ws)) return fail("Antrean perintah mesin penuh, coba lagi.");
  return ok("\xE2\x96\xB6 Proses " + String(slot) + " dikirim ke mesin.");
}

CmdResult commandRunManual(const Tahap& t, uint8_t ws) {
  if (!tahapAllowed(t.type, ws)) return fail(String(tahapLabel(t.type)) + " tidak tersedia di workspace " + wsName(ws) + ".");
  String busy = busyReason(ws);
  if (busy.length()) return fail("Ditolak. " + busy);
  String err;
  if (!tahapValidate(t, err, true)) return fail(err);
  if (!machineRunManual(t, ws)) return fail("Antrean perintah mesin penuh, coba lagi.");
  return ok(String("\xE2\x96\xB6 Manual: ") + tahapLabel(t.type) + " dikirim ke mesin.");
}

CmdResult commandDecide(const String& choice, uint8_t ws) {
  MachineStatus s;
  machineGetStatus(s, ws);
  if (s.state != MS_DECIDE) return fail("Tidak ada keputusan yang menunggu.");
  uint8_t opt = decisionOptFromName(choice);
  if (!opt) return fail("Pilihan tidak dikenal. Gunakan: ulang, lanjut, batal, buang, pakai.");
  if (!(s.decisionOpts & opt)) {
    String have;
    for (uint8_t b = 1; b <= OP_PAKAI; b <<= 1)
      if (s.decisionOpts & b) have += String(have.length() ? ", " : "") + decisionOptName(b);
    return fail(String("Pilihan tidak tersedia. Yang ada: ") + have + ".");
  }
  MachineCmd c;
  memset(&c, 0, sizeof(c));
  c.type = MC_DECIDE;
  c.arg = opt;
  c.ws = ws;
  c.finishAt = -1;
  if (!machinePost(c)) return fail("Antrean perintah mesin penuh, coba lagi.");
  return ok(String("Keputusan dikirim: ") + decisionOptLabel(opt) + ".");
}

// "HH:MM:SS" atau "HH:MM" atau menit saja -> detik
static bool parseDuration(const String& a, uint32_t& sec) {
  int p1 = a.indexOf(':');
  if (p1 < 0) {
    int m;
    if (!parseInt(a, 1, 1439, m)) return false;
    sec = (uint32_t)m * 60;
    return true;
  }
  int p2 = a.indexOf(':', p1 + 1);
  int h, m, s = 0;
  if (!parseInt(a.substring(0, p1), 0, 23, h)) return false;
  if (!parseInt(p2 < 0 ? a.substring(p1 + 1) : a.substring(p1 + 1, p2), 0, 59, m)) return false;
  if (p2 >= 0 && !parseInt(a.substring(p2 + 1), 0, 59, s)) return false;
  sec = (uint32_t)h * 3600 + (uint32_t)m * 60 + (uint32_t)s;
  return sec > 0;
}

CmdResult executeCommand(const String& command, const String& argsIn, CmdSource src) {
  String name = normalizeCommand(command);
  String args = argsIn;
  args.trim();

  // Permintaan dari jarak jauh: hanya perintah yang diizinkan
  if (src == SRC_REMOTE && !remoteCmdAllowed(name.c_str())) return fail("Perintah ini hanya bisa dari web lokal (demi keamanan).");

  if (name == "remote") {
    String a = token(args, 0);
    a.toLowerCase();
    if (a == "on" || a == "off") {
      if (src == SRC_REMOTE) return fail("Hanya bisa diubah dari web lokal.");
      S.remoteOn = a == "on";
      settingsSave();
      return ok(String("Kontrol jarak jauh ") + (S.remoteOn ? "diaktifkan." : "dinonaktifkan."));
    }
    String t = String("Kontrol jarak jauh: ");
    if (!remoteConfigured()) t += "belum dikonfigurasi (REMOTE_URL / REMOTE_DEVICE_TOKEN di secrets.h)";
    else if (!S.remoteOn) t += "dinonaktifkan";
    else t += remoteConnected() ? "terhubung ke server" : "mencoba menyambung";
    return ok(t);
  }

  // ---- Proses & kontrol mesin ----
  if (name == "proses") {
    uint8_t ws = WS_CUCI;
    {   // kata pertama boleh menyebut workspace: "proses pengering 2"
      String first = token(args, 0);
      first.toLowerCase();
      if (first == "cuci" || first == "pengering") {
        ws = (uint8_t)wsFromName(first);
        args = args.substring(first.length());
        args.trim();
      }
    }
    String a = token(args, 0);
    if (a.length() == 0) return ok(prosesListText());
    int slot;
    if (!parseInt(a, 1, PROSES_SLOTS, slot)) return fail(String("Nomor Proses 1-") + PROSES_SLOTS + ". Ketik proses untuk daftar.");
    String mode = token(args, 1);
    mode.toLowerCase();
    uint32_t wait = 0;
    int finishAt = -1;
    if (mode == "tunda") {
      if (!parseDuration(token(args, 2), wait)) return fail("Format tunda: tunda HH:MM:SS (atau menit saja, 1-1439).");
    } else if (mode == "selesai") {
      String hm = token(args, 2);
      int colon = hm.indexOf(':');
      int h, m;
      if (colon < 0 || !parseInt(hm.substring(0, colon), 0, 23, h) || !parseInt(hm.substring(colon + 1), 0, 59, m))
        return fail("Format: selesai HH:MM, contoh selesai 07:30");
      finishAt = h * 60 + m;
    } else if (mode.length()) {
      return fail("Format: proses <nomor> [tunda HH:MM:SS | selesai HH:MM]");
    }
    return commandRunProses(slot, wait, finishAt, ws);
  }
  if (name == "pilih") {
    int ws = wsFromName(token(args, 1));
    if (ws < 0) return fail("Format: pilih <opsi> [pengering]");
    return commandDecide(token(args, 0), (uint8_t)ws);
  }
  if (name == "buang_manual") {
    int m;
    if (!parseInt(token(args, 0), 1, 60, m)) return fail("Format: buang_manual <menit 1-60>");
    Tahap t;
    tahapDefault(TH_BUANG_MANUAL, t);
    t.lama = (uint16_t)m;
    return commandRunManual(t);
  }
  if (name == "relay_test") {
    int r = relayFromName(token(args, 0));
    if (r < 0) return fail("Relay tidak dikenal. Pilihan: kiri, kanan, isi, buang, spin, pewangi.");
    String busy = busyReason(machineRelayWs(r));
    if (busy.length()) return fail("Ditolak. " + busy);
    MachineCmd c;
    memset(&c, 0, sizeof(c));
    c.type = MC_RUN_RELAYTEST;
    c.arg = (uint8_t)r;
    c.finishAt = -1;
    if (!machinePost(c)) return fail("Antrean perintah mesin penuh, coba lagi.");
    return ok(String("Uji relay ") + relayName((RelayId)r) + " dikirim.");
  }
  if (name == "kalibrasi_relay") {
    int secs = 3;
    if (args.length() && !parseInt(token(args, 0), 1, 30, secs)) return fail("Lama nyala per relay: 1-30 detik.");
    String busy = busyReason(WS_CUCI);
    if (!busy.length()) busy = busyReason(WS_KERING);
    if (busy.length()) return fail("Ditolak. " + busy);
    MachineCmd c;
    memset(&c, 0, sizeof(c));
    c.type = MC_RUN_RELAYSEQ;
    c.arg = (uint8_t)secs;
    c.finishAt = -1;
    if (!machinePost(c)) return fail("Antrean perintah mesin penuh, coba lagi.");
    return ok("Kalibrasi relay dimulai.");
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
      return postSimple(MC_STOP, "Semua relay dimatikan.", WS_ALL);
    }
    int r = relayFromName(who);
    if (r < 0) return fail("Relay tidak dikenal. Pilihan: kiri, kanan, isi, buang, spin, pewangi, semua.");
    String busy = busyReason(machineRelayWs(r));
    if (busy.length()) return fail("Ditolak: kontrol relay manual hanya saat tidak ada Proses. " + busy);
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
    MachineCmd c;
    memset(&c, 0, sizeof(c));
    c.type = MC_RELAY;
    c.arg = (uint8_t)r;
    c.arg2 = on ? 1 : 0;
    c.finishAt = -1;
    if (!machinePost(c)) return fail("Antrean perintah mesin penuh, coba lagi.");
    if (!on) return ok(String("Relay ") + who + " dimatikan.");
    return ok(String("Relay ") + who + " dinyalakan (otomatis mati 60 detik).");
  }
  if (name == "stop" || name == "jeda" || name == "lanjutkan" || name == "lewati" || name == "batalkan") {
    String who = token(args, 0);
    int ws = wsFromName(who);
    if (ws < 0) return fail("Format: " + name + " [cuci|pengering]");
    if (name == "stop") {
      if (!who.length()) return postSimple(MC_STOP, "\xE2\x8F\xB9 Perintah STOP dikirim. Semua relay dimatikan.", WS_ALL);
      return postSimple(MC_STOP, ws == WS_KERING ? "\xE2\x8F\xB9 STOP pengering dikirim." : "\xE2\x8F\xB9 STOP cuci dikirim.", (uint8_t)ws);
    }
    if (name == "jeda") return postSimple(MC_PAUSE, "\xE2\x8F\xB8 Perintah jeda dikirim.", (uint8_t)ws);
    if (name == "lanjutkan") return postSimple(MC_RESUME, "\xE2\x96\xB6 Perintah lanjutkan dikirim.", (uint8_t)ws);
    if (name == "lewati") return postSimple(MC_SKIP, "\xE2\x8F\xAD Tahap dilewati.", (uint8_t)ws);
    return postSimple(MC_CANCEL_RESUME, "Pemulihan Proses dibatalkan.", (uint8_t)ws);
  }

  if (name == "cek_air") {
    MachineStatus s;
    machineGetStatus(s);
    static const char* t[] = {"Air belum mencukupi (kosong stabil).", "Air sudah mencukupi (penuh stabil).",
                              "Level air belum stabil (sedang mengukur)."};
    return ok(t[s.waterState]);
  }

  // ---- laporan ----
  if (name == "status" || name == "menu") return ok(statusText());
  if (name == "pengaturan") return ok(settingsText());
  if (name == "riwayat") return ok(historyText());
  if (name == "statistik") return ok(statsText());
  if (name == "wifi_list") return ok(wifiListText());
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

  // ---- pengaturan ----
  if (name == "notif") {
    String a = token(args, 0);
    a.toLowerCase();
    if (a == "lengkap") S.notif = NOTIF_LENGKAP;
    else if (a == "ringkas") S.notif = NOTIF_RINGKAS;
    else if (a == "senyap") S.notif = NOTIF_SENYAP;
    else return fail("Pilihan: lengkap, ringkas, senyap.");
    settingsSave();
    return ok(String("Mode notifikasi: ") + notifModeName(S.notif) + ". (Log web tetap lengkap; pesan Tahap Notifikasi selalu terkirim.)");
  }
  if (name == "reset_semua") {
    settingsReset();
    return ok("Pengaturan direset ke default (daftar WiFi, Proses, dan kalibrasi dipertahankan).");
  }
  if (name == "kalibrasi_air") {
    calibReset();
    return ok("Data kalibrasi waktu isi direset.");
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
    if (!parseInt(token(args, 0), 1, MAX_WIFI_NETWORKS, n) || !wifiRemove(n - 1)) return fail("Nomor WiFi tidak valid (lihat wifi_list).");
    netReconnectNow();
    return ok("WiFi nomor " + String(n) + " dihapus.");
  }
  if (name == "wifi_urutan") {
    int a, b;
    if (!parseInt(token(args, 0), 1, MAX_WIFI_NETWORKS, a) || !parseInt(token(args, 1), 1, MAX_WIFI_NETWORKS, b) ||
        !wifiMove(a - 1, b - 1))
      return fail("Nomor tidak valid (lihat wifi_list).");
    return ok(wifiListText());
  }

  // ---- sistem ----
  if (name == "restart") {
    if (!machineIsIdle()) return fail("Mesin sedang bekerja. Hentikan Proses dulu.");
    scheduleRestart(1000);
    return ok("Restart dalam 1 detik...");
  }
  if (name == "web_password") {
    if (src != SRC_WEB) return fail("Hanya bisa diganti dari dashboard web.");
    if (args.length() < 12) return fail("Password minimal 12 karakter.");
    authSet(args);
    return ok("Password web diganti.");
  }

  return fail("Perintah tidak dikenal: " + name + ". Ketik bantuan.");
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
