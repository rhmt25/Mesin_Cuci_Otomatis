#include "telegram_ui.h"
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include "commands.h"
#include "config.h"
#include "logger.h"
#include "machine.h"
#include "net_wifi.h"
#include "notifier.h"
#include "ota.h"
#include "settings.h"

#define CHAT_ID TELEGRAM_CHAT_ID

static WiFiClientSecure client;
static UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, client);

// ---- alur input bertahap ("kirim angka") --------------------------------------
struct PendingInput {
  bool active = false;
  CmdInfo info;
  String collected;
  uint8_t step = 0;
};
static PendingInput pending;

// ============================================================================
// Keyboard
// ============================================================================
struct Btn {
  const char* label;
  const char* data;
};

static String keyboard(const Btn* btns, size_t n, size_t perRow) {
  String json = "[";
  for (size_t i = 0; i < n; i++) {
    if (i % perRow == 0) json += i ? "],[" : "[";
    else json += ",";
    json += "{\"text\":\"";
    json += btns[i].label;
    json += "\",\"callback_data\":\"";
    json += btns[i].data;
    json += "\"}";
  }
  json += "]]";
  return json;
}

static void sendText(const String& text) {
  String t = text;
  if (t.length() > 4000) t = t.substring(0, 4000);
  bot.sendMessage(CHAT_ID, t, "");
}

static void sendWithKeyboard(const String& text, const String& kb) {
  String t = text;
  if (t.length() > 4000) t = t.substring(0, 4000);
  bot.sendMessageWithInlineKeyboard(CHAT_ID, t, "", kb);
}

static void sendMenu() {
  static const Btn btns[] = {
      {"\xE2\x96\xB6 Normal", "program normal"}, {"\xF0\x9F\x8C\xB8 Gentle", "program gentle"},
      {"\xF0\x9F\xA7\x95 Hijab", "program hijab"}, {"\xE2\x9A\xA1 Cepat", "program cepat"},
      {"\xF0\x9F\x9B\xA0 Kustom", "program kustom"}, {"\xF0\x9F\x94\xA7 Manual", "#manual"}, {"\xF0\x9F\x94\x8C Relay", "#relay"},
      {"\xF0\x9F\x93\x8A Status", "status"}, {"\xE2\x8F\xB8 Jeda", "jeda"},
      {"\xE2\x96\xB6 Lanjutkan", "lanjutkan"}, {"\xE2\x8F\xB9 STOP", "?stop"},
      {"\xE2\x9A\x99 Pengaturan", "#settings"}, {"\xF0\x9F\x92\xA7 Air", "#air"},
      {"\xF0\x9F\x93\xB6 WiFi", "#wifi"}, {"\xF0\x9F\x94\x94 Notif", "#notif"},
      {"\xF0\x9F\x93\x9C Riwayat", "#history"},
  };
  sendWithKeyboard("\xF0\x9F\xA7\xBA Mesin Cuci\n" + statusText(), keyboard(btns, sizeof(btns) / sizeof(btns[0]), 3));
}

