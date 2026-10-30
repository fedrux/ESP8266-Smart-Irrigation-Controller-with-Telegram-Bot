# ESP8266 Smart Irrigation Controller with Telegram Bot

**Version 8.1.1** adds a local irrigation dashboard and the shared **ESP Device
Core 1.1.1** administration interface to the historical v8 controller. Irrigation
timing, relay pins, scheduling and Telegram commands retain the v8 behavior.

The complete firmware compiles for **NodeMCU ESP8266** with Arduino ESP8266 core
**3.1.2**. Nine source/asset contracts and five simulated UI scenarios pass.
**The new integration has not been installed or physically validated on the
irrigation controller.** Compilation does not establish relay, migration, radio
or OTA behavior on real hardware.

## Firmware layout

- `firmware/nodemcu_spruzzini/`: current application and vendored shared core.
- `web/`: dashboard HTML, CSS and JavaScript, embedded as compressed flash assets.
- `tools/`: portable asset builder, compiler wrapper and EDC packager.
- `tests/`: contracts against the historical source and simulated UI scenarios.
- `controller_irrigazione_anonimo.ino`: historical v8 reference, with a remaining
  personal notification recipient replaced by an example value.

The shared core snapshot is copied unchanged from
[ESP Device Core](https://github.com/fedrux/esp-device-core). Its fingerprints and
MIT notice are in [CORE-PROVENANCE.json](docs/CORE-PROVENANCE.json) and
[ESP-DEVICE-CORE-LICENSE](docs/ESP-DEVICE-CORE-LICENSE). This is a vendored 1.1.1
snapshot; upstream `main` may still contain the earlier implementation. Do not
replace the vendored files independently without validating storage and OTA
compatibility.

## Features and wiring

| Function | Behavior |
|---|---|
| Five relay outputs | D0, D1, D2, D3, D4; active LOW, off HIGH |
| Physical button | D8, active HIGH; starts the configured full cycle |
| Status LED | D6; toggles every 250 ms (500 ms full blink cycle) |
| Full cycle | Five sectors in order, equal duration per sector |
| Single sector | One selected sector with a 1–120 minute duration |
| Daily program | Start hour 0–23; stores an hourly run reference to prevent repetition |
| Weather smart mode | Skips automatic irrigation after a detected Rain/Drizzle observation |
| Dashboard | Protected status, outputs, remaining time, start/stop and settings |
| Administration | Primary/secondary Wi-Fi, recovery AP, diagnostics, restart and `.edc` OTA |

D3/GPIO0, D4/GPIO2 and D8/GPIO15 also affect ESP8266 boot mode. Keep the existing
v8 wiring and verify the board boots correctly with the actual relay/button
circuit. The built-in LED shares D4 with a relay and is not used as a Wi-Fi LED.

The default device name is **`nodemcu-spruzzini`**, with HTTP discovery at
**`http://nodemcu-spruzzini.local/`** where the LAN/client supports mDNS. The
public configuration uses **DHCP**. To use a static network, set `USE_DHCP`
to `false` and replace the example addresses in your private configuration
header. No personal installation addresses are included. Saved core network settings take precedence over build defaults;
subsequent changes use `/admin`. No router configuration is performed by this
firmware.

## Telegram commands

The current implementation uses the original Italian commands:

| Command | Action |
|---|---|
| `/start` | Help |
| `/impostazioni` | Guided daily program and weather settings |
| `/stato` | Program and irrigation state |
| `/ciclo` | Start a full cycle with a chosen duration |
| `/settore` | Choose a sector and duration |
| `/stop` | Request cycle stop or stop the active single sector |
| `/reset` | Reset irrigation settings |
| `/meteo` | Current OpenWeatherMap response as JSON |

Only chat IDs configured in `chatConsentite` are authorized. Credentials,
weather location and chat IDs are supplied through a private header. Default
notifications use the first configured chat.

## Configure and build

Install Arduino CLI, Python 3, Node.js and the following Arduino dependencies:

```sh
arduino-cli core update-index --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core install esp8266:esp8266@3.1.2 --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli lib install "UniversalTelegramBot@1.3.0" "ArduinoJson@7.4.3"
```

1. Copy `firmware/nodemcu_spruzzini/config_privata.example.h` to
   `firmware/nodemcu_spruzzini/config_privata.h`.
2. Replace all example Wi-Fi, Telegram, weather, administrator, recovery and chat
   values. The recovery password must contain 8–63 characters. Keep real values
   in the ignored private header.
3. Configure DHCP or your own static network values in the private header.
4. From the repository root, run:

```sh
python tools/compile.py
```

On Windows, `py -3 tools/compile.py` also works. If Arduino CLI is not on PATH,
pass `--arduino-cli "C:/path/to/arduino-cli.exe"`. The Arduino IDE installation
may already include it. The script rebuilds web assets, runs all 14 checks,
compiles the full sketch and verifies the generated EDC package.

Private outputs are written to `output/nodemcu-spruzzini-8.1.1.bin` and `.edc`.
**Compiled images embed the configuration; do not publish your configured
images or private header.** No personal preconfigured firmware is provided by
this public repository. The original private installation package is separate.

## Installation and migration

Identify the actual irrigation board before writing flash. Back up and verify
its complete flash, and compare flash size and the previous partition layout.
The build targets a 4 MB NodeMCU with the standard 2 MB filesystem layout.
Do not erase all flash or upload a filesystem image when preserving v8 settings.

The first migration from historical v8 uses **USB**: v8 has no shared-core EDC
updater. Upload the application `.bin` at address **0x0** with the appropriate
ESP8266 uploader. Subsequent updates use `/admin` with a package for application
**`nodemcu-spruzzini`**, target **`esp8266`**. Packages for another application or
board are rejected. Do not load this firmware onto an unrelated device.

The core owns EEPROM bytes 0–1023. The application validates the old v8 24-byte
record before core initialization and imports it into LittleFS. Application
records include a version and CRC; writes use a temporary file, readback and
rename. Outputs are initialized off before migration. Only an entirely erased
filesystem partition may be formatted automatically; populated or unreadable
partitions are left intact. Invalid records do not configure automatic starts.

## Web interface

- `/`: protected irrigation dashboard.
- `/admin`: shared core administration.
- `GET /api/spruzzini/status`: authenticated application state and CSRF token.
- `POST /api/spruzzini/command`: authenticated `cycle`, `sector`, `stop`,
  `settings`, `reset`, with `X-CSRF-Token`.

The web interface rejects overlapping starts and settings changes while
irrigation is active. Core restart, network trial and firmware upload are
rejected before upload parsing while an irrigation is active. This application
guard does not modify the shared core. Administration uses local HTTP Basic
Authentication; keep it on the intended LAN.

## Historical behavior retained

- The saved rain-hours value is 1–24, but v8's actual condition uses a separate
  **fixed six-hour** variable. The dashboard displays this distinction.
- The clock retains v8's **fixed UTC+2**, without seasonal clock adjustment.
- Weather comes from the **current weather** endpoint, not a forecast. Only
  Rain/Drizzle observations update a RAM timestamp; it is lost on reboot.
  Weather errors return `false`, as in v8.
- The periodic branch has a nominal one-second interval, despite an old source
  comment saying 60 seconds. Manual cycles bypass weather smart mode.
- The button has no true debounce or edge detection. Holding it can repeatedly
  restart the sequence. Existing Telegram overlap behavior is also unchanged.
- Telegram and weather requests are synchronous and may delay timer processing.
  A full-cycle stop is a request handled by that loop, not a guarantee of an
  immediate hardware stop during network calls.

These issues are documented rather than silently corrected in a core/UI
migration. The new web controls prevent overlap on their own entry point.

## Verification

```sh
python -m unittest discover -s tests -v
node --test tests/ui.test.js
```

Contracts compare the retained functions and full loop with the historical
source, allowing only network infrastructure and private-recipient extraction.
Core fingerprints and compressed assets are checked. UI scenarios use a
simulated DOM; they do not contact or operate valves.

Physical validation is still required for EEPROM import, relay/button behavior,
Wi-Fi recovery, mDNS, actual browser control, persistence and OTA. Test with
loads disconnected before returning a migrated controller to service.

MIT. See [LICENSE](LICENSE).
