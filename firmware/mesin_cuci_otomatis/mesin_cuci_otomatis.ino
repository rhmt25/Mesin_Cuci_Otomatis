#include <WiFi.h>    // Libary untuk koneksi WiFi
#include <WiFiClientSecure.h>  // Libary untuk komunikasi TLS
#include <UniversalTelegramBot.h>  // Libary untuk mengontrol bot Telegram
#include <Preferences.h>

// Kredensial (WiFi & Telegram) ada di secrets.h, satu folder dengan sketch ini.
// secrets.h tidak di-commit; salin dari secrets.example.h.
#include "secrets.h"

#define CHAT_ID TELEGRAM_CHAT_ID  // Chat ID untuk menerima notifikasi dari bot

const char* defaultSsid = WIFI_SSID;
const char* defaultPassword = WIFI_PASSWORD;
const unsigned long wifiAttemptTimeout = 10000;
const int maxWifiAttempts = 3;

Preferences preferences;
String savedSsid;
String savedPassword;
enum PendingInput {
  NO_INPUT,
  SSID_INPUT,
  PASSWORD_INPUT,
  MODE_INPUT,
  SOAK_SESSION_INPUT,
  WASH_TIME_INPUT,
  RINSE_TIME_INPUT,
  PARFUME_INPUT,
  SPIN_TIME_INPUT,
  WATER_FILL_TIME_INPUT,
  WATER_DRAIN_TIME_INPUT,
  DRAIN_SESSION_LIMIT_INPUT,
  DELAY_HOUR_INPUT,
  DELAY_MINUTE_INPUT,
  DELAY_SECOND_INPUT,
  FILL_LIMIT_HOUR_INPUT,
  FILL_LIMIT_MINUTE_INPUT,
  FILL_LIMIT_SECOND_INPUT
};
PendingInput pendingInput = NO_INPUT;

void connectToWiFi();

void clearSavedWiFiCredentials() {
  savedSsid = "";
  savedPassword = "";
  preferences.remove("ssid");
  preferences.remove("password");
}

void clearSavedWiFiPassword() {
  savedPassword = "";
  preferences.remove("password");
}

// Token bot Telegram
#define BOTtoken TELEGRAM_BOT_TOKEN  // Token bot dari BotFather (diatur di secrets.h)

WiFiClientSecure client;  // Klien untuk komunikasi TLS
UniversalTelegramBot bot(BOTtoken, client);  // Bot Telegram

int botRequestDelay = 100;  // Delay untuk permintaan bot
unsigned long lastTimeBotRan;  // Waktu terakhir bot dijalankan



#define motor_right_rotation 18
#define motor_left_rotation 5
#define water_in 17
#define water_out 16
#define spin_motor 4
#define parfume 13
#define water_level 27

bool waterLevelStable(bool expectedLevel, unsigned long timeoutMs = 0) {
  int stableSeconds = 0;
  unsigned long startTime = millis();

  while (stableSeconds < 10) {
    bool currentLevel = digitalRead(water_level) == HIGH;
    if (currentLevel == expectedLevel) {
      stableSeconds++;
      Serial.println("Pembacaan level air stabil: " + String(stableSeconds) + "/10 detik");
    } else {
      stableSeconds = 0;
      Serial.println("Pembacaan level air berubah, menghitung ulang");
    }

    if (timeoutMs > 0 && millis() - startTime >= timeoutMs) {
      return false;
    }
    delay(1000);
  }

  return true;
}

// void start(){
//   bool mulai = false;
//   while (!mulai){
//     mulai=digitalRead(water_level);
//   }
//     Serial.println("Mulai proses");
//     bot.sendMessage(CHAT_ID, "Mulai proses", ""); // Mengirim notifikasi ke Telegram
//     mulai = false;
// }


String mode ="normal";
int soak_session=2;
int wash_time=15;
int rinse_time=10;
float jumlah_parfume=1.0f;
int spin_time=10;
int water_fill_time=30;
int water_drain_time=5;
int drain_session_limit=3;
int drain_session_count=0;
int delay_hour=0;
int delay_minute=0;
int delay_second=0;
int fill_limit_hour=0;
int fill_limit_minute=30;
int fill_limit_second=0;
String water_fill_mode="sensor";
String water_drain_mode="sensor";
bool processPaused = false;
bool pausedWhileClearingWaterForFill = false;

enum PausedProcessStage {
  NO_PAUSED_PROCESS,
  FULL_SOAK_AND_WASH,
  FULL_RINSE,
  FULL_RINSE_PARFUME,
  FULL_SOAK_AND_WASH_DRAIN,
  FULL_RINSE_DRAIN,
  FULL_RINSE_PARFUME_DRAIN
};
PausedProcessStage pausedProcessStage = NO_PAUSED_PROCESS;
bool pausedStandaloneDrain = false;

void saveProcessVariables() {
  preferences.putString("mode", mode);
  preferences.putInt("soak", soak_session);
  preferences.putInt("wash", wash_time);
  preferences.putInt("rinse", rinse_time);
  preferences.putFloat("parfume", jumlah_parfume);
  preferences.putInt("spin", spin_time);
  preferences.putInt("fill_time", water_fill_time);
  preferences.putInt("drain_time", water_drain_time);
  preferences.putInt("drain_limit", drain_session_limit);
  preferences.putInt("delay_hour", delay_hour);
  preferences.putInt("delay_minute", delay_minute);
  preferences.putInt("delay_second", delay_second);
  preferences.putInt("fl_h", fill_limit_hour);
  preferences.putInt("fl_m", fill_limit_minute);
  preferences.putInt("fl_s", fill_limit_second);
  preferences.putString("fill_mode", water_fill_mode);
  preferences.putString("drain_mode", water_drain_mode);
}

