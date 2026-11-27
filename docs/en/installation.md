# Installation

> **Other language:** [Bahasa Indonesia](../id/pemasangan.md)

Order: **(1)** prepare credentials → **(2)** flash firmware → **(3)** test the local dashboard → **(4)** remote control (optional).

## 1. Prepare `secrets.h`

```
copy firmware\mesin_cuci_otomatis\secrets.example.h firmware\mesin_cuci_otomatis\secrets.h
```

Fill in `secrets.h` (this file is **never committed**, it is in `.gitignore`):

| Macro | Purpose |
|---|---|
| `WIFI_SSID` / `WIFI_PASSWORD` | Primary home WiFi |
| `WIFI_SSID_2` / `WIFI_PASSWORD_2` | Initial backup WiFi (optional; can also be added in the dashboard) |
| `AP_PASSWORD` | Emergency hotspot password, **at least 8 characters** |
| `WEB_PASSWORD` | Local dashboard password (changeable from the dashboard) |
| `REMOTE_URL` / `REMOTE_DEVICE_TOKEN` | `wss://…/device` address and device token for remote control. Leave empty to disable |

The Telegram bot token is **not** stored on the ESP32; it is a Worker secret.

## 2. Flash the firmware

### Option A: PlatformIO (recommended)

```
pio run -e rymcu-esp32-devkitc -t upload      # over USB
pio device monitor                            # serial 115200
```

The `.bin` is at `.pio/build/rymcu-esp32-devkitc/firmware.bin`. Libraries download automatically.

### Option B: Arduino IDE

1. Open `firmware/mesin_cuci_otomatis/mesin_cuci_otomatis.ino`.
2. Board: **ESP32 Dev Module**. **Tools → Partition Scheme → "Minimal SPIFFS (1.9MB APP with OTA)"** (required).
3. Library Manager: `ArduinoJson` (v7), `WebSockets` (Links2004), `AsyncTCP` and `ESPAsyncWebServer` (both the **mathieucarbou** forks; install from GitHub if needed).
4. Upload over USB. *Sketch → Export Compiled Binary* produces a `.bin` for web flashing.

### Option C: over the web (after the first firmware is installed)

Dashboard → **Sistem → Pemeliharaan → Firmware** → choose `.bin` → *Flash*. IDLE only.

### Option D: one-click deploy (Windows)

Updates the ESP32 over WiFi (OTA) **and** the Worker in one go.

1. Copy `.env.example` to `.env.local` (git-ignored); set `DEPLOY_ESP_PASSWORD` (local dashboard password), `DEPLOY_ESP_MAC` (the washer ESP32's MAC address; deploy aborts on a mismatch) and, if needed, `DEPLOY_ESP_HOST`.
2. Double-click `tools\deploy.cmd` (or `powershell -File tools\deploy.ps1`).
3. The script builds the web UI and firmware, uploads the `.bin` to `mesincuci.local` (only when the machine is IDLE), waits for
   the ESP32 to come back, then tests and deploys the Worker.

Options: `-SkipEsp`, `-SkipWorker`.

## 3. Test the local dashboard

1. Open the Serial Monitor; the ESP32 prints its IP address once it joins WiFi.
2. Open `http://mesincuci.local` (or that IP) and log in with `WEB_PASSWORD`.
3. Follow the [User Manual](user-manual.md), starting with the relay test in the **Manual** menu.

## 4. Remote control (optional)

Uses a Cloudflare Worker + Durable Object on the **Free** plan (no credit card). The ESP32 opens just **one outbound WSS
connection**; no port forwarding or router changes.

Summary (full details in [`cloud/README.md`](../../cloud/README.md), currently Indonesian):

1. Create a Cloudflare account, **enable 2FA**, pick a `*.workers.dev` subdomain.
2. `cd cloud && npm install && npx wrangler login`.
3. Create secrets and push them to Cloudflare:
   ```powershell
   $env:REMOTE_PASSWORD="a-long-random-remote-password"
   $env:TELEGRAM_BOT_TOKEN="token-from-BotFather"
   $env:TELEGRAM_CHAT_ID="your-chat-id"
   node tools/gen-secrets.mjs
   node tools/put-secrets.mjs
   ```
4. `npx wrangler deploy`.
5. Copy `DEVICE_TOKEN` from `cloud/.secrets.local.json` into `REMOTE_DEVICE_TOKEN`, and the Worker address into `REMOTE_URL`, then re-flash.
6. Once the machine is online, set the remote password directly from the local dashboard (**Sistem → Keamanan**).

### Telegram bot

Create a bot with BotFather, take its token, and find your `chat id`. Provide them through the `TELEGRAM_BOT_TOKEN` and
`TELEGRAM_CHAT_ID` environment variables; `put-secrets.mjs` uploads them to the Worker as secrets. The token never needs to be
on the ESP32. The bot only **sends** notifications; it accepts no commands.

### Updating the Worker UI

When `web/` changes: `python tools/build_web.py --remote`, then `npx wrangler deploy` inside `cloud`.
