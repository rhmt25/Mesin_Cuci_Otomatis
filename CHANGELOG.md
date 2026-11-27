# Changelog / Catatan Perubahan

Format mengikuti [Keep a Changelog](https://keepachangelog.com/); versi mengikuti `FW_VERSION` di `config.h`.

## [Belum dirilis / Unreleased]

### Ditambahkan / Added
- Dua workspace yang berjalan bersamaan: **Cuci** dan **Pengering**, masing-masing dengan 10 Proses, jeda/stop/lewati,
  jadwal, dan pemulihan listrik padam sendiri. / Two workspaces running side by side: wash and spin-dry.
- Failsafe motor spin (61 menit). / Spin motor failsafe.
- Kartu **Pengaturan air** (gabungan dengan kalibrasi isi): lama konfirmasi 1-60 detik (bawaan 10) agar cipratan diabaikan; berlaku juga di pra-cek isi
  dan verifikasi buang. / Configurable water sensor confirmation time to ignore splashes.
- Deploy memeriksa judul halaman dan alamat MAC (`DEPLOY_ESP_MAC`) sebelum mengirim password atau firmware.
  / Deploy verifies the target device before sending anything.
- Persiapan open source: lisensi **GPL-3.0-or-later**, header SPDX di semua berkas sumber, README dwibahasa (EN/ID),
  dokumentasi lengkap di `docs/` termasuk manual pengguna, CONTRIBUTING, CODE_OF_CONDUCT, SECURITY, template issue/PR,
  dan CI GitHub Actions. / Open-source preparation: GPL-3.0-or-later license, SPDX headers, bilingual README and full docs,
  community files, and CI.

### Diubah / Changed
- Tahap Spin hanya ada di Proses pengering dan tidak lagi membaca sensor air. Spin di Proses cuci lama otomatis menjadi
  Notifikasi (di akhir) atau Minta tindakan (di tengah). / The Spin Stage now belongs to dry Programs only.
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
