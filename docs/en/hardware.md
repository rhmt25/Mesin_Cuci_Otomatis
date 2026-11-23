# Hardware

> **Other language:** [Bahasa Indonesia](../id/perangkat-keras.md)

> ⚠️ **Safety warning.** This project controls mains-powered (AC) appliances and water. Mains wiring can cause electric shock
> and fire. Only do it if you understand electrical work: disconnect power before touching wires, fit a fuse/MCB and an
> earth-leakage breaker (ELCB/RCD), and insulate every joint. This project is provided **without warranty** (see
> [LICENSE](../../LICENSE)); you are responsible for your installation.

## Parts

| Part | Notes |
|---|---|
| Microcontroller | ESP32 **WROOM** module (dev board, e.g. RYMCU ESP32-DevKitC) |
| Relay module | 6 channels, **Normally Open, trigger LOW**, 5 V |
| Water level sensor | Level sensor/switch (dry contact) at the top of the drum |
| Loads | Wash motor (left/right), water fill valve, drain-valve pull motor, spin motor, fragrance pump |
| Power | 5 V with enough current for ESP32 + relays (separate relay supply recommended), 470–1000 µF capacitor |

## Pin map (ESP32 WROOM)

Defined in [`config.h`](../../firmware/mesin_cuci_otomatis/config.h).

| Function | GPIO | Notes |
|---|---|---|
| Motor rotate left | **25** | formerly GPIO5 (strapping pin); move the relay IN wire |
| Motor rotate right | 18 | |
| Fill valve | 17 | |
| Drain-valve pull motor | 16 | |
| Spin motor | 4 | |
| Fragrance pump | 13 | |
| Water level sensor | 27 | `INPUT_PULLUP`; `HIGH` = water present |
| Sensor/button source | 14 | always `HIGH` |

The relay board is **active-LOW** and outputs use **open-drain** mode (`RELAY_ACTIVE_LOW`, `RELAY_OPEN_DRAIN` in `config.h`):
`HIGH` means the pin floats, which is safe for 5 V relays driven from 3.3 V logic.

A simulation diagram is in [`diagram.json`](../../diagram.json) (Wokwi).

## Safety layers

Left and right motors **never** run together:

1. Interlock in `relaySet`.
2. The opposite motor pin is physically forced off before a motor starts.
3. Minimum 500 ms direction-change gap.
4. `relayGuard()` every tick: if left+right, fill+drain, or drain+motor is detected, **all relays go off and stay locked until
   restart**.
5. Wash motor forced off after 65 s continuous; fragrance pump after 240 s; manual control after 60 s.
6. All relays off at boot, on error, and on Stop. A 10 s watchdog resets the ESP32 if the program hangs.

## Network & power

Measured on the developer's device:

- WiFi TX power is reduced (`WIFI_TX_POWER`, default **11 dBm**) with HT20 bandwidth. At full power (19.5 dBm) the unit lost
  ±20–35 % of packets because the 5 V/USB supply sagged while transmitting.
- **Fix the supply first** (thick cable, adequate supply, separate relay supply, 470–1000 µF capacitor) before raising TX power.

## Testing after assembly

1. Flash the firmware ([Installation](installation.md)).
2. Dashboard → **Manual → Relay → Sequential test** *with no AC load connected*: listen for relay clicks in order
   left → right → fill → drain → spin → fragrance.
3. Match each relay to its load one at a time, then test the level sensor by raising/lowering the float.
4. Run a short Program with real water while supervising.

## Simulation (Wokwi)

`pio run -e wokwi` builds with `TIME_SCALE=60` (1 Program minute = 1 second) and push-pull relays. The water sensor is
simulated with a slide switch on GPIO27. The dashboard is forwarded to `http://localhost:8180`. The ESP32 hotspot can't be
tested in Wokwi. `wokwi.toml` and `diagram.json` intentionally stay at the repo root because the Wokwi extension looks for
them there.