void loadProcessVariables() {
  mode = preferences.getString("mode", "normal");
  soak_session = preferences.getInt("soak", 2);
  wash_time = preferences.getInt("wash", 15);
  rinse_time = preferences.getInt("rinse", 10);
  jumlah_parfume = preferences.getFloat("parfume", 1.0f);
  spin_time = preferences.getInt("spin", 10);
  water_fill_time = preferences.getInt("fill_time", 30);
  water_drain_time = preferences.getInt("drain_time", 5);
  drain_session_limit = preferences.getInt("drain_limit", 3);
  delay_hour = preferences.getInt("delay_hour", 0);
  delay_minute = preferences.getInt("delay_minute", 0);
  delay_second = preferences.getInt("delay_second", 0);
  fill_limit_hour = preferences.getInt("fl_h", 0);
  fill_limit_minute = preferences.getInt("fl_m", 30);
  fill_limit_second = preferences.getInt("fl_s", 0);
  water_fill_mode = preferences.getString("fill_mode", "sensor");
  water_drain_mode = preferences.getString("drain_mode", "sensor");
  if (mode != "normal" && mode != "gentle" && mode != "hijab") mode = "normal";
  if (soak_session < 0 || soak_session > 1440) soak_session = 2;
  if (wash_time < 0 || wash_time > 1440) wash_time = 15;
  if (rinse_time < 0 || rinse_time > 1440) rinse_time = 10;
  if (jumlah_parfume < 0 || jumlah_parfume > 1440 ||
      static_cast<int>(jumlah_parfume * 2.0f) != jumlah_parfume * 2.0f) jumlah_parfume = 1.0f;
  if (spin_time < 0 || spin_time > 1440) spin_time = 10;
  if (water_fill_time <= 0 || water_fill_time > 1440) water_fill_time = 30;
  if (water_drain_time <= 0 || water_drain_time > 1440) water_drain_time = 5;
  if (drain_session_limit <= 0 || drain_session_limit > 1440) drain_session_limit = 3;
  if (delay_hour < 0 || delay_hour > 23) delay_hour = 0;
  if (delay_minute < 0 || delay_minute > 59) delay_minute = 0;
  if (delay_second < 0 || delay_second > 59) delay_second = 0;
  if (fill_limit_hour < 0 || fill_limit_hour > 23) fill_limit_hour = 0;
  if (fill_limit_minute < 0 || fill_limit_minute > 59) fill_limit_minute = 30;
  if (fill_limit_second < 0 || fill_limit_second > 59) fill_limit_second = 0;
  if (fill_limit_hour == 0 && fill_limit_minute == 0 && fill_limit_second == 0) {
    fill_limit_minute = 30;
  }
  if (water_fill_mode != "sensor" && water_fill_mode != "waktu") water_fill_mode = "sensor";
  if (water_drain_mode != "sensor" && water_drain_mode != "waktu") water_drain_mode = "sensor";
}

bool parseNonNegativeInt(const String &text, int &value) {
  if (text.length() == 0) {
    return false;
  }
  for (unsigned int i = 0; i < text.length(); i++) {
    if (!isDigit(text[i])) {
      return false;
    }
  }
  long parsedValue = text.toInt();
  if (parsedValue < 0 || parsedValue > 1440) {
    return false;
  }
  value = static_cast<int>(parsedValue);
  return true;
}

bool parsePositiveInt(const String &text, int &value) {
  if (!parseNonNegativeInt(text, value) || value == 0) {
    return false;
  }
  return true;
}

bool parseHalfCup(const String &text, float &value) {
  if (text.length() == 0) return false;
  String normalized = text;
  normalized.replace(',', '.');
  int separatorCount = 0;
  for (unsigned int i = 0; i < normalized.length(); i++) {
    char character = normalized[i];
    if (character == '.') {
      separatorCount++;
    } else if (!isDigit(character)) {
      return false;
    }
  }
  if (separatorCount > 1) return false;
  float parsedValue = normalized.toFloat();
  if (parsedValue < 0.0f || parsedValue > 1440.0f ||
      static_cast<int>(parsedValue * 2.0f) != parsedValue * 2.0f) return false;
  value = parsedValue;
  return true;
}

void resetProcessVariables() {
  mode = "normal";
  soak_session = 2;
  wash_time = 15;
  rinse_time = 10;
  jumlah_parfume = 1;
  spin_time = 10;
  water_fill_time = 30;
  water_drain_time = 5;
  drain_session_limit = 3;
  delay_hour = 0;
  delay_minute = 0;
  delay_second = 0;
  fill_limit_hour = 0;
  fill_limit_minute = 30;
  fill_limit_second = 0;
  water_fill_mode = "sensor";
  water_drain_mode = "sensor";
  processPaused = false;
  pausedProcessStage = NO_PAUSED_PROCESS;
  pausedStandaloneDrain = false;
  drain_session_count = 0;
  pausedWhileClearingWaterForFill = false;
  saveProcessVariables();
}

void waitBeforeProcess() {
  unsigned long delayMs = (static_cast<unsigned long>(delay_hour) * 3600UL +
                           static_cast<unsigned long>(delay_minute) * 60UL +
                           static_cast<unsigned long>(delay_second)) * 1000UL;
  if (delayMs == 0) return;
  bot.sendMessage(CHAT_ID, "Proses ditunda selama " + String(delay_hour) + " jam " +
                            String(delay_minute) + " menit " + String(delay_second) + " detik.", "");
  delay(delayMs);
  bot.sendMessage(CHAT_ID, "Delay selesai. Proses dimulai.", "");
}

unsigned long waterFillTimeoutMs() {
  unsigned long totalSeconds = static_cast<unsigned long>(fill_limit_hour) * 3600UL +
                               static_cast<unsigned long>(fill_limit_minute) * 60UL +
                               static_cast<unsigned long>(fill_limit_second);
  return totalSeconds * 1000UL;
}


bool water_out_on(bool ensureEmpty = false,
                  PausedProcessStage drainPauseStage = NO_PAUSED_PROCESS) {
  int sessionsToRun = ensureEmpty ? drain_session_limit : 1;
  while (!ensureEmpty || drain_session_count < sessionsToRun) {
    int session = ensureEmpty ? ++drain_session_count : 1;
    bot.sendMessage(CHAT_ID, ensureEmpty
                                ? "Sesi pembuangan air " + String(session) + "/" + String(sessionsToRun) + "..."
                                : "Membuang air...", "");
    digitalWrite(water_out, LOW);
    Serial.println("Membuang air... sesi " + String(session));
    if (water_drain_mode == "waktu") {
      delay(static_cast<unsigned long>(water_drain_time) * 60UL * 1000UL);
    } else {
      delay(5000UL * 60UL);
    }
    digitalWrite(water_out, HIGH);

    if (!ensureEmpty && water_drain_mode == "waktu") {
      Serial.println("Pembuangan air selesai berdasarkan waktu.");
      bot.sendMessage(CHAT_ID, "Pembuangan air selesai berdasarkan waktu.", "");
      return true;
    }

    Serial.println("Mengecek sisa air...");
    bot.sendMessage(CHAT_ID, "Mengecek apakah air sudah benar-benar habis...", "");
    bool waterStillDetected = !waterLevelStable(false, 10UL * 1000UL);

    if (!waterStillDetected) {
      Serial.println("Air sudah habis!");
      bot.sendMessage(CHAT_ID, "Air sudah benar-benar habis.", "");
      processPaused = false;
      pausedStandaloneDrain = false;
      drain_session_count = 0;
      return true;
    }

    Serial.println("Air masih tersisa!");
    bot.sendMessage(CHAT_ID, "Air masih tersisa setelah pembuangan.", "");
    if (!ensureEmpty) return true;
    if (drain_session_count < sessionsToRun) {
      bot.sendMessage(CHAT_ID, "Air masih terdeteksi. Mengulangi sesi pembuangan.", "");
    }
  }

  if (ensureEmpty) {
    processPaused = true;
    if (drainPauseStage == NO_PAUSED_PROCESS) {
      pausedStandaloneDrain = true;
    } else {
      pausedProcessStage = drainPauseStage;
    }
    Serial.println("Batas sesi pembuangan air tercapai. Proses dijeda.");
    bot.sendMessage(CHAT_ID, "Batas sesi pembuangan air tercapai. Proses dijeda. Gunakan /lanjutkan_proses untuk mencoba lagi.", "");
    return false;
  }
  return true;
}

