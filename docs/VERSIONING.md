# Versioning Policy

## Scheme

Gateman uses [Semantic Versioning 2.0.0](https://semver.org/) (`MAJOR.MINOR.PATCH`) for the product as a whole — one version number covers the dashboard, Supabase backend (schema + Edge Functions), and firmware as a shipped snapshot, rather than versioning each component independently. This matches how the product is actually built and deployed today: a single engineer maintains all three layers together, and a "release" means "this combination of dashboard + backend + firmware was validated together."

- **MAJOR** — breaking changes to the device↔backend protocol, the database schema in a way that requires migration/downtime, or dashboard behavior that changes how existing customers work. Requires a migration plan.
- **MINOR** — backward-compatible functionality: new Edge Functions, new dashboard features, new firmware capability that doesn't change existing request/response shapes.
- **PATCH** — bug fixes, security patches, documentation, and reliability improvements that don't change any public behavior or API shape.

## Current version

**v1.0.0** — first tagged release, defining the MVP baseline. See [`RELEASE_NOTES_v1.0.0.md`](RELEASE_NOTES_v1.0.0.md) and [`CHANGELOG.md`](CHANGELOG.md).

Per the project's own release workflow, the `v1.0.0` git tag is created **after** hardware acceptance testing passes (see [`TESTING.md`](TESTING.md)), not before. Documentation and code are prepared as "v1.0.0 candidate" ahead of that gate.

## What "version" currently means per layer

| Layer | How it's versioned today |
|---|---|
| Dashboard (`dashboard/index.html`) | Deployed as a static file via `git pull` on the VPS. No in-app version string is displayed. |
| Backend (Supabase schema + Edge Functions) | Schema tracked via `supabase/migrations/NNN_*.sql`, applied in order. Edge Functions deployed individually via `supabase functions deploy <name>`; Supabase does not expose a function-level version number. |
| Firmware (`wroom_brain.ino`, `esp32cam_slave.ino`) | **TODO: Needs verification / known gap.** The `devices` table has a `firmware_version` column, but no code path currently sets it — no Edge Function writes to it and no firmware code sends a version string. In practice this column is always `NULL` today. Firmware version is only tracked implicitly by which git commit was flashed to a given physical device, which is not recorded anywhere. This is a real gap for fleet management once more than a handful of devices are deployed — see [`ROADMAP.md`](ROADMAP.md). |

## Recommendation for future releases

Once the `firmware_version` field is actually populated (a firmware change: add a `#define FIRMWARE_VERSION "1.0.0"` and send it on `device-login`/`device-provision`), firmware and product version can be tracked together. Until then, treat the git tag as authoritative for "what code was flashed" and cross-reference commit dates against any given device's provisioning date if you need to reconstruct which firmware a specific unit is running.

## Pre-1.0 history

There is no `v0.x` tag history — this repository went through several architecture changes (Node.js/SQLite → Supabase) and provisioning-flow rewrites without formal versioning before this documentation effort. `v1.0.0` is the first point at which the project adopts a formal versioning and release process; see [`LESSONS_LEARNED.md`](LESSONS_LEARNED.md) for why that matters going forward.
