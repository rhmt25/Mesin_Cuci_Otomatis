# Referensi

> **Bahasa lain:** [English](../en/reference.md)

## Perintah

Dipakai lewat Konsol web (lokal), Serial USB 115200, dan (yang diizinkan) jarak jauh. Satu dispatcher untuk semuanya.

| Perintah | Fungsi |
|---|---|
| `proses` | Daftar Proses |
| `proses <nomor> [tunda HH:MM:SS \| selesai HH:MM]` | Jalankan Proses |
| `pilih <ulang\|lanjut\|batal\|buang\|pakai>` | Jawab keputusan yang menunggu |
| `stop` · `jeda` · `lanjutkan` · `lewati` · `batalkan` | Kontrol Proses berjalan |
| `cek_air` | Baca sensor level |
| `status` · `pengaturan` · `riwayat` · `statistik` · `info` · `bantuan` | Informasi |
| `relay <kiri\|kanan\|isi\|buang\|spin\|pewangi> <on\|off>` · `relay semua off` | Kontrol relay langsung (hanya IDLE) |
| `kalibrasi_relay [detik]` · `relay_test <nama>` | Uji berurutan / uji satu relay |
| `buang_manual <menit>` | Tarik valve buang tanpa sensor |
| `notif lengkap\|ringkas\|senyap` | Mode notifikasi |
| `remote [on\|off]` | Kontrol jarak jauh |
| `kalibrasi_air` | Kalibrasi waktu isi untuk mode persen |
| `reset_semua` · `restart` | Reset pengaturan / mulai ulang |
| `wifi_list` · `wifi_tambah` · `wifi_hapus` · `wifi_urutan` | WiFi (hanya lokal) |
| `heap` · `tasks` · `log` · `serial debug on\|off` | Diagnostik (konsol saja) |

Variabel air/putar/pewangi tidak berupa perintah: diatur per Tahap di dashboard.

**Diizinkan dari jarak jauh:** `proses pilih stop jeda lanjutkan lewati batalkan relay relay_test kalibrasi_relay buang_manual
cek_air status menu pengaturan riwayat statistik info bantuan notif kalibrasi_air reset_semua restart remote`.
Daftar sebenarnya ada di `cloud/src/allow.js` dan `remote_policy.cpp`.

## Endpoint API (ESP32)

Semua memerlukan sesi login (cookie) kecuali `/api/login`.

| Endpoint | Fungsi |
|---|---|
| `POST /api/login` · `/api/logout` | Masuk / keluar |
| `GET /api/poll` | Status ringkas + log baru (polling dashboard) |
| `GET /api/status` · `/api/info` · `/api/job` | Status mesin, info perangkat, Proses berjalan (Rel Tahap) |
| `GET/POST /api/proses` · `/api/proses/export` · `/api/proses/import` | CRUD Proses |
| `GET /api/tahap/meta` | Katalog Tahap (jenis, rentang, bawaan) |
| `POST /api/run` · `/api/decide` · `/api/cmd` | Jalankan, putuskan, perintah |
| `GET/POST /api/settings` · `/api/wifi` · `/api/password` · `/api/devices` | Pengaturan (sebagian hanya lokal) |
| `GET /api/logs` · `/api/history` · `/api/console` | Log, riwayat, konsol |
| `GET /api/backup` · `POST /api/restore[?dry=1]` | Cadangan / pulihkan |
| `POST /api/update` · `/api/rollback` · `GET /api/update/info` | Flash OTA & kembali ke versi lama |

## Format cadangan

Satu berkas JSON **versi 3**: pengaturan + daftar WiFi. Batas 12 KB (kasus terburuk < 1 KB). Password WiFi hanya bila dicentang.
Data kalibrasi dan jadwal sekali pakai tidak disertakan. Pemulihan atomik; nilai di luar rentang disesuaikan.

## Konfigurasi build (`config.h`)

| Makro | Bawaan | Arti |
|---|---|---|
| `FW_VERSION` | `2.0.0` | Versi firmware |
| `DEVICE_HOSTNAME` | `mesincuci` | Nama mDNS |
| `TIME_SCALE` | `1` | Pengali waktu (Wokwi: 60) |
| `RELAY_ACTIVE_LOW` / `RELAY_OPEN_DRAIN` | `1` / `1` | Polaritas relay / mode pin |
| `WIFI_TX_POWER` | 11 dBm | Daya pancar WiFi |
| `MOTOR_MAX_ON_MS` | 65000 | Failsafe motor cuci |
| `PERFUME_MAX_ON_MS` | 240000 | Failsafe pompa pewangi |
| `MANUAL_MAX_ON_MS` | 60000 | Batas kontrol relay manual |
| `WATER_STABLE_MS` | 10000 | Sensor harus stabil sebelum dianggap berubah |
| `MAX_WIFI_NETWORKS` | 5 | Jumlah WiFi tersimpan |
| `NTP_TZ` | `WIB-7` | Zona waktu |

## Variabel Worker (`cloud/wrangler.toml`)

`OFFLINE_MIN` (bawaan 5): menit tanpa kabar sebelum alarm offline. `RPC_TIMEOUT_MS` (bawaan 8000).