bool water_in_on(bool resumeAfterPause = false){
  delay(2000);
  unsigned long fillTimeout = waterFillTimeoutMs();
  unsigned long fillStartTime = millis();
  if (water_fill_mode == "sensor") {
    if (resumeAfterPause && pausedWhileClearingWaterForFill) {
      if (!water_out_on(true)) {
        pausedStandaloneDrain = false;
        return false;
      }
      pausedWhileClearingWaterForFill = false;
    }

    if (resumeAfterPause && waterLevelStable(true, 10UL * 1000UL)) {
      processPaused = false;
      Serial.println("Air masih cukup. Melanjutkan proses tanpa mengisi ulang.");
      bot.sendMessage(CHAT_ID, "Air sudah cukup. Melanjutkan proses tanpa mengisi ulang.", "");
      return true;
    }

    bool display=true;
    while (!resumeAfterPause && !waterLevelStable(false, 10UL * 1000UL)) {
      if (millis() - fillStartTime >= fillTimeout) {
        processPaused = true;
        Serial.println("Batas waktu pengisian air tercapai saat menunggu kondisi awal.");
        bot.sendMessage(CHAT_ID, "Batas waktu pengisian air tercapai. Proses dijeda.", "");
        return false;
      }
      if (display==true){
        Serial.println("Menunggu air kosong...");
        bot.sendMessage(CHAT_ID, "Menunggu air kosong...", "");
        display=false;
      }
      delay(500);
      pausedWhileClearingWaterForFill = true;
      if (!water_out_on(true)) {
        pausedStandaloneDrain = false;
        return false;
      }
      pausedWhileClearingWaterForFill = false;
    }
  }
  Serial.println("Mengisi air...");
  bot.sendMessage(CHAT_ID, "Mengisi air...", "");
  digitalWrite(water_in, LOW);
  if (water_fill_mode == "waktu") {
    delay(static_cast<unsigned long>(water_fill_time) * 60UL * 1000UL);
  } else {
    unsigned long elapsedTime = millis() - fillStartTime;
    unsigned long remainingTimeout = elapsedTime >= fillTimeout ? 0 : fillTimeout - elapsedTime;
    if (remainingTimeout == 0 || !waterLevelStable(true, remainingTimeout)) {
      digitalWrite(water_in, HIGH);
      processPaused = true;
      Serial.println("Waktu Mengisi Air Sudah Melebihi Batas Waktu, Proses di jeda");
      bot.sendMessage(CHAT_ID, "Waktu Mengisi Air Sudah Melebihi Batas Waktu, Proses di jeda", "");
      return false;
    }
  }
  if (water_fill_mode == "sensor" && millis() - fillStartTime >= fillTimeout) {
    digitalWrite(water_in, HIGH);
    processPaused = true;
    Serial.println("Waktu Mengisi Air Sudah Melebihi Batas Waktu, Proses di jeda");
    bot.sendMessage(CHAT_ID, "Waktu Mengisi Air Sudah Melebihi Batas Waktu, Proses di jeda", "");
    return false;
  }
  digitalWrite(water_in, HIGH);
  processPaused = false;
  Serial.println("Air sudah penuh!");
  bot.sendMessage(CHAT_ID, "Air sudah penuh!", "");
  return true;
}

void motor_on(int total_time) {
  
  bot.sendMessage(CHAT_ID, "Motor ON", "");
  Serial.println("Motor ON");
  total_time=total_time*60*1000;
  int on_time=0;
  int off_time=2500;
  if (mode=="normal"){
    on_time=7000;
    off_time=4000;
  }
  if (mode=="gentle"){
    on_time=5300;
    off_time=7000;
  }
  if (mode=="hijab"){
    on_time=3000;
    off_time=9000;
  }
  int i=1;
  while (total_time>0){
    digitalWrite(motor_left_rotation, LOW);
    delay(on_time);
    total_time = total_time - on_time;
    digitalWrite(motor_left_rotation, HIGH);
    delay(off_time);
    total_time = total_time - off_time;
    digitalWrite(motor_right_rotation, LOW);
    delay(on_time);
    total_time = total_time - on_time;
    digitalWrite(motor_right_rotation, HIGH);
    delay(off_time);
    total_time = total_time - off_time;
    Serial.println("Putaran Ke");
    Serial.println(i);
    i++;
  }
  digitalWrite(motor_left_rotation, HIGH);
  digitalWrite(motor_right_rotation, HIGH);
  
  Serial.println("Motor OFF");
  bot.sendMessage(CHAT_ID, "Motor OFF", "");
}



void add_parfume(){
  if(jumlah_parfume>0){
    Serial.println("Memasukkan pewangi...");
    bot.sendMessage(CHAT_ID, "Memasukkan pewangi...", "");
    digitalWrite(parfume, LOW);
    unsigned long pumpTime = static_cast<unsigned long>(jumlah_parfume * 5000.0f) + 3000UL;
    delay(pumpTime);
    digitalWrite(parfume, HIGH);
    Serial.println("Pewangi selesai dimasukkan!");
    bot.sendMessage(CHAT_ID, "Pewangi selesai dimasukkan!", "");
  }
  else {
    Serial.println("Pewangi tidak dimasukkan!");
    bot.sendMessage(CHAT_ID, "Pewangi tidak dimasukkan!", "");
  }

}

void soak (){
  if(soak_session>0){
    
    bot.sendMessage(CHAT_ID, "Soak proses dimulai", "");
    Serial.println("Soak proses dimulai");
    for(int i=1; i<=soak_session;i++){
      motor_on(1);
      delay(4000*60);
      Serial.println("Soak ke ");
      Serial.println(i);
    }
    Serial.println("Soak proses selesai");
    bot.sendMessage(CHAT_ID, "Soak proses selesai", "");
  }
  else {
    Serial.println("Soak proses tidak dilakukan");
    bot.sendMessage(CHAT_ID, "Soak proses tidak dilakukan", "");
  }
}


