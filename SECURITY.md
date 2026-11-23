# Kebijakan Keamanan / Security Policy

## Versi yang didukung / Supported versions

Hanya rilis terbaru di cabang `master` yang menerima perbaikan keamanan. / Only the latest release on `master` receives security fixes.

## Melaporkan celah / Reporting a vulnerability

**Jangan membuka issue publik** untuk celah keamanan. / **Do not open a public issue** for a vulnerability.

Gunakan **GitHub → tab Security → "Report a vulnerability"** (private advisory) pada repositori ini:
<https://github.com/rhmt25/Mesin_Cuci_Otomatis/security/advisories/new>

Sertakan / Include: deskripsi, langkah mereproduksi, dampak, versi firmware (`info`), dan (bila ada) usulan perbaikan.
Jangan sertakan password/token asli. Kami berusaha menanggapi dalam beberapa hari dan mengumumkan perbaikan setelah tersedia.

## Cakupan / Scope

Menarik / In scope: bypass login atau allowlist jarak jauh, kebocoran kredensial, eksekusi perintah tak sah, kegagalan interlock
relay yang bisa merusak perangkat, kelemahan sesi/CSRF di Worker atau ESP32.

Di luar cakupan / Out of scope: akses fisik ke perangkat, serangan yang memerlukan password yang sah, DoS volumetrik terhadap
akun Cloudflare milik pengguna, kelemahan pada layanan pihak ketiga.

## Praktik terbaik bagi pengguna / Hardening tips

- Password lokal dan jarak jauh **berbeda**, ≥ 16 karakter acak. / Use different, long random passwords.
- Aktifkan 2FA pada akun Cloudflare dan Telegram. / Enable 2FA on Cloudflare and Telegram.
- Jangan pernah meng-commit `secrets.h`, `.env.local`, atau `.secrets.local.json`. / Never commit secrets.
- Jaga firmware dan Worker tetap terbaru. / Keep firmware and Worker up to date.

Rincian model keamanan: [docs/id/arsitektur.md](docs/id/arsitektur.md#model-keamanan) · [EN](docs/en/architecture.md#security-model).
