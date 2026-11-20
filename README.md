# Mesin Cuci Otomatis (ESP32)

Firmware ESP32 (modul **WROOM**) untuk mesin cuci dua tabung: kontrol lewat **dashboard web lokal** (jaringan rumah atau
hotspot darurat ESP32) tanpa bergantung pada internet, plus **web jarak jauh** lewat Cloudflare Worker (opsional) dan
**notifikasi Telegram** yang dikirim oleh Worker.

- **Core 1** menjalankan mesin (state machine berbasis `millis()`, tanpa `delay()`), **Core 0** untuk WiFi/Telegram/Web.
- Semua relay mati saat boot, error, atau STOP (relay trigger LOW, mode `OUTPUT_OPEN_DRAIN`).
- Satu *command layer* dan satu dispatcher API (`api_core`) dipakai web lokal, web jarak jauh, dan Serial/Konsol → kontrolnya sama persis.
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
`WIFI_SSID/PASSWORD`, `AP_PASSWORD` (hotspot, min. 8 karakter),
`WEB_PASSWORD` (login dashboard), dan (opsional) `REMOTE_URL` + `REMOTE_DEVICE_TOKEN` untuk kontrol jarak jauh.
Token bot Telegram **tidak** ada di ESP32: disimpan sebagai secret Worker (lihat `cloud/README.md`).

## Flash

### PlatformIO
```
pio run -e rymcu-esp32-devkitc -t upload      # lewat USB
```
Berkas `.bin`: `.pio/build/rymcu-esp32-devkitc/firmware.bin`.

### Arduino IDE
1. Buka `firmware/mesin_cuci_otomatis/mesin_cuci_otomatis.ino`.
2. Board: *ESP32 Dev Module*. **Tools → Partition Scheme → "Minimal SPIFFS (1.9MB APP with OTA)"** (wajib).
3. Library Manager: `ArduinoJson` (v7), `WebSockets` (Links2004), `AsyncTCP` dan `ESPAsyncWebServer`
   (keduanya versi **mathieucarbou**; jika perlu pasang dari GitHub).
4. Upload lewat USB. *Sketch → Export Compiled Binary* menghasilkan `.bin` untuk flash lewat web (tab Firmware).

### Lewat web
Dashboard → tab **Firmware** → pilih `.bin` → *Flash*. Hanya saat mesin siap (IDLE). Firmware lama tetap
utuh di partisi cadangan; tombol **Kembali ke versi sebelumnya** tersedia.

## Dashboard web

| Dari | Alamat |
|---|---|
| Jaringan rumah | `http://mesincuci.local` atau IP yang dikirim Telegram saat online |
| Hotspot ESP32 (darurat) | **hanya menyala bila semua WiFi tersimpan gagal**: sambung ke `MesinCuci-XXXX` (password `AP_PASSWORD`) → `http://192.168.4.1`. Mati lagi 2 menit setelah WiFi rumah pulih dan tanpa klien |

Tab: Beranda, Proses, Manual, Relay, Pengaturan, WiFi, Log, Konsol (serial monitor web), Riwayat, Firmware, Sistem.
Sumber dashboard ada di `web/`; setelah diubah jalankan `python tools/build_web.py` (menghasilkan `web_ui.h`).

## Proses & Tahap

**Tidak ada program bawaan.** Beranda hanya menampilkan **Proses** buatanmu (maks 10, tiap Proses maks 40 **Tahap**). Proses
disusun di tab **Proses** dari Tahap terkecil; tiap Tahap punya variabelnya sendiri. Katalog (jenis, rentang, bawaan) ada di
`tahap.cpp` dan dikirim ke dashboard lewat `/api/tahap/meta`, jadi dashboard dan firmware tidak bisa berbeda.

| Tahap | Variabel (bawaan) |
|---|---|
| **Isi air** | mode `sensor` / `persen` (25·50·75%, dari kalibrasi) / `waktu`; batas waktu `00:30:00` (sensor, persen); lama isi 30 mnt (waktu); coba ulang 3×; tunggu antar coba 15 mnt; pra-cek bila air sudah penuh: `buang` / `jeda` / `pakai` |
| **Buang air** | lama buang 5 mnt; **Verifikasi Pengurangan Jumlah Air** 45 dtk; **Tarik Ulang Valve** 2×; batas ulang sesi 3× (cek akhir "tidak penuh" selalu dilakukan) |
| **Putar mesin** | lama 15 mnt; pola `normal` (7 dtk nyala/4 jeda) · `gentle` (5,3/7) · `hijab` (3/9) |
| **Soak** | ulangi 2; waktu putar 1 mnt; waktu rendam 4 mnt; pola |
| **Rendam** | lama 4 mnt (diam, semua relay mati) |
| **Spin** | lama 10 mnt (memeriksa sensor dulu: air harus sudah turun) |
| **Pewangi** | 1 cup (kelipatan 0,5; **0,5 cup = 10 dtk**; failsafe pompa 240 dtk) |
| **Tunggu** | 00:00:10 |
| **Cek air** | harus `penuh` / `tidak penuh` |
| **Minta tindakan** | pesan (≤ 40 karakter); mesin berhenti sampai Lanjut/Batal; notifikasi selalu terkirim |
| **Notifikasi** | pesan (≤ 40 karakter); terkirim walau mode Senyap, lalu lanjut |