void wash (){
  if(wash_time>0){
    Serial.println("Wash proses dimulai");
    bot.sendMessage(CHAT_ID, "Wash proses dimulai", "");
    if (!water_in_on()) {
      return;
    }
    delay(500);
    
    motor_on(wash_time);
    delay(500);

    if (!water_out_on(true)) return;
    delay(500);

    Serial.println("Wash proses selesai");
    bot.sendMessage(CHAT_ID, "Wash proses selesai", "");
  }
  else {
    Serial.println("Wash proses tidak dilakukan");
    bot.sendMessage(CHAT_ID, "Wash proses tidak dilakukan", "");
  }
}

bool soak_and_wash (bool waterAlreadyFilled = false,
                    PausedProcessStage drainPauseStage = NO_PAUSED_PROCESS){
  if(soak_session>0 && wash_time>0){
    if (!waterAlreadyFilled && !water_in_on()) {
      return false;
    }
    delay(500);

    soak();

    Serial.println("Wash proses dimulai");
    bot.sendMessage(CHAT_ID, "Wash proses dimulai", "");
   
    motor_on(wash_time);
    delay(500);

    if (!water_out_on(true, drainPauseStage)) return false;
    delay(500);

    Serial.println("Wash proses selesai");
    bot.sendMessage(CHAT_ID, "Wash proses selesai", "");
    return true;
  }
  else {
    Serial.println("Soak and Wash proses tidak dilakukan");
    bot.sendMessage(CHAT_ID, "Soak and Wash proses tidak dilakukan", "");
    return true;
  }
}

bool rinse (bool waterAlreadyFilled = false,
            PausedProcessStage drainPauseStage = NO_PAUSED_PROCESS){
  if (rinse_time>0){
    Serial.println("Rinse proses dimulai");
    bot.sendMessage(CHAT_ID, "Rinse proses dimulai", "");
      if (!waterAlreadyFilled && !water_in_on()) {
        return false;
      }
      delay(500);
      
      motor_on(rinse_time);
      delay(500);

      if (!water_out_on(true, drainPauseStage)) return false;
      delay(500);

    Serial.println("Rinse proses selesai");
    bot.sendMessage(CHAT_ID, "Rinse proses selesai", "");
    return true;
    }
  else {
    Serial.println("Rinse proses tidak dilakukan");   
    bot.sendMessage(CHAT_ID, "Rinse proses tidak dilakukan", "");
    return true;
  }
}

bool rinse_parfume(bool waterAlreadyFilled = false,
                   PausedProcessStage drainPauseStage = NO_PAUSED_PROCESS) {
  if (rinse_time>0){
    Serial.println("Rinse dengan pewangi proses dimulai");
    bot.sendMessage(CHAT_ID, "Rinse dengan pewangi proses dimulai", "");
    
    if (!waterAlreadyFilled && !water_in_on()) {
      return false;
    }
    delay(500);

    add_parfume();
    delay(500);

    motor_on(rinse_time);
    delay(500);

    if (!water_out_on(true, drainPauseStage)) return false;
    delay(500);

    Serial.println("Rinse dengan pewangi proses selesai");
    bot.sendMessage(CHAT_ID, "Rinse dengan pewangi proses selesai", "");
    return true;
  }
  else {
    Serial.println("Rinse parfume proses tidak dilakukan");    
    bot.sendMessage(CHAT_ID, "Rinse parfume proses tidak dilakukan", "");
    return true;
  }
}


void spin (){
  if(spin_time>0){
    Serial.println("Spin proses dimulai");
    bot.sendMessage(CHAT_ID, "Spin proses dimulai", "");
    
    digitalWrite(spin_motor, LOW);
    delay(spin_time*60*1000);
    digitalWrite(spin_motor, HIGH);

    Serial.println("Spin proses selesai");
    bot.sendMessage(CHAT_ID, "Spin proses selesai", "");
    }
  else {
    Serial.println("Spin proses tidak dilakukan");
    bot.sendMessage(CHAT_ID, "Spin proses tidak dilakukan", "");
  }
}

void runFullProcess(bool resumeAfterWaterFill = false) {
  if (pausedProcessStage == NO_PAUSED_PROCESS) {
    pausedProcessStage = FULL_SOAK_AND_WASH;
  }

  if (pausedProcessStage == FULL_SOAK_AND_WASH_DRAIN) {
    if (!water_out_on(true, FULL_SOAK_AND_WASH_DRAIN)) return;
    pausedProcessStage = FULL_RINSE;
    resumeAfterWaterFill = false;
  }

  if (pausedProcessStage == FULL_RINSE_DRAIN) {
    if (!water_out_on(true, FULL_RINSE_DRAIN)) return;
    pausedProcessStage = FULL_RINSE_PARFUME;
    resumeAfterWaterFill = false;
  }

  if (pausedProcessStage == FULL_RINSE_PARFUME_DRAIN) {
    if (!water_out_on(true, FULL_RINSE_PARFUME_DRAIN)) return;
    pausedProcessStage = NO_PAUSED_PROCESS;
    bot.sendMessage(CHAT_ID, "Full proses selesai", "");
    return;
  }

  if (pausedProcessStage == FULL_SOAK_AND_WASH) {
    if (resumeAfterWaterFill && !water_in_on(true)) return;
    if (!soak_and_wash(resumeAfterWaterFill, FULL_SOAK_AND_WASH_DRAIN)) {
      processPaused = true;
      return;
    }
    if (processPaused) return;
    pausedProcessStage = FULL_RINSE;
    resumeAfterWaterFill = false;
  }

  if (pausedProcessStage == FULL_RINSE) {
    if (resumeAfterWaterFill && !water_in_on(true)) return;
    if (!rinse(resumeAfterWaterFill, FULL_RINSE_DRAIN)) {
      processPaused = true;
      return;
    }
    if (processPaused) return;
    pausedProcessStage = FULL_RINSE_PARFUME;
    resumeAfterWaterFill = false;
  }

  if (pausedProcessStage == FULL_RINSE_PARFUME) {
    if (resumeAfterWaterFill && !water_in_on(true)) return;
    if (!rinse_parfume(resumeAfterWaterFill, FULL_RINSE_PARFUME_DRAIN)) {
      processPaused = true;
      return;
    }
    if (processPaused) return;
    pausedProcessStage = NO_PAUSED_PROCESS;
    bot.sendMessage(CHAT_ID, "Full proses selesai", "");
  }
}


