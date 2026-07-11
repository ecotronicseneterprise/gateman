# Design Decisions

Architecture Decision Record style — one entry per significant, deliberate choice, including the tradeoff accepted and why. This is not a list of things that happened by accident (those belong in [`LESSONS_LEARNED.md`](LESSONS_LEARNED.md)); it's a record of choices someone would reasonably ask "why did you do it that way?" about.

## ADR-1: Two independently-flashed ESP32 boards instead of one

**Decision**: split RFID+WiFi ("Brain", ESP32-WROOM) from camera+storage ("CAM/Slave", ESP32-CAM), connected by a UART line protocol.

**Why**: the ESP32-CAM's GPIOs are largely consumed by the camera's parallel bus and SD_MMC lines, leaving too few free pins to reliably also drive an SPI RFID reader. Keeping camera work (which blocks for tens to hundreds of milliseconds per capture) off the board also servicing WiFi and RFID reduces the chance of the RFID read loop stalling.

**Tradeoff accepted**: two physical boards to source, wire, and flash per site, connected by an informal, unversioned UART protocol (see [`FIRMWARE.md`](FIRMWARE.md)) instead of one board with one firmware image. This is the single largest candidate for simplification in a hardware revision — see [`ROADMAP.md`](ROADMAP.md) V2 notes — but is not being revisited for v1.0.0.

## ADR-2: Supabase instead of a self-hosted backend

**Decision**: Postgres + RLS + Auth + Storage + Edge Functions + Realtime, all via Supabase, replacing an earlier Node.js/Express/SQLite backend.

**Why**: gets multi-tenant data isolation (RLS), authentication, file storage, and a serverless compute layer without operating any of that infrastructure — appropriate for a solo-maintained product. No trace of the prior backend remains in the current codebase; the migration was total, not incremental.

**Tradeoff accepted**: vendor dependency on Supabase's specific primitives (RLS semantics, Edge Function gateway behavior, `SECURITY DEFINER` RPCs) — the exact mechanism that made the v1.0.0 security fixes necessary (RPCs and Edge Functions that bypass RLS have to independently reimplement the access check RLS would otherwise provide for free, and one did, and didn't; see [`SECURITY.md`](SECURITY.md)).

## ADR-3: Single-file, framework-free dashboard

**Decision**: `dashboard/index.html` — one file, vanilla JS, no build step, no component framework.

**Why**: trivial to deploy (served as a static file by Caddy, no build pipeline, no Node process — see [`DEPLOYMENT.md`](DEPLOYMENT.md)) and trivial to reason about for a single maintainer.

**Tradeoff accepted**: no component structure, no type safety, no code-splitting as the UI grows past its current ~1550 lines. This is a real cost that will compound if the dashboard's feature surface grows significantly (see [`ROADMAP.md`](ROADMAP.md) future integrations) — not a decision to revisit reflexively, but one to watch.

## ADR-4: Device identity lives in the request body, not the transport-layer Authorization header

**Decision**: firmware sends `Authorization: Bearer <anon key>` purely to satisfy the Supabase Edge Function gateway's requirement for a structurally valid JWT; actual device authentication (`device_uid` + `device_secret`) travels inside the JSON body and is checked in application code (`_shared/auth.ts`).

**Why**: the anon key is a shared, public, non-device-specific credential — it can't carry per-device identity. Application-level auth in the function body was the available mechanism given that constraint.

**Tradeoff accepted**: this is why device secrets currently need their own comparison logic instead of relying on Supabase's built-in JWT verification — and why that comparison is currently plaintext (see ADR-5).

## ADR-5: Device secrets are plaintext in v1.0.0, hashing deferred to Phase 4.1

**Decision**: `device_secret` is generated, stored, and compared as a raw string, not hashed.

**Why deferred rather than fixed now**: this is a moderate-severity issue (not remotely exploitable — RLS blocks all client roles from reading the column; exploitation requires database-level access). The three v1.0.0 fixes were all critical-severity, remotely exploitable via a public API with no other access required, and none of them touch firmware-facing behavior. Hashing device secrets touches the authentication path for every device-facing Edge Function (`_shared/auth.ts`) right before a hardware validation cycle — the project's explicit priority ("never break existing hardware," discussed and agreed with the repo owner) means isolating that change to its own validated cycle rather than bundling it with unrelated fixes.

**Planned resolution**: documented in [`SECURITY.md`](SECURITY.md) and [`ROADMAP.md`](ROADMAP.md) — hash at comparison time, migrate existing rows in place, zero firmware changes required since firmware already sends the plaintext secret it was issued.

## ADR-6: Token + QR provisioning instead of auto-discovery or MAC-pairing

**Decision**: an admin generates a single-use token from the dashboard; the installer scans a QR code or pastes the token into the device's captive portal along with WiFi credentials.

**Why**: two alternative flows were built and rejected — see [`PROVISIONING.md`](PROVISIONING.md) for the full history. The token flow was reverted back to explicitly because it was "simpler and proven," per the commit message that reinstated it.

**Tradeoff accepted**: requires the installer to have the dashboard open and a token in hand at install time, rather than a "device announces itself, admin claims it later" model. Judged worth it for reliability over convenience, based on direct experience building and discarding the alternative.

## ADR-7: One product version number, not per-component versioning

**Decision**: a single semver number (starting at v1.0.0) covers dashboard + backend + firmware as a validated-together snapshot, rather than versioning each independently.

**Why**: matches how the product is actually built and released — one person, one validation cycle, one git history. Per-component versioning would add process overhead without a corresponding benefit until there's a team or a release cadence that needs it.

**Known gap this creates**: firmware doesn't currently report its own version to the backend (`devices.firmware_version` is always `NULL`), so there's no way to query "what firmware is device X running" without physical access. Tracked in [`ROADMAP.md`](ROADMAP.md), not fixed in v1.0.0.
