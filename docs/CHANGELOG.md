# Changelog

All notable changes to Gateman are documented in this file. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning follows [Semantic Versioning](https://semver.org/) — see [`VERSIONING.md`](VERSIONING.md).

## [1.0.0] — Candidate, pending hardware acceptance test

First formally tagged release. Establishes the MVP baseline: multi-tenant RFID attendance logging on ESP32 hardware, Supabase backend, Paystack billing, single-file dashboard. See [`RELEASE_NOTES_v1.0.0.md`](RELEASE_NOTES_v1.0.0.md) for the full picture of what v1.0.0 includes and excludes.

### Fixed

- **Critical: unauthenticated cross-tenant device provisioning.** `create-provision-token` accepted any `organization_id` with no verification of the caller's identity or org membership, allowing any party to mint a valid device-provisioning token for an organization they don't belong to. Now verifies the caller's Supabase session and requires `owner`/`admin` membership in the target org, mirroring the pattern already used correctly in `create-checkout`. (`supabase/functions/create-provision-token/index.ts`)
- **Critical: cross-tenant attendance data read.** The `get_smart_attendance` RPC — called by the dashboard's attendance page — was `SECURITY DEFINER` with no membership check, unlike the schema's other four dashboard RPCs. Any authenticated user could read another tenant's attendance history and photo paths by passing an arbitrary `org_id`. Now enforces the same `org_members` membership guard as the other RPCs. (`supabase/migrations/004_secure_smart_attendance.sql`)
- **Moderate: unverified caller identity in admin-initiated enrollment.** `start-enrollment` trusted a client-supplied `caller_user_id` field instead of verifying the actual session. Now resolves the caller from a verified JWT the same way as the two fixes above. (`supabase/functions/start-enrollment/index.ts`, `dashboard/index.html`)

### Security

- Reviewed all 13 Edge Functions, all RLS policies, and all RPCs. No further authentication gaps found beyond the three above; RLS policy design is sound (see [`SECURITY.md`](SECURITY.md)).
- Identified but deliberately **deferred** (not fixed in v1.0.0): device secrets are stored and compared in plaintext. This is scheduled as a post-v1.0 hardening task (Phase 4.1) rather than bundled into this release, to keep the current hardware-validation cycle isolated to one variable at a time. See [`SECURITY.md`](SECURITY.md) and [`ROADMAP.md`](ROADMAP.md).

### Added

- Rate limiting on `create-provision-token` (10 tokens/org/10min) and `start-enrollment` (20/org/5min) — both were previously unprotected admin-facing endpoints; `submit-log` and `device-login` already had this pattern via `checkRateLimit()`, which was imported but unused in `create-provision-token` prior to this change. Backend-only, additive, no request/response shape change, no firmware impact. (Phase 4)

### Documentation

- Added the full `docs/` reference set: `ARCHITECTURE.md`, `SECURITY.md`, `PROVISIONING.md`, `DEPLOYMENT.md`, `PROJECT_STRUCTURE.md`, `FIRMWARE.md`, `HARDWARE.md`, `DATABASE.md`, `EDGE_FUNCTIONS.md`, `API.md`, `INSTALLATION.md`, `TESTING.md`, `ROADMAP.md`, `BUSINESS_MODEL.md`, `DESIGN_DECISIONS.md`, `LESSONS_LEARNED.md`, `VERSIONING.md`.
- Rewrote `README.md` to flagship standard.
- Added `docs/CLEANUP_REPORT.md` inventorying dead code and stale documentation for approval before removal (Phase 3 — nothing removed yet).

### Prior history

This project did not maintain a changelog before v1.0.0 — it went through an undocumented backend migration (Node.js/SQLite → Supabase) and several reverted provisioning-flow redesigns, visible in `git log` but not tracked here. v1.0.0 is the point at which formal release discipline begins; see [`LESSONS_LEARNED.md`](LESSONS_LEARNED.md).