// Fungsi untuk menangani pesan baru dari bot
void handleNewMessages(int numNewMessages) {

  for (int i = 0; i < numNewMessages; i++) { // Iterasi melalui setiap pesan baru
    String chat_id = String(bot.messages[i].chat_id); // Mendapatkan ID chat dari pesan
    if (chat_id != CHAT_ID) { // Memeriksa jika pengguna tidak diotorisasi
      
      bot.sendMessage(chat_id, "Unauthorized user", ""); // Mengirim pesan pengguna tidak diotorisasi
      continue; // Melanjutkan ke code berikutnya
    }

    String text = bot.messages[i].text; // Mendapatkan teks pesan
    String from_name = bot.messages[i].from_name; // Mendapatkan nama pengguna
    Serial.println("Pesan Dari " + from_name + " : " + text);

    if (pendingInput != NO_INPUT && text == "/batal") {
      pendingInput = NO_INPUT;
      bot.sendMessage(chat_id, "Input dibatalkan. Nilai lama tetap digunakan.", "");
      continue;
    }

    if (pendingInput != NO_INPUT && !text.startsWith("/")) {
      if (pendingInput == SSID_INPUT) {
        savedSsid = text;
        preferences.putString("ssid", savedSsid);
        pendingInput = NO_INPUT;
        bot.sendMessage(chat_id, "SSID berhasil disimpan. Gunakan /set_password untuk menyimpan password.", "");
      } else if (pendingInput == PASSWORD_INPUT) {
        savedPassword = text;
        preferences.putString("password", savedPassword);
        pendingInput = NO_INPUT;
        bot.sendMessage(chat_id, "Password berhasil disimpan. Mencoba koneksi dengan konfigurasi baru...", "");
        connectToWiFi();
      } else if (pendingInput == PARFUME_INPUT) {
        float parsedValue = 0.0f;
        if (!parseHalfCup(text, parsedValue)) {
          bot.sendMessage(chat_id, "Nilai parfum tidak valid. Gunakan kelipatan 0,5, misalnya 0,5 atau 1. Gunakan koma atau titik sebagai pemisah desimal.", "");
          continue;
        }
        jumlah_parfume = parsedValue;
        saveProcessVariables();
        pendingInput = NO_INPUT;
        bot.sendMessage(chat_id, "Jumlah parfum berhasil disimpan. Durasi pompa: " + String(jumlah_parfume, 1) + " cup x 5 detik + 3 detik awal.", "");
      } else if (pendingInput == DELAY_HOUR_INPUT ||
                 pendingInput == DELAY_MINUTE_INPUT ||
                 pendingInput == DELAY_SECOND_INPUT) {
        int parsedValue = 0;
        bool validValue = parseNonNegativeInt(text, parsedValue);
        int maximumValue = pendingInput == DELAY_HOUR_INPUT ? 23 : 59;
        validValue = validValue && parsedValue <= maximumValue;
        if (!validValue) {
          bot.sendMessage(chat_id, "Nilai delay tidak valid. Jam harus 0-23, sedangkan menit dan detik harus 0-59.", "");
          continue;
        }
        if (pendingInput == DELAY_HOUR_INPUT) {
          delay_hour = parsedValue;
          pendingInput = DELAY_MINUTE_INPUT;
          bot.sendMessage(chat_id, "Jam tersimpan. Kirim menit delay (0-59).", "");
        } else if (pendingInput == DELAY_MINUTE_INPUT) {
          delay_minute = parsedValue;
          pendingInput = DELAY_SECOND_INPUT;
          bot.sendMessage(chat_id, "Menit tersimpan. Kirim detik delay (0-59).", "");
        } else {
          delay_second = parsedValue;
          saveProcessVariables();
          pendingInput = NO_INPUT;
          bot.sendMessage(chat_id, "Delay berhasil disimpan: " + String(delay_hour) + " jam " + String(delay_minute) + " menit " + String(delay_second) + " detik.", "");
        }
      } else if (pendingInput == FILL_LIMIT_HOUR_INPUT ||
                 pendingInput == FILL_LIMIT_MINUTE_INPUT ||
                 pendingInput == FILL_LIMIT_SECOND_INPUT) {
        int parsedValue = 0;
        bool validValue = parseNonNegativeInt(text, parsedValue);
        int maximumValue = pendingInput == FILL_LIMIT_HOUR_INPUT ? 23 : 59;
        validValue = validValue && parsedValue <= maximumValue;
        if (!validValue) {
          bot.sendMessage(chat_id, "Batas waktu pengisian tidak valid. Jam harus 0-23, sedangkan menit dan detik harus 0-59.", "");
          continue;
        }
        if (pendingInput == FILL_LIMIT_HOUR_INPUT) {
          fill_limit_hour = parsedValue;
          pendingInput = FILL_LIMIT_MINUTE_INPUT;
          bot.sendMessage(chat_id, "Jam batas pengisian tersimpan. Kirim menit (0-59).", "");
        } else if (pendingInput == FILL_LIMIT_MINUTE_INPUT) {
          fill_limit_minute = parsedValue;
          pendingInput = FILL_LIMIT_SECOND_INPUT;
          bot.sendMessage(chat_id, "Menit batas pengisian tersimpan. Kirim detik (0-59).", "");
        } else {
          fill_limit_second = parsedValue;
          if (waterFillTimeoutMs() == 0) {
            bot.sendMessage(chat_id, "Batas waktu tidak boleh 0. Masukkan nilai lebih dari 0.", "");
            continue;
          }
          saveProcessVariables();
          pendingInput = NO_INPUT;
          bot.sendMessage(chat_id, "Batas waktu pengisian berhasil disimpan: " + String(fill_limit_hour) + " jam " + String(fill_limit_minute) + " menit " + String(fill_limit_second) + " detik.", "");
        }
      } else {
        int parsedValue = 0;
        bool validValue = parseNonNegativeInt(text, parsedValue);
        if (pendingInput == WATER_FILL_TIME_INPUT || pendingInput == WATER_DRAIN_TIME_INPUT ||
          pendingInput == DRAIN_SESSION_LIMIT_INPUT) {
          validValue = parsePositiveInt(text, parsedValue);
        }
        if (!validValue) {
          bot.sendMessage(chat_id, "Nilai tidak valid. Masukkan bilangan bulat sesuai batas yang diminta.", "");
          continue;
        }
        if (pendingInput == SOAK_SESSION_INPUT) soak_session = parsedValue;
        if (pendingInput == WASH_TIME_INPUT) wash_time = parsedValue;
        if (pendingInput == RINSE_TIME_INPUT) rinse_time = parsedValue;
        if (pendingInput == SPIN_TIME_INPUT) spin_time = parsedValue;
        if (pendingInput == WATER_FILL_TIME_INPUT) water_fill_time = parsedValue;
        if (pendingInput == WATER_DRAIN_TIME_INPUT) water_drain_time = parsedValue;
        if (pendingInput == DRAIN_SESSION_LIMIT_INPUT) drain_session_limit = parsedValue;
        saveProcessVariables();
        pendingInput = NO_INPUT;
        bot.sendMessage(chat_id, "Nilai berhasil disimpan.", "");
      }
      continue;
    }

    if (text == "/start") { // Jika pesan adalah perintah /start
      String control = "Selamat Datang, " + from_name + ".\n"; // Pesan sambutan

      control += "Menu:\n\n";
      control += "/cek_variabel - Cek nilai variabel\n\n";
      control += "/set_ssid - Simpan SSID WiFi\n\n";
      control += "/set_password - Simpan password WiFi\n\n";
      control += "/hapus_ssid - Hapus pasangan SSID dan password\n\n";
      control += "/hapus_password - Hapus pasangan SSID dan password\n\n";
      control += "/batal - Batalkan input SSID/password\n\n";
      control += "/reset_variabel - Reset semua variabel\n\n";
      control += "/set_delay - Atur delay proses (jam, menit, detik)\n\n";
      control += "/reset_delay - Reset delay menjadi 0\n\n";
      control += "/set_batas_waktu_pengisian_air - Atur batas waktu pengisian (jam, menit, detik)\n\n";
      control += "/reset_batas_waktu_pengisian_air - Reset batas waktu pengisian\n\n";
      control += "/set_batas_sesi_pembungan_air - Atur batas sesi pembuangan sebelum jeda\n\n";
      control += "/reset_batas_sesi_pembungan_air - Reset batas sesi pembuangan\n\n";
      control += "/cek_air - Cek status air\n\n\n";
      
      control += "Mode:\n\n";
      control += "/set_mode_normal\n\n";
      control += "/set_mode_gentle\n\n";
      control += "/set_mode_hijab\n\n\n";
      
      control += "Soak:\n\n";
      control += "/set_soak_session\n\n";
      control += "/reset_soak_session\n\n\n";
      
      control += "Wash:\n\n";
      control += "/set_wash_time\n\n";
      control += "/reset_wash_time\n\n\n";
      
      control += "Rinse:\n\n";
      control += "/set_rinse_time\n\n";
      control += "/reset_rinse_time\n\n\n";
      
      control += "Parfume:\n\n";
      control += "/set_jumlah_parfume\n\n";
      control += "/reset_jumlah_parfume\n\n\n";
      
      control += "Spin:\n\n";
      control += "/set_spin_time\n\n";
      control += "/reset_spin_time\n\n\n";

      control += "Air:\n\n";
      control += "/set_waktu_pengisian_air\n\n";
      control += "/reset_waktu_pengisian_air\n\n";
      control += "/set_waktu_pembuangan_air\n\n";
      control += "/reset_waktu_pembuangan_air\n\n";
      control += "/mode_waktu_pengisian_air\n\n";
      control += "/mode_sensor_pengisian_air\n\n";
      control += "/mode_waktu_pembuangan_air\n\n";
      control += "/mode_sensor_pembuangan_air\n\n\n";
      
      control += "Proses:\n\n";
      control += "/full_proses\n\n";
      control += "/soak\n\n";
      control += "/wash\n\n";
      control += "/soak_and_wash\n\n";
      control += "/rinse\n\n";
      control += "/rinse_parfume\n\n";
      control += "/spin\n\n";
      control += "/water_in\n\n";
      control += "/water_out\n\n";
      control += "/add_parfume\n\n";
      control += "/motor_on\n\n";
      control += "/lanjutkan_proses\n\n";
      
      bot.sendMessage(CHAT_ID, control, ""); // Mengirim pesan daftar menu
    }

    if (text == "/cek_variabel") { 
      String report = "Variabel :\n";
      report += "SSID WiFi\t: " + (savedSsid.length() > 0 ? savedSsid : String("(belum diset)")) + "\n";
      report += "Password WiFi\t: " + (savedPassword.length() > 0 ? savedPassword : String("(kosong)")) + "\n";
      report += "Mode\t: "+mode+"\n";
      report += "Soak\t: " + String(soak_session) + " sesi\n";
      report += "Wash\t: " + String(wash_time) + " menit\n";
      report += "Rinse\t: " + String(rinse_time) + " menit\n";
      report += "Parfume\t: " + String(jumlah_parfume, 1) + " cup\n";
      report += "Spin\t: " + String(spin_time) + " menit\n";
      report += "Waktu pengisian air\t: " + String(water_fill_time) + " menit\n";
      report += "Mode pengisian air\t: " + water_fill_mode + "\n";
      report += "Waktu pembuangan air\t: " + String(water_drain_time) + " menit\n";
      report += "Batas sesi pembuangan air\t: " + String(drain_session_limit) + " sesi\n";
      report += "Mode pembuangan air\t: " + water_drain_mode + "\n";
      report += "Delay\t: " + String(delay_hour) + " jam " + String(delay_minute) + " menit " + String(delay_second) + " detik\n";
      report += "Batas pengisian air\t: " + String(fill_limit_hour) + " jam " + String(fill_limit_minute) + " menit " + String(fill_limit_second) + " detik\n";
    
    bot.sendMessage(CHAT_ID, report, "");
    }
    if (text == "/set_ssid") {
      pendingInput = SSID_INPUT;
      bot.sendMessage(chat_id, "Silakan kirim SSID WiFi sekarang.", "");
    }
    if (text == "/set_password") {
      pendingInput = PASSWORD_INPUT;
      bot.sendMessage(chat_id, "Silakan kirim password WiFi sekarang. Untuk WiFi tanpa password, gunakan /hapus_password setelah SSID tersimpan.", "");
    }
    if (text == "/hapus_ssid" || text == "/hapus_password") {
      pendingInput = NO_INPUT;
      if (text == "/hapus_ssid") {
        clearSavedWiFiCredentials();
        bot.sendMessage(chat_id, "SSID dan password tersimpan telah dihapus. Menggunakan WiFi default.", "");
      } else {
        clearSavedWiFiPassword();
        bot.sendMessage(chat_id, "Password dihapus. SSID tersimpan akan digunakan tanpa password.", "");
      }
      connectToWiFi();
    }
    if (text == "/reset_variabel") {
      resetProcessVariables();

      Serial.println("Semua variabel telah direset ke default.");
      bot.sendMessage(CHAT_ID, "Semua variabel telah direset ke default.", "");
    }

    if (text == "/cek_air") { 
      if (waterLevelStable(true, 10UL * 1000UL)) {
        Serial.println("Air sudah mencukupi!");
        bot.sendMessage(CHAT_ID, "Air sudah mencukupi!", "");
      } else if (waterLevelStable(false, 10UL * 1000UL)) {
        Serial.println("Air belum mencukupi!");
        bot.sendMessage(CHAT_ID, "Air belum mencukupi!", "");
      } else {
        Serial.println("Level air tidak stabil!");
        bot.sendMessage(CHAT_ID, "Level air tidak stabil setelah 10 detik pengecekan.", "");
      }
    }

    // MODE
    if (text == "/set_mode_normal") { 
      mode="normal";
      saveProcessVariables();
      Serial.println("Mode normal digunakan");
      bot.sendMessage(CHAT_ID, "Mode normal digunakan", "");
    }
    if (text == "/set_mode_gentle") { 
      mode="gentle";
      saveProcessVariables();
      Serial.println("Mode gentle digunakan");
      bot.sendMessage(CHAT_ID, "Mode gentle digunakan", "");
    }
    if (text == "/set_mode_hijab") { 
      mode="hijab";
      saveProcessVariables();
      Serial.println("Mode hijab digunakan");
      bot.sendMessage(CHAT_ID, "Mode hijab digunakan", "");
    }
    if (text == "/set_soak_session") { pendingInput = SOAK_SESSION_INPUT; bot.sendMessage(chat_id, "Kirim jumlah sesi soak (bilangan bulat 0-1440).", ""); }
    if (text == "/reset_soak_session") { soak_session = 2; saveProcessVariables(); bot.sendMessage(chat_id, "Soak session direset menjadi 2.", ""); }
    if (text == "/set_wash_time") { pendingInput = WASH_TIME_INPUT; bot.sendMessage(chat_id, "Kirim waktu wash dalam menit (bilangan bulat 0-1440).", ""); }
    if (text == "/reset_wash_time") { wash_time = 15; saveProcessVariables(); bot.sendMessage(chat_id, "Wash time direset menjadi 15 menit.", ""); }
    if (text == "/set_rinse_time") { pendingInput = RINSE_TIME_INPUT; bot.sendMessage(chat_id, "Kirim waktu rinse dalam menit (bilangan bulat 0-1440).", ""); }
    if (text == "/reset_rinse_time") { rinse_time = 10; saveProcessVariables(); bot.sendMessage(chat_id, "Rinse time direset menjadi 10 menit.", ""); }
    if (text == "/set_jumlah_parfume") { pendingInput = PARFUME_INPUT; bot.sendMessage(chat_id, "Kirim jumlah parfum dalam cup dengan kelipatan 0,5, misalnya 0,5 atau 1. Bisa menggunakan koma atau titik.", ""); }
    if (text == "/reset_jumlah_parfume") { jumlah_parfume = 1; saveProcessVariables(); bot.sendMessage(chat_id, "Jumlah parfume direset menjadi 1 cup.", ""); }
    if (text == "/set_delay") { pendingInput = DELAY_HOUR_INPUT; bot.sendMessage(chat_id, "Kirim jam delay (0-23).", ""); }
    if (text == "/reset_delay") { delay_hour = 0; delay_minute = 0; delay_second = 0; pendingInput = NO_INPUT; saveProcessVariables(); bot.sendMessage(chat_id, "Delay direset menjadi 0 jam 0 menit 0 detik.", ""); }
    if (text == "/set_batas_waktu_pengisian_air") { pendingInput = FILL_LIMIT_HOUR_INPUT; bot.sendMessage(chat_id, "Kirim jam batas waktu pengisian air (0-23).", ""); }
    if (text == "/reset_batas_waktu_pengisian_air") { fill_limit_hour = 0; fill_limit_minute = 30; fill_limit_second = 0; pendingInput = NO_INPUT; saveProcessVariables(); bot.sendMessage(chat_id, "Batas waktu pengisian direset menjadi 0 jam 30 menit 0 detik.", ""); }
    if (text == "/set_batas_sesi_pembungan_air") { pendingInput = DRAIN_SESSION_LIMIT_INPUT; bot.sendMessage(chat_id, "Kirim batas sesi pembuangan air sebelum jeda (bilangan bulat 1-1440).", ""); }
    if (text == "/reset_batas_sesi_pembungan_air") { drain_session_limit = 3; saveProcessVariables(); bot.sendMessage(chat_id, "Batas sesi pembuangan air direset menjadi 3 sesi.", ""); }
    if (text == "/set_spin_time") { pendingInput = SPIN_TIME_INPUT; bot.sendMessage(chat_id, "Kirim waktu spin dalam menit (bilangan bulat 0-1440).", ""); }
    if (text == "/reset_spin_time") { spin_time = 10; saveProcessVariables(); bot.sendMessage(chat_id, "Spin time direset menjadi 10 menit.", ""); }
    if (text == "/set_waktu_pengisian_air") { pendingInput = WATER_FILL_TIME_INPUT; bot.sendMessage(chat_id, "Kirim waktu pengisian air dalam menit (bilangan bulat lebih dari 0).", ""); }
    if (text == "/reset_waktu_pengisian_air") { water_fill_time = 30; saveProcessVariables(); bot.sendMessage(chat_id, "Waktu pengisian air direset menjadi 30 menit.", ""); }
    if (text == "/set_waktu_pembuangan_air") { pendingInput = WATER_DRAIN_TIME_INPUT; bot.sendMessage(chat_id, "Kirim waktu pembuangan air dalam menit (bilangan bulat lebih dari 0).", ""); }
    if (text == "/reset_waktu_pembuangan_air") { water_drain_time = 5; saveProcessVariables(); bot.sendMessage(chat_id, "Waktu pembuangan air direset menjadi 5 menit.", ""); }
    if (text == "/mode_waktu_pengisian_air") { water_fill_mode = "waktu"; saveProcessVariables(); bot.sendMessage(chat_id, "Mode pengisian air menggunakan waktu.", ""); }
    if (text == "/mode_sensor_pengisian_air") { water_fill_mode = "sensor"; saveProcessVariables(); bot.sendMessage(chat_id, "Mode pengisian air menggunakan sensor.", ""); }
    if (text == "/mode_waktu_pembuangan_air") { water_drain_mode = "waktu"; saveProcessVariables(); bot.sendMessage(chat_id, "Mode pembuangan air menggunakan waktu.", ""); }
    if (text == "/mode_sensor_pembuangan_air") { water_drain_mode = "sensor"; saveProcessVariables(); bot.sendMessage(chat_id, "Mode pembuangan air menggunakan sensor.", ""); }

    if (text == "/full_proses") { 
      waitBeforeProcess();
      processPaused = false;
      pausedProcessStage = NO_PAUSED_PROCESS;
      pausedStandaloneDrain = false;
      drain_session_count = 0;
      pausedWhileClearingWaterForFill = false;
      Serial.println("Full proses dimulai");
      bot.sendMessage(CHAT_ID, "Full proses dimulai", "");
      runFullProcess();
    }
    if (text == "/soak") { 
      waitBeforeProcess();
      if (waterLevelStable(true, 10UL * 1000UL)) {
        soak();
      }
      else{
        Serial.println("Air belum mencukupi!");
        bot.sendMessage(CHAT_ID, "Air belum mencukupi!", "");
      }
    }
    if (text == "/spin") { 
      waitBeforeProcess();
      spin();
    }
    if (text == "/wash") { 
      waitBeforeProcess();
      drain_session_count = 0;
      wash();
    }
    if (text == "/soak_and_wash") { 
      waitBeforeProcess();
      drain_session_count = 0;
      soak_and_wash();
    }
    if (text == "/rinse") { 
      waitBeforeProcess();
      drain_session_count = 0;
      rinse();
    }
    if (text == "/rinse_parfume") { 
      waitBeforeProcess();
      drain_session_count = 0;
      rinse_parfume();
    }
    if (text == "/water_in") { 
      waitBeforeProcess();
      water_in_on();
    }
    if (text == "/lanjutkan_proses") {
      if (processPaused && pausedStandaloneDrain) {
        Serial.println("Melanjutkan sesi pembuangan air...");
        bot.sendMessage(CHAT_ID, "Melanjutkan sesi pembuangan air...", "");
        drain_session_count = 0;
        processPaused = false;
        if (water_out_on(true)) {
          bot.sendMessage(CHAT_ID, "Pembuangan air selesai. Proses dapat dilanjutkan.", "");
        }
      } else if (processPaused && pausedProcessStage != NO_PAUSED_PROCESS) {
        Serial.println("Melanjutkan proses dari tahap yang dijeda...");
        bot.sendMessage(CHAT_ID, "Melanjutkan proses dari tahap yang dijeda...", "");
        processPaused = false;
        if (pausedProcessStage == FULL_SOAK_AND_WASH_DRAIN ||
            pausedProcessStage == FULL_RINSE_DRAIN ||
            pausedProcessStage == FULL_RINSE_PARFUME_DRAIN) {
          drain_session_count = 0;
        } else if (pausedWhileClearingWaterForFill) {
          drain_session_count = 0;
        }
        runFullProcess(true);
      } else {
        Serial.println("Tidak ada proses yang sedang dijeda.");
        bot.sendMessage(CHAT_ID, "Tidak ada proses yang sedang dijeda.", "");
      }
    }
    if (text == "/water_out") { 
      waitBeforeProcess();
      water_out_on();
    }
    if (text == "/add_parfume") { 
      waitBeforeProcess();
      add_parfume();
    }
    if (text == "/motor_on") { 
      waitBeforeProcess();
      if (waterLevelStable(true, 10UL * 1000UL)) {
        motor_on(5);
      }
      else{
        Serial.println("Air belum mencukupi!");
        
        bot.sendMessage(CHAT_ID, "Air belum mencukupi!", "");
      }
    }
  }
}