Menu **Manual** menjalankan satu Tahap dengan form yang sama, ditambah **Buang manual** (valve ditarik selama waktu tertentu
**tanpa sensor**, jalan darurat bila sensor rusak). Kontrol relay langsung dan kalibrasi relay ada di tab **Relay**.

**Mulai:** sekarang, tunda `HH:MM:SS`, atau selesai pada jam `HH:MM` (perkiraan: isi/buang berbasis sensor bisa meleset).
Proses yang sedang dipakai (berjalan, dijeda, menunggu keputusan, menunggu mulai, atau pemulihan listrik padam tertunda)
**tidak bisa diubah/dihapus**: tekan Stop dulu. Proses disimpan sebagai berkas JSON di LittleFS (partisi `spiffs`), ditulis
atomik; ekspor/impor per Proses ada di tab Proses.

### Keputusan menunggu
Bila Isi/Buang/Spin/Cek gagal atau ada Tahap *Minta tindakan*, mesin **berhenti dan tidak pernah lanjut sendiri**. Dashboard
menampilkan tombol sesuai kasus dan notifikasi dikirim (ulang tiap 10 menit):

| Kasus | Pilihan |
|---|---|
| Isi gagal, Buang gagal, Spin masih penuh, Cek gagal | **Ulang** / **Lanjut** (dengan peringatan) / **Batal** |
| Air sudah penuh sebelum mengisi (pra-cek `jeda`) | **Buang lalu isi** / **Pakai air yang ada** / **Batal** |
| Minta tindakan | **Lanjut** / **Batal** |

**Sensor level ada di paling atas** (hanya tahu *penuh* / *tidak penuh*): pada Tahap Buang, valve ditarik dan air harus mulai
turun dari posisi penuh dalam waktu *Verifikasi*; bila tidak, valve dilepas 3 dtk lalu ditarik lagi (*Tarik Ulang Valve*).
Setelah terbukti, valve ditarik sampai *lama buang*, lalu dicek: bila air masih penuh sesi diulang; semua sesi gagal → keputusan.
Listrik padam → saat menyala ditanya **Lanjutkan / Batalkan** (tidak pernah lanjut otomatis; dibatalkan bila Proses sudah diubah).
Catatan fitur lama yang dihapus: `Old_Vertion/proses_lama.md` (lokal).

## Perintah (Web · Serial/Konsol)

`proses` (daftar), `proses <nomor> [tunda HH:MM:SS | selesai HH:MM]`, `pilih <ulang|lanjut|batal|buang|pakai>`, `stop`, `jeda`,
`lanjutkan`, `lewati`, `batalkan`, `cek_air`, `status`, `pengaturan`, `riwayat`, `statistik`, `info`, `bantuan`;
`relay <kiri|kanan|isi|buang|spin|pewangi> <on|off>`, `relay semua off`, `kalibrasi_relay [detik]`, `buang_manual <menit>`;
`notif lengkap|ringkas|senyap`, `remote [on|off]`, `kalibrasi_air`, `reset_semua`; WiFi: `wifi_list wifi_tambah wifi_hapus
wifi_urutan`; `restart`. Konsol Serial/web juga punya `heap`, `tasks`, `log`, `relay_test <nama>`, `serial debug on|off`.
Variabel air/putar/pewangi tidak lagi berupa perintah: diatur per Tahap di dashboard (tab Proses dan Manual).

## Wokwi

`pio run -e wokwi` → env dengan `TIME_SCALE=60` (1 menit proses = 1 detik) dan relay push-pull.
Sensor air disimulasikan dengan slide switch di GPIO27. Hotspot ESP32 tidak bisa diuji di Wokwi.

## Uji logika di PC

`sh tools/hosttest/run.sh` (butuh `g++`) menjalankan state machine dengan jam palsu dan model tangki air (194 pemeriksaan:
semua Tahap, semua keputusan dan pilihannya, pra-cek, soak, tunda, pemulihan listrik padam, kunci slot, failsafe pompa,
interlock, backup) dan katalog Tahap + penyimpanan Proses (181 pemeriksaan: batas tiap variabel, JSON, peringatan, estimasi).
Fixture uji UI `tools/uitest/meta.json` dibuat dari firmware: `test_tahap --meta > tools/uitest/meta.json`.

## Uji relay & failsafe

- Dashboard → tab **Relay**: tombol ON/OFF per relay (hanya saat mesin siap, otomatis mati 60 detik) dan **Uji berurutan**
  (kiri → kanan → isi → buang → spin → pewangi). perintah `relay <nama> <on|off>`,
  `relay semua off`, `kalibrasi_relay [detik]`.
