# Panduan Desain — Dashboard Mesin Cuci (v4)

Sumber kebenaran visual & interaksi untuk `web/` (dipakai web lokal ESP32 **dan** web jarak jauh Cloudflare dari kode yang sama).
Baca dokumen ini sebelum mengubah UI. Rencana kerja ada di `plan.md` (lokal); dokumen ini yang di-commit.

---

## 1. Siapa, kapan, untuk apa

| Pengguna | Situasi | Yang paling penting |
|---|---|---|
| Pemilik di rumah (HP, satu tangan, di dekat mesin) | Memulai Proses, menjawab keputusan, cek sisa waktu | Tombol Mulai & keputusan besar dan jelas; status terbaca dalam 2 detik |
| Pemilik di luar rumah (HP, data seluler) | Memantau, menjawab "Minta tindakan", Stop darurat | Status segar + kapan terakhir diperbarui; offline jelas |
| Pemilik di PC (layar lebar) | Menyusun Proses, melihat log, mengatur sistem | Editor dua/tiga kolom, log nyaman dibaca |

Prinsip: **status dulu, aksi kedua, pengaturan terakhir.** Layar Beranda menjawab tiga pertanyaan tanpa gulir di HP:
"Mesin sedang apa?", "Berapa lama lagi?", "Apa yang harus saya lakukan?"

---

## 2. Arah visual

**"Instrumen presisi"** — panel kendali alat rumah tangga premium: permukaan tenang, satu warna aksen yang hidup,
angka besar dan tegas, garis tipis, kedalaman halus. Bukan dashboard SaaS generik.

