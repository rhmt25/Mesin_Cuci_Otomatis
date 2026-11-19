# Mesin Cuci Otomatis (ESP32)

Firmware ESP32 (modul **WROOM**) untuk mesin cuci dua tabung: kontrol lewat **Telegram** dan
**dashboard web** (jaringan rumah atau hotspot ESP32 sendiri), tanpa bergantung pada internet.

- **Core 1** menjalankan mesin (state machine berbasis `millis()`, tanpa `delay()`), **Core 0** untuk WiFi/Telegram/Web.
- Semua relay mati saat boot, error, atau STOP (relay trigger LOW, mode `OUTPUT_OPEN_DRAIN`).
- Satu *command layer* dipakai Telegram, Web, dan Serial/Konsol → kontrolnya sama persis.
- Proses tetap jalan saat WiFi mati; log diantrekan dan dikirim begitu online.
- Satu sumber kode: folder `firmware/mesin_cuci_otomatis/` dibuka di Arduino IDE **dan** PlatformIO.

## Wiring (WROOM)

| Fungsi | GPIO | Catatan |
|---|---|---|
| Motor putar kiri | **25** | dulu GPIO5 (strapping pin) — pindahkan kabel IN relay |
| Motor putar kanan | 18 | |
| Valve isi air | 17 | |
| Motor penarik valve buang | 16 | |
| Motor spin | 4 | |
| Pompa pewangi | 13 | |
| Sensor level air | 27 | `INPUT_PULLUP`, HIGH = ada air |
| Sumber sensor/tombol | 14 | selalu HIGH |

Modul relay: **Normally Open, trigger LOW**. Pengaturan ada di `config.h`
(`RELAY_ACTIVE_LOW`, `RELAY_OPEN_DRAIN`).

## Menyiapkan `secrets.h`

Salin `firmware/mesin_cuci_otomatis/secrets.example.h` menjadi `secrets.h` (tidak di-commit) lalu isi:
`WIFI_SSID/PASSWORD`, `TELEGRAM_BOT_TOKEN`, `TELEGRAM_CHAT_ID`, `AP_PASSWORD` (hotspot, min. 8 karakter),
`WEB_PASSWORD` (login dashboard), `OTA_PASSWORD` (flash lewat jaringan).

## Flash

### PlatformIO
```
pio run -e rymcu-esp32-devkitc -t upload      # lewat USB
pio run -e ota -t upload                       # lewat jaringan (isi OTA_PASSWORD di env OTA_PASSWORD)
```
Berkas `.bin`: `.pio/build/rymcu-esp32-devkitc/firmware.bin`.

### Arduino IDE
1. Buka `firmware/mesin_cuci_otomatis/mesin_cuci_otomatis.ino`.
2. Board: *ESP32 Dev Module*. **Tools → Partition Scheme → "Minimal SPIFFS (1.9MB APP with OTA)"** (wajib).
3. Library Manager: `UniversalTelegramBot`, `ArduinoJson` (v7), `AsyncTCP` dan `ESPAsyncWebServer`
   (keduanya versi **mathieucarbou**; jika perlu pasang dari GitHub).
4. Upload lewat USB, atau lewat jaringan: muncul sebagai *Network Port* `mesincuci` (password = `OTA_PASSWORD`).
   *Sketch → Export Compiled Binary* menghasilkan `.bin` untuk flash lewat web.

### Lewat web
Dashboard → tab **Firmware** → pilih `.bin` → *Flash*. Hanya saat mesin siap (IDLE). Firmware lama tetap
utuh di partisi cadangan; tombol **Kembali ke versi sebelumnya** tersedia.

## Dashboard web

| Dari | Alamat |
|---|---|
| Jaringan rumah | `http://mesincuci.local` atau IP yang dikirim Telegram saat online |
| Hotspot ESP32 | sambung ke `MesinCuci-XXXX` (password `AP_PASSWORD`) → otomatis terbuka / `http://192.168.4.1` |

Tab: Beranda, Manual, Pengaturan, Air, WiFi, Log, Konsol (serial monitor web), Riwayat, Firmware, Sistem.
Sumber dashboard ada di `web/`; setelah diubah jalankan `python tools/build_web.py` (menghasilkan `web_ui.h`).

## Program

| Program | Pola | Soak | Wash | Bilas |
|---|---|---|---|---|
| Normal | normal | 2 sesi | 15 mnt | 10 mnt |
| Gentle | gentle | 2 | 12 | 8 |
| Hijab | hijab | 1 | 10 | 8 |
| Cepat | normal | 0 | 8 | 5 |
| Kustom | pengaturan | pengaturan | pengaturan | pengaturan |

Nilai preset ada di `programs.cpp`. Urutan: isi air → soak/wash → buang → bilas → buang → bilas + pewangi → buang.

