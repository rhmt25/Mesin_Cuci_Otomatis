# Pemasangan

> **Bahasa lain:** [English](../en/installation.md)

Urutan: **(1)** siapkan kredensial → **(2)** flash firmware → **(3)** uji dashboard lokal → **(4)** kontrol jarak jauh (opsional).

## 1. Menyiapkan `secrets.h`

```
copy firmware\mesin_cuci_otomatis\secrets.example.h firmware\mesin_cuci_otomatis\secrets.h
```

Isi `secrets.h` (berkas ini **tidak pernah di-commit**, ada di `.gitignore`):

| Makro | Fungsi |
|---|---|
| `WIFI_SSID` / `WIFI_PASSWORD` | WiFi rumah utama |
| `WIFI_SSID_2` / `WIFI_PASSWORD_2` | WiFi cadangan awal (opsional, bisa juga lewat dashboard) |
| `AP_PASSWORD` | Password hotspot darurat, **minimal 8 karakter** |
| `WEB_PASSWORD` | Password dashboard lokal (bisa diganti dari dashboard) |
| `REMOTE_URL` / `REMOTE_DEVICE_TOKEN` | Alamat `wss://…/device` dan token perangkat untuk kontrol jarak jauh. Kosongkan untuk menonaktifkan |

Token bot Telegram **tidak** disimpan di ESP32; ia menjadi secret di Worker.

## 2. Flash firmware

### Opsi A: PlatformIO (disarankan)

```
pio run -e rymcu-esp32-devkitc -t upload      # lewat USB
pio device monitor                            # serial 115200
```

Berkas `.bin` ada di `.pio/build/rymcu-esp32-devkitc/firmware.bin`. Pustaka diunduh otomatis.

### Opsi B: Arduino IDE

1. Buka `firmware/mesin_cuci_otomatis/mesin_cuci_otomatis.ino`.
2. Board: **ESP32 Dev Module**. **Tools → Partition Scheme → "Minimal SPIFFS (1.9MB APP with OTA)"** (wajib).
3. Library Manager: `ArduinoJson` (v7), `WebSockets` (Links2004), `AsyncTCP` dan `ESPAsyncWebServer` (keduanya versi **mathieucarbou**; pasang dari GitHub bila perlu).
4. Upload lewat USB. *Sketch → Export Compiled Binary* menghasilkan `.bin` untuk flash lewat web.

### Opsi C: lewat web (setelah firmware pertama terpasang)

Dashboard → **Sistem → Pemeliharaan → Firmware** → pilih `.bin` → *Flash*. Hanya saat mesin IDLE.

### Opsi D: deploy satu klik (Windows)

Memperbarui ESP32 lewat WiFi (OTA) **dan** Worker sekaligus.

1. Salin `.env.example` menjadi `.env.local` (git-ignored); isi `DEPLOY_ESP_PASSWORD` (password dashboard lokal) dan bila perlu `DEPLOY_ESP_HOST`.
2. Klik ganda `tools\deploy.cmd` (atau `powershell -File tools\deploy.ps1`).
3. Skrip membangun web dan firmware, mengunggah `.bin` ke `mesincuci.local` (hanya bila mesin IDLE), menunggu ESP32 hidup lagi,
   lalu menguji dan men-deploy Worker.

Opsi: `-SkipEsp`, `-SkipWorker`.

## 3. Uji dashboard lokal

1. Buka Serial Monitor; ESP32 mencetak alamat IP-nya setelah tersambung WiFi.
2. Buka `http://mesincuci.local` (atau IP itu), masuk dengan `WEB_PASSWORD`.
3. Ikuti [Manual Pengguna](manual-pengguna.md), mulai dari uji relay di menu **Manual**.

## 4. Kontrol jarak jauh (opsional)

Memakai Cloudflare Worker + Durable Object di paket **Free** (tanpa kartu kredit). ESP32 hanya membuka **satu koneksi WSS
keluar**; tidak perlu membuka port atau mengatur router.

Ringkasan (rincian lengkap di [`cloud/README.md`](../../cloud/README.md)):

1. Buat akun Cloudflare, **aktifkan 2FA**, pilih subdomain `*.workers.dev`.
2. `cd cloud && npm install && npx wrangler login`.
3. Buat rahasia lalu kirim ke Cloudflare:
   ```powershell
   $env:REMOTE_PASSWORD="password-jarak-jauh-panjang-dan-acak"
   $env:TELEGRAM_BOT_TOKEN="token-dari-BotFather"
   $env:TELEGRAM_CHAT_ID="chat-id-kamu"
   node tools/gen-secrets.mjs
   node tools/put-secrets.mjs
   ```
4. `npx wrangler deploy`.
5. Salin `DEVICE_TOKEN` dari `cloud/.secrets.local.json` ke `REMOTE_DEVICE_TOKEN`, dan alamat Worker ke `REMOTE_URL`, lalu flash ulang.
6. Setelah mesin online, atur password jarak jauh langsung dari dashboard lokal (**Sistem → Keamanan**).

### Bot Telegram

Buat bot lewat BotFather, ambil token, dan ambil `chat id` kamu. Isi lewat variabel lingkungan `TELEGRAM_BOT_TOKEN` dan `TELEGRAM_CHAT_ID`; `put-secrets.mjs` mengirimnya ke Worker sebagai secret. Token tidak perlu ada di ESP32.
Bot hanya **mengirim** notifikasi; ia tidak menerima perintah.

### Memperbarui UI Worker

Bila folder `web/` berubah: `python tools/build_web.py --remote` lalu `npx wrangler deploy` di folder `cloud`.
