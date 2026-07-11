# Security

This document is the authoritative record of Gateman's security posture as of v1.0.0: what was reviewed, what was found, what was fixed, what was deliberately deferred, and why. It is written to be honest rather than reassuring — an accurate threat model is more useful to future maintainers than a clean-looking one.

## Scope of the v1.0.0 security review

Every Edge Function (13), every RPC, every RLS policy, both firmware authentication paths, and the provisioning flow were reviewed against the live, deployed system — not just the code, but which endpoints the dashboard and firmware actually call, cross-referenced against `git log` to understand how each issue was introduced.

## Fixed in v1.0.0

### 1. Unauthenticated cross-tenant device provisioning — Critical

**Location**: `supabase/functions/create-provision-token/index.ts`

**How it happened**: git history shows a 2026-02-28 debugging session fighting the Supabase gateway stripping the `Authorization` header. It ended in a commit titled `REMOVE ALL AUTH - just require organization_id to generate token` — a temporary unblock during debugging that was never reverted. This function is the one the live dashboard calls to generate device-provisioning tokens.

**Impact**: no login was required. Anyone who knew or could observe an `organization_id` (a UUID, visible client-side to any member of that org, and potentially inferable through normal product usage) could mint a valid 10-minute provisioning token for a tenant they don't belong to, then redeem it via `device-provision` to register a rogue device that could pull that organization's employee/RFID roster and submit fabricated attendance records with photos.

**Fix**: added JWT verification (`supabase.auth.getUser()` against the `Authorization` header) and an `org_members` role check (`owner`/`admin`), mirroring the pattern already proven correct in `create-checkout`. The dashboard was already sending the `Authorization` header on this call, so no frontend request-shape change was required.

**Exposure window**: approximately 2026-02-28 to the date this fix is deployed (see `CHANGELOG.md` for the deploy date once confirmed). If you are auditing for prior exploitation, check `provision_tokens` and `devices` for entries you don't recognize, and `audit_logs` for `provision_token.created` / `device.provisioned` events outside your own activity.

### 2. Cross-tenant attendance data read — Critical

**Location**: `supabase/migrations/004_secure_smart_attendance.sql` (function `get_smart_attendance`)

**How it happened**: this RPC was authored directly in the Supabase SQL editor across three iterations (`smart_attendance_view.sql`, `003_fix_smart_attendance.sql`, `FIX_ATTENDANCE_SQL.sql` at various points — all now superseded), outside the pattern used by the schema's other four dashboard RPCs (`get_hourly_stats`, `get_weekly_stats`, `get_department_presence`, `get_dashboard_stats`), which all correctly check `org_members` membership. This one never got that check.

**Impact**: it is `SECURITY DEFINER` and granted to the `authenticated` role. Any signed-in user — including a newly self-registered trial account — could call it with an arbitrary `org_id` and read another tenant's full attendance history, including photo storage paths.

**Fix**: added the identical membership guard already used by the other four RPCs, via `CREATE OR REPLACE FUNCTION`. Query logic otherwise unchanged (kept the `FULL OUTER JOIN` version, the most complete of the three prior iterations).

### 3. Unverified caller identity in admin-initiated enrollment — Moderate

**Location**: `supabase/functions/start-enrollment/index.ts`, `dashboard/index.html`

**How it happened**: same 2026-02-28 session, one step better than issue #1 — commit message `Simplify auth: send user_id in body, verify org membership via service role`. The function did check `org_members`, but against a client-supplied `caller_user_id` field rather than a verified identity.

**Impact**: narrower than #1 — exploitation requires already knowing a real owner/admin's UUID for the *target* organization, which isn't exposed through any normal UI flow. Still the same class of bug.

**Fix**: same JWT-verification pattern as #1 and #2. Also discovered the dashboard's call to this endpoint sent **no `Authorization` header at all** prior to this fix — flagged as `TODO: Needs verification` against hardware testing whether admin-initiated enrollment was functioning before this change, or whether this fix repairs a latent breakage as a side effect.

## Reviewed, no issue found