Yang membuatnya tidak "AI slop":
- Satu aksen (#00E5C3) dipakai **hemat**: hanya untuk status aktif, aksi utama, progres. Tidak ada gradien ungu-biru, tidak ada
  emoji sebagai ikon, tidak ada kartu-kartu identik berderet tanpa hierarki.
- Bentuk mengikuti data: Tahap divisualkan sebagai **rel/timeline**, Proses sebagai **kartu dengan "jejak" ikon Tahap**,
  relay sebagai **panel sakelar fisik**, air sebagai **indikator dua keadaan** (sensor hanya tahu penuh/tidak penuh — jangan
  pernah menampilkan persen level air yang tidak ada datanya).
- Tidak ada angka/aktivitas karangan. Semua yang tampil berasal dari API.

---

## 3. Token warna

Semua warna lewat CSS custom properties di `:root` (`style.css`). Komponen **tidak boleh** memakai hex mentah.

### 3.1 Gelap (bawaan bila sistem gelap) — "hitam tidak pekat"
| Token | Nilai | Pakai |
|---|---|---|
| `--bg` | `#101517` | latar halaman (bukan #000) |
| `--surface` | `#161D20` | kartu |
| `--surface-2` | `#1D262A` | kartu terangkat, input, sheet |
| `--surface-3` | `#253136` | hover, segmented aktif |
| `--line` | `#2A363B` | garis/border 1px |
| `--text` | `#E8EFEE` | teks utama (kontras 14:1 pada `--bg`) |
| `--text-2` | `#9FB0AF` | teks sekunder (≥ 6:1) |
| `--text-3` | `#6F807F` | keterangan kecil (≥ 4.5:1 pada `--surface`) — jangan untuk teks penting |
| `--accent` | `#00E5C3` | aksen: progres, tombol utama, status aktif |
| `--accent-ink` | `#04201B` | teks DI ATAS aksen (kontras 12:1) |
| `--accent-soft` | `rgba(0,229,195,.12)` | latar chip/sorotan aksen |
| `--accent-text` | `#3FF0D3` | aksen sebagai teks/ikon di atas permukaan gelap |
| `--ok` | `#3DDC97` | selesai/aman |
| `--warn` | `#FFB547` | peringatan, menunggu keputusan |
| `--err` | `#FF6B6B` | galat, Stop |
| `--info` | `#7AB8FF` | informasi netral |

### 3.2 Terang — "putih tidak menyilaukan"
| Token | Nilai | Catatan |
|---|---|---|
| `--bg` | `#EEF3F2` | latar abu-hijau sangat muda |
| `--surface` | `#F8FBFA` | kartu (bukan #FFF murni) |
| `--surface-2` | `#FFFFFF` | sheet/input terangkat |
| `--surface-3` | `#E3EBEA` | hover |
| `--line` | `#D3DEDC` | |
| `--text` | `#14201E` | |
| `--text-2` | `#4A5C59` | |
| `--text-3` | `#6A7C79` | |
| `--accent` | `#00E5C3` | **hanya sebagai isian** (tombol, progres) — teks di atasnya `--accent-ink` |
| `--accent-ink` | `#04201B` | |
| `--accent-soft` | `rgba(0,179,152,.12)` | |
| `--accent-text` | `#00796B` | aksen sebagai teks/ikon di latar terang (kontras ≥ 4.6:1). **#00E5C3 sebagai teks di latar terang DILARANG** (kontras 1,6:1) |
| `--ok` `--warn` `--err` | `#1E9E6A` `#B86E00` `#D64545` | versi gelap agar kontras teks ≥ 4.5:1 |

### 3.3 Aturan tema
- `[data-theme=light|dark]` pada `<html>`; tanpa atribut → ikuti `prefers-color-scheme`. Pilihan tersimpan di `localStorage`.
- Pengaturan tema: tiga pilihan **Otomatis / Terang / Gelap** (di Sistem → Tampilan), plus tombol cepat di header.
- Status tidak boleh hanya warna: selalu ada ikon + teks (buta warna).

---

## 4. Tipografi

- Font: **sistem**, tanpa unduhan (web lokal harus jalan tanpa internet, flash ESP32 terbatas):
  `font-family: ui-sans-serif, system-ui, -apple-system, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif`.
- Angka (waktu, persen, langkah) selalu `font-variant-numeric: tabular-nums` agar tidak "loncat".
- Skala (rem, dasar 16px): `display 2.5/2.75` (sisa waktu besar), `h1 1.375/1.75`, `h2 1.125/1.5`, `body 1/1.5`,
  `small .875/1.4`, `micro .75/1.3` (hanya label sekunder, tidak untuk aksi).
- Berat: 400 isi, 600 judul/label tombol, 700 angka hero. Huruf besar semua hanya untuk label mikro (letter-spacing .06em).

---

## 5. Ruang, bentuk, kedalaman

- Grid 4px. Jarak: `--s1 4` `--s2 8` `--s3 12` `--s4 16` `--s5 24` `--s6 32` `--s7 48`.
- Radius: `--r-sm 8` (chip, input), `--r-md 14` (kartu), `--r-lg 22` (hero, sheet), `--r-pill 999`.
- Bayangan (terang): `0 1px 2px rgba(16,24,22,.06), 0 8px 24px -12px rgba(16,24,22,.18)`; gelap: bayangan sangat tipis +
  garis atas 1px `rgba(255,255,255,.04)` sebagai "cahaya".
- Kaca (backdrop-blur 14px) **hanya** untuk header lengket, bottom-nav, dan sheet — tidak untuk kartu konten (performa HP murah).
- Lebar konten maks 1200px. Gutter HP 16px.

---

## 6. Ikon

- Set SVG inline (sprite `<symbol>` di `index.html`), garis 1.75px, 24×24, `stroke="currentColor"`. **Tanpa emoji.**
- Ikon dekoratif `aria-hidden="true"`; tombol ikon-saja wajib `aria-label`.
- Daftar ikon (buat sendiri, gaya konsisten): `home`, `layers` (Proses), `hand` (Manual), `activity` (Aktivitas), `settings`
  (Sistem), `play`, `pause`, `stop`, `skip`, `refresh` (Ulang), `check`, `x`, `alert`, `wifi`, `wifi-off`, `cloud`, `cloud-off`,
  `drop` (isi), `drain` (buang), `rotate` (putar), `soak` (soak), `moon` (rendam), `spin` (spin), `flower` (pewangi),
  `timer` (tunggu), `search` (cek air), `bell` (notifikasi), `user-check` (minta tindakan), `plus`, `copy`, `trash`, `grip`
  (geser), `chevron-up/down/right`, `download`, `upload`, `lock`, `sun`, `moon-theme`, `monitor`, `power`, `cpu`, `shield`.
- Pemetaan jenis Tahap → ikon & warna rel ada di `app.js` (`TAHAP_UI`), bukan diulang di banyak tempat.

---

## 7. Navigasi (11 menu → 5)

| Menu baru | Isi (dari menu lama) |
|---|---|
| **Beranda** | status hero, keputusan, mulai Proses (Beranda lama) |
| **Proses** | daftar & editor Proses (Proses lama) |
| **Manual** | Tahap manual + kontrol relay + kalibrasi relay (Manual + Relay lama) |
| **Aktivitas** | segmen: Log · Riwayat & statistik · Konsol (lokal) (Log + Riwayat + Konsol lama) |
| **Sistem** | grup: Koneksi (WiFi, jarak jauh) · Keamanan (2 password, perangkat diingat) · Mesin (notifikasi, kalibrasi air, tema) · Pemeliharaan (firmware, cadangan, restart) (Pengaturan + WiFi + Firmware + Sistem lama) |

- HP (< 900px): **bottom-nav** 5 ikon+label, tinggi 64px + safe-area, item ≥ 48px. Header ringkas di atas.
- Desktop (≥ 900px): **rail kiri** 72px (ikon+label kecil) atau 220px (≥ 1280px, ikon+teks).
- Mode jarak jauh: item/grup bertanda `data-local` disembunyikan (Konsol, WiFi, Firmware, Keamanan, Cadangan, impor Proses).
- URL hash per menu (`#beranda`, `#proses`, …) agar tombol kembali & muat ulang tetap di menu yang sama.
- Badge di ikon Beranda bila ada **keputusan menunggu** (titik `--warn` + label tersembunyi untuk pembaca layar).

---

## 8. Layar per layar

### 8.1 Beranda (bento, bukan baris)
HP (urutan tumpuk): Kartu keputusan (bila ada) → Hero → Rel Tahap mini → Status perangkat (air · relay · koneksi) → Mulai Proses.
Desktop (grid 12 kolom):
```
┌────────────── Hero (8) ──────────────┐┌──── Rel Tahap (4) ────┐
│ cincin progres  │ Tahap 3/7 Soak       ││ ● Isi air       selesai│
│   64%           │ "Soak: rendam 2/2"   ││ ● Putar         selesai│
│  sisa ±42m      │ sisa Tahap 3m 10d    ││ ◉ Soak          berjalan│
│                 │ [Jeda] [Lewati] [Stop]││ ○ Buang         antre  │
└──────────────────────────────────────┘│ ○ Spin  … [Lihat semua]│
┌ Air (3) ┐┌ Relay (5) ───────┐┌ Koneksi (4)┘└──────────────────────┘
│ ◐ Penuh ││ kiri ▮ kanan ▯ …  ││ WiFi · Server · jam │
└─────────┘└───────────────────┘└─────────────────────┘
┌─────────── Mulai Proses (12) — kartu dengan jejak ikon Tahap ───────────┐
```
- **Hero**: cincin progres (stroke aksen, ujung membulat, ketebalan 10px), angka sisa total besar (`display`), nama Proses,
  label Tahap "Tahap 3 dari 7 · Soak", detail fase ("Agitasi (kanan)"), dan tombol Jeda/Lanjutkan · Lewati · **Stop** (Stop
  merah, konfirmasi). Saat siap: hero berubah jadi ajakan "Pilih Proses untuk mulai" + Proses terakhir dipakai (dari riwayat)
  sebagai tombol cepat.
- **Area "Sisa Tahap" dapat diklik** → membuka **Sheet Rel Tahap** (8.2).
- Waktu: tampilkan "±" pada perkiraan; jangan presisi palsu (detik hanya untuk < 10 menit).
- Indikator air: dua keadaan + "sedang mengukur" (`penuh` / `tidak penuh` / `mengukur…`), ikon tetes berisi/kosong.
- Relay: 6 sakelar kecil read-only (LED + nama), motor kiri/kanan beranimasi putar halus saat aktif (dimatikan pada
  `prefers-reduced-motion`).
- Koneksi: WiFi (RSSI ke 4 batang), server jarak jauh (terhubung/menyambung/nonaktif), jam.
- **Kartu keputusan**: lebar penuh, di atas segalanya, border kiri 4px `--warn`, ikon alert, pesan, tombol pilihan besar
  (≥ 48px). "Lanjut" membuka dialog peringatan (teks yang sudah ada di `LANJUT_WARN`). Sticky di bawah header saat menggulir.
- Banner: listrik padam (pemulihan), relay terkunci, offline (jarak jauh) — gaya "callout" dengan ikon, bukan blok merah penuh.

### 8.2 Sheet Rel Tahap (fitur baru)
Dibuka dari klik hero/rel. HP: **bottom sheet** tinggi 90vh, bisa ditarik turun / tombol tutup / Esc. Desktop: **panel samping**
kanan 440px.
- Kepala: nama Proses, "Tahap 3 dari 7", perkiraan selesai jam (bila jam sinkron) dan total sisa.
- Isi: **rel vertikal** (garis 2px) dengan simpul per Tahap; tiap simpul = **kartu Tahap lengkap**:
  ikon & nama jenis, status (Selesai ✓ / Berjalan ◉ beranimasi / Antre ○ / Dilewati), **semua variabel** dalam grid label–nilai
  (mis. Isi air: Mode Sensor · Batas 00:30:00 · Coba ulang 3× · Tunggu 15 mnt · Pra-cek Buang dulu), perkiraan durasi.
  Kartu yang berjalan diperluas dan menampilkan detail fase + sisa Tahap; kartu selesai diringkas (bisa dibuka).
- Gulir otomatis ke Tahap berjalan saat dibuka.
- Label & format variabel memakai meta firmware (`/api/tahap/meta`) — sama dengan editor; tidak ada teks ganda.
- Saat menunggu mulai (tunda): simpul paling atas "Menunggu mulai · mulai ±07:12".
- Manual/uji relay: satu kartu.
- Data dari endpoint baru `GET /api/job` (lihat plan); dimuat saat sheet dibuka dan saat nomor Tahap berubah, bukan tiap poll.

### 8.3 Proses
- Daftar: **grid kartu** (2 kolom HP lebar ≥ 560px, 3 kolom desktop). Kartu: nama, jumlah Tahap, perkiraan, **jejak ikon Tahap**
  (deretan ikon kecil berurutan, maks 12 lalu "+n"), status kunci (gembok + "sedang dipakai"), menu ⋯ (Edit, Salin, Ekspor,
  Hapus). Tombol **Mulai** ada di kartu Proses pada Beranda (satu tempat memulai; kartu di menu Proses untuk mengelola). Kartu "+ Proses baru" sebagai kartu bergaris putus-putus di akhir grid.
- Opsi mulai (Sekarang / Tunda / Selesai pada jam) sebagai **segmented control** di atas grid Beranda, bukan select.
- Editor (layar penuh di HP, tiga kolom di desktop):
  kiri **Palet Tahap** (kartu kecil ikon+nama+deskripsi satu baris; HP: sheet yang dibuka tombol "+ Tahap"),
  tengah **Rel Tahap yang disusun** (kartu sama seperti 8.2 tetapi bisa diedit, nomor, pegangan geser, ▲▼ ⧉ 🗑 sebagai ikon
  ber-label), kanan **Ringkasan** lengket: nama, total Tahap, perkiraan, peringatan, tombol Simpan / Simpan sebagai baru.
- Field mengikuti meta; field yang tidak berlaku untuk mode disembunyikan dengan animasi tinggi 150ms.
- Input waktu `HH:MM:SS` sebagai tiga segmen bernomor dengan label, bukan tiga kotak tanpa label.
- Validasi di bawah field (bukan hanya di atas), pesan bahasa Indonesia dari firmware/meta.

### 8.4 Manual
Dua kolom desktop, tumpuk di HP:
- **Jalankan Tahap**: pilih jenis sebagai **kisi kartu ikon** (bukan dropdown), form variabel muncul di bawah, tombol Jalankan.
  Buang manual diberi label "Darurat" dan teks penjelas.
- **Relay**: panel sakelar (6 kartu sakelar 2×3) dengan LED, nama, pin; konflik interlock ditunjukkan dengan gembok + alasan.
  Peringatan keselamatan sebagai callout di atas. "Uji berurutan" + "Matikan semua" di kepala panel.
- Saat Proses berjalan: seluruh panel dikunci dengan overlay tipis + alasan + tautan ke Beranda.

### 8.5 Aktivitas
Segmented control: **Log** · **Riwayat** · **Konsol** (Konsol `data-local`).
- Log: daftar bergaya timeline ringkas, ikon/warna per level, filter level sebagai chip, waktu tabular, tombol unduh.
- Riwayat: 3 kartu statistik kecil (Proses selesai, jam motor, rata-rata isi) + daftar riwayat dengan badge hasil.
- Konsol: font monospace, area gelap meski tema terang (identitas "terminal").

### 8.6 Sistem
Halaman bergrup (kartu-kartu pengaturan dengan judul grup), daftar isi lengket di desktop:
1. **Koneksi**: WiFi (daftar + tambah), Kontrol jarak jauh (status + aktif/nonaktif).
2. **Keamanan**: Password lokal, Password jarak jauh, Perangkat diingat.
3. **Mesin**: Notifikasi (segmented Lengkap/Ringkas/Senyap), Kalibrasi isi air, Tampilan (tema).
4. **Pemeliharaan**: Info sistem, Firmware (unggah + rollback), Cadangkan & pulihkan, Reset pengaturan, Restart.
Aksi berbahaya (reset, restart, rollback, hapus) memakai gaya "danger-outline" dan konfirmasi.

---

## 9. Komponen (spesifikasi singkat)

| Komponen | Spesifikasi |
|---|---|
| Tombol | tinggi 44 (kecil 36 hanya di desktop/toolbar), radius `--r-sm`+2, varian: `primary` (isi aksen, teks `--accent-ink`), `secondary` (surface-3), `ghost`, `danger` (isi `--err`), `danger-outline`. Ikon kiri opsional. Status: hover, active (skala .98), focus-visible (cincin 2px aksen + offset 2px), disabled (opasitas .45 + `cursor:not-allowed`), loading (spinner, teks tetap) |
| Segmented | pill, item aktif `--surface-3` + teks `--text`, transisi 150ms |
| Kartu | surface + border 1px line + radius md; kartu interaktif hover naik 1px |
| Chip | tinggi 28, radius pill, varian netral/aksen/warn/err |
| Field | label di atas (selalu terlihat), input 44px, bantuan/galat di bawah, satuan sebagai sufiks di dalam field |
| Sheet/Dialog | latar gelap 50%, fokus terkunci di dalam, Esc menutup, fokus kembali ke pemicu |
| Toast | kanan bawah desktop / atas bottom-nav di HP, ikon per jenis, maks 1 sekaligus, 3,2 dtk, `role=status` |
| Skeleton | blok surface-3 berdenyut halus untuk kartu yang memuat |
| Rel Tahap | garis 2px `--line`, simpul 14px; selesai isi `--ok`, berjalan cincin aksen berdenyut, antre outline |

---

## 10. Status yang wajib ada

| Status | Tampilan |
|---|---|
| Memuat pertama | skeleton hero + kartu (tanpa "—" kosong berkedip) |
| Siap, belum ada Proses | ilustrasi garis sederhana (SVG kecil) + "Buat Proses pertama" |
| Berjalan / Dijeda / Menunggu mulai / Menunggu keputusan | chip status di header + hero menyesuaikan |
| Keputusan | kartu keputusan + badge nav |
| Offline (jarak jauh) | banner atas + kontrol dinonaktifkan + "terakhir diperbarui jam …" |
| Koneksi lokal tersendat | indikator kecil di header "menyambung ulang…" (poll gagal ≥ 2×) |
| Relay terkunci | banner err dengan langkah pemulihan |
| Terkunci karena dipakai | gembok + alasan di kartu Proses/editor/Manual |
| Galat simpan | pesan di dekat field/tombol, bukan hanya toast |
| Hanya lokal | elemen `data-local` tersembunyi di mode jarak jauh |

---

## 11. Gerak

- Durasi: mikro 120–150ms, panel/sheet masuk 220ms (`cubic-bezier(.2,.8,.2,1)`), keluar 160ms.
- Animasi yang bermakna saja: progres cincin, simpul berjalan berdenyut (2s), motor berputar saat relay aktif, sheet meluncur.
- `prefers-reduced-motion: reduce` → matikan denyut/putar/geser; ganti dengan perubahan instan.
- Jangan animasikan `width/height` besar; pakai `transform`/`opacity`.

---

## 12. Aksesibilitas & sentuh

- Kontras teks ≥ 4.5:1, ikon/garis bermakna ≥ 3:1 di kedua tema.
- Target sentuh ≥ 44×44, jarak ≥ 8px. Bottom-nav item ≥ 48px.
- Semua kontrol bisa dengan keyboard; urutan tab = urutan visual; `:focus-visible` jelas.
- `aria-live="polite"` untuk ringkasan status hero (diperbarui hanya saat Tahap/keadaan berubah, bukan tiap 2 dtk).
- Keputusan menunggu diumumkan `aria-live="assertive"` sekali.
- Bahasa: `lang="id"`; teks tombol kata kerja ("Mulai", "Lanjutkan", "Hentikan").

---

## 13. Batas teknis (wajib)

- Satu halaman (`index.html` + `style.css` + `app.js`) dibundel `tools/build_web.py` → gzip ke `web_ui.h` (PROGMEM ESP32) dan
  `cloud/public/index.html`. **Target ukuran gzip ≤ 36 KB** (sekarang ±33 KB). Tanpa pustaka/CDN/font luar/gambar raster.
- Polling, bukan WebSocket (`/api/poll` 2 dtk, 10 dtk saat tab tersembunyi). Jangan menambah permintaan per poll.
- Semua teks bahasa Indonesia. Istilah: **Proses**, **Tahap**, **keputusan**, **Tarik Ulang Valve**, **Verifikasi Pengurangan
  Jumlah Air**.
- Data variabel/label/rentang Tahap selalu dari `/api/tahap/meta`.

---

## 14. Catatan implementasi (v4)

- Palet Tahap di HP berupa **strip gulir horizontal** (bukan sheet); di desktop kolom kiri.
- Konfirmasi memakai dialog bawaan peramban (`confirm`), bukan dialog kustom: andal, aksesibel, dan hemat ukuran.
- Kartu Proses di daftar memuat `trail` (maks 12 jenis Tahap) dari `GET /api/proses`.
- Ukuran halaman: ±33 KB gzip (batas dinaikkan ke 36 KB) (`build_web.py` membuang komentar dan indentasi).
