# Perangkat Keras

> **Bahasa lain:** [English](../en/hardware.md)

> ⚠️ **Peringatan keselamatan.** Proyek ini mengendalikan mesin bertegangan listrik rumah (AC) dan air. Pekerjaan kabel AC
> berisiko sengatan listrik dan kebakaran. Kerjakan hanya bila paham kelistrikan, cabut daya sebelum menyentuh kabel, pasang
> sekring/MCB dan pengaman arus bocor (ELCB/RCD), dan isolasi semua sambungan. Proyek ini diberikan **tanpa jaminan** (lihat
> [LICENSE](../../LICENSE)); kamu bertanggung jawab atas pemasangannya.

## Komponen

| Komponen | Keterangan |
|---|---|
| Mikrokontroler | ESP32 modul **WROOM** (board dev, mis. RYMCU ESP32-DevKitC) |
| Modul relay | 6 kanal, **Normally Open, trigger LOW**, 5 V |
| Sensor level air | Sensor/switch level (kontak kering) di bagian atas tabung |
| Beban | Motor cuci (kiri/kanan), valve isi air, motor penarik valve buang, motor spin, pompa pewangi |
| Catu daya | 5 V cukup arus untuk ESP32 + relay (disarankan catu relay terpisah), kapasitor 470–1000 µF |

## Pemetaan pin (ESP32 WROOM)

Didefinisikan di [`config.h`](../../firmware/mesin_cuci_otomatis/config.h).

| Fungsi | GPIO | Catatan |
|---|---|---|
| Motor putar kiri | **25** | dulu GPIO5 (pin strapping); pindahkan kabel IN relay |
| Motor putar kanan | 18 | |
| Valve isi air | 17 | |
| Motor penarik valve buang | 16 | |
| Motor spin | 4 | |
| Pompa pewangi | 13 | |
| Sensor level air | 27 | `INPUT_PULLUP`; `HIGH` = ada air |
| Sumber sensor/tombol | 14 | selalu `HIGH` |

Modul relay memakai logika **aktif-LOW** dan pin keluaran mode **open-drain** (`RELAY_ACTIVE_LOW`, `RELAY_OPEN_DRAIN` di
`config.h`): `HIGH` berarti pin mengambang, aman untuk relay 5 V yang digerakkan dari logika 3,3 V.

Diagram simulasi ada di [`diagram.json`](../../diagram.json) (Wokwi).

## Lapisan pengaman

Motor kiri dan kanan **tidak pernah** menyala bersamaan:

1. Interlock di `relaySet`.
2. Pin motor lawan dipaksa mati secara fisik sebelum motor dinyalakan.
3. Jeda ganti arah minimal 500 ms.
4. `relayGuard()` tiap tick: bila terdeteksi kiri+kanan, isi+buang, atau buang+motor, **semua relay dimatikan dan dikunci
   sampai restart**.
5. Motor cuci dimatikan paksa bila menyala terus > 65 detik; pompa pewangi > 240 detik; kontrol manual > 60 detik.
6. Semua relay mati saat boot, error, atau Stop. Watchdog 10 detik me-reset ESP32 bila program menggantung.

## Jaringan & catu daya

Hasil ukur di perangkat pengembang:

- Daya pancar WiFi diturunkan (`WIFI_TX_POWER`, bawaan **11 dBm**) dan bandwidth HT20. Pada daya penuh (19,5 dBm) perangkat
  kehilangan ±20–35 % paket karena suplai 5 V/USB drop saat memancar.
- **Perbaiki suplai dulu** (kabel tebal, catu cukup arus, catu relay terpisah, kapasitor 470–1000 µF) sebelum menaikkan daya pancar.

## Uji setelah merakit

1. Flash firmware ([Pemasangan](pemasangan.md)).
2. Dashboard → **Manual → Relay → Uji berurutan** *tanpa beban AC terpasang*: dengarkan klik relay berurutan
   kiri → kanan → isi → buang → spin → pewangi.
3. Bandingkan tiap relay dengan bebannya (satu per satu), lalu uji sensor level dengan mengangkat/menurunkan pelampung.
4. Jalankan Proses pendek dengan air sungguhan sambil diawasi.

## Simulasi (Wokwi)

`pio run -e wokwi` membangun firmware dengan `TIME_SCALE=60` (1 menit Proses = 1 detik) dan relay push-pull. Sensor air
disimulasikan dengan slide switch di GPIO27. Dashboard diteruskan ke `http://localhost:8180`. Hotspot ESP32 tidak bisa diuji
di Wokwi. `wokwi.toml` dan `diagram.json` sengaja berada di akar repo karena ekstensi Wokwi mencarinya di sana.