- **Row-Level Security policies** (`supabase/migrations/001_complete_schema.sql`): every tenant-scoped table's policies correctly filter on `organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())`. `audit_logs` has no UPDATE/DELETE policy (correctly immutable). `payment_references` has no client-facing policy at all (service-role only). The three issues above were RPC/Edge-Function-level gaps that bypassed RLS via `SECURITY DEFINER` or the service-role key — the underlying RLS design itself is sound.
- **`paystack-webhook`**: verifies `x-paystack-signature` via HMAC-SHA512, then independently re-verifies the transaction against Paystack's own API before trusting the webhook payload, with idempotency via a `payment_references` table keyed on transaction reference. This is correctly implemented.
- **`submit-log` and `device-login`**: both rate-limited (`checkRateLimit()` — 60/min per device on `submit-log`, brute-force protection on `device-login`).

## Deferred — not fixed in v1.0.0, tracked for Phase 4.1

### Device secrets are stored and compared in plaintext

**Location**: `supabase/functions/_shared/auth.ts` (`authenticateDevice()`: `deviceSecret === device.device_secret`), `supabase/functions/device-provision/index.ts` (`const hashedSecret = rawSecret` — the variable name is misleading; nothing is hashed), and the firmware's NVS flash storage.

**Severity**: Moderate. Not remotely exploitable today the way issues #1–#3 were — RLS blocks every client role from reading the `devices.device_secret` column, so exploitation requires database-level access, not just an API call. It matters as defense-in-depth and in the event of any future database-access incident.

**Why it's deferred rather than fixed now**: this is the authentication path for the entire live device fleet (`_shared/auth.ts` is imported by `submit-log`, `get-users`, `device-login`, `device-enroll`, `check-enrollment`) — the highest-blast-radius file in the backend. The explicit decision, made jointly with the repo owner, was to isolate the hardware validation cycle to one variable at a time: deploy and validate issues #1–#3 (which change no firmware-facing behavior) first, confirm the physical fleet works end-to-end, *then* change the device authentication mechanism. See `CHANGELOG.md` and [`ROADMAP.md`](ROADMAP.md) for the Phase 4.1 plan.

**Planned fix (Phase 4.1, safe even for already-deployed devices)**: firmware always sends the plaintext secret it was issued at provisioning time — that never has to change. The server-side fix is to hash incoming secrets at comparison time and migrate existing `devices.device_secret` values to their hash in place; already-provisioned hardware keeps working with zero firmware changes.

### Firmware-side findings (require physical re-flash — out of scope for a backend-only release)

- Hardcoded WiFi fallback credentials in `wroom_brain.ino` (a real-looking SSID/password baked into firmware source as a default).
- An unauthenticated serial-console command (`PROVISION:<secret>`) that writes a device secret directly to NVS, bypassing the backend entirely, plus a `RESET` command that wipes all stored credentials — both require physical USB access to exploit.
- No TLS certificate validation configured on the firmware's HTTPS calls (relies on ESP32 Arduino-core `HTTPClient` defaults).

None of these are patched in v1.0.0 because doing so requires re-flashing every deployed device, which the project's explicit rule ("never break existing hardware") puts behind the hardware validation gate. See [`FIRMWARE.md`](FIRMWARE.md) §Known Limitations and [`ROADMAP.md`](ROADMAP.md).

### Dead Edge Functions with latent risk

`device-login`, `pair-device`, `claim-device`, `poll-claim` are deployed but called by nothing in the current dashboard or firmware — leftovers from abandoned provisioning-flow experiments (see [`PROVISIONING.md`](PROVISIONING.md) for that history). `poll-claim` in particular has no authentication and would return a plaintext WiFi password to anyone supplying a device MAC address, if the `device_claims` table it reads from were ever populated again. It is currently inert because nothing populates that table (the only function that writes to it, `claim-device`, is itself unreachable from the live product). Scheduled for removal in Phase 3 cleanup — see [`CLEANUP_REPORT.md`](CLEANUP_REPORT.md) — rather than patched, since deleting unused code is lower-risk than modifying it.

## Reporting

This is a single-maintainer project without a formal disclosure program at time of writing. If you find a security issue, open an issue in the repository or contact the maintainer directly rather than filing a public issue with exploit details.
