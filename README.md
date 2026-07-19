# Gateman — Smart Attendance System

RFID attendance system built on two ESP32 boards + Supabase cloud. An employee taps a card, the ESP32-CAM snaps a photo, the event syncs to Supabase, and the web dashboard updates live.

**Live dashboard:** https://gateman.89.167.93.25.sslip.io
**Full technical reference:** [WIKI.md](WIKI.md) · **VPS rebuild/recovery:** [VPS_RECOVERY_GUIDE.md](VPS_RECOVERY_GUIDE.md)

---

## Architecture

```
[RFID card] → ESP32-WROOM (brain) ──UART 9600──► ESP32-CAM (slave, photo + SD)
                    │
                    └──HTTPS──► Supabase (edge functions + Postgres + storage)
                                      ▲
             Dashboard (static HTML, Caddy on Hetzner VPS) ── talks directly to Supabase
```

- **Supabase project:** `ueobebsgheecclwcbigy.supabase.co` — edge functions in [supabase/functions/](supabase/functions/)
- **Dashboard:** single static file [dashboard/index.html](dashboard/index.html), no build step
- **The VPS only hosts the dashboard.** Boards talk directly to Supabase; attendance keeps working if the VPS is down.

## Hardware & Wiring

Two boards: **ESP32-WROOM DevKit** (the brain) and **AI-Thinker ESP32-CAM** (the slave). Ground truth is the firmware pin defines in [wroom_brain.ino](firmware/wroom_brain/wroom_brain.ino) and [esp32cam_slave.ino](firmware/esp32cam_slave/esp32cam_slave.ino).

> **✅ Wiring verified against the physical build 2026-07-18.** See **[WIRING.md](WIRING.md)** for the full continuity-tested reference: wire colours, circuit diagram, power topology (18650 bank → USB hub → both boards), and rebuild checklist.

### WROOM ↔ MFRC522 RFID reader (SPI)

| MFRC522 pin | WROOM pin |
|---|---|
| SDA (SS) | GPIO5 |
| SCK | GPIO18 |
| MOSI | GPIO23 |
| MISO | GPIO19 |
| RST | 3.3V (tied high, not GPIO-controlled) |
| 3.3V | 3.3V |
| GND | GND |

### WROOM ↔ ESP32-CAM (UART, 9600 baud 8N1)

| WROOM | ESP32-CAM |
|---|---|
| GPIO16 (RX) | GPIO13 (TX) |
| GPIO17 (TX) | GPIO12 (RX) |
| GND | GND (**shared ground is mandatory**) |

### Other WROOM connections

| Function | Pin |
|---|---|
| Enroll button | GPIO4 → GND — **not wired in current build** (internal pullup, safe unconnected; enrollment is dashboard-driven) |
| Status LED | GPIO2 (onboard) |
| MFRC522 IRQ | Not connected (unused by firmware) |

ESP32-CAM extras: OV2640 camera on standard AI-Thinker pins, microSD in 1-bit mode, indicator LED on GPIO33 (onboard red).

## Flashing (order matters)

1. **ESP32-CAM first** — via FTDI (TX→GPIO3, RX→GPIO1, 5V, GND; IO0→GND for boot mode). Board: `AI Thinker ESP32-CAM`.
2. **WROOM second** — over USB, hold BOOT if needed. Board: `ESP32 Dev Module`.
3. Set WiFi credentials at the top of `wroom_brain.ino` (or leave blank for the captive provisioning portal). Serial monitor at 115200; type `STATUS` to verify.

See [WIKI.md §8.4](WIKI.md) for full flashing details and device provisioning.

## Enrollment workflow

1. Dashboard → Enrollment → start enrollment for an employee. (Firmware also supports a hold-2s button on GPIO4, but no button is wired in the current build.)
2. Tap the new RFID card on the reader.
3. Card is registered against the employee; subsequent taps log IN/OUT attendance with a photo.

## Dashboard deployment (VPS)

Static file served by Caddy at `/var/www/gateman/dashboard/` on the Hetzner VPS (`89.167.93.25`, SSH as `deploy` — sudo requires password). To update:

```bash
scp dashboard/index.html deploy@89.167.93.25:/home/deploy/gateman-dashboard-index.html
ssh -t deploy@89.167.93.25 "sudo cp /home/deploy/gateman-dashboard-index.html /var/www/gateman/dashboard/index.html"
```

## Repo map

| Path | What |
|---|---|
| [firmware/wroom_brain/](firmware/wroom_brain/) | Brain firmware (RFID, WiFi, sync, provisioning portal) |
| [firmware/esp32cam_slave/](firmware/esp32cam_slave/) | Camera slave firmware (photo, SD queue) |
| [dashboard/index.html](dashboard/index.html) | Entire admin dashboard (vanilla JS SPA) |
| [supabase/functions/](supabase/functions/) | Edge functions (device auth, logs, enrollment, payments) |
| [supabase/migrations/](supabase/migrations/) | Database schema |
| [WIKI.md](WIKI.md) | Deep technical documentation (API, schema, firmware internals) |
| [VPS_RECOVERY_GUIDE.md](VPS_RECOVERY_GUIDE.md) | Server rebuild / disaster recovery |
