# Gateman — Verified Wiring & Circuit Reference

> **Status: VERIFIED against the physical build on 2026-07-18** (continuity-tested wire by wire).
> This is the canonical rebuild reference. Firmware pin defines: [wroom_brain.ino](firmware/wroom_brain/wroom_brain.ino) (`PINS` section) and [esp32cam_slave.ino](firmware/esp32cam_slave/esp32cam_slave.ino).

## Bill of Materials (as built)

| # | Component | Notes |
|---|---|---|
| 1 | ESP32-WROOM-32 DevKit | The "brain" — RFID, WiFi, cloud sync |
| 2 | ESP32-CAM (AI-Thinker, OV2640) | The "slave" — photos + SD storage |
| 3 | MFRC522 RFID reader | SPI, 3.3V only |
| 4 | MicroSD card (in CAM) | 1-bit MMC mode |
| 5 | 18650 power bank + USB hub | Powers both boards; NEPA-outage bridge |
| 6 | RFID cards / keyfobs | Mifare Classic (UID-based) |

**Deliberately NOT present** (firmware tolerates their absence):
- **Enroll button (GPIO4)** — not connected. GPIO4 uses an internal pullup so it floats safely HIGH. Manual on-device enrollment is therefore unavailable; all enrollment is admin-driven from the dashboard (Enrollment page → device polls → tap card). This is the normal workflow.
- Relay / buzzer / door lock — no such output exists in firmware or hardware.

## Circuit Diagram

```
                    18650 POWER BANK
                          │
                       USB HUB ──────────────┐
                     5V   │                  │ 5V
                          │                  │
        ┌─────────────────▼────┐      ┌──────▼─────────────┐
        │   ESP32-WROOM-32     │      │   ESP32-CAM        │
        │      (Brain)         │      │   (AI-Thinker)     │
        │                      │      │                    │
        │  GPIO16 (RX2) ◄──────┼──────┼── GPIO13 (TX)      │
        │  GPIO17 (TX2) ───────┼──────┼─► GPIO12 (RX)      │
        │  GND ────────────────┼──────┼── GND  (shared!)   │
        │                      │      │                    │
        │  GPIO2  = status LED │      │  GPIO33 = LED      │
        │  GPIO4  = (no button)│      │  OV2640 camera     │
        │                      │      │  microSD (1-bit)   │
        │  3.3V ──┬── RFID VCC │      └────────────────────┘
        │         └── RFID RST │
        │  GND ────── RFID GND │
        │  GPIO5  ─── RFID SDA │
        │  GPIO18 ─── RFID SCK │
        │  GPIO23 ─── RFID MOSI│
        │  GPIO19 ─── RFID MISO│
        │  (n/c)  ─── RFID IRQ │  ← floating, by design
        └──────────────────────┘
                   │
              ┌────▼─────┐
              │ MFRC522  │  3.3V ONLY — 5V destroys it
              │  reader  │
              └──────────┘
```

## Group 1 — MFRC522 → WROOM (SPI)

| # | MFRC522 pin | WROOM pin | Wire colour | Verified |
|---|---|---|---|---|
| 1 | SDA (SS) | GPIO5 (D5) | Orange | ✅ |
| 2 | SCK | GPIO18 (D18) | Yellow | ✅ |
| 3 | MOSI | GPIO23 (D23) | Green | ✅ |
| 4 | MISO | GPIO19 (D19) | Blue | ✅ |
| 5 | RST | 3.3V rail | White/Grey | ✅ (tied high, not GPIO-controlled) |
| 6 | VCC | 3.3V rail | Red | ✅ (**never 5V**) |
| 7 | GND | GND rail | Black | ✅ |
| 8 | IRQ | **nothing — floating** | Purple | ✅ (unused by firmware) |

## Group 2 — WROOM ↔ ESP32-CAM (UART, 9600 baud 8N1)

| # | WROOM pin | CAM pin | Direction | Verified |
|---|---|---|---|---|
| 9 | GPIO16 (RX2) | GPIO13 (TX) | CAM → WROOM | ✅ |
| 10 | GPIO17 (TX2) | GPIO12 (RX) | WROOM → CAM | ✅ |
| 11 | GND | GND | **Shared ground — mandatory**, even with separate USB feeds | ✅ |

TX/RX are **crossed**: one board's TX goes to the other's RX. If the CAM never answers `PING` with `PONG`, these two wires being swapped is the first thing to check.

## Group 3 — Power

| # | From | To | Note | Verified |
|---|---|---|---|---|
| 12 | USB hub 5V | WROOM 5V/USB | Own feed from hub | ✅ |
| 13 | USB hub 5V | CAM 5V | Separate feed from hub | ✅ |
| 14 | WROOM GND | CAM GND | Common ground across feeds | ✅ |

Power source: 18650 power bank → USB hub → both boards. The MFRC522 is powered from the WROOM's **3.3V** pin (not the hub).

## Rebuild checklist (continuity test before first power-up)

1. Beep out all 8 Group-1 wires against the table above.
2. Confirm MFRC522 VCC goes to **3.3V, never 5V**.
3. Confirm IRQ (purple) does **not** beep to any WROOM GPIO.
4. Beep WROOM GPIO16 ↔ CAM GPIO13, WROOM GPIO17 ↔ CAM GPIO12 (crossed).
5. Beep WROOM GND ↔ CAM GND ↔ MFRC522 GND — all one net.
6. Power up, open WROOM serial monitor at 115200, type `STATUS`.
7. Healthy boot: 3 slow LED flashes, WiFi connect, `PONG` heartbeat from CAM in log.

## Firmware ↔ hardware gaps (known, intentional)

| Firmware feature | Physical status |
|---|---|
| Enroll button on GPIO4 | Not wired — dashboard-driven enrollment only |
| MFRC522 IRQ | Not used by firmware, wire left floating |
| MFRC522 RST software control | Not used — RST tied to 3.3V, firmware inits with `MFRC522 rfid(RFID_SS, -1)` |
