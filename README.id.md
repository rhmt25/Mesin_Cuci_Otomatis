# Mesin Cuci Otomatis

[![Lisensi: GPL v3](https://img.shields.io/badge/Lisensi-GPLv3-blue.svg)](LICENSE)
[![CI](https://github.com/rhmt25/Mesin_Cuci_Otomatis/actions/workflows/ci.yml/badge.svg)](https://github.com/rhmt25/Mesin_Cuci_Otomatis/actions/workflows/ci.yml)
![Platform: ESP32](https://img.shields.io/badge/platform-ESP32-informational)

> 🇬🇧 **English:** see [README.md](README.md)

Firmware ESP32 open source yang mengubah mesin cuci dua tabung menjadi mesin yang pintar, aman, dan bisa diprogram. Kontrol lewat
**dashboard web lokal** (jaringan rumah atau hotspot darurat ESP32, tanpa internet), opsional dari mana saja lewat
**Cloudflare Worker**, plus **notifikasi Telegram**.

## Fitur

- **Susun Proses sendiri** dari Tahap (isi air, buang, putar, soak, rendam, spin, pewangi, tunggu, cek air, minta tindakan, notifikasi), masing-masing dengan variabelnya. Tanpa program bawaan.
- **Aman sejak desain:** semua relay mati saat boot/error/Stop; interlock berlapis; failsafe waktu; watchdog. Bila ada yang gagal, mesin **berhenti dan bertanya**, tidak pernah lanjut sendiri.
- **Tetap jalan tanpa internet:** state machine di core sendiri (berbasis `millis()`, tanpa `delay()`); Proses berlanjut saat WiFi putus.
- **Satu command layer** untuk web lokal, web jarak jauh, dan konsol Serial, jadi kontrolnya sama.
- **Akses jarak jauh tanpa membuka port:** ESP32 hanya membuka satu koneksi WSS keluar ke Cloudflare Worker (paket Free).
- **Login diperkuat:** dua password terpisah, penguncian brute-force, sesi bertanda tangan, allowlist jarak jauh di dua sisi.
- **Dashboard ramah ponsel** (terang/gelap), update firmware OTA dengan rollback, cadangan & pulihkan, pemulihan listrik padam.
- **Teruji:** 385 pemeriksaan logika di PC, uji UI, dan uji end-to-end Worker, semuanya tanpa perangkat keras.

## Mulai cepat

```bash
git clone https://github.com/rhmt25/Mesin_Cuci_Otomatis.git
cd Mesin_Cuci_Otomatis
cp firmware/mesin_cuci_otomatis/secrets.example.h firmware/mesin_cuci_otomatis/secrets.h   # lalu isi
pio run -e rymcu-esp32-devkitc -t upload                                                   # flash lewat USB
```

Buka `http://mesincuci.local`, lalu ikuti [Manual Pengguna](docs/id/manual-pengguna.md).

## Dokumentasi

| | Bahasa Indonesia | English |
|---|---|---|
| Manual pengguna | [docs/id/manual-pengguna.md](docs/id/manual-pengguna.md) | [docs/en/user-manual.md](docs/en/user-manual.md) |
| Perangkat keras & kabel | [docs/id/perangkat-keras.md](docs/id/perangkat-keras.md) | [docs/en/hardware.md](docs/en/hardware.md) |
| Pemasangan | [docs/id/pemasangan.md](docs/id/pemasangan.md) | [docs/en/installation.md](docs/en/installation.md) |
| Arsitektur & keamanan | [docs/id/arsitektur.md](docs/id/arsitektur.md) | [docs/en/architecture.md](docs/en/architecture.md) |
| Referensi (perintah, API) | [docs/id/referensi.md](docs/id/referensi.md) | [docs/en/reference.md](docs/en/reference.md) |
| Pengembangan | [docs/id/pengembangan.md](docs/id/pengembangan.md) | [docs/en/development.md](docs/en/development.md) |

Lainnya: [Relay jarak jauh (Worker)](cloud/README.md) · [Kontribusi](CONTRIBUTING.md) · [Kebijakan keamanan](SECURITY.md) · [Catatan perubahan](CHANGELOG.md)

## Struktur repositori

```
firmware/mesin_cuci_otomatis/   Firmware ESP32 (Arduino IDE + PlatformIO)
web/                            Sumber dashboard, dibangun ke firmware dan Worker
cloud/                          Cloudflare Worker + Durable Object (akses jarak jauh, Telegram)
tools/                          Skrip build/deploy, uji PC, uji UI, git hook
docs/                           Dokumentasi (id/, en/)
diagram.json, wokwi.toml        Simulasi Wokwi
```

## Perangkat keras singkat

ESP32 WROOM + modul relay 6 kanal aktif-LOW + satu sensor level air. Peta pin dan catatan keselamatan:
[docs/id/perangkat-keras.md](docs/id/perangkat-keras.md).

> ⚠️ Proyek ini menyalakan peralatan bertegangan listrik rumah. Kabel AC berbahaya; rakit hanya bila kamu kompeten, dengan risiko sendiri.

## Berkontribusi

Issue dan pull request sangat diterima. Baca [CONTRIBUTING.md](CONTRIBUTING.md) dan [Kode Etik](CODE_OF_CONDUCT.md) dulu.
Untuk melaporkan celah keamanan, lihat [SECURITY.md](SECURITY.md).

## Lisensi

Hak cipta © 2026 Rahmat Hidayat. Berlisensi **GNU General Public License v3.0 atau yang lebih baru**, lihat [LICENSE](LICENSE).
Program ini disebarkan dengan harapan bermanfaat, tetapi **tanpa jaminan apa pun**.
