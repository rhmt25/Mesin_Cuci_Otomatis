#include <WiFi.h>    // Libary untuk koneksi WiFi
#include <WiFiClientSecure.h>  // Libary untuk komunikasi TLS
#include <UniversalTelegramBot.h>  // Libary untuk mengontrol bot Telegram

#define CHAT_ID "6151999510"  // Chat ID untuk menerima notifikasi dari bot
// Detail WiFi
const char* ssid = "POCO F4";              // Nama jaringan WiFi
const char* password_wifi = "REALME0885"; // Password WiFi

// Token bot Telegram
#define BOTtoken "8155093286:AAF5UJaOtbBuJF1mxowE5ijReQGV2zSth6g"  // Token bot dari BotFather

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
int jumlah_parfume=1;
int spin_time=10;


void water_in_on(){
  delay(2000);
  bool water_full = false;
  bool display=true;
  while (digitalRead(water_level) == HIGH) {
    if (display==true){
      Serial.println("Menunggu air kosong...");
      bot.sendMessage(CHAT_ID, "Menunggu air kosong...", "");
      display=false;
    }
    delay(500);
  }
  display=true;
  digitalWrite(water_in, LOW);
  Serial.println("Mengisi air...");
  bot.sendMessage(CHAT_ID, "Mengisi air...", "");
  int test=0;
  while (test<=10){
    water_full=digitalRead(water_level);
    if (display==true){
      Serial.println("Menunggu air penuh...");
      bot.sendMessage(CHAT_ID, "Menunggu air penuh...", "");
      display=false;
    }
    if (digitalRead(water_level) == HIGH) {
      test++;
      Serial.println(test);
      
    }
    else{
      test=0;
      Serial.println(test);
    }
    delay(1000);
  }
  digitalWrite(water_in, HIGH);
  Serial.println("Air sudah penuh!");
  bot.sendMessage(CHAT_ID, "Air sudah penuh!", "");
  water_full = false;

}

void motor_on(int total_time) {
  bot.sendMessage(CHAT_ID, "Motor ON", "");
  Serial.println("Motor ON");
  total_time=total_time*60*1000;
  int on_time=0;
  int off_time=2500;
  if (mode=="normal"){
    on_time=5000;
  }
  if (mode=="gentle"){
    on_time=3000;
  }
  if (mode=="hijab"){
    on_time=2500;
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
  bot.sendMessage(CHAT_ID, "Motor OFF", "");
  Serial.println("Motor OFF");
}

void water_out_on(){
  digitalWrite(water_out, LOW);
  Serial.println("Membuang air...");
  bot.sendMessage(CHAT_ID, "Membuang air...", "");
  delay(5000*60);
  digitalWrite(water_out, HIGH);
  Serial.println("Air sudah habis!");
  bot.sendMessage(CHAT_ID, "Air sudah habis!", "");
}


void add_parfume(){
  if(jumlah_parfume>0){
    Serial.println("Memasukkan pewangi...");
    bot.sendMessage(CHAT_ID, "Memasukkan pewangi...", "");
    for (int i=1;i<=jumlah_parfume;i++){
      digitalWrite(parfume, LOW);
      delay(1000);
      digitalWrite(parfume, HIGH);
    }
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
      delay(4000*60);

      motor_on(1);
      delay(500);
      Serial.println("Soak ke ");
      Serial.println(i);

    }
    bot.sendMessage(CHAT_ID, "Soak proses selesai", "");
    Serial.println("Soak proses selesai");
  }
  else {
    bot.sendMessage(CHAT_ID, "Soak proses tidak dilakukan", "");
    Serial.println("Soak proses tidak dilakukan");
  }
}


void wash (){
  if(wash_time>0){
    bot.sendMessage(CHAT_ID, "Wash proses dimulai", "");
    Serial.println("Wash proses dimulai");
    water_in_on();
    delay(500);
    
    motor_on(wash_time);
    delay(500);

    water_out_on();
    delay(500);
    bot.sendMessage(CHAT_ID, "Wash proses selesai", "");
    Serial.println("Wash proses selesai");
  }
  else {
    bot.sendMessage(CHAT_ID, "Wash proses tidak dilakukan", "");
    Serial.println("Wash proses tidak dilakukan");
  }
}