**Level air:** Penuh (sensor, default) / Persen 25–75 (kalibrasi otomatis dari pengisian mode sensor) / Waktu manual.
Pengisian yang tidak penuh dalam batas waktu → valve ditutup, tunggu, coba ulang (default 3×), lalu **dijeda**.
Pembuangan gagal → "unjam" valve (lepas-tarik ulang), ulang sesi, lalu **dijeda**.
Listrik padam → saat menyala ditanya **Lanjutkan / Batalkan** (tidak pernah lanjut otomatis).

## Perintah (Telegram `/` · Web · Serial)

`/menu` (tombol), `/status`, `/program normal|gentle|hijab|cepat|kustom`, `/stop`, `/jeda`, `/lanjutkan`, `/lewati`,
`/batalkan`, `/cek_air`, `/pengaturan`, `/riwayat`, `/statistik`, `/info`, `/bantuan`;
manual: `/soak /wash /soak_and_wash /rinse /rinse_parfume /spin /water_in /water_out /add_parfume /motor_on`;
pengaturan: `/pola`, `/set_soak_session`, `/set_wash_time`, `/set_rinse_time`, `/set_jumlah_parfume`, `/set_spin_time`,
`/set_delay`, `/mulai_nanti`, `/selesai_jam`, `/level_air penuh|25|50|75|waktu`, `/mode_buang sensor|waktu`,
`/set_batas_waktu_pengisian_air`, `/set_coba_ulang_isi`, `/set_unjam`, `/notif lengkap|ringkas|senyap`, `/kalibrasi_air`,
`/reset_semua`; WiFi: `/wifi_list /wifi_tambah /wifi_hapus /wifi_urutan`; `/restart`.
Semua perintah lama (`/full_proses`, `/set_mode_gentle`, `/cek_variabel`, `/lanjutkan_proses`, dst.) tetap berfungsi sebagai alias.
Konsol Serial/web juga punya `heap`, `tasks`, `log`, `relay_test <kiri|kanan|isi|buang|spin|pewangi>`.

## Wokwi

`pio run -e wokwi` → env dengan `TIME_SCALE=60` (1 menit proses = 1 detik) dan relay push-pull.
Sensor air disimulasikan dengan slide switch di GPIO27. Hotspot ESP32 tidak bisa diuji di Wokwi.

## Uji logika di PC

`sh tools/hosttest/run.sh` (butuh `g++`) menjalankan state machine dengan jam palsu dan model tangki air:
full proses, air habis, valve macet, stop, jeda/lanjutkan, listrik padam, level persen, pengaman meluap, interlock.

## Uji relay & failsafe

- Dashboard → tab **Relay**: tombol ON/OFF per relay (hanya saat mesin siap, otomatis mati 60 detik) dan **Uji berurutan**
  (kiri → kanan → isi → buang → spin → pewangi). Telegram: menu **Relay**; perintah `relay <nama> <on|off>`,
  `relay semua off`, `kalibrasi_relay [detik]`.
- Motor kiri & kanan **tidak pernah** menyala bersamaan, dijaga berlapis: (1) interlock di `relaySet`, (2) pin motor lawan
  dipaksa mati secara fisik sebelum motor dinyalakan, (3) jeda ganti arah, (4) `relayGuard()` tiap tick: bila terdeteksi
  kiri+kanan / isi+buang / buang+motor, semua relay dimatikan dan **dikunci sampai restart**, (5) motor cuci dimatikan paksa
  bila menyala terus > 65 detik, (6) semua relay mati saat boot, error, atau STOP.
- Login web: centang **Ingat perangkat ini** untuk cookie 30 hari; sesi tersimpan di NVS sehingga tetap berlaku setelah restart.

## Custom Proses

Dashboard → tab **Custom**: susun proses sendiri dari blok (*Cuci, Soak+Cuci, Bilas, Bilas+Pewangi*) dan langkah dasar
(*Isi air, Putar, Soak, Pewangi, Buang air, Spin, Tunggu*), atur nilainya (menit, sesi, pola putar, cup pewangi), tukar urutan
dengan menggeser **☰** (atau tombol ▲▼), gandakan/hapus langkah, lalu simpan (maks 5 proses, tiap proses maks 24 langkah).
Contoh: *Bilas 10 menit* lalu *Bilas 5 menit* = dua blok Bilas. Jalankan dari tab Custom, dari Beranda, atau Telegram (`/custom`,
`/custom <nomor>`). Isi air/buang tetap mengikuti pengaturan level air, coba ulang, unjam, dan pemulihan listrik padam.

## Perangkat yang diingat

Login dengan **Ingat perangkat ini** disimpan di NVS (maks **5 perangkat**; bila penuh, yang terlama otomatis dihapus) dan tidak
kedaluwarsa di sisi mesin. Cookie browser berumur 400 hari (batas browser) dan diperpanjang tiap dipakai. Kelola/hapus di
tab **Sistem → Perangkat yang diingat**. Mengganti password web mengeluarkan semua perangkat.

## Uji antarmuka (opsional)

`cd tools/uitest && npm install && npm test` — jsdom + server palsu: editor Custom, drag & drop, penguncian relay, pesan penolakan, daftar perangkat.
