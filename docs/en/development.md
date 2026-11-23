# Development Guide

> **Other language:** [Bahasa Indonesia](../id/pengembangan.md)

## Repository layout

```
.
├── firmware/mesin_cuci_otomatis/   Arduino/PlatformIO sketch (single source of truth)
├── web/                            Dashboard source (index.html, style.css, app.js)
├── cloud/                          Cloudflare Worker + Durable Object (remote relay)
│   ├── src/  test/  tools/  public/
├── tools/
│   ├── build_web.py                web/ → web_ui.h (+ cloud/public/index.html)
│   ├── deploy.ps1 / deploy.cmd     One-click deploy (OTA + Worker)
│   ├── hosttest/                   Machine logic tests on a PC (g++)
│   ├── uitest/                     UI tests (jsdom)
│   └── hooks/pre-commit            Credential blocker
├── docs/                           Documentation (id/ and en/)
├── diagram.json, wokwi.toml        Wokwi simulation (must stay at repo root)
├── platformio.ini                  Build configuration
├── .github/                        CI, issue/PR templates
└── LICENSE                         GNU GPL v3
```

## Setting up

| Need | For |
|---|---|
| [PlatformIO](https://platformio.org/) | Building/flashing firmware |
| Python 3 | `tools/build_web.py` |
| `g++` (≥ C++17) | PC logic tests |
| Node.js ≥ 18 | Worker, UI tests |

Enable the credential-leak hook (once per clone):

```
git config core.hooksPath tools/hooks
```

## Workflow

1. **Changing the dashboard?** Edit `web/`, then `python tools/build_web.py --remote`. Never hand-edit `web_ui.h` or `cloud/public/index.html` (generated; both are committed so the firmware builds without Python).
2. **Changing the Stage catalog?** Edit `tahap.cpp`; the dashboard picks it up automatically. Refresh the UI fixture: `test_tahap --meta > tools/uitest/meta.json`.
3. **Changing the remote command list?** Change `cloud/src/allow.js` **and** `remote_policy.cpp`, then update `cloud/test/allow-vectors.txt`. Both sides must stay identical.
4. Run all tests (below), then commit.

## Running tests

| Test | Command | Covers |
|---|---|---|
| Machine logic | `sh tools/hosttest/run.sh` | State machine with a fake clock + water-tank model (204 checks) and Stage catalog + Program storage (181) |
| UI | `cd tools/uitest && npm install && npm test` | Program/Stage editor, decision card, Manual, drag & drop, relay lock, devices |
| Worker | `cd cloud && npm install && npm test` | Local `wrangler dev` + ESP32 simulator + fake Telegram (~1 min), no Cloudflare account needed |
| Firmware build | `pio run -e rymcu-esp32-devkitc` | Compilation |

`hosttest` needs ArduinoJson in `.pio/libdeps/...`; run `pio run` once first. The same tests run in CI (`.github/workflows/ci.yml`).

## Code style

- Comments and UI strings are in **Indonesian**; identifiers are mixed, following existing code. Match the surrounding file.
- Machine: no `delay()`, no network calls from Core 1.
- Keep pure logic (policy, backup, catalog) separate from I/O so it can be tested on a PC.
- Every source file starts with `SPDX-License-Identifier: GPL-3.0-or-later` and `Copyright (C) 2026 Rahmat Hidayat`. New files must use the same header.
- Editor formatting: see `.editorconfig`.

## Commit messages

`<type>: <summary>` with types `feat`, `fix`, `style`, `refactor`, `docs`, `test`, `chore`. Example: `fix: Stage card drag only via handle`.

## Releasing

1. Bump `FW_VERSION` in `config.h` and update the [CHANGELOG](../../CHANGELOG.md).
2. Run all tests.
3. `pio run`, attach `firmware.bin` to the GitHub release.
4. Tag `vX.Y.Z`.

See also [CONTRIBUTING.md](../../CONTRIBUTING.md).
