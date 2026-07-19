# Release Notes — v1.0.0

**Status: Candidate.** Code-complete and documented; the git tag is created only after the hardware acceptance test in [`TESTING.md`](TESTING.md) passes. This document describes what that tag will represent.

## What v1.0.0 is

The first formally versioned release of Gateman: a multi-tenant SaaS platform for RFID-based attendance/access logging on ESP32 edge hardware, with photo capture, a Supabase backend, and a web dashboard. It is an MVP — functional, currently deployed to one production VPS, and now brought up to a documented, security-reviewed baseline rather than a rewrite.

## What's included

- **Firmware**: two-board ESP32 system (WROOM "Brain" + ESP32-CAM "Slave") — RFID scanning, duplicate-tap suppression, photo capture with graceful no-photo fallback, offline queueing on both boards, WiFi captive-portal provisioning with QR-code token entry, admin-initiated remote enrollment. See [`FIRMWARE.md`](FIRMWARE.md).
- **Backend**: Supabase Postgres with row-level security, 13 Deno Edge Functions, Storage for attendance photos, Paystack billing integration with signature-verified webhooks. See [`DATABASE.md`](DATABASE.md) and [`EDGE_FUNCTIONS.md`](EDGE_FUNCTIONS.md).
- **Dashboard**: single-file web app for employee management, live attendance feed (Supabase Realtime), remote card enrollment, device provisioning, CSV export, subscription/billing management.
- **Multi-tenancy**: organizations, role-based membership (owner/admin/viewer), subscription tiers (starter/growth/enterprise) with device and user limits.
- **Security baseline**: the three cross-tenant authentication gaps found in this release cycle are fixed — see [`SECURITY.md`](SECURITY.md) and the [`CHANGELOG.md`](CHANGELOG.md).

## What's explicitly NOT included in v1.0.0

Documented here so nobody mistakes silence for an oversight:

- **No physical access control.** Despite the product's framing, no firmware code drives a relay, lock, or door strike. v1.0.0 is an attendance/presence logger, not a door-access system.
- **No device-secret hashing.** Deliberately deferred to Phase 4.1 (post-v1.0) — see [`ROADMAP.md`](ROADMAP.md) and [`SECURITY.md`](SECURITY.md) for why.
- **No OTA firmware updates.** Every firmware change requires physical USB access to the device.
- **No hardware watchdog.** Disabled on both boards; a firmware hang requires a manual power cycle.
- **No firmware version reporting.** The `devices.firmware_version` column exists but nothing populates it yet.
- **No automated test suite.** Verification today is manual — see [`TESTING.md`](TESTING.md).

## Known limitations carried into v1.0.0

See [`FIRMWARE.md`](FIRMWARE.md) §Known Limitations and [`SECURITY.md`](SECURITY.md) §Deferred Items for the full list. Headline items: enrollment photos are captured but never reach the backend (a genuine bug, not a design choice); the offline queue has no max-age check against the server's 7-day rejection window; several Edge Functions (`device-login`, `pair-device`, `claim-device`, `poll-claim`) are deployed but unused, left over from abandoned provisioning-flow experiments.

## Upgrade / deployment notes

This is the first tagged release, so there is no "upgrade from a prior tag" path yet. Deployment of the fixes that define this release is described in [`DEPLOYMENT.md`](DEPLOYMENT.md); nothing about the request/response shape of any existing Edge Function or firmware endpoint changed, so no firmware re-flash is required for this release.

## Acceptance criteria for tagging v1.0.0

Per the project's release workflow, the tag is created only after:

1. All three security fixes are deployed to production (Supabase Edge Functions + migration + dashboard).
2. The full hardware validation checklist in [`TESTING.md`](TESTING.md) passes on real hardware: device login, fresh provisioning, RFID enrollment, attendance logging, dashboard sync, reporting, and (if applicable) multi-device behavior.

Until both are confirmed, treat this as a release candidate.
