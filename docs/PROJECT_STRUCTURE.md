# Project Structure

A map of this repository as of 2026-07-11. This document describes what belongs where; it does not describe *why* stale files exist beyond flagging them — that is Phase 3 cleanup scope.

## Top-level layout

```
gateman/
├── README.md               The only markdown file at repo root, by design
├── firmware/              Two independent ESP32 Arduino sketches (see below)
├── supabase/               Backend: Postgres migrations + Deno Edge Functions
├── dashboard/              Static admin web dashboard (single HTML file)
├── public/                 Brand assets (logo SVGs, brand guide page)
├── docs/                   Every other markdown file — current reference docs
│                            AND legacy/scratch files, side by side (see below)
├── package.json            Root Node package — only used to `npm start` a static file server for dashboard/
├── deploy-functions.bat    Windows batch script, presumably for deploying Supabase functions
└── *.sql (root-level)      Standalone SQL patches applied outside the migrations/ pipeline
```

**Update since this document was first written**: all markdown files that used to sit at repo root (`WIKI.md`, `plan.md`, `audit.md`, `FIX.MD`, `homepage.md`, `deployment_guide.md`, `VPS_RECOVERY_GUIDE.md`, the three provisioning-flow docs, the two stale firmware-snapshot docs, `deploy_quick_commands.md`, and `CHANGELOG.md`) were relocated into `docs/` via `git mv` (history preserved) as a repository-hygiene pass. This was a **physical move only** — it does not change any of this document's or `docs/CLEANUP_REPORT.md`'s conclusions about which files are stale/superseded/dead; those files just no longer clutter the root while the removal decisions in `docs/CLEANUP_REPORT.md` are pending approval. One genuinely stray file (`DEPLOYMENT.MD` at root — accidentally-saved chat text, not real content) was found and deleted during that pass, not relocated.

## `firmware/`

```
firmware/
├── wroom_brain/
│   ├── wroom_brain.ino        Main sketch for the Brain board (1191 lines)
│   └── provision_portal.h     WiFi/provisioning captive portal, #included by wroom_brain.ino (468 lines)
└── esp32cam_slave/
    └── esp32cam_slave.ino     Main sketch for the CAM (Slave) board (668 lines)
```

Two separate Arduino sketches, each flashed to its own physical ESP32 board — see `docs/FIRMWARE.md` for the full architecture, protocol, and command reference, and `docs/HARDWARE.md` for the pinouts and wiring. Neither directory contains a `libraries/` folder, `platformio.ini`, or board manifest — dependency management (MFRC522, ArduinoJson, esp32-camera, etc.) is external to this repo (Arduino IDE library manager or equivalent). `TODO: Needs verification` on the exact library versions pinned for a reproducible build.

## `supabase/`

```
supabase/
├── .temp/                 Supabase CLI local state (cli-latest) — not application code
├── functions/              Deno-based Edge Functions, one directory per function
│   ├── _shared/
│   │   ├── auth.ts         authenticateDevice(), checkSubscriptionActive(), auditLog(), checkRateLimit()
│   │   └── cors.ts         CORS + JSON response helpers shared across functions
│   ├── check-enrollment/   Device polls for admin-initiated enrollment requests
│   ├── claim-device/       (dashboard-facing; device claim flow)
│   ├── create-checkout/    Billing/checkout integration
│   ├── create-provision-token/  Admin generates a provisioning token for a new device
│   ├── device-enroll/      Card → employee credential assignment
│   ├── device-login/       (device-facing auth, distinct from device-provision)
│   ├── device-provision/   First-boot device provisioning (device_uid + token → device_secret)
│   ├── get-users/          Roster + RFID credential pull for device caching
│   ├── pair-device/        (device pairing flow)
│   ├── paystack-webhook/   Payment provider webhook handler
│   ├── poll-claim/         (dashboard-facing; polls device claim status)
│   ├── start-enrollment/   Admin initiates an enrollment request (server side of check-enrollment)
│   └── submit-log/         Per-attendance-event ingestion (the endpoint the firmware calls most)
└── migrations/              5 SQL migration files: 001_complete_schema.sql, 002_admin_enrollment.sql,
                              002_device_claims.sql, 003_fix_smart_attendance.sql, 004_secure_smart_attendance.sql
```

