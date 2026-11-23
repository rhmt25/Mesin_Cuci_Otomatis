# Mesin Cuci Otomatis · Automatic Washing Machine

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![CI](https://github.com/rhmt25/Mesin_Cuci_Otomatis/actions/workflows/ci.yml/badge.svg)](https://github.com/rhmt25/Mesin_Cuci_Otomatis/actions/workflows/ci.yml)
![Platform: ESP32](https://img.shields.io/badge/platform-ESP32-informational)

> 🇮🇩 **Bahasa Indonesia:** see [README.id.md](README.id.md)

Open-source ESP32 firmware that turns a twin-tub washing machine into a smart, safe, programmable one. Control it from a
**local web dashboard** (home network or the ESP32's own emergency hotspot, no internet needed), optionally from anywhere
through a **Cloudflare Worker**, and get **Telegram notifications**.

## Features

- **Build your own Programs** from Stages (fill, drain, agitate, soak, rest, spin, fragrance, wait, water check, ask-for-action, notify), each with its own variables. No hard-coded programs.
- **Safe by design:** all relays off at boot/error/Stop; layered interlocks; time failsafes; watchdog. On any failure the machine **stops and asks you**, never continuing on its own.
- **Works offline:** the state machine runs on its own core (`millis()`-based, no `delay()`); Programs continue when WiFi drops.
- **One command layer** shared by the local web, remote web and Serial console, so controls are identical.
- **Remote access without opening ports:** the ESP32 makes a single outbound WSS connection to a Cloudflare Worker (Free plan).
- **Hardened login:** two separate passwords, brute-force lockouts, signed sessions, remote allowlist enforced on both ends.
- **Mobile-first dashboard** (light/dark), OTA firmware update with rollback, backup & restore, power-loss recovery.
- **Well tested:** 385 host-side logic checks, UI tests, and a Worker end-to-end suite, all runnable without hardware.

## Quick start

```bash
git clone https://github.com/rhmt25/Mesin_Cuci_Otomatis.git
cd Mesin_Cuci_Otomatis
cp firmware/mesin_cuci_otomatis/secrets.example.h firmware/mesin_cuci_otomatis/secrets.h   # then edit it
pio run -e rymcu-esp32-devkitc -t upload                                                   # flash over USB
```

Then open `http://mesincuci.local` and follow the [User Manual](docs/en/user-manual.md).

## Documentation

| | English | Bahasa Indonesia |
|---|---|---|
| User manual | [docs/en/user-manual.md](docs/en/user-manual.md) | [docs/id/manual-pengguna.md](docs/id/manual-pengguna.md) |
| Hardware & wiring | [docs/en/hardware.md](docs/en/hardware.md) | [docs/id/perangkat-keras.md](docs/id/perangkat-keras.md) |
| Installation | [docs/en/installation.md](docs/en/installation.md) | [docs/id/pemasangan.md](docs/id/pemasangan.md) |
| Architecture & security | [docs/en/architecture.md](docs/en/architecture.md) | [docs/id/arsitektur.md](docs/id/arsitektur.md) |
| Reference (commands, API) | [docs/en/reference.md](docs/en/reference.md) | [docs/id/referensi.md](docs/id/referensi.md) |
| Development | [docs/en/development.md](docs/en/development.md) | [docs/id/pengembangan.md](docs/id/pengembangan.md) |

Also: [Remote relay (Worker)](cloud/README.md) · [Contributing](CONTRIBUTING.md) · [Security policy](SECURITY.md) · [Changelog](CHANGELOG.md)

## Repository layout

```
firmware/mesin_cuci_otomatis/   ESP32 firmware (Arduino IDE + PlatformIO)
web/                            Dashboard source, built into the firmware and the Worker
cloud/                          Cloudflare Worker + Durable Object (remote access, Telegram)
tools/                          Build/deploy scripts, host tests, UI tests, git hook
docs/                           Documentation (en/, id/)
diagram.json, wokwi.toml        Wokwi simulation
```

## Hardware at a glance

ESP32 WROOM + 6-channel active-LOW relay board + one water-level sensor. Pin map and safety notes:
[docs/en/hardware.md](docs/en/hardware.md).

> ⚠️ This project switches mains-powered appliances. Mains wiring is dangerous; build it only if you are qualified, and at your own risk.

## Contributing

Issues and pull requests are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) and the [Code of Conduct](CODE_OF_CONDUCT.md) first.
To report a vulnerability, see [SECURITY.md](SECURITY.md).

## License

Copyright © 2026 Rahmat Hidayat. Licensed under the **GNU General Public License v3.0 or later**, see [LICENSE](LICENSE).
This program is distributed in the hope that it will be useful, but **without any warranty**.
