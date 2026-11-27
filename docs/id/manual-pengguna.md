# Manual Pengguna

Panduan memakai mesin cuci otomatis lewat dashboard web. Untuk pemasangan kabel lihat [Perangkat Keras](perangkat-keras.md),
untuk memasang firmware lihat [Pemasangan](pemasangan.md).

> **Bahasa lain:** [English](../en/user-manual.md)

## Daftar isi

1. [Konsep dasar](#1-konsep-dasar)
2. [Membuka dashboard](#2-membuka-dashboard)
3. [Beranda](#3-beranda)
4. [Membuat Proses](#4-membuat-proses)
5. [Menjalankan Proses](#5-menjalankan-proses)
6. [Keputusan yang menunggu](#6-keputusan-yang-menunggu)
7. [Menu Manual](#7-menu-manual)
8. [Aktivitas: Log, Riwayat, Konsol](#8-aktivitas-log-riwayat-konsol)
9. [Sistem: pengaturan](#9-sistem-pengaturan)
10. [Kontrol jarak jauh & notifikasi Telegram](#10-kontrol-jarak-jauh--notifikasi-telegram)
11. [Listrik padam & pemulihan](#11-listrik-padam--pemulihan)
12. [Pemecahan masalah](#12-pemecahan-masalah)

---

## 1. Konsep dasar

| Istilah | Arti |
|---|---|
| **Workspace** | **Cuci** (tabung cuci) atau **Pengering** (tabung spin). Pilih lewat tombol di atas Beranda/Proses/Manual. Keduanya bisa berjalan bersamaan, masing-masing punya 10 Proses sendiri. Proses pengering berisi Spin, Tunggu, Minta tindakan, dan Notifikasi. |
| **Tahap** | Satu langkah kerja mesin: isi air, buang air, putar, rendam, spin, pewangi, dst. Tiap Tahap punya variabelnya sendiri (lama, mode, jumlah ulang). |
| **Proses** | Rangkaian Tahap yang kamu susun sendiri dan beri nama (maks. **10 Proses**, tiap Proses maks. **40 Tahap**). |
| **Sensor level air** | Sensor di bagian atas tabung. Hanya tahu **penuh** atau **tidak penuh**. |
| **Keputusan** | Saat ada yang gagal, mesin **berhenti dan menunggu kamu**. Mesin tidak pernah melanjutkan sendiri setelah gagal. |

**Tidak ada program bawaan.** Setelah firmware baru terpasang, daftar Proses kosong. Buat Proses pertamamu di tab **Proses**.

## 2. Membuka dashboard

| Dari mana | Alamat |
|---|---|
| WiFi rumah | `http://mesincuci.local` (atau alamat IP yang dikirim lewat Telegram saat mesin online) |
| Hotspot darurat | Sambung ke WiFi `MesinCuci-XXXX` (kata sandi = `AP_PASSWORD`), lalu buka `http://192.168.4.1` |
| Dari mana saja (opsional) | Alamat Cloudflare Worker milikmu, mis. `https://mesin-cuci-relay.<akun>.workers.dev` |

Hotspot **hanya menyala bila semua WiFi tersimpan gagal tersambung**. Ia mati 2 menit setelah WiFi rumah pulih dan tidak ada klien.

Masuk dengan password dashboard (`WEB_PASSWORD` di `secrets.h`, bisa diganti di menu **Sistem → Keamanan**). Centang
**Ingat perangkat ini** agar tidak perlu login ulang (maks. 5 perangkat diingat).

Dashboard memakai lima menu: bar bawah di ponsel, rel samping di layar lebar. Tema **Otomatis / Terang / Gelap** diatur di
**Sistem → Mesin**.

## 3. Beranda

Beranda menampilkan keadaan mesin sekilas:

- **Status** dengan cincin progres dan sisa waktu.
- **Kartu keputusan** (muncul hanya saat mesin menunggu kamu, lihat [bagian 6](#6-keputusan-yang-menunggu)).
- **Rel Tahap**: ketuk area *Sisa Tahap* (atau *Lihat semua*) untuk melihat semua Tahap Proses yang berjalan: selesai, berjalan,
  dan antre, lengkap dengan variabelnya.
- Status **air**, **relay**, dan **koneksi**.
- Tombol **mulai Proses** dan kontrol **Jeda / Lanjutkan / Lewati / Stop**.

## 4. Membuat Proses

1. Buka tab **Proses** → **Proses baru**, beri nama.
2. Tambahkan **Tahap** dari yang terkecil. Tiap kartu Tahap berisi variabelnya sendiri.
3. Atur urutan dengan **menyeret pegangan di pojok kartu** (hanya pegangan yang bisa menyeret, supaya layar tidak ikut tergeser saat menggulir di ponsel).
4. Simpan. Estimasi durasi dan peringatan (mis. nilai di luar rentang) tampil otomatis.

Operasi lain di tab Proses: **salin** Proses (langsung tersimpan), **hapus**, **ekspor** dan **impor** satu Proses sebagai berkas JSON.

> Proses yang **sedang dipakai** (berjalan, dijeda, menunggu keputusan, menunggu mulai, atau menunggu pemulihan listrik padam)
> **tidak bisa diubah atau dihapus**. Tekan **Stop** dulu.

### Katalog Tahap

| Tahap | Fungsi & variabel utama (nilai bawaan) |
|---|---|
| **Isi air** | Mode `sensor` (isi sampai sensor penuh), `persen` (25 / 50 / 75 % dari hasil kalibrasi), atau `waktu`. Batas waktu `00:30:00` (sensor, persen); lama isi 30 mnt (waktu); coba ulang 3×; tunggu antar coba 15 mnt; **pra-cek** bila tabung sudah penuh: `buang` / `jeda` / `pakai`. |
| **Buang air** | Lama buang 5 mnt; **Verifikasi Pengurangan Jumlah Air** 45 dtk; **Tarik Ulang Valve** 2×; batas ulang sesi 3×. Cek akhir "tidak penuh" selalu dilakukan. |
| **Putar mesin** | Lama 15 mnt; pola `normal` (7 dtk nyala / 4 jeda), `gentle` (5,3 / 7), `hijab` (3 / 9). |
| **Soak** | Ulangi 2×; waktu putar 1 mnt; waktu rendam 4 mnt; pola. |
| **Rendam** | Lama 4 mnt (diam, semua relay mati). |
| **Spin** | Lama 10 mnt. Hanya di Proses pengering; tidak membaca sensor air. |
| **Pewangi** | 1 cup (kelipatan 0,5; **0,5 cup = 10 dtk**; pengaman pompa 240 dtk). |
| **Tunggu** | Jeda 00:00:10. |
| **Cek air** | Air harus `penuh` atau `tidak penuh`. |
| **Minta tindakan** | Pesan ≤ 40 karakter. Mesin berhenti sampai kamu pilih Lanjut/Batal; notifikasi selalu terkirim. |
| **Notifikasi** | Pesan ≤ 40 karakter. Terkirim walau mode Senyap, lalu lanjut. |

Rentang nilai yang sah untuk tiap variabel dikirim dari firmware ke dashboard (`/api/tahap/meta`), jadi dashboard dan mesin
tidak mungkin berbeda.

## 5. Menjalankan Proses

Dari **Beranda** (atau kartu Proses) pilih cara mulai:

| Cara | Keterangan |
|---|---|
| **Sekarang** | Langsung jalan. |
| **Tunda `HH:MM:SS`** | Mulai setelah jeda tertentu. |
| **Selesai pada `HH:MM`** | Mesin menghitung mundur dari perkiraan durasi. Isi/buang berbasis sensor bisa meleset, jadi waktu selesai hanya perkiraan. |

Mesin hanya bisa dimulai saat kondisi **IDLE**. Selama berjalan kamu bisa **Jeda**, **Lanjutkan**, **Lewati** Tahap, atau **Stop**
(semua relay mati seketika).

## 6. Keputusan yang menunggu

Bila Tahap Isi/Buang/Cek gagal atau ada Tahap *Minta tindakan*, mesin **berhenti dan tidak pernah lanjut sendiri**.
Dashboard menampilkan tombol sesuai kasus dan mengirim notifikasi (diulang tiap 10 menit).

| Kasus | Pilihan |
|---|---|
| Isi gagal · Buang gagal · Cek gagal | **Ulang** · **Lanjut** (dengan peringatan) · **Batal** |
| Air sudah penuh sebelum mengisi (pra-cek `jeda`) | **Buang lalu isi** · **Pakai air yang ada** · **Batal** |
| Minta tindakan | **Lanjut** · **Batal** |

**Cara kerja Buang air:** valve ditarik dan air harus mulai turun dari posisi penuh dalam waktu *Verifikasi*. Bila tidak, valve
dilepas 3 detik lalu ditarik lagi (*Tarik Ulang Valve*). Setelah terbukti turun, valve ditarik sampai *lama buang*, lalu dicek;
bila masih penuh, sesi diulang. Bila semua sesi gagal, mesin meminta keputusanmu.

## 7. Menu Manual

- **Jalankan satu Tahap** dengan form yang sama seperti di editor Proses.
- **Buang manual**: valve ditarik selama waktu tertentu **tanpa sensor**. Jalan darurat bila sensor rusak.
- **Panel Relay**: tombol ON/OFF per relay (hanya saat mesin IDLE, otomatis mati setelah 60 detik) dan **Uji berurutan**
  (kiri → kanan → isi → buang → spin → pewangi). Berguna setelah merakit kabel.
- **Kalibrasi air** (di Sistem → Mesin) mengukur waktu isi untuk mode `persen`.

## 8. Aktivitas: Log, Riwayat, Konsol

- **Log**: 100 kejadian terakhir (INFO/WARN/ERROR), diantrekan bila WiFi mati dan dikirim saat online.
- **Riwayat & statistik**: Proses yang pernah selesai, gagal, atau dibatalkan.
- **Konsol**: serial monitor via web, menerima [perintah](referensi.md#perintah). **Hanya tersedia di web lokal.**

## 9. Sistem: pengaturan

| Panel | Isi |
|---|---|
| **Koneksi** | Daftar WiFi (maks. 5, urutan prioritas), kontrol jarak jauh on/off. |
| **Keamanan** | **Password web lokal** dan **Password jarak jauh** (masing-masing ≥ 12 karakter, diketik dua kali); daftar perangkat yang diingat. Mengganti password web mengeluarkan semua perangkat. |
| **Mesin** | Mode notifikasi (`lengkap` / `ringkas` / `senyap`), kalibrasi air, tema tampilan. |
| **Pemeliharaan** | Info perangkat, **flash firmware** lewat web, **cadangan & pulihkan**, restart, reset semua. |

### Cadangkan & pulihkan

- **Unduh cadangan**: satu berkas JSON berisi pengaturan dan daftar WiFi. Password WiFi hanya ikut bila dicentang (simpan berkasnya dengan aman). Proses diekspor satu per satu di menu Proses.
- **Pulihkan**: berkas diperiksa dulu (simulasi) dan ringkasannya ditampilkan untuk dikonfirmasi. Pemulihan bersifat atomik: bila satu bagian tidak valid, tidak ada yang berubah.
- Hanya dari web lokal.

### Memperbarui firmware

**Sistem → Pemeliharaan → Firmware** → pilih berkas `.bin` → *Flash*. Hanya saat mesin IDLE. Firmware lama tetap utuh di partisi
cadangan; tombol **Kembali ke versi sebelumnya** tersedia bila versi baru bermasalah.

## 10. Kontrol jarak jauh & notifikasi Telegram

- Setelah Worker terpasang ([Pemasangan](pemasangan.md#4-kontrol-jarak-jauh-opsional)), buka alamat Worker dari mana saja dan
  masuk dengan **password jarak jauh** (berbeda dari password lokal, sengaja).
- Tampilan sama dengan web lokal, tetapi fitur sensitif **disembunyikan dan ditolak**: flash firmware, pengaturan WiFi, ganti
  password, perangkat diingat, konsol, dan pulihkan cadangan.
- Notifikasi Telegram dikirim oleh Worker: Proses selesai/gagal, keputusan menunggu, login terkunci, password diganti, dan
  alarm **"mesin tidak terhubung"**. Telegram hanya menerima notifikasi; **tidak ada perintah lewat chat**.
- Mode notifikasi diatur di Sistem → Mesin. Tahap *Notifikasi* dan *Minta tindakan* selalu terkirim.

## 11. Listrik padam & pemulihan

Saat listrik padam di tengah Proses, mesin **tidak** melanjutkan otomatis. Begitu menyala, dashboard menanyakan
**Lanjutkan** atau **Batalkan**. Pemulihan dibatalkan otomatis bila Proses itu sudah diubah. Semua relay mati saat boot.

## 12. Pemecahan masalah

| Gejala | Kemungkinan penyebab & solusi |
|---|---|
| `mesincuci.local` tidak terbuka | mDNS tidak didukung jaringan/OS. Pakai alamat IP dari notifikasi Telegram atau router. Di Windows lama pasang Bonjour. |
| Tidak bisa login, muncul hitungan mundur | Terkunci setelah 5 password salah. Tunggu hitungan selesai (30 dtk, 1, 2, 5, 15 mnt, lalu 1 jam). |
| Hotspot `MesinCuci-XXXX` tidak ada | Normal bila WiFi rumah tersambung. Hotspot hanya menyala saat semua WiFi tersimpan gagal. |
| Isi air selalu "gagal" | Periksa sensor level (GPIO27, `HIGH` = ada air) dan sumber 14; naikkan batas waktu; cek keran. |
| Air tidak turun saat Buang | Periksa motor penarik valve dan selang; sensor mungkin rusak, pakai **Buang manual**. |
| Relay mati semua & terkunci | Pengaman mendeteksi kombinasi berbahaya (mis. kiri+kanan). **Restart** mesin setelah memeriksa kabel. |
| Web terasa tersendat / putus-putus | Suplai 5V/USB drop saat WiFi memancar. Pakai catu cukup arus, kabel tebal, kapasitor 470–1000 µF, catu relay terpisah. |
| Mesin ditandai "tidak terhubung" di Telegram | ESP32 kehilangan WiFi/internet >5 menit. Cek router; mesin tetap menjalankan Proses yang sedang berjalan. |
| Flash lewat web ditolak | Mesin harus IDLE. Hentikan Proses lebih dulu. |

Masih bermasalah? Buka [issue](https://github.com/rhmt25/Mesin_Cuci_Otomatis/issues) dengan menyertakan versi firmware
(Sistem → Pemeliharaan → Info) dan potongan Log. **Jangan** sertakan password atau token.