Five of the thirteen Edge Functions are the ones directly called by the Brain firmware and documented in `docs/FIRMWARE.md` §8: `device-provision`, `submit-log`, `get-users`, `check-enrollment`, `device-enroll`. The remaining functions (`claim-device`, `create-checkout`, `create-provision-token`, `device-login`, `pair-device`, `paystack-webhook`, `poll-claim`, `start-enrollment`) serve the dashboard/admin/billing side of the product, not the device firmware — see `docs/EDGE_FUNCTIONS.md` for the full API reference (parallel doc).

Note two migration files share the `002_` prefix (`002_admin_enrollment.sql` and `002_device_claims.sql`) — `TODO: Needs verification` on their actual applied order; `docs/DATABASE.md` (parallel doc) is the authoritative source for schema/migration details, not this file.

## `dashboard/`

```
dashboard/
└── index.html      Single-file static admin dashboard (1556 lines)
```

Served via `npm start` → `serve dashboard -l 3000` (see root `package.json`). No build step, framework, or bundler — the entire admin UI is one static HTML file.

## `public/`

```
public/
├── gateman_brand.html   Brand style reference page (380 lines)
├── gateman_dark.svg     Logo, dark variant
├── gateman_icon.svg     Icon-only logo
└── gateman_primary.svg  Primary logo
```

Static brand assets, not wired into any build pipeline observed in this repo.

## `docs/`

`docs/` is now the single home for every markdown file in this repository except `README.md`. Two tiers live side by side:

**Current, authoritative reference set** (each written/verified directly against source, cross-linked throughout):

`ARCHITECTURE.md` · `SECURITY.md` · `PROVISIONING.md` · `DEPLOYMENT.md` · `PROJECT_STRUCTURE.md` (this file) · `FIRMWARE.md` · `HARDWARE.md` · `DATABASE.md` · `EDGE_FUNCTIONS.md` · `API.md` · `INSTALLATION.md` · `TESTING.md` · `ROADMAP.md` · `BUSINESS_MODEL.md` · `DESIGN_DECISIONS.md` · `LESSONS_LEARNED.md` · `VERSIONING.md` · `CHANGELOG.md` · `RELEASE_NOTES_v1.0.0.md` · `CLEANUP_REPORT.md`

**Legacy / historical** (relocated from repo root, not yet reviewed line-by-line, several recommended for removal — see below):

`WIKI.md` · `plan.md` · `audit.md` · `FIX.MD` · `homepage.md` · `deployment_guide.md` · `VPS_RECOVERY_GUIDE.md` · `AUTO_DISCOVERY_PROVISIONING.md` · `SIMPLIFIED_PROVISIONING.md` · `FINAL_PROVISIONING_FLOW.md` · `ESP32-WROOM BRAIN FIRMWARE.md` · `ESP32-CAM SLAVE FIRMWARE.md` · `deploy_quick_commands.md` · `hotel_rfid_access_solution.md` (speculative, unimplemented hotel-vertical proposal, not stale so much as forward-looking — see `docs/CLEANUP_REPORT.md`)

If a legacy document and a current one disagree, the current one wins — that's the entire reason the current set exists. Full disposition of every legacy file (why it exists, still used, risk of removal, recommendation) is in [`CLEANUP_REPORT.md`](CLEANUP_REPORT.md); nothing has been deleted yet.

## Root config files

- `package.json` — minimal, single dependency (`serve`), single script (`start`) to serve `dashboard/` as static files on port 3000. Not a Node backend.
- `.gitignore` — excludes `.env` (flagged in-file as containing the Supabase service-role key), `node_modules/`, build output directories, IDE folders, OS cruft, and `*.log`/`*.tmp`/`*.temp`.

No `.env` or `.env.example` file is present in the repository (consistent with `.gitignore` excluding it) — environment variable names/values used at runtime are not independently confirmed by this document; see `docs/DEPLOYMENT.md` (parallel doc).