void connectToWiFi() {
  String ssids[2] = { savedSsid, String(defaultSsid) };
  String passwords[2] = { savedPassword, String(defaultPassword) };

  for (int networkIndex = 0; networkIndex < 2; networkIndex++) {
    if (ssids[networkIndex].length() == 0 ||
        (networkIndex == 1 && ssids[networkIndex] == savedSsid)) {
      continue;
    }

    for (int attempt = 1; attempt <= maxWifiAttempts; attempt++) {
      Serial.println("Mencoba konek ke SSID: " + ssids[networkIndex] +
                     " (percobaan " + String(attempt) + ")");
      WiFi.disconnect(true);
      WiFi.begin(ssids[networkIndex].c_str(), passwords[networkIndex].c_str());

      unsigned long startAttemptTime = millis();
      while (WiFi.status() != WL_CONNECTED &&
             millis() - startAttemptTime < wifiAttemptTimeout) {
        delay(500);
        Serial.print(".");
      }

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nBerhasil konek ke " + ssids[networkIndex]);
        Serial.println("IP Address: " + WiFi.localIP().toString());
        bot.sendMessage(CHAT_ID, "Mesin Cuci Terkoneksi ke WiFi " + WiFi.SSID() +
                                  " dengan IP " + WiFi.localIP().toString(), "");
        return;
      }
      Serial.println("\nGagal konek ke " + ssids[networkIndex]);
    }
    Serial.println("Batas percobaan tercapai, mencoba jaringan berikutnya.");
  }

  Serial.println("Tidak ada WiFi yang berhasil tersambung.");
}


