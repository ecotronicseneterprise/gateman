# Testing

## Current state: no automated test suite

There are no unit tests, integration tests, or CI pipeline in this repository today. Verification is entirely manual, performed against real hardware and the live Supabase project. This is a real gap, not a stylistic choice — see [`ROADMAP.md`](ROADMAP.md) for what a future automated suite might cover (Edge Function unit tests are the highest-leverage starting point, since they're plain Deno/TypeScript and don't require physical hardware).

## Hardware acceptance test (required before the v1.0.0 tag)

This is the gating checklist referenced by [`RELEASE_NOTES_v1.0.0.md`](RELEASE_NOTES_v1.0.0.md). Follow it in order — each step isolates one layer of the system, so a failure at step *N* usually means the problem is in what step *N* newly exercises, not in everything before it.

Connect the Brain over USB, Arduino IDE Serial Monitor at **115200 baud**.

### 0. Baseline check

Type `STATUS`. Confirms `DEVICE_ID`, `SUPABASE_URL`, secret length, WiFi status, IP, free heap, and cached user count before you change anything.

- [ ] `STATUS` returns a coherent snapshot (not garbled/empty)

### 1. Existing device login (if testing an already-provisioned unit)

Let the device boot and connect to WiFi normally.

- [ ] Status LED gives the two-blink success pattern (`blinkOK`), not the five-blink error pattern (`blinkError`)
- [ ] `devices.last_seen` updates in Supabase
- [ ] An `audit_logs` row appears with `action = 'device.login'`

### 2. Fresh provisioning

Trigger via `RESET` on an existing device, or use a factory-new unit. Follow [`PROVISIONING.md`](PROVISIONING.md) exactly.

- [ ] Device broadcasts the `GATEMAN-SETUP-<last4MAC>` open AP
- [ ] Captive portal loads at `192.168.4.1`
- [ ] Dashboard-generated token/QR is accepted
- [ ] Device reconnects to real WiFi and reboots into normal operation
- [ ] `STATUS` now shows a real `DEVICE_ID` and non-empty secret
- [ ] This step specifically exercises the `create-provision-token` auth fix — confirm it still works for a legitimate admin (see [`SECURITY.md`](SECURITY.md) fix #1)

### 3. RFID enrollment

Dashboard → Enrollment → select employee + device → "Enroll Card".

- [ ] Device LED shows the rapid 6-blink enroll-mode pattern
- [ ] Tapping a card completes enrollment within the dashboard's ~2s poll cycle
- [ ] This step exercises the `start-enrollment` auth fix — note whether this flow worked *before* the fix too, per the open `TODO: Needs verification` in [`SECURITY.md`](SECURITY.md) fix #3

### 4. Attendance logging

Tap the now-enrolled card.

- [ ] LED gives `blinkOK`
- [ ] Event appears in the dashboard Live Feed (Supabase Realtime) within a few seconds
- [ ] A photo is attached (or the no-photo fallback triggers cleanly if the CAM board is unreachable — confirm this doesn't hang the Brain)

### 5. Dashboard sync / reporting

- [ ] Attendance page loads correctly (exercises the patched `get_smart_attendance` RPC — see [`SECURITY.md`](SECURITY.md) fix #2)
- [ ] CSV export works (note: capped at 5,000 records, unrelated to any recent change)

### 6. Multi-device testing

- [ ] **Before testing this**: check `subscriptions.device_limit` for the test org. A fresh trial org defaults to `device_limit = 1` — provisioning a second device will correctly return `403 Device limit reached`. That is the subscription logic working as designed, not a bug. Raise the limit manually in the `subscriptions` table first if you actually want to test concurrent devices.
- [ ] Two devices provisioned to the same org both sync independently without event collisions (idempotency key is `device_id + device_event_id`, so this should hold by construction — worth confirming empirically once)

### 7. Enrollment-photo bug (expected failure — documented, not a regression)

- [ ] Confirm enrollment photos do *not* currently reach Storage — this is a known, pre-existing bug (`device-enroll` reads `photo_base64` but firmware sends `photo_path`), not something introduced by this release. See [`FIRMWARE.md`](FIRMWARE.md).

## What "pass" means for the v1.0.0 tag

All checkboxes in sections 0–5 pass on at least one physical device. Section 6 passes if you have hardware to test it with; if not, note it as untested rather than failed (the idempotency mechanism it relies on is covered by code review, just not empirically confirmed with two simultaneous devices). Section 7 is expected to "fail" — it's there so the known bug doesn't get mistaken for a new one during testing.

## Recommended future automated coverage (not implemented — see `ROADMAP.md`)

- Deno unit tests for the Edge Functions' request validation, auth checks, and rate-limiting logic — no hardware required, highest leverage per effort.
- A Postgres RLS test suite (Supabase supports `pgTAP`) exercising each policy with multiple simulated `auth.uid()` values, specifically to catch the class of bug fixed in `SECURITY.md` #2 before it ships next time.
- Firmware: genuinely difficult to automate without a hardware-in-the-loop rig; realistic next step is a documented manual regression checklist (this document) kept current, rather than full automation.