void soak_and_wash (){
  if(soak_session>0 && wash_time>0){
    water_in_on();
    delay(500);

    soak();

    bot.sendMessage(CHAT_ID, "Wash proses dimulai", "");
    Serial.println("Wash proses dimulai");
    motor_on(wash_time);
    delay(500);

    water_out_on();
    delay(500);
    bot.sendMessage(CHAT_ID, "Wash proses selesai", "");
    Serial.println("Wash proses selesai");
  }
  else {
    bot.sendMessage(CHAT_ID, "Soak and Wash proses tidak dilakukan", "");
    Serial.println("Soak and Wash proses tidak dilakukan");
  }
}

void rinse (){
  if (rinse_time>0){
    bot.sendMessage(CHAT_ID, "Rinse proses dimulai", "");
    Serial.println("Rinse proses dimulai");
      water_in_on();
      delay(500);
      
      motor_on(rinse_time);
      delay(500);

      water_out_on();
      delay(500);
    bot.sendMessage(CHAT_ID, "Rinse proses selesai", "");
    Serial.println("Rinse proses selesai");
    }
  else {
    bot.sendMessage(CHAT_ID, "Rinse proses tidak dilakukan", "");
    Serial.println("Rinse proses tidak dilakukan");
  }
}

void rinse_parfume() {
  if (rinse_time>0){
    bot.sendMessage(CHAT_ID, "Rinse dengan pewangi proses dimulai", "");
    Serial.println("Rinse dengan pewangi proses dimulai");
    water_in_on();
    delay(500);

    add_parfume();
    delay(500);

    motor_on(rinse_time);
    delay(500);

    water_out_on();
    delay(500);
    bot.sendMessage(CHAT_ID, "Rinse dengan pewangi proses selesai", "");
    Serial.println("Rinse dengan pewangi proses selesai");
  }
  else {
    bot.sendMessage(CHAT_ID, "Rinse parfume proses tidak dilakukan", "");
    Serial.println("Rinse parfume proses tidak dilakukan");
  }
}


