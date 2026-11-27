# User Manual

How to operate the washing machine through the web dashboard. For wiring see [Hardware](hardware.md); for installing the
firmware see [Installation](installation.md).

> **Other language:** [Bahasa Indonesia](../id/manual-pengguna.md)
> Note: the dashboard UI itself is in Indonesian; Indonesian UI labels are shown in *italics* where they differ.

## Contents

1. [Core concepts](#1-core-concepts)
2. [Opening the dashboard](#2-opening-the-dashboard)
3. [Home](#3-home)
4. [Creating a Program](#4-creating-a-program)
5. [Running a Program](#5-running-a-program)
6. [Pending decisions](#6-pending-decisions)
7. [Manual menu](#7-manual-menu)
8. [Activity: Log, History, Console](#8-activity-log-history-console)
9. [System: settings](#9-system-settings)
10. [Remote control & Telegram notifications](#10-remote-control--telegram-notifications)
11. [Power outage & recovery](#11-power-outage--recovery)
12. [Troubleshooting](#12-troubleshooting)

---

## 1. Core concepts

| Term | Meaning |
|---|---|
| **Workspace** | **Cuci** (wash tub) or **Pengering** (spin tub), chosen with the buttons above Home/Programs/Manual. Both can run at the same time and each has its own 10 Programs. Dry Programs offer Spin, Wait, Ask action and Notify. |
| **Stage** (*Tahap*) | One machine step: fill, drain, agitate, rinse-soak, spin, fragrance… Each Stage has its own variables (duration, mode, retries). |
| **Program** (*Proses*) | A sequence of Stages you build and name yourself (max **10 Programs**, each up to **40 Stages**). |
| **Water level sensor** | A sensor at the top of the drum. It only knows **full** or **not full**. |
| **Decision** | When something fails, the machine **stops and waits for you**. It never continues on its own after a failure. |

**There are no built-in programs.** After a fresh install the list is empty; create your first one in the **Proses** tab.

## 2. Opening the dashboard

| From | Address |
|---|---|
| Home WiFi | `http://mesincuci.local` (or the IP address sent via Telegram when the machine comes online) |
| Emergency hotspot | Join WiFi `MesinCuci-XXXX` (password = `AP_PASSWORD`), open `http://192.168.4.1` |
| Anywhere (optional) | Your own Cloudflare Worker URL, e.g. `https://mesin-cuci-relay.<account>.workers.dev` |

The hotspot **only turns on when every saved WiFi network fails**. It turns off 2 minutes after home WiFi returns and no
client is connected.

Log in with the dashboard password (`WEB_PASSWORD` in `secrets.h`, changeable in **Sistem → Keamanan**). Tick
**Ingat perangkat ini** (remember this device) to stay signed in (up to 5 devices).

Five menus: bottom bar on phones, side rail on wide screens. Theme **Otomatis / Terang / Gelap** (auto / light / dark) is set
in **Sistem → Mesin**.

## 3. Home

*Beranda* shows the machine at a glance:

- **Status** with a progress ring and time remaining.
- **Decision card** (only appears when the machine is waiting for you; see [section 6](#6-pending-decisions)).
- **Stage rail** (*Rel Tahap*): tap the *Sisa Tahap* area (or *Lihat semua*) to see every Stage of the running Program (done,
  running, queued) with all its variables.
- **Water**, **relay** and **connection** status.
- **Start Program** and **Pause / Resume / Skip / Stop** controls.

## 4. Creating a Program

1. Open **Proses** → new Program, give it a name.
2. Add **Stages**, smallest first. Each Stage card holds its own variables.
3. Reorder by **dragging the handle in the card's corner** (only the handle drags, so scrolling on a phone does not move cards).
4. Save. Duration estimate and warnings (e.g. out-of-range values) appear automatically.

Other actions: **copy** a Program (saved immediately), **delete**, **export** / **import** a single Program as JSON.

> A Program that is **in use** (running, paused, awaiting a decision, waiting to start, or waiting for power-loss recovery)
> **cannot be edited or deleted**. Press **Stop** first.

### Stage catalog

| Stage | Purpose & main variables (defaults) |
|---|---|
| **Isi air** (Fill) | Mode `sensor` (until sensor says full), `persen` (25 / 50 / 75 % from calibration) or `waktu` (timed). Timeout `00:30:00` (sensor, percent); fill time 30 min (timed); 3 retries; 15 min wait between tries; **pre-check** if drum already full: `buang` (drain) / `jeda` (ask) / `pakai` (use it). |
| **Buang air** (Drain) | Drain time 5 min; **water-drop verification** 45 s; **valve re-pull** 2×; session retry limit 3×. A final "not full" check is always done. |
| **Putar mesin** (Agitate) | 15 min; pattern `normal` (7 s on / 4 pause), `gentle` (5.3 / 7), `hijab` (3 / 9). |
| **Soak** | Repeat 2×; spin time 1 min; soak time 4 min; pattern. |
| **Rendam** (Rest) | 4 min, motionless, all relays off. |
| **Spin** | 10 min. Dry Programs only; does not read the water sensor. |
| **Pewangi** (Fragrance) | 1 cup (multiples of 0.5; **0.5 cup = 10 s**; pump failsafe 240 s). |
| **Tunggu** (Wait) | Pause 00:00:10. |
| **Cek air** (Water check) | Water must be `penuh` (full) or `tidak penuh` (not full). |
| **Minta tindakan** (Ask action) | Message ≤ 40 chars. Machine stops until you choose Continue/Cancel; notification always sent. |
| **Notifikasi** (Notify) | Message ≤ 40 chars. Sent even in Silent mode, then continues. |

Valid ranges for every variable are served by the firmware (`/api/tahap/meta`), so the dashboard and the machine can never disagree.

## 5. Running a Program

From **Home** (or a Program card) choose how to start:

| Mode | Meaning |
|---|---|
| **Now** | Start immediately. |
| **Delay `HH:MM:SS`** | Start after a delay. |
| **Finish at `HH:MM`** | The machine counts back from the estimated duration. Sensor-based fill/drain can drift, so the finish time is an estimate. |

A Program can only start while the machine is **IDLE**. While running you can **Pause**, **Resume**, **Skip** a Stage, or
**Stop** (all relays off instantly).

## 6. Pending decisions

When a Fill/Drain/Check Stage fails, or an *Ask action* Stage is reached, the machine **stops and never continues by
itself**. The dashboard shows buttons for the case and sends a notification (repeated every 10 minutes).

| Case | Choices |
|---|---|
| Fill failed · Drain failed · Check failed | **Retry** · **Continue** (with a warning) · **Cancel** |
| Already full before filling (pre-check `jeda`) | **Drain then fill** · **Use existing water** · **Cancel** |
| Ask action | **Continue** · **Cancel** |

**How draining works:** the valve is pulled and the water must start dropping from the full position within the
*verification* time. If not, the valve is released for 3 s and pulled again (*valve re-pull*). Once the drop is confirmed the
valve stays pulled for the drain time, then a check runs; if still full the session repeats. If all sessions fail, the machine
asks you.

## 7. Manual menu

- **Run a single Stage** using the same form as the Program editor.
- **Manual drain**: pulls the valve for a set time **without the sensor**. Emergency route if the sensor is broken.
- **Relay panel**: ON/OFF per relay (IDLE only, auto-off after 60 s) and **Sequential test**
  (left → right → fill → drain → spin → fragrance). Useful after wiring.
- **Water calibration** (Sistem → Mesin) measures fill time for `persen` mode.
- **Water sensor confirmation** (Sistem → Mesin, card *Pengaturan air*): confirmation time 1-60 s (default 10). The sensor is only trusted after it has stayed wet, or stayed dry, for that long, so splashes are ignored. While filling, the valve stays open for this long after water reaches the sensor, so do not set it too high. It can only be changed while the wash tub is idle.

## 8. Activity: Log, History, Console

- **Log**: last 100 events (INFO/WARN/ERROR); queued while WiFi is down and sent when online.
- **History & statistics**: Programs that finished, failed or were cancelled.
- **Console**: web serial monitor accepting [commands](reference.md#commands). **Local web only.**

## 9. System: settings

| Panel | Contents |
|---|---|
| **Koneksi** | WiFi list (max 5, priority order), remote control on/off. |
| **Keamanan** | **Local web password** and **Remote password** (each ≥ 12 chars, typed twice); remembered devices. Changing the web password signs out every device. |
| **Mesin** | Notification mode (`lengkap` full / `ringkas` brief / `senyap` silent), water settings (sensor confirmation and calibration), theme. |
| **Pemeliharaan** | Device info, **firmware flash** via web, **backup & restore**, restart, reset all. |

### Backup & restore

- **Download backup**: one JSON file with settings and the WiFi list. WiFi passwords are included only if ticked (store the file safely). Programs are exported one by one from the Proses menu.
- **Restore**: the file is validated first (dry run) and a summary is shown for confirmation. Restore is atomic: if any part is invalid, nothing changes.
- Local web only.

### Updating firmware

**Sistem → Pemeliharaan → Firmware** → choose a `.bin` → *Flash*. Only while IDLE. The previous firmware stays intact in the
backup partition; use **Kembali ke versi sebelumnya** (roll back) if the new one misbehaves.

## 10. Remote control & Telegram notifications

- Once the Worker is deployed ([Installation](installation.md#4-remote-control-optional)), open its URL from anywhere and sign
  in with the **remote password** (intentionally different from the local one).
- Same UI as the local dashboard, but sensitive features are **hidden and rejected**: firmware flash, WiFi settings, password
  changes, remembered devices, console and restore.
- Telegram notifications come from the Worker: Program finished/failed, decision waiting, login lockout, password changed, and
  a **"machine offline"** alarm. Telegram is notifications only; **there are no chat commands**.
- Notification mode is set in Sistem → Mesin. *Notify* and *Ask action* Stages always send.

## 11. Power outage & recovery

If power fails mid-Program, the machine does **not** resume on its own. On boot the dashboard asks **Continue** or **Cancel**.
Recovery is cancelled automatically if the Program was modified. All relays are off at boot.

## 12. Troubleshooting

| Symptom | Likely cause & fix |
|---|---|
| `mesincuci.local` doesn't open | mDNS not supported by network/OS. Use the IP from Telegram or your router. Older Windows needs Bonjour. |
| Can't log in, countdown shown | Locked after 5 wrong passwords. Wait it out (30 s, 1, 2, 5, 15 min, then 1 h). |
| Hotspot `MesinCuci-XXXX` missing | Normal while home WiFi is connected. It only starts when all saved WiFi fail. |
| Fill always "fails" | Check the level sensor (GPIO27, `HIGH` = water) and source pin 14; raise the timeout; check the tap. |
| Water doesn't drop when draining | Check the valve-pull motor and hose; the sensor may be faulty, use **Manual drain**. |
| All relays off and locked | The safety guard saw a dangerous combination (e.g. left+right). Inspect wiring, then **restart** the machine. |
| Web feels laggy / drops | The 5 V/USB supply sags while WiFi transmits. Use a stronger supply, thick cable, 470–1000 µF capacitor, separate relay supply. |
| Telegram says "offline" | ESP32 lost WiFi/internet for >5 min. Check the router; a running Program continues regardless. |
| Web flash refused | The machine must be IDLE. Stop the Program first. |

Still stuck? Open an [issue](https://github.com/rhmt25/Mesin_Cuci_Otomatis/issues) with the firmware version
(Sistem → Pemeliharaan → Info) and a Log excerpt. **Never** include passwords or tokens.