static void sendSubmenu(const String& name) {
  if (name == "manual") {
    static const Btn b[] = {
        {"Soak", "soak"}, {"Wash", "wash"}, {"Soak+Wash", "soak_and_wash"},
        {"Rinse", "rinse"}, {"Rinse+Pewangi", "rinse_parfume"}, {"Spin", "spin"},
        {"Isi air", "water_in"}, {"Buang air", "water_out"}, {"Pewangi", "add_parfume"},
        {"Motor 5 mnt", "motor_on"}, {"\xE2\xAC\x85 Menu", "#menu"},
    };
    sendWithKeyboard("\xF0\x9F\x94\xA7 Kontrol manual (butuh air untuk soak & motor)", keyboard(b, sizeof(b) / sizeof(b[0]), 3));
  } else if (name == "relay") {
    static const Btn b[] = {
        {"Kiri ON", "relay kiri on"}, {"Kiri OFF", "relay kiri off"},
        {"Kanan ON", "relay kanan on"}, {"Kanan OFF", "relay kanan off"},
        {"Isi ON", "relay isi on"}, {"Isi OFF", "relay isi off"},
        {"Buang ON", "relay buang on"}, {"Buang OFF", "relay buang off"},
        {"Spin ON", "relay spin on"}, {"Spin OFF", "relay spin off"},
        {"Pewangi ON", "relay pewangi on"}, {"Pewangi OFF", "relay pewangi off"},
        {"\xF0\x9F\x94\x81 Uji berurutan", "kalibrasi_relay"}, {"\xE2\x9B\x94 Semua OFF", "relay semua off"},
        {"\xE2\xAC\x85 Menu", "#menu"},
    };
    sendWithKeyboard("\xF0\x9F\x94\x8C Kontrol relay (hanya saat mesin siap, otomatis mati 60 dtk).\n"
                     "Lepas beban / tutup kran dulu. Motor kiri & kanan tidak pernah bisa nyala bersamaan.",
                     keyboard(b, sizeof(b) / sizeof(b[0]), 2));
  } else if (name == "settings") {
    static const Btn b[] = {
        {"Pola Normal", "pola normal"}, {"Pola Gentle", "pola gentle"}, {"Pola Hijab", "pola hijab"},
        {"Soak", "set_soak_session"}, {"Wash", "set_wash_time"}, {"Rinse", "set_rinse_time"},
        {"Pewangi", "set_jumlah_parfume"}, {"Spin", "set_spin_time"}, {"Delay", "set_delay"},
        {"Mulai nanti", "mulai_nanti"}, {"Selesai jam", "selesai_jam"}, {"Reset semua", "?reset_semua"},
        {"\xE2\xAC\x85 Menu", "#menu"},
    };
    sendWithKeyboard(settingsText(), keyboard(b, sizeof(b) / sizeof(b[0]), 3));
  } else if (name == "air") {
    static const Btn b[] = {
        {"Penuh", "level_air penuh"}, {"25%", "level_air 25"}, {"50%", "level_air 50"},
        {"75%", "level_air 75"}, {"Waktu", "level_air waktu"}, {"Cek air", "cek_air"},
        {"Waktu isi", "set_waktu_pengisian_air"}, {"Batas isi", "set_batas_waktu_pengisian_air"},
        {"Coba ulang", "set_coba_ulang_isi"}, {"Tunggu ulang", "set_tunggu_coba_ulang"},
        {"Buang: sensor", "mode_buang sensor"}, {"Buang: waktu", "mode_buang waktu"},
        {"Waktu buang", "set_waktu_pembuangan_air"}, {"Batas sesi", "set_batas_sesi_pembuangan_air"},
        {"Unjam", "set_unjam"}, {"Reset kalibrasi", "?kalibrasi_air"}, {"\xE2\xAC\x85 Menu", "#menu"},
    };
    sendWithKeyboard(airText(), keyboard(b, sizeof(b) / sizeof(b[0]), 3));
  } else if (name == "wifi") {
    static const Btn b[] = {
        {"Tambah", "wifi_tambah"}, {"Hapus", "wifi_hapus"}, {"Urutan", "wifi_urutan"}, {"\xE2\xAC\x85 Menu", "#menu"},
    };
    sendWithKeyboard(wifiListText(), keyboard(b, sizeof(b) / sizeof(b[0]), 3));
  } else if (name == "notif") {
    static const Btn b[] = {
        {"Lengkap", "notif lengkap"}, {"Ringkas", "notif ringkas"}, {"Senyap", "notif senyap"}, {"\xE2\xAC\x85 Menu", "#menu"},
    };
    sendWithKeyboard(String("\xF0\x9F\x94\x94 Mode notifikasi: ") + notifModeName(S.notif) +
                         "\nLengkap = semua log, Ringkas = awal/akhir tahap, Senyap = hanya selesai & error.",
                     keyboard(b, sizeof(b) / sizeof(b[0]), 3));
  } else if (name == "history") {
    sendText(historyText() + "\n\n" + statsText());
  } else {
    sendMenu();
  }
}

// ============================================================================
// Perintah
// ============================================================================
static void askPrompt() {
  sendText(String(pending.info.prompts[pending.step]) + "\n(kirim /batal untuk membatalkan)");
}