void spin (){
  if(spin_time>0){
    bot.sendMessage(CHAT_ID, "Spin proses dimulai", "");
    Serial.println("Spin proses dimulai");
    digitalWrite(spin_motor, LOW);
    delay(spin_time*60*1000);
    digitalWrite(spin_motor, HIGH);
    bot.sendMessage(CHAT_ID, "Spin proses selesai", "");
    Serial.println("Spin proses selesai");
    }
  else {
    bot.sendMessage(CHAT_ID, "Spin proses tidak dilakukan", "");
    Serial.println("Spin proses tidak dilakukan");
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

    if (text == "/start") { // Jika pesan adalah perintah /start
      String control = "Selamat Datang, " + from_name + ".\n"; // Pesan sambutan

      control += "Menu:\n\n";
      control += "/cek_variabel - Cek nilai variabel\n\n";
      control += "/reset_variabel - Reset semua variabel\n\n";
      control += "/cek_air - Cek status air\n\n\n";
      
      control += "Mode:\n\n";
      control += "/set_mode_normal\n\n";
      control += "/set_mode_gentle\n\n";
      control += "/set_mode_hijab\n\n\n";
      
      control += "Soak:\n\n";
      control += "/tambah_soak_session\n\n";
      control += "/kurangi_soak_session\n\n";
      control += "/reset_soak_session\n\n\n";
      
      control += "Wash:\n\n";
      control += "/tambah_wash_time\n\n";
      control += "/kurangi_wash_time\n\n";
      control += "/reset_wash_time\n\n\n";
      
      control += "Rinse:\n\n";
      control += "/tambah_rinse_time\n\n";
      control += "/kurangi_rinse_time\n\n";
      control += "/reset_rinse_time\n\n\n";
      
      control += "Parfume:\n\n";
      control += "/tambah_jumlah_parfume\n\n";
      control += "/kurangi_jumlah_parfume\n\n";
      control += "/reset_jumlah_parfume\n\n\n";
      
      control += "Spin:\n\n";
      control += "/tambah_spin_time\n\n";
      control += "/kurangi_spin_time\n\n";
      control += "/reset_spin_time\n\n\n";
      
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



      // control += "Menu:\n\n"; // Daftar menu perintah
      // control += "/cek_variabel \n\n";
      // control += "/reset_variabel \n\n";
      // control += "/cek_air \n\n";

      // control += "/set_mode_normal \n\n";
      // control += "/set_mode_gentle \n\n";
      // control += "/set_mode_hijab \n\n";

      // control += "/tambah_soak_session \n\n";
      // control += "/kurangi_soak_session \n\n";
      // control += "/reset_soak_session \n\n";
      
      // control += "/tambah_wash_time \n\n";
      // control += "/kurangi_wash_time \n\n";
      // control += "/reset_wash_time \n\n";

      // control += "/tambah_rinse_time \n\n";
      // control += "/kurangi_rinse_time \n\n";
      // control += "/reset_rinse_time \n\n";

      // control += "/tambah_jumlah_parfume \n\n";
      // control += "/kurangi_jumlah_parfume \n\n";
      // control += "/reset_jumlah_parfume \n\n";

      // control += "/tambah_spin_time \n\n";
      // control += "/kurangi_spin_time \n\n";
      // control += "/reset_spin_time \n\n";

      // control += "/full_proses \n\n";
      // control += "/soak \n\n";
      // control += "/wash \n\n";
      // control += "/soak_and_wash \n\n";
      // control += "/rinse \n\n";
      // control += "/rinse_parfume \n\n";
      // control += "/water_in \n\n";
      // control += "/water_out \n\n";
      // control += "/add_parfume \n\n";
      // control += "/motor_on \n\n";
      bot.sendMessage(CHAT_ID, control, ""); // Mengirim pesan daftar menu
    }

    if (text == "/cek_variabel") { 
      String report = "Variabel :\n";
      report += "Mode\t: "+mode+"\n";
      report += "Soak\t: " + String(soak_session) + " sesi\n";
      report += "Wash\t: " + String(wash_time) + " menit\n";
      report += "Rinse\t: " + String(rinse_time) + " menit\n";
      report += "Parfume\t: " + String(jumlah_parfume) + " cup\n";
      report += "Spin\t: " + String(spin_time) + " menit\n";
    bot.sendMessage(CHAT_ID, report, "");
    }
    if (text == "/reset_variabel") {
      mode = "normal";
      soak_session = 2;
      wash_time = 15;
      rinse_time = 10;
      jumlah_parfume = 1;
      spin_time = 10;
      bot.sendMessage(CHAT_ID, "Semua variabel telah direset ke default.", "");
      Serial.println("Semua variabel telah direset ke default.");
    }

    if (text == "/cek_air") { 
      delay(2000);
      if (digitalRead(water_level) == HIGH) {
        Serial.println("Air sudah mencukupi!");
        bot.sendMessage(CHAT_ID, "Air sudah mencukupi!", "");
      }
      else{
        Serial.println("Air belum mencukupi!");
        bot.sendMessage(CHAT_ID, "Air belum mencukupi!", "");
      }
    }

    //MODE
    if (text == "/set_mode_normal") { 
      mode="normal";
      bot.sendMessage(CHAT_ID, "Mode normal digunakan", "");
      Serial.println("Mode normal digunakan");
    }
    if (text == "/set_mode_gentle") { 
      mode="gentle";
      bot.sendMessage(CHAT_ID, "Mode gentle digunakan", "");
      Serial.println("Mode gentle digunakan");
    }
    if (text == "/set_mode_hijab") { 
      mode="hijab";
      bot.sendMessage(CHAT_ID, "Mode hijab digunakan", "");
      Serial.println("Mode hijab digunakan");
    }
    //SOAK
    if (text == "/tambah_soak_session") { 
      soak_session=soak_session+1;
      bot.sendMessage(CHAT_ID, "Soak " + String(soak_session) + " sesi diset", "");
      Serial.println("Soak " + String(soak_session) + " sesi diset");
    }
    if (text == "/kurangi_soak_session") { 
      if (soak_session>0){
        soak_session=soak_session-1;
        bot.sendMessage(CHAT_ID, "Soak " + String(soak_session) + " sesi diset", "");
        Serial.println("Soak " + String(soak_session) + " sesi diset");
      }
      else {
        bot.sendMessage(CHAT_ID, "Soak session sudah 0", "");
        Serial.println("Soak session sudah 0");
      }
    }
    if (text == "/reset_soak_session") { 
      soak_session=2;
      bot.sendMessage(CHAT_ID, "Soak session diriset menjadi 2 sesi", "");
      Serial.println("Soak session diriset menjadi 2 sesi");
    }
    //WASH
    if (text == "/tambah_wash_time") { 
      wash_time=wash_time+5;
      bot.sendMessage(CHAT_ID, "Wash time " + String(wash_time) + " menit diset", "");
      Serial.println("Wash time " + String(wash_time) + " menit diset");
    }
    if (text == "/kurangi_wash_time") { 
      if (wash_time>0){
        wash_time=wash_time-5;
        bot.sendMessage(CHAT_ID, "Wash time " + String(wash_time) + " menit diset", "");
        Serial.println("Wash time " + String(wash_time) + " menit diset");
      }
      else {
        bot.sendMessage(CHAT_ID, "Wash time sudah 0", "");
        Serial.println("Wash time sudah 0");
      }
    }
    if (text == "/reset_wash_time") { 
      wash_time=15;
      bot.sendMessage(CHAT_ID, "Wash time diriset menjadi 15 menit", "");
      Serial.println("Wash time diriset menjadi 15 menit");
    }
    //RINSE
    if (text == "/tambah_rinse_time") { 
      rinse_time=rinse_time+5;
      bot.sendMessage(CHAT_ID, "Rinse time " + String(rinse_time) + " menit diset", "");
      Serial.println("Rinse time " + String(rinse_time) + " menit diset");
    }
    if (text == "/kurangi_rinse_time") { 
      if (rinse_time>0){
        rinse_time=rinse_time-5;
        bot.sendMessage(CHAT_ID, "Rinse time " + String(rinse_time) + " menit diset", "");
        Serial.println("Rinse time " + String(rinse_time) + " menit diset");
      }
      else {
        bot.sendMessage(CHAT_ID, "Rinse time sudah 0", "");
        Serial.println("Rinse time sudah 0");
      }
    }
    if (text == "/reset_rinse_time") { 
      rinse_time=10;
      bot.sendMessage(CHAT_ID, "Rinse time diriset menjadi 10 menit", "");
      Serial.println("Rinse time diriset menjadi 10 menit");
    }
    //PARFUME
    if (text == "/tambah_jumlah_parfume") { 
      jumlah_parfume=jumlah_parfume+1;
      bot.sendMessage(CHAT_ID, "Jumlah parfume " + String(jumlah_parfume) + " cup diset", "");
      Serial.println("Jumlah parfume " + String(jumlah_parfume) + " cup diset");
    }
    if (text == "/kurangi_jumlah_parfume") { 
      if (jumlah_parfume>0){
        jumlah_parfume=jumlah_parfume-1;
        bot.sendMessage(CHAT_ID, "Jumlah parfume " + String(jumlah_parfume) + " cup diset", "");
        Serial.println("Jumlah parfume " + String(jumlah_parfume) + " cup diset");
      }
      else {
        bot.sendMessage(CHAT_ID, "Jumlah parfume sudah 0", "");
        Serial.println("Jumlah parfume sudah 0");
      }
    }
    if (text == "/reset_jumlah_parfume") { 
      jumlah_parfume=1;
      bot.sendMessage(CHAT_ID, "Jumlah parfume diriset menjadi 1 cup", "");
      Serial.println("Jumlah parfume diriset menjadi 1 cup");
    }
    //SPIN
    if (text == "/tambah_spin_time") { 
      spin_time=spin_time+5;
      bot.sendMessage(CHAT_ID, "Spin time " + String(spin_time) + " menit diset", "");
      Serial.println("Spin time " + String(spin_time) + " menit diset");
    }
    if (text == "/kurangi_spin_time") { 
      if (spin_time>0){
        spin_time=spin_time-5;
        bot.sendMessage(CHAT_ID, "Spin time " + String(spin_time) + " menit diset", "");
        Serial.println("Spin time " + String(spin_time) + " menit diset");
      }
      else {
        bot.sendMessage(CHAT_ID, "Spin time sudah 0", "");
        Serial.println("Spin time sudah 0");
      }
    }
    if (text == "/reset_spin_time") { 
      spin_time=10;
      bot.sendMessage(CHAT_ID, "Spin time diriset menjadi 10 menit", "");
      Serial.println("Spin time diriset menjadi 10 menit");
    }

    if (text == "/full_proses") { 
      bot.sendMessage(CHAT_ID, "Full proses dimulai", "");
      Serial.println("Full proses dimulai");
      soak_and_wash();

      bot.sendMessage(CHAT_ID, "Rinse proses dimulai", "");
      Serial.println("Rinse proses dimulai");
      rinse();

      bot.sendMessage(CHAT_ID, "Rinse dengan pewangi proses dimulai", "");
      Serial.println("Rinse dengan pewangi proses dimulai");
      rinse_parfume();

      bot.sendMessage(CHAT_ID, "Full proses selesai", "");
      Serial.println("Full proses selesai");
    }
    if (text == "/soak") { 
      delay(2000);
      if (digitalRead(water_level) == HIGH) {
        soak();
      }
      else{
        Serial.println("Air belum mencukupi!");
        bot.sendMessage(CHAT_ID, "Air belum mencukupi!", "");
      }
    }
    if (text == "/spin") { 
      spin();
    }
    if (text == "/wash") { 
      wash();
    }
    if (text == "/soak_and_wash") { 
      soak_and_wash();
    }
    if (text == "/rinse") { 
      rinse();
    }
    if (text == "/rinse_parfume") { 
      rinse_parfume();
    }
    if (text == "/water_in") { 
      water_in_on();
    }
    if (text == "/water_out") { 
      water_out_on();
    }
    if (text == "/add_parfume") { 
      add_parfume();
    }
    if (text == "/motor_on") { 
      delay(2000);
      if (digitalRead(water_level) == HIGH) {
        motor_on(5);
      }
      else{
        Serial.println("Air belum mencukupi!");
        bot.sendMessage(CHAT_ID, "Air belum mencukupi!", "");
      }
    }
  }
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

  WiFi.mode(WIFI_STA);   // Mengatur mode WiFi ke Station
  WiFi.begin(ssid, password_wifi);  // Menyambungkan ke WiFi

  #ifdef ESP32
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);  // Menambahkan sertifikat Telegram
  #endif

  while (WiFi.status() != WL_CONNECTED) {  // Tunggu hingga terhubung ke WiFi
    delay(1000);
    Serial.println("Connecting to WiFi..");  // Menampilkan status koneksi
  }
  Serial.println(WiFi.localIP());  // Menampilkan alamat IP
    // Bersihkan pesan Telegram lama
  while (bot.getUpdates(bot.last_message_received + 1)) {
    delay(100);
  }
  Serial.println("Pesan Telegram lama telah dibersihkan");

}




void loop() {

  int numNewMessages = bot.getUpdates(bot.last_message_received + 1); // Mendapatkan pesan baru dari bot
  while (numNewMessages) { // Selama ada pesan baru
    handleNewMessages(numNewMessages); // Tangani pesan baru
    numNewMessages = bot.getUpdates(bot.last_message_received + 1); // Periksa pesan baru kembali
  }
  // Serial.println("Done");

  // bool run=true;
  // while (run){
 
  // }
  
}
