# Installation

This document covers setting up a **new physical site**: flashing both ESP32 boards and provisioning them against a Supabase project. For deploying *code changes* to an already-running dashboard/backend, see [`DEPLOYMENT.md`](DEPLOYMENT.md) instead.

## Prerequisites

- Arduino IDE (or `arduino-cli`) with ESP32 board support installed.
- A configured Supabase project matching [`DATABASE.md`](DATABASE.md)'s schema, with all 13 Edge Functions deployed (see [`DEPLOYMENT.md`](DEPLOYMENT.md)) and the `attendance-photos` storage bucket created per `storage_policies.sql`.
- Hardware: one ESP32-WROOM-32 dev board, one ESP32-CAM (AI-Thinker or compatible with `esp_camera.h` support), one MFRC522 RFID reader module, an FTDI/USB-serial adapter for flashing the ESP32-CAM (it has no onboard USB). See [`HARDWARE.md`](HARDWARE.md) for the full pinout and wiring.

## Firmware libraries required

Verified against the actual `#include` lines in the firmware source — install these via Arduino Library Manager before compiling:

| Library | Used by | Notes |
|---|---|---|
| `MFRC522` (miguelbalboa) | `wroom_brain.ino` | RFID reader driver |
| `ArduinoJson` (Benoit Blanchon) | `wroom_brain.ino` | JSON construction/parsing for all backend calls |
| `WiFi`, `HTTPClient`, `SPI`, `SPIFFS`, `Preferences`, `WebServer`, `DNSServer` | `wroom_brain.ino`, `provision_portal.h` | Bundled with the ESP32 Arduino core — no separate install |
| `esp_camera`, `SD_MMC`, `FS`, `esp_sleep`, `esp_task_wdt`, `mbedtls/base64` | `esp32cam_slave.ino` | Bundled with the ESP32 Arduino core (camera driver ships with ESP32-CAM board support) |

## Flashing order

Per the project's own convention, flash the CAM board first, then the Brain — the Brain's boot sequence expects to be able to reach the CAM over UART.

1. **ESP32-CAM ("Slave")**: connect via FTDI adapter (GPIO0 to GND for flash mode), select the correct board profile (e.g. "AI Thinker ESP32-CAM") in Arduino IDE, upload `firmware/esp32cam_slave/esp32cam_slave.ino`.
2. **ESP32-WROOM ("Brain")**: connect via USB, upload `firmware/wroom_brain/wroom_brain.ino` (and `provision_portal.h`, which is included automatically — both files must be in the same sketch folder).
3. Wire the two boards' UART cross-over per [`HARDWARE.md`](HARDWARE.md) (Brain's CAM_RX/CAM_TX to the CAM board's corresponding TX/RX), and wire the MFRC522 to the Brain's SPI pins.

## First boot and provisioning

Power on the assembled unit. With no `device_secret` in NVS, the Brain starts an open WiFi access point (`GATEMAN-SETUP-<last 4 of MAC>`) and a captive portal. Full provisioning steps — generating a token from the dashboard, scanning the QR code, entering site WiFi credentials — are in [`PROVISIONING.md`](PROVISIONING.md). Do not skip that document; the flow has specific timing constraints (10-minute token expiry) worth reading before you're standing at the install site.

## Verifying a successful install

Connect to the Brain's USB serial port at **115200 baud** and type `STATUS`. A successful install shows a non-empty `DEVICE_ID`, a `DEVICE_SECRET` with a non-zero length, `WiFi: Connected`, and a real IP address. Then tap a test RFID card and confirm the status LED gives the two-blink success pattern (`blinkOK`) rather than the five-blink error pattern (`blinkError`) — see [`FIRMWARE.md`](FIRMWARE.md) for the full LED reference table.

## Local dashboard development (optional)

The dashboard has no build step. To run it locally against your Supabase project:

```bash
npm install
npm start
# serves dashboard/ at http://localhost:3000 via the `serve` package
```

The Supabase URL and anon key are hardcoded near the top of `dashboard/index.html`'s `<script>` block — update them if pointing at a different Supabase project than the one already configured there.

## Known gaps in this process

There is no automated end-to-end install test and no OTA path — if a firmware bug is found after a unit is in the field, it requires a physical USB re-flash. See [`TESTING.md`](TESTING.md) and [`ROADMAP.md`](ROADMAP.md).
