# Roadmap

This document separates what v1.0.0 actually includes from what's planned. Nothing here is implemented yet unless explicitly marked otherwise — see [`RELEASE_NOTES_v1.0.0.md`](RELEASE_NOTES_v1.0.0.md) for the v1.0.0 baseline itself.

## Phase 4.1 — Authentication hardening (next, post-v1.0)

Deliberately deferred out of v1.0.0 to keep the hardware validation cycle isolated to one variable — see [`SECURITY.md`](SECURITY.md).

- **Hash device secrets at rest.** Firmware keeps sending the plaintext secret it already has; the server hashes on comparison and existing `devices.device_secret` rows are migrated to their hash in place. Zero firmware changes required, zero impact on already-deployed hardware.
- Revisit whether `create-checkout`'s JWT-verification pattern (the one the three v1.0.0 fixes were modeled on) is actually reachable from the dashboard — a documentation pass found no confirmed call site for `create-checkout` in `dashboard/index.html`; worth confirming billing checkout actually works end-to-end.

## Phase 4 (continued) — Firmware reliability, requires physical re-flash

Not done in v1.0.0 because every item here requires re-flashing deployed hardware, which is gated behind the hardware acceptance test in [`TESTING.md`](TESTING.md). Ordered roughly by risk/impact:

1. **Re-enable the hardware watchdog** on both boards. It was disabled after it caused "constant resets" — the root cause of those resets was never diagnosed, just worked around. Re-enabling without fixing the underlying cause will just reintroduce the resets, so this is really two tasks: find why the watchdog was tripping, then re-enable it.
2. **Offline queue max-age check.** The backend rejects `submit-log` events older than 7 days; the firmware's offline queue has no matching client-side check, so a device offline for longer than that silently loses data instead of surfacing the problem.
3. **Fix the enrollment-photo bug.** Firmware sends `photo_path`; `device-enroll` only reads `photo_base64`. Either change firmware to send base64 (matching how `submit-log` already does it) or have the backend read `photo_path` and pull the file — the former is more consistent with the rest of the codebase.
4. **Remove the hardcoded WiFi fallback credentials** and the serial `PROVISION:<secret>` backdoor from firmware source (see [`SECURITY.md`](SECURITY.md)).
5. **TLS certificate validation** for the firmware's HTTPS calls, currently relying on ESP32 Arduino-core defaults.
6. **Firmware version reporting.** Populate `devices.firmware_version` (currently always `NULL` — nothing writes to it) so fleet management is possible once there's more than a handful of units. See [`VERSIONING.md`](VERSIONING.md).
7. **OTA update capability.** Every firmware fix currently requires physical USB access per device. Not attempted in v1.0.0 or its immediate follow-up — significant scope, needs its own design pass before implementation.

## Phase 3 — Repository cleanup (documented, not yet executed)

See [`CLEANUP_REPORT.md`](CLEANUP_REPORT.md) for the full inventory of dead code and stale documentation awaiting approval to remove: four orphaned Edge Functions (`device-login`, `pair-device`, `claim-device`, `poll-claim`), three abandoned provisioning-flow docs, two stale firmware snapshot docs, and several other files. Nothing is deleted until that report is explicitly approved.

## Automated testing (not started)

Currently zero automated tests exist (see [`TESTING.md`](TESTING.md)). Recommended order once prioritized: Edge Function unit tests (no hardware needed, highest leverage — would have caught the auth gaps this release fixed), then RLS policy tests via `pgTAP`, then a firmware regression checklist kept current as a living document rather than full hardware-in-the-loop automation.

## Version 2 — under consideration, not designed, not started

This section records direction discussed with the repo owner, not committed architecture. Per explicit instruction, this roadmap does not design or implement V2.

**Hardware consolidation**: the current two-board (Brain + CAM) split, connected by an informal UART protocol, is a candidate for consolidation onto a single ESP32-S3-class board with an integrated camera connector (e.g. Seeed XIAO ESP32S3 Sense), paired with an external RFID module (camera+RFID+battery doesn't currently exist as a single off-the-shelf board). This would eliminate the UART bridge protocol entirely — one of the firmware's structural fragility points — at the cost of a hardware revision and requalification. No custom PCB is planned before hardware-consolidation demand is proven; the "buy a proven module" approach is intentional to avoid becoming a PCB manufacturer prematurely.

**Product scope**: whether Gateman stays a single-purpose attendance logger or becomes a multi-vertical platform (the "physical identity platform" framing explored in `docs/hotel_rfid_access_solution.md`, a speculative hotel-access proposal with no implemented code) is an open business decision, not an engineering one — see [`BUSINESS_MODEL.md`](BUSINESS_MODEL.md).

**Explicitly not being pursued right now**: face recognition/verification, mobile NFC/BLE credentials, visitor pre-registration, geofencing, multi-door support, payroll/HR system integrations. These may become real roadmap items once v1.0.0 is validated and has at least one paying customer informing priority — listed here so they're not forgotten, not because they're scheduled.
