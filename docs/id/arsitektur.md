# Arsitektur & Keamanan

> **Bahasa lain:** [English](../en/architecture.md)

## Gambaran umum

```
                         ┌────────────────────── ESP32 (WROOM) ──────────────────────┐
HP/browser (rumah) ─HTTP─►  Core 0: WiFi · Web server · Remote · Notifier             │
                         │        │ command layer + api_core (satu dispatcher)        │
                         │        ▼                                                   │
                         │  Core 1: machine (state machine, millis(), tanpa delay())  │──► relay ──► motor/valve/pompa
                         │        ▲                                                   │◄── sensor level air
                         └────────┼───────────────────────────────────────────────────┘
                                  │ SATU koneksi WSS keluar
HP/browser (mana saja) ─HTTPS─► Cloudflare Worker + Durable Object ──► Telegram (notifikasi)
```

- **Core 1** menjalankan mesin; **Core 0** mengurus jaringan. Mesin tidak pernah memanggil fungsi jaringan: ia hanya menulis
  log dan *snapshot* status.
- Proses tetap berjalan saat WiFi mati. Log dan notifikasi diantrekan lalu dikirim saat online.
- **Satu command layer** dan **satu dispatcher API** (`api_core`) dipakai web lokal, web jarak jauh, dan Serial/Konsol. Kontrolnya sama persis.
- Dashboard memakai *polling* (`/api/poll`, 2 dtk; 10 dtk saat tab tersembunyi), tanpa WebSocket server.

## Peta kode

| Lokasi | Peran |
|---|---|
| `firmware/mesin_cuci_otomatis/machine.*` | State machine Proses (Core 1) |
| `tahap.*` | Katalog Tahap: jenis, rentang, bawaan, validasi (sumber kebenaran tunggal, dikirim ke dashboard lewat `/api/tahap/meta`) |
| `proses_store.*` | Penyimpanan Proses (JSON di LittleFS, penulisan atomik) |
| `relay.*`, `water_sensor.*` | Keluaran relay + interlock; pembacaan sensor level |
| `commands.*`, `api_core.*` | Perintah dan dispatcher API bersama |
| `web_server.*`, `web_ui.h` | Server HTTP/API; UI hasil generate (jangan diedit) |
| `net_wifi.*`, `net_policy.*` | WiFi multi-jaringan, hotspot darurat, watchdog konektivitas |
| `remote.*`, `remote_policy.*` | Klien WSS ke Worker + allowlist perintah jarak jauh |
| `notifier.*`, `logger.*` | Antrean notifikasi, ring log |
| `settings.*`, `backup.*`, `ota.*` | Pengaturan (NVS), cadangan/pulihkan, flash OTA |
| `web/` | Sumber dashboard (HTML/CSS/JS) → `tools/build_web.py` → `web_ui.h` & `cloud/public/index.html` |
| `cloud/` | Cloudflare Worker (login, sesi, allowlist, relay, Telegram) |
| `tools/` | Skrip build/deploy dan uji (`hosttest`, `uitest`) |

## Jaringan

- WiFi multi-jaringan (maks. 5). Hotspot darurat `MesinCuci-XXXX` **hanya** menyala bila semua WiFi tersimpan gagal.
- **Watchdog konektivitas:** tiap 30 dtk ping gateway; 3 gagal → sambung ulang WiFi; 3 reconnect gagal → WiFi dimulai ulang.
  Aktif hanya setelah gateway pernah menjawab ping.
- **Heartbeat jarak jauh:** `hb` tiap 30 dtk, Worker membalas `ack`; tanpa `ack` 100 dtk, ESP32 menyambung ulang.
- Worker mengirim alarm "mesin tidak terhubung" setelah `OFFLINE_MIN` (bawaan 5) menit tanpa kabar.

## Model keamanan

| Lapisan | Perlindungan |
|---|---|
| Dua password berbeda | Web lokal dan jarak jauh dipisah: bocornya satu tidak membuka yang lain. Minimal 12 karakter, diatur dari dashboard lokal. |
| Password jarak jauh | Dikirim ESP32 **sekali** lewat TLS; Worker menyimpan hash **PBKDF2-SHA256** (100.000 iterasi) di Durable Object. Tidak disimpan di ESP32 atau log. |
| Sesi | Cookie bertanda tangan HMAC, `HttpOnly`, `SameSite=Strict`, `Secure`/`__Host-` di HTTPS; pemeriksaan `Origin` (CSRF). Epoch sesi naik saat password diganti → semua sesi keluar. |
| Brute-force lokal | Tiap 5 salah beruntun: kunci 30 dtk, 1, 2, 5, 15 mnt, lalu 1 jam; masuk log WARN + notifikasi. |
| Brute-force jarak jauh | Kunci per IP bertingkat 5 mnt → 24 jam; >50 gagal dalam 10 menit dari banyak IP → semua login terkunci 5 menit. |
| Allowlist jarak jauh | Flash firmware, WiFi, ganti password, perangkat diingat, konsol, dan restore **hanya** dari web lokal. Ditegakkan **dua kali**: di Worker (`cloud/src/allow.js`) dan di ESP32 (`remote_policy.cpp`), diuji identik dengan `cloud/test/allow-vectors.txt`. |
| Rahasia | `secrets.h`, `.env.local`, `.secrets.local.json` ada di `.gitignore`; hook `pre-commit` menolak kredensial. Token Telegram hanya ada di Worker. |
| Mesin | Interlock relay berlapis, failsafe waktu, watchdog, semua relay mati saat boot/error/Stop (lihat [Perangkat Keras](perangkat-keras.md#lapisan-pengaman)). |

Menemukan celah? Lihat [SECURITY.md](../../SECURITY.md).

## Penyimpanan data

| Data | Lokasi |
|---|---|
| Proses | Berkas JSON di LittleFS (partisi `spiffs`), ditulis atomik |
| Pengaturan, WiFi, perangkat diingat (maks. 5) | NVS |
| Log (100 entri), antrean notifikasi (30) | RAM (ring buffer) |
| Hash password jarak jauh, sesi | Durable Object di Cloudflare |

Catatan deploy: bila kode Durable Object berubah, tambahkan migrasi `renamed_classes` di `cloud/wrangler.toml` (instans lama
dapat bertahan selama sambungan ESP32 masih terbuka; saat ini `Relay → RelayV2`).