void setup() {
  pinMode(14, OUTPUT);
  digitalWrite(14, HIGH);
  Serial.begin(115200);
  pinMode(motor_left_rotation, OUTPUT);
  pinMode(motor_right_rotation, OUTPUT);
  pinMode(water_in, OUTPUT);
  pinMode(water_out, OUTPUT);
  pinMode(water_level, INPUT_PULLUP);
  pinMode(parfume, OUTPUT);
  pinMode(spin_motor, OUTPUT);

  // Pastikan semua output HIGH di awal
  digitalWrite(motor_left_rotation, HIGH);
  digitalWrite(motor_right_rotation, HIGH);
  digitalWrite(water_in, HIGH);
  digitalWrite(water_out, HIGH);
  digitalWrite(parfume, HIGH);
  digitalWrite(spin_motor, HIGH);

  WiFi.mode(WIFI_STA);
  preferences.begin("wifi-config", false);
  savedSsid = preferences.getString("ssid", "");
  savedPassword = preferences.getString("password", "");
  loadProcessVariables();
  connectToWiFi();

  #ifdef ESP32
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  #endif

  

  // Bersihkan pesan Telegram lama
  while (bot.getUpdates(bot.last_message_received + 1)) {
    delay(100);
  }

  Serial.println("Pesan Telegram lama telah dibersihkan");
}






void loop() {
   while (WiFi.status() != WL_CONNECTED) {
  Serial.println("\n WiFi terputus! Mencoba reconnect...");
  connectToWiFi();
  }
  
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1); // Mendapatkan pesan baru dari bot
  while (numNewMessages) { // Selama ada pesan baru
    handleNewMessages(numNewMessages); // Tangani pesan baru
    numNewMessages = bot.getUpdates(bot.last_message_received + 1); // Periksa pesan baru kembali
  }


  
}
