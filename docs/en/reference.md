# Reference

> **Other language:** [Bahasa Indonesia](../id/referensi.md)

## Commands

Available from the web Console (local), USB Serial at 115200 baud, and (the permitted subset) remotely. One dispatcher serves all.
Command names are Indonesian, as implemented in firmware.

| Command | Purpose |
|---|---|
| `proses` | List Programs |
| `proses <number> [tunda HH:MM:SS \| selesai HH:MM]` | Run a Program (`tunda` = delay, `selesai` = finish at) |
| `pilih <ulang\|lanjut\|batal\|buang\|pakai>` | Answer a pending decision (retry / continue / cancel / drain / use) |
| `stop` · `jeda` · `lanjutkan` · `lewati` · `batalkan` | Control the running Program (stop / pause / resume / skip / cancel) |
| `cek_air` | Read the level sensor |
| `status` · `pengaturan` · `riwayat` · `statistik` · `info` · `bantuan` | Information (status / settings / history / statistics / info / help) |
| `relay <kiri\|kanan\|isi\|buang\|spin\|pewangi> <on\|off>` · `relay semua off` | Direct relay control (only while the owning workspace is IDLE; spin = dryer) |
| `kalibrasi_relay [seconds]` · `relay_test <name>` | Sequential / single relay test |
| `buang_manual <minutes>` | Pull the drain valve without the sensor |
| `notif lengkap\|ringkas\|senyap` | Notification mode (full / brief / silent) |
| `remote [on\|off]` | Remote control |
| `kalibrasi_air` | Calibrate fill time for percent mode |
| `reset_semua` · `restart` | Reset settings / reboot |
| `wifi_list` · `wifi_tambah` · `wifi_hapus` · `wifi_urutan` | WiFi (local only) |
| `heap` · `tasks` · `log` · `serial debug on\|off` | Diagnostics (console only) |

Water/agitate/fragrance variables are not commands: they are set per Stage in the dashboard.

**Allowed remotely:** `proses pilih stop jeda lanjutkan lewati batalkan relay relay_test kalibrasi_relay buang_manual cek_air
status menu pengaturan riwayat statistik info bantuan notif kalibrasi_air reset_semua restart remote`. The authoritative list
is in `cloud/src/allow.js` and `remote_policy.cpp`.

## API endpoints (ESP32)

All require a login session (cookie) except `/api/login`.

| Endpoint | Purpose |
|---|---|
| `POST /api/login` · `/api/logout` | Sign in / out |
| `GET /api/poll` | Compact status + new log lines (dashboard polling) |
| `GET /api/status` · `/api/info` · `/api/job` | Machine status, device info, running Program (Stage rail) |
| `GET/POST /api/proses` · `/api/proses/export` · `/api/proses/import` | Program CRUD |
| `GET /api/tahap/meta` | Stage catalog (kinds, ranges, defaults) |
| `POST /api/run` · `/api/decide` · `/api/cmd` | Run, decide, command |
| `ws=cuci\|pengering` | Query parameter (or `ws` key in the JSON body) for `/api/job`, `/api/proses*`, `/api/run`, `/api/decide`; empty = wash. Text commands: `proses pengering 1`, `jeda pengering`, `stop pengering`; bare `stop` stops both |
| `GET/POST /api/settings` · `/api/wifi` · `/api/password` · `/api/devices` | Settings (some local only) |
| `GET /api/logs` · `/api/history` · `/api/console` | Log, history, console |
| `GET /api/backup` · `POST /api/restore[?dry=1]` | Backup / restore |
| `POST /api/update` · `/api/rollback` · `GET /api/update/info` | OTA flash & roll back |

## Backup format

One JSON file, **version 3**: settings + WiFi list. Size limit 12 KB (worst case < 1 KB). WiFi passwords only when ticked.
Calibration data and one-shot schedules are not included. Restore is atomic; out-of-range values are clamped.

## Build configuration (`config.h`)

| Macro | Default | Meaning |
|---|---|---|
| `FW_VERSION` | `2.0.0` | Firmware version |
| `DEVICE_HOSTNAME` | `mesincuci` | mDNS name |
| `TIME_SCALE` | `1` | Time multiplier (Wokwi: 60) |
| `RELAY_ACTIVE_LOW` / `RELAY_OPEN_DRAIN` | `1` / `1` | Relay polarity / pin mode |
| `WIFI_TX_POWER` | 11 dBm | WiFi transmit power |
| `MOTOR_MAX_ON_MS` | 65000 | Wash-motor failsafe |
| `PERFUME_MAX_ON_MS` | 240000 | Fragrance-pump failsafe |
| `MANUAL_MAX_ON_MS` | 60000 | Manual relay control limit |
| `WATER_STABLE_MS` | 10000 | Sensor must be stable this long before a change counts |
| `MAX_WIFI_NETWORKS` | 5 | Saved WiFi networks |
| `NTP_TZ` | `WIB-7` | Time zone |

## Worker variables (`cloud/wrangler.toml`)

`OFFLINE_MIN` (default 5): minutes of silence before the offline alarm. `RPC_TIMEOUT_MS` (default 8000).
