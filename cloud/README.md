# Relay jarak jauh (Cloudflare Worker + Durable Object)

Web UI jarak jauh untuk Mesin Cuci. ESP32 hanya membuka **satu koneksi WebSocket keluar** ke Worker ini; Worker menyajikan
halaman UI yang sama dengan web lokal (fitur sensitif disembunyikan), meneruskan perintah, menyimpan cache status/log, dan
mengirim notifikasi Telegram (termasuk alarm "mesin tidak terhubung"). Web lokal ESP32 tetap berfungsi tanpa internet.

Semua berjalan di paket **Cloudflare Free** (tanpa kartu kredit). Melebihi batas gratis = operasi gagal, bukan ditagih.

## Uji lokal (tanpa akun Cloudflare)
```
cd cloud
npm install
npm test          # wrangler dev lokal + simulator ESP32 + Telegram palsu (±1 menit)
```

## Panduan akun & deploy (sekali saja)

### 1. Akun + 2FA
1. Daftar di <https://dash.cloudflare.com/sign-up> (email + password). **Verifikasi email** dulu.
2. Aktifkan 2FA: **My Profile → Authentication → Mobile App Authentication → Add**, pindai QR dengan aplikasi authenticator,
   masukkan kode, lalu **simpan backup codes** (tiap kode sekali pakai). Disarankan dua metode (authenticator + security key).
3. **Workers & Pages** → pilih subdomain `*.workers.dev` (sekali saja).

### 2. Login wrangler
```
cd cloud
npx wrangler login      # membuka browser, izinkan akses
```

### 3. Rahasia
PowerShell:
```
$env:REMOTE_PASSWORD="password-web-jarak-jauh-min-10-karakter"
node tools/gen-secrets.mjs          # membuat cloud/.secrets.local.json (tidak masuk git)
node tools/put-secrets.mjs          # mengirim ke Cloudflare; token & chat id Telegram diambil dari secrets.h ESP32
```
Lalu salin `DEVICE_TOKEN` dari `cloud/.secrets.local.json` ke `firmware/mesin_cuci_otomatis/secrets.h` sebagai
`REMOTE_DEVICE_TOKEN` (dikerjakan bersama saat Tahap 4).

### 4. Deploy
```
npx wrangler deploy
```
Alamat: `https://mesin-cuci-relay.<subdomain-kamu>.workers.dev` (UI) dan `wss://.../device` (ESP32).
Setelah ini perbarui UI bila `web/` berubah: `python tools/build_web.py --remote` lalu `npx wrangler deploy`.

## Keamanan
- Login: PBKDF2-SHA256 (100.000 iterasi), 5 gagal → kunci 5 menit per IP. Sesi = cookie bertanda tangan HMAC
  (`HttpOnly`, `SameSite=Strict`, `Secure`/`__Host-` di HTTPS), 400 hari bila "ingat", diperpanjang tiap dipakai.
  Putar `SESSION_SECRET` (`node tools/gen-secrets.mjs --rotate`) untuk mengeluarkan semua sesi.
- Allowlist: flash firmware, WiFi, ganti password, perangkat, konsol, dan restore **tidak** bisa dari jarak jauh
  (ditolak di Worker dan di ESP32).
- Token Telegram hanya ada di Worker. Token perangkat dipakai ESP32 untuk `/device`.

## Variabel (wrangler.toml)
`OFFLINE_MIN` (default 5): menit tanpa kabar sebelum alarm offline. `RPC_TIMEOUT_MS` (default 8000).