static void runAndReply(const String& cmd, const String& args) {
  String name = normalizeCommand(cmd);
  CmdResult r = executeCommand(cmd, args, SRC_TELEGRAM);
  if (name == "menu" || name == "start") {
    sendMenu();
    return;
  }
  sendText((r.ok ? "" : "\xE2\x9D\x8C ") + r.msg);
}

// Tangani satu perintah (dari teks "/cmd args" atau tombol). confirmed=true melewati konfirmasi.
static void handleCommand(const String& cmdRaw, const String& argsRaw, bool confirmed) {
  CmdInfo info;
  cmdInfo(cmdRaw, info);
  String args = argsRaw;
  args.trim();
  if (args.length() == 0) args = info.fixedArgs;

  bool needConfirm = info.danger;
  if (info.name == "stop") needConfirm = !machineIsIdle();
  if (needConfirm && !confirmed) {
    String kb = String("[[{\"text\":\"\xE2\x9C\x85 Ya, lanjutkan\",\"callback_data\":\"!") + info.name +
                "\"},{\"text\":\"\xE2\x9D\x8C Batal\",\"callback_data\":\"#menu\"}]]";
    sendWithKeyboard("Yakin menjalankan /" + info.name + "?", kb);
    return;
  }

  if (info.nPrompts > 0 && args.length() == 0) {
    pending.active = true;
    pending.info = info;
    pending.collected = "";
    pending.step = 0;
    askPrompt();
    return;
  }
  runAndReply(info.name, args);
}

static void feedPending(const String& text) {
  if (pending.collected.length()) pending.collected += pending.info.joiner;
  pending.collected += text;
  pending.step++;
  if (pending.step < pending.info.nPrompts) {
    askPrompt();
    return;
  }
  String cmd = pending.info.name;
  String args = pending.collected;
  pending.active = false;
  CmdResult r = executeCommand(cmd, args, SRC_TELEGRAM);
  if (!r.ok) {
    // Validasi gagal: ulangi dari awal
    sendText("\xE2\x9D\x8C " + r.msg);
    pending.active = true;
    pending.collected = "";
    pending.step = 0;
    askPrompt();
    return;
  }
  sendText(r.msg);
}

static void handleMessage(const String& text) {
  String t = text;
  t.trim();
  if (t.startsWith("/")) {
    int sp = t.indexOf(' ');
    String cmd = sp > 0 ? t.substring(0, sp) : t;
    String args = sp > 0 ? t.substring(sp + 1) : "";
    if (normalizeCommand(cmd) == "batal") {
      if (pending.active) {
        pending.active = false;
        sendText("Input dibatalkan. Nilai lama tetap digunakan.");
      } else {
        sendText("Tidak ada input yang sedang berjalan.");
      }
      return;
    }
    pending.active = false;
    handleCommand(cmd, args, false);
    return;
  }
  if (pending.active) {
    feedPending(t);
    return;
  }
  sendText("Ketik /menu untuk membuka menu atau /bantuan untuk daftar perintah.");
}

static void handleCallback(const String& data, const String& queryId) {
  bot.answerCallbackQuery(queryId);
  if (data.length() == 0) return;
  if (data[0] == '#') {
    sendSubmenu(data.substring(1));
    return;
  }
  bool confirmed = data[0] == '!';
  String d = (data[0] == '!' || data[0] == '?') ? data.substring(1) : data;
  pending.active = false;
  int sp = d.indexOf(' ');
  String cmd = sp > 0 ? d.substring(0, sp) : d;
  String args = sp > 0 ? d.substring(sp + 1) : "";
  handleCommand(cmd, args, confirmed);
}

static void handleUpdates(int count) {
  for (int i = 0; i < count; i++) {
    String chatId = String(bot.messages[i].chat_id);
    if (chatId != CHAT_ID) {
      bot.sendMessage(chatId, "Unauthorized user", "");
      continue;
    }
    if (bot.messages[i].type == "callback_query") {
      handleCallback(bot.messages[i].text, bot.messages[i].query_id);
    } else {
      logf(LOG_DEBUG, "Telegram dari %s: %s", bot.messages[i].from_name.c_str(), bot.messages[i].text.c_str());
      handleMessage(bot.messages[i].text);
    }
  }
}

