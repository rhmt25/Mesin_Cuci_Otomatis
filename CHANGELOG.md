# Changelog / Catatan Perubahan

Format mengikuti [Keep a Changelog](https://keepachangelog.com/); versi mengikuti `FW_VERSION` di `config.h`.

## [Belum dirilis / Unreleased]

### Ditambahkan / Added
- Persiapan open source: lisensi **GPL-3.0-or-later**, header SPDX di semua berkas sumber, README dwibahasa (EN/ID),
  dokumentasi lengkap di `docs/` termasuk manual pengguna, CONTRIBUTING, CODE_OF_CONDUCT, SECURITY, template issue/PR,
  dan CI GitHub Actions. / Open-source preparation: GPL-3.0-or-later license, SPDX headers, bilingual README and full docs,
  community files, and CI.

### Diubah / Changed
- Folder kosong bawaan PlatformIO (`include/`, `lib/`, `test/`) dihapus. / Removed empty PlatformIO placeholder folders.

## [2.0.0]

Rilis besar pertama yang terdokumentasi. / First documented major release.

- State machine Proses/Tahap yang dapat disusun pengguna (tanpa program bawaan), 11 jenis Tahap.
- Dashboard web lokal (polling), hotspot darurat, WiFi multi-jaringan, watchdog konektivitas.
- Kontrol jarak jauh lewat Cloudflare Worker + Durable Object; notifikasi Telegram dari Worker.
- Dua password terpisah, penguncian brute-force, perangkat diingat, allowlist jarak jauh dua sisi.
- Flash firmware lewat web dengan rollback, cadangan & pulihkan atomik, pemulihan listrik padam.
- Uji logika di PC (385 pemeriksaan), uji UI (jsdom), uji end-to-end Worker.
- Deploy satu klik (OTA WiFi + Worker) lewat `tools/deploy.cmd`.
