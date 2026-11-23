# Panduan Pengembangan

> **Bahasa lain:** [English](../en/development.md)

## Struktur repositori

```
.
├── firmware/mesin_cuci_otomatis/   Sketch Arduino/PlatformIO (satu sumber kode)
├── web/                            Sumber dashboard (index.html, style.css, app.js)
├── cloud/                          Cloudflare Worker + Durable Object (relay jarak jauh)
│   ├── src/  test/  tools/  public/
├── tools/
│   ├── build_web.py                web/ → web_ui.h (+ cloud/public/index.html)
│   ├── deploy.ps1 / deploy.cmd     Deploy satu klik (OTA + Worker)
│   ├── hosttest/                   Uji logika mesin di PC (g++)
│   ├── uitest/                     Uji antarmuka (jsdom)
│   └── hooks/pre-commit            Penolak kredensial
├── docs/                           Dokumentasi (id/ dan en/)
├── diagram.json, wokwi.toml        Simulasi Wokwi (harus di akar repo)
├── platformio.ini                  Konfigurasi build
├── .github/                        CI, template issue/PR
└── LICENSE                         GNU GPL v3
```

## Menyiapkan lingkungan

| Kebutuhan | Untuk |
|---|---|
| [PlatformIO](https://platformio.org/) | Membangun/flash firmware |
| Python 3 | `tools/build_web.py` |
| `g++` (≥ C++17) | Uji logika di PC |
| Node.js ≥ 18 | Worker, uji UI |

Aktifkan hook pencegah kebocoran kredensial (sekali per clone):

```
git config core.hooksPath tools/hooks
```

## Alur kerja

1. **Ubah dashboard?** Edit `web/`, lalu `python tools/build_web.py --remote`. Jangan edit `web_ui.h` dan `cloud/public/index.html` manual (hasil generate; keduanya di-commit agar firmware bisa dibangun tanpa Python).
2. **Ubah katalog Tahap?** Edit `tahap.cpp`; dashboard mengambilnya otomatis. Perbarui fixture UI: `test_tahap --meta > tools/uitest/meta.json`.
3. **Ubah daftar perintah jarak jauh?** Ubah `cloud/src/allow.js` **dan** `remote_policy.cpp`, lalu perbarui `cloud/test/allow-vectors.txt`. Kedua sisi harus identik.
4. Jalankan semua uji (di bawah), lalu commit.

## Menjalankan uji

| Uji | Perintah | Cakupan |
|---|---|---|
| Logika mesin | `sh tools/hosttest/run.sh` | State machine dengan jam palsu + model tangki air (204 pemeriksaan) dan katalog Tahap + penyimpanan Proses (181) |
| Antarmuka | `cd tools/uitest && npm install && npm test` | Editor Proses/Tahap, kartu keputusan, Manual, drag & drop, penguncian relay, perangkat |
| Worker | `cd cloud && npm install && npm test` | `wrangler dev` lokal + simulator ESP32 + Telegram palsu (±1 menit), tanpa akun Cloudflare |
| Build firmware | `pio run -e rymcu-esp32-devkitc` | Kompilasi |

`hosttest` perlu pustaka ArduinoJson di `.pio/libdeps/...`; jalankan `pio run` sekali lebih dulu.

## Gaya kode

- Komentar dan pesan UI dalam **bahasa Indonesia**; identifier campuran sesuai kode yang ada. Ikuti gaya berkas di sekitarnya.
- Mesin: tanpa `delay()`, tanpa pemanggilan jaringan dari Core 1.
- Logika murni (kebijakan, backup, katalog) dipisah dari I/O agar bisa diuji di PC.
- Setiap berkas sumber diawali header `SPDX-License-Identifier: GPL-3.0-or-later` dan `Copyright (C) 2026 Rahmat Hidayat`. Berkas baru harus memakai header yang sama.
- Format penyunting: lihat `.editorconfig`.

## Pesan commit

`<tipe>: <ringkasan>` dengan tipe `feat`, `fix`, `style`, `refactor`, `docs`, `test`, `chore`. Contoh: `fix: drag kartu Tahap hanya lewat pegangan`.

## Rilis

1. Naikkan `FW_VERSION` di `config.h` dan perbarui [CHANGELOG](../../CHANGELOG.md).
2. Jalankan semua uji.
3. `pio run`, lampirkan `firmware.bin` pada rilis GitHub.
4. Beri tag `vX.Y.Z`.

Lihat juga [CONTRIBUTING.md](../../CONTRIBUTING.md).