// ============================================================================
// Task
// ============================================================================
static const char* kCommandList =
    "[{\"command\":\"menu\",\"description\":\"Menu tombol\"},"
    "{\"command\":\"status\",\"description\":\"Status mesin\"},"
    "{\"command\":\"program\",\"description\":\"Mulai program: normal/gentle/hijab/cepat/kustom\"},"
    "{\"command\":\"stop\",\"description\":\"Hentikan & matikan semua relay\"},"
    "{\"command\":\"jeda\",\"description\":\"Jeda proses\"},"
    "{\"command\":\"lanjutkan\",\"description\":\"Lanjutkan proses\"},"
    "{\"command\":\"cek_air\",\"description\":\"Cek level air\"},"
    "{\"command\":\"pengaturan\",\"description\":\"Lihat pengaturan\"},"
    "{\"command\":\"wifi_list\",\"description\":\"Daftar WiFi\"},"
    "{\"command\":\"notif\",\"description\":\"Mode notifikasi\"},"
    "{\"command\":\"riwayat\",\"description\":\"Riwayat siklus\"},"
    "{\"command\":\"statistik\",\"description\":\"Statistik\"},"
    "{\"command\":\"info\",\"description\":\"Info sistem\"},"
    "{\"command\":\"bantuan\",\"description\":\"Daftar perintah\"}]";

static void telegramTask(void*) {
  notifierInit();
  bool started = false;
  bool resumePrompted = false;
  uint32_t lastPoll = 0;
  bool lastOnline = false;

  for (;;) {
    bool online = netOnline();
    if (online != lastOnline) {
      notifierSetOnline(online);
      lastOnline = online;
    }
    notifierPoll();

    if (!online || otaBusy()) {
      vTaskDelay(pdMS_TO_TICKS(300));
      continue;
    }

    if (!started) {
      client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
      for (int i = 0; i < 20 && bot.getUpdates(bot.last_message_received + 1); i++) vTaskDelay(pdMS_TO_TICKS(100));
      logf(LOG_DEBUG, "Pesan Telegram lama dibersihkan");
      bot.setMyCommands(kCommandList);
      String boot = String("\xF0\x9F\xA7\xBA Mesin Cuci online (v") + FW_VERSION + ")\n";
      boot += "WiFi: " + netSsid() + " \xE2\x80\xA2 IP " + netIp() + "\n";
      boot += String("Dashboard: http://") + DEVICE_HOSTNAME + ".local\n";
      boot += "Hotspot: " + netApSsid() + " (http://192.168.4.1)\n";
      boot += String("Reset terakhir: ") + loggerResetReason();
      sendText(boot);
      started = true;
    }

    String msg;
    if (notifierNextMessage(msg)) sendText(msg);

    MachineStatus ms;
    machineGetStatus(ms);
    if (ms.resumePending && !resumePrompted) {
      static const char* kb =
          "[[{\"text\":\"\xE2\x96\xB6 Lanjutkan\",\"callback_data\":\"lanjutkan\"},"
          "{\"text\":\"\xE2\x9D\x8C Batalkan\",\"callback_data\":\"batalkan\"}]]";
      sendWithKeyboard(String("\xE2\x9A\xA1 Listrik sempat padam saat ") + ms.resumeInfo +
                           ".\nMesin tidak akan lanjut otomatis. Lanjutkan proses?",
                       kb);
      resumePrompted = true;
    }
    if (!ms.resumePending) resumePrompted = false;

    if (millis() - lastPoll >= 1000) {
      lastPoll = millis();
      int n = bot.getUpdates(bot.last_message_received + 1);
      while (n) {
        handleUpdates(n);
        n = bot.getUpdates(bot.last_message_received + 1);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void telegramBegin() {
  xTaskCreatePinnedToCore(telegramTask, "telegram", 12288, nullptr, 1, nullptr, 0);
}
