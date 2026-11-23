# Architecture & Security

> **Other language:** [Bahasa Indonesia](../id/arsitektur.md)

## Overview

```
                         ┌────────────────────── ESP32 (WROOM) ──────────────────────┐
Phone/browser (home) HTTP►  Core 0: WiFi · Web server · Remote · Notifier             │
                         │        │ command layer + api_core (one dispatcher)         │
                         │        ▼                                                   │
                         │  Core 1: machine (state machine, millis(), no delay())     │──► relays ──► motors/valve/pump
                         │        ▲                                                   │◄── water level sensor
                         └────────┼───────────────────────────────────────────────────┘
                                  │ ONE outbound WSS connection
Phone/browser (anywhere) HTTPS► Cloudflare Worker + Durable Object ──► Telegram (notifications)
```

- **Core 1** runs the machine; **Core 0** handles networking. The machine never calls network functions: it only writes logs
  and a status *snapshot*.
- Programs keep running with WiFi down. Logs and notifications queue and flush once online.
- **One command layer** and **one API dispatcher** (`api_core`) serve the local web, the remote web and Serial/Console. Controls are identical everywhere.
- The dashboard *polls* (`/api/poll`, every 2 s; 10 s when the tab is hidden); the ESP32 runs no WebSocket server.

## Code map

| Location | Role |
|---|---|
| `firmware/mesin_cuci_otomatis/machine.*` | Program state machine (Core 1) |
| `tahap.*` | Stage catalog: kinds, ranges, defaults, validation (single source of truth, sent to the dashboard via `/api/tahap/meta`) |
| `proses_store.*` | Program storage (JSON on LittleFS, atomic writes) |
| `relay.*`, `water_sensor.*` | Relay outputs + interlocks; level sensor reading |
| `commands.*`, `api_core.*` | Shared commands and API dispatcher |
| `web_server.*`, `web_ui.h` | HTTP/API server; generated UI (do not edit) |
| `net_wifi.*`, `net_policy.*` | Multi-network WiFi, emergency hotspot, connectivity watchdog |
| `remote.*`, `remote_policy.*` | WSS client to the Worker + remote command allowlist |
| `notifier.*`, `logger.*` | Notification queue, log ring |
| `settings.*`, `backup.*`, `ota.*` | Settings (NVS), backup/restore, OTA flashing |
| `web/` | Dashboard source (HTML/CSS/JS) → `tools/build_web.py` → `web_ui.h` & `cloud/public/index.html` |
| `cloud/` | Cloudflare Worker (login, sessions, allowlist, relay, Telegram) |
| `tools/` | Build/deploy scripts and tests (`hosttest`, `uitest`) |

## Networking

- Multi-network WiFi (max 5). The emergency hotspot `MesinCuci-XXXX` starts **only** when every saved network fails.
- **Connectivity watchdog:** pings the gateway every 30 s; 3 failures → reconnect WiFi; 3 failed reconnects → restart WiFi.
  Active only after the gateway has answered a ping at least once.
- **Remote heartbeat:** `hb` every 30 s, the Worker replies `ack`; with no `ack` for 100 s the ESP32 reconnects.
- The Worker raises a "machine offline" alarm after `OFFLINE_MIN` (default 5) minutes of silence.

## Security model

| Layer | Protection |
|---|---|
| Two different passwords | Local and remote are separate: leaking one does not open the other. Min. 12 characters, set from the local dashboard. |
| Remote password | Sent by the ESP32 **once** over TLS; the Worker stores a **PBKDF2-SHA256** hash (100,000 iterations) in the Durable Object. Never stored on the ESP32 or in logs. |
| Sessions | HMAC-signed cookie, `HttpOnly`, `SameSite=Strict`, `Secure`/`__Host-` on HTTPS; `Origin` check (CSRF). The session epoch bumps when the password changes → everyone is signed out. |
| Local brute-force | Every 5 consecutive failures: lock 30 s, 1, 2, 5, 15 min, then 1 h; logged as WARN + notification. |
| Remote brute-force | Per-IP tiered lock 5 min → 24 h; >50 failures in 10 min from many IPs → all logins locked 5 min. |
| Remote allowlist | Firmware flash, WiFi, password change, remembered devices, console and restore are **local only**. Enforced **twice**: in the Worker (`cloud/src/allow.js`) and on the ESP32 (`remote_policy.cpp`), tested identical against `cloud/test/allow-vectors.txt`. |
| Secrets | `secrets.h`, `.env.local`, `.secrets.local.json` are git-ignored; the `pre-commit` hook rejects credentials. The Telegram token lives only in the Worker. |
| Machine | Layered relay interlocks, time failsafes, watchdog, all relays off at boot/error/Stop (see [Hardware](hardware.md#safety-layers)). |

Found a vulnerability? See [SECURITY.md](../../SECURITY.md).

## Data storage

| Data | Where |
|---|---|
| Programs | JSON files on LittleFS (`spiffs` partition), atomic writes |
| Settings, WiFi, remembered devices (max 5) | NVS |
| Log (100 entries), notification queue (30) | RAM (ring buffers) |
| Remote password hash, sessions | Durable Object on Cloudflare |

Deploy note: when Durable Object code changes, add a `renamed_classes` migration in `cloud/wrangler.toml` (old instances can
survive while the ESP32 connection stays open; currently `Relay → RelayV2`).
