# Contributing / Berkontribusi

Terima kasih sudah ingin membantu! / Thanks for helping out!

## Ringkas / TL;DR

1. Fork → buat branch dari `master` (`feat/...`, `fix/...`). / Fork, branch from `master`.
2. Ikuti [Panduan Pengembangan](docs/id/pengembangan.md) ([EN](docs/en/development.md)).
3. Jalankan semua uji sebelum membuka PR. / Run all tests before opening a PR.
4. Buka Pull Request dengan deskripsi jelas. / Open a PR with a clear description.

## Sebelum mulai / Before you start

- Cari issue yang sudah ada; untuk perubahan besar, buka issue dulu agar arahnya disepakati.
  For big changes, open an issue first.
- **Keselamatan nomor satu.** Perubahan pada relay, interlock, failsafe, atau allowlist jarak jauh wajib disertai uji dan
  penjelasan risikonya. Mesin tidak boleh melanjutkan sendiri setelah gagal.
  Changes touching relays, interlocks, failsafes or the remote allowlist must come with tests and a risk note.
- **Jangan pernah** meng-commit kredensial (`secrets.h`, `.env.local`, token). Aktifkan hook: `git config core.hooksPath tools/hooks`.

## Pengaturan lokal / Local setup

```
git config core.hooksPath tools/hooks
sh tools/hosttest/run.sh                       # logika mesin / machine logic
(cd tools/uitest && npm install && npm test)   # antarmuka / UI
(cd cloud && npm install && npm test)          # Worker
pio run -e rymcu-esp32-devkitc                 # kompilasi firmware / build
```

Edit `web/` lalu `python tools/build_web.py --remote`; jangan edit `web_ui.h` / `cloud/public/index.html` manual.

## Gaya / Style

- Komentar & teks UI bahasa Indonesia; ikuti gaya berkas di sekitarnya. Dokumentasi: `docs/id` dan `docs/en` dijaga setara.
- Berkas baru memakai header `SPDX-License-Identifier: GPL-3.0-or-later` + `Copyright (C) <tahun> <nama>`.
- Commit: `<tipe>: <ringkasan>` (`feat`, `fix`, `style`, `refactor`, `docs`, `test`, `chore`).

## Lisensi kontribusi / Licensing

Dengan mengirim kontribusi, kamu setuju kontribusimu dilisensikan di bawah **GNU GPL v3.0 atau yang lebih baru**, sama dengan proyek ini.
By submitting a contribution you agree it is licensed under **GPL-3.0-or-later**, same as the project.

## Kode etik / Code of conduct

Dengan berpartisipasi kamu setuju pada [Kode Etik](CODE_OF_CONDUCT.md).