- Motor kiri & kanan **tidak pernah** menyala bersamaan, dijaga berlapis: (1) interlock di `relaySet`, (2) pin motor lawan
  dipaksa mati secara fisik sebelum motor dinyalakan, (3) jeda ganti arah, (4) `relayGuard()` tiap tick: bila terdeteksi
  kiri+kanan / isi+buang / buang+motor, semua relay dimatikan dan **dikunci sampai restart**, (5) motor cuci dimatikan paksa
  bila menyala terus > 65 detik, (6) semua relay mati saat boot, error, atau STOP.
- Login web: centang **Ingat perangkat ini** agar tetap masuk; sesi tersimpan di NVS sehingga tetap berlaku setelah restart.

## Perangkat yang diingat

Login dengan **Ingat perangkat ini** disimpan di NVS (maks **5 perangkat**; bila penuh, yang terlama otomatis dihapus) dan tidak
kedaluwarsa di sisi mesin. Cookie browser berumur 400 hari (batas browser) dan diperpanjang tiap dipakai. Kelola/hapus di
tab **Sistem → Perangkat yang diingat**. Mengganti password web mengeluarkan semua perangkat.

## Uji antarmuka (opsional)

`cd tools/uitest && npm install && npm test` — jsdom + server palsu: editor Proses/Tahap, kartu keputusan, Manual, drag & drop, penguncian relay, pesan penolakan, daftar perangkat.

## Ringan & andal (Tahap 2)

- Dashboard memakai **polling** (`/api/poll`, 2 dtk; 10 dtk saat tab tersembunyi), tanpa WebSocket server. ArduinoOTA dihapus
  (flash lewat web atau USB). Ring log 100 entri, antrean notifikasi 30 entri.
- **Watchdog konektivitas:** tiap 30 dtk ping gateway; 3 kali gagal → sambung ulang WiFi; 3 kali reconnect gagal → WiFi dimulai
  ulang. Aktif hanya setelah gateway pernah menjawab ping (router yang memblokir ICMP tidak memicu apa pun).
- Log DEBUG ke serial USB hanya bila diaktifkan: konsol `serial debug on|off` (tidak disimpan).

## Kontrol jarak jauh & notifikasi (Tahap 3–4)

```
ESP32 ──(SATU koneksi WSS keluar)──► Cloudflare Worker + Durable Object ◄── HP/browser (UI sama, fitur sensitif disembunyikan)
                                         └── notifikasi Telegram + alarm "mesin tidak terhubung"
Web lokal (HTTP, rumah/hotspot) ─────► ESP32   (jalur utama, tetap jalan tanpa internet)
```
- Perintah chat Telegram dihapus; Telegram hanya untuk **notifikasi** (dikirim Worker). Daftar perintah yang boleh dari jarak jauh
  ada di `cloud/src/allow.js` dan `firmware/.../remote_policy.cpp`, diuji identik dengan `cloud/test/allow-vectors.txt`.
  Flash firmware, WiFi, ganti password, perangkat diingat, konsol, dan restore **hanya** dari web lokal.
- Konfigurasi & deploy Worker: `cloud/README.md` (panduan akun, 2FA, `npm test` untuk uji lokal tanpa akun).
- Di web lokal: tab **Sistem → Kontrol jarak jauh** untuk mengaktifkan/menonaktifkan sambungan; `remote on|off` di konsol.

## Cadangkan & pulihkan (tab Sistem, hanya web lokal)

- **Unduh cadangan:** satu berkas JSON (versi 3) berisi pengaturan dan daftar WiFi; Proses diekspor satu per satu di tab Proses. Password WiFi hanya ikut bila
  dicentang (simpan berkasnya dengan aman). Data kalibrasi dan jadwal sekali pakai tidak disertakan.
- **Pulihkan dari berkas:** berkas diperiksa dulu (simulasi, `?dry=1`) dan ringkasannya ditampilkan untuk dikonfirmasi. Pemulihan
  **atomik**: bila satu bagian tidak valid, tidak ada yang berubah. Nilai di luar rentang disesuaikan; batas berkas 12 KB
  (kasus terburuk < 1 KB). Tanpa password di berkas, WiFi yang sama mempertahankan password lamanya.
- Tidak tersedia dari jarak jauh. Logika ada di `backup.cpp` (murni, diuji di `tools/hosttest`).

## Catatan jaringan WiFi (hasil ukur di perangkat)

- Daya pancar diset **15 dBm** (`WIFI_TX_POWER` di `config.h`) dan bandwidth HT20. Pada daya penuh (19,5 dBm) perangkat ini kehilangan
  ±20-35% paket (ping 1400 B hilang 36%, permintaan web sesekali tertahan 2-8 dtk); pada 15 dBm: 300/300 permintaan sukses.
  Ini ciri suplai 5V/USB yang drop saat memancar: perbaiki suplai (kabel tebal, catu cukup arus, catu relay terpisah, kapasitor
  470-1000 µF) sebelum menaikkan daya pancar.
