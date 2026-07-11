# Cleanup Report (Phase 3)

Inventory only — **nothing in this report has been deleted.** Each item is assessed for why it exists, whether it's still used, and the risk of removing it, with a recommendation. Removal happens only after the repo owner approves specific items, per the project's workflow rules.

**Update**: all root-level markdown files referenced below (previously "repo root") were physically relocated into `docs/` as a repository-hygiene pass — this report's "still used / risk / recommendation" analysis is unchanged, only the location tags below have been corrected. Relocation is not the same as the removal this report still recommends for several of them; a file being in `docs/` doesn't mean it's been reviewed and kept, only that the repo root is no longer cluttered with it while the decision is pending.

## Dead Edge Functions

### `supabase/functions/device-login/index.ts`

- **Why it exists**: `TODO: Needs verification` — no commit message or doc explains its original purpose. Most plausibly an early, simpler "just check credentials" endpoint that predates the per-endpoint `authenticateDevice()` pattern now used consistently by `submit-log`, `get-users`, `device-enroll`, `check-enrollment`.
- **Still used**: No. Confirmed by grepping `firmware/wroom_brain/wroom_brain.ino` for its endpoint — not called. It *is* still deployed via `deploy-functions.bat`, but "deployable" and "called by any client" are different facts.
- **Risk of removal**: Low. No client references it. It does correctly implement rate-limiting and audit logging, so if there's a future use case for a lightweight credential-check-only endpoint, this is a reasonable template to keep as reference before deleting outright.
- **Recommendation**: Remove from `deploy-functions.bat`; either delete the function or explicitly mark it `@deprecated` in a header comment pending one more confirmation that nothing external (e.g., a monitoring script) calls it.

### `supabase/functions/pair-device/index.ts`

- **Why it exists**: part of the "MAC-based pairing codes" provisioning flow, built and reverted within about 48 hours (`git log`, see `docs/PROVISIONING.md`).
- **Still used**: No — not called by current dashboard or firmware. Also **broken**: it queries `organizations.subscription_status` and `organizations.plan_type` columns that don't exist in the schema (subscription data lives in the separate `subscriptions` table). It would error if invoked today.
- **Risk of removal**: None functionally (it doesn't work) — the only risk is losing the historical reference to that provisioning design, which `docs/PROVISIONING.md` now preserves in prose.
- **Recommendation**: Remove. It's simultaneously dead and broken; there's no scenario where keeping it deployed helps.

### `supabase/functions/claim-device/index.ts`

- **Why it exists**: part of the "auto-discovery/claim" provisioning flow (`AUTO_DISCOVERY_PROVISIONING.md`), also reverted.
- **Still used**: No. Correctly implements JWT verification (unlike the functions that needed security fixes) — it's a reasonable auth-pattern reference, just unused.
- **Risk of removal**: Low. Its only write target, `device_claims`, has no other writer, so removing it doesn't orphan data anyone depends on.
- **Recommendation**: Remove alongside `poll-claim` (its counterpart) and the `device_claims` table.

### `supabase/functions/poll-claim/index.ts`

- **Why it exists**: the device-side half of the auto-discovery flow — polls for a claim by MAC address.
- **Still used**: No, and unlike the other three orphans, this one has an active latent risk if reactivated: **no authentication at all**, and would return a plaintext WiFi password to anyone supplying a device MAC address (MACs broadcast unencrypted in WiFi probe requests). See `docs/SECURITY.md`.
- **Risk of removal**: None — removing an unauthenticated endpoint that leaks credentials-on-populate is strictly a risk reduction.
- **Recommendation**: Remove. Priority above the other three orphans given the latent credential-exposure design, even though it's currently inert (nothing populates `device_claims`).

### `device_claims` table (`supabase/migrations/002_device_claims.sql`)

- **Why it exists**: backing store for the abandoned auto-discovery flow.
- **Still used**: No — only written by `claim-device` (unused) and read by `poll-claim` (unused).
- **Risk of removal**: Low, but this is a schema change (a `DROP TABLE`), which carries more weight than deleting an Edge Function — recommend doing this as its own reviewed migration, not bundled with the Edge Function removals.
- **Recommendation**: Drop in a dedicated migration once the three functions above are confirmed removed and redeployed cleanly.

## Stale documentation

### `AUTO_DISCOVERY_PROVISIONING.md`, `SIMPLIFIED_PROVISIONING.md`, `FINAL_PROVISIONING_FLOW.md` (`docs/`)

- **Why they exist**: design docs for the two abandoned provisioning flows above, written 2026-03-01.
- **Still used**: No — describe flows that were built and reverted. `docs/PROVISIONING.md` now covers this history in one place, correctly labeled as historical.
- **Risk of removal**: None — content is preserved (accurately, and more concisely) in `docs/PROVISIONING.md`.
- **Recommendation**: Remove all three. If the repo owner wants to preserve the original documents for sentimental/historical reasons, `git log` already preserves them permanently — deleting from the working tree doesn't lose them.

### `ESP32-WROOM BRAIN FIRMWARE.md`, `ESP32-CAM SLAVE FIRMWARE.md` (`docs/`)

- **Why they exist**: appear to be verbatim source snapshots of an earlier, pre-Supabase firmware generation, committed as `.md` files rather than maintained as prose documentation.
- **Still used**: No — actively misleading if read as current (different auth scheme, different endpoints, watchdog shown as enabled where it's now disabled). `docs/FIRMWARE.md` is the current, verified replacement.
- **Risk of removal**: None for accuracy; same `git log` preservation argument as above.
- **Recommendation**: Remove both.

### `deployment_guide.md` (`docs/`)

- **Why it exists**: an earlier deployment runbook (Nginx or PM2-managed `http-server`, `/var/www/ecotronics/`).
- **Still used**: No — contradicts the actual, current Caddy/static-file setup documented (with the literal Caddyfile) in `VPS_RECOVERY_GUIDE.md`. Following this doc's instructions today would misconfigure the server.
- **Risk of removal**: Low. `docs/DEPLOYMENT.md` is now the current reference and explicitly calls out this contradiction so nobody who's already read it gets confused.
- **Recommendation**: Remove, or at minimum prepend a large "SUPERSEDED — see docs/DEPLOYMENT.md" banner if the repo owner wants to keep it for historical reference instead of deleting.

### `WIKI.md` (`docs/`, 56KB)

- **Why it exists**: a broad, largely accurate technical reference — the most trustworthy of the pre-existing root docs based on this review.
- **Still used**: Partially. Most of its content is now superseded by the more precise, individually-verified files in `docs/` (this cleanup effort's whole purpose), but it may still contain detail not yet ported over — it wasn't re-verified line-by-line against source the way the new `docs/` set was.
- **Risk of removal**: **Medium** — unlike the other items in this report, deleting this without a careful diff against the new `docs/` set could lose real information. This is the one item in this report that should not be removed on the strength of this report alone.
- **Recommendation**: Do not delete yet. Do a side-by-side comparison against `docs/ARCHITECTURE.md`, `docs/DATABASE.md`, `docs/EDGE_FUNCTIONS.md`, and `docs/FIRMWARE.md` first; port over anything genuinely missing, then archive.

### `plan.md` (`docs/`, 78KB)

- **Why it exists**: identified during initial review as three unrelated documents concatenated together — a stale Flask/Railway/OLED-hardware MVP plan, a generic React-scaffolding tutorial that doesn't match this project, and the actual current Supabase migration plan (the only accurate third).
- **Still used**: The current-architecture third is now superseded by `docs/ARCHITECTURE.md` and `docs/DESIGN_DECISIONS.md`. The other two-thirds were never accurate to this project.
- **Risk of removal**: Low for the two stale sections; the current-architecture section carries the same "verify before deleting" caution as `WIKI.md`, on a smaller scale.
- **Recommendation**: Split rather than blanket-delete: discard the Flask/OLED and React-tutorial sections outright (they describe a different project), confirm the Supabase-migration section is fully captured in `docs/`, then remove.

### `homepage.md` (`docs/`, 50KB)

- **Why it exists**: despite the name suggesting marketing content, this is a verbatim copy of `dashboard/index.html`'s source — not documentation, not marketing copy.
- **Still used**: No — it's a stale duplicate of a file that already exists and is actively maintained at `dashboard/index.html`.
- **Risk of removal**: None — it's a copy, not a source of unique information.
- **Recommendation**: Remove.

### `FIX.MD` (`docs/`, 19KB)

- **Why it exists**: despite the name, this is not a changelog — it's a raw `information_schema` dump plus pasted debugging-session notes from one specific test session.
- **Still used**: No unique current information; the schema dump is superseded by `docs/DATABASE.md` (verified against the actual migration SQL, not a runtime dump that may drift).
- **Risk of removal**: Low.
- **Recommendation**: Remove, or fold any still-relevant "known risks" notes into `docs/ROADMAP.md`/`docs/SECURITY.md` first if the repo owner wants to double-check nothing there was missed.

### `smart_attendance_view.sql`, `FIX_ATTENDANCE_SQL.sql` (repo root — SQL scripts, not markdown, not moved)

- **Why they exist**: two earlier iterations of the `get_smart_attendance` function, applied manually via the SQL editor before `supabase/migrations/004_secure_smart_attendance.sql` made it a tracked, security-fixed migration.
- **Still used**: No — superseded by migration 004, which is now the authoritative definition (`CREATE OR REPLACE FUNCTION` means whichever ran last in the database wins, and 004 is meant to be applied last).
- **Risk of removal**: Low, but **do not remove until migration 004 is confirmed deployed** — keeping them around costs nothing and is a safety net until the new version is verified live.
- **Recommendation**: Remove once `docs/RELEASE_NOTES_v1.0.0.md`'s deployment step confirming migration 004 is live has actually happened.

### `audit.md` (`docs/`, 32KB)

- **Why it exists**: a prior self-audit performed at the time of the Node.js→Supabase migration, with concrete findings that map directly onto what became the current schema.
- **Still used**: As a historical record, yes — it documents *why* several current design choices (per-org-scoped RFID uniqueness, `ON CONFLICT` idempotency, RLS everywhere) exist, which is genuinely useful context that `docs/DESIGN_DECISIONS.md` and `docs/LESSONS_LEARNED.md` reference but don't fully restate.
- **Risk of removal**: Medium — this is a "why" document, not a "how it currently works" document; deleting it loses institutional memory that isn't duplicated anywhere else.
- **Recommendation**: Keep, but move to a clearly historical location (e.g. `docs/history/audit-2026-02.md`) rather than sitting flat in `docs/` alongside the current reference set, so it doesn't read as current documentation.

## Schema hygiene (not deletions — numbering/tracking issues)

- **Duplicate migration numbering**: `supabase/migrations/002_admin_enrollment.sql` and `supabase/migrations/002_device_claims.sql` both use `002`. They don't currently conflict (independent changes), but this is fragile — a future migration tool that enforces strict ordering could break. **Recommendation**: renumber one on the next migration touch (e.g., rename `002_device_claims.sql` → `002b_device_claims.sql` or bump subsequent files) — cosmetic, no rush, but worth fixing before it causes a real problem.
- **`storage_policies.sql` (repo root — SQL script, not markdown, not moved)**: still accurate and current, but lives outside `supabase/migrations/` as a manually-applied script. **Recommendation**: fold into a proper numbered migration so it's applied automatically by `supabase db push` instead of requiring a manual SQL-editor step.
- **`supabase/.temp/cli-latest`**: tracked in git despite being a Supabase CLI version cache file, not project source. **Recommendation**: add `supabase/.temp/` to `.gitignore` and remove it from tracking. Harmless either way, but it's noise.

## What this report deliberately does not touch

`docs/hotel_rfid_access_solution.md` — a speculative, unimplemented hotel-access proposal. It isn't stale (nothing in it claims to describe current behavior), it's forward-looking, so it doesn't belong in this cleanup report. Recommend adding a one-line header noting it's speculative/unimplemented so it doesn't get mistaken for a current feature, but that's an edit, not a removal — out of scope for Phase 3 as defined.

## Summary table

| Item | Recommendation | Risk |
|---|---|---|
| `device-login`, `pair-device`, `claim-device`, `poll-claim` Edge Functions | Remove | Low (`poll-claim`: removal reduces risk) |
| `device_claims` table | Remove (own migration) | Low |
| 3 abandoned provisioning docs | Remove | None |
| 2 stale firmware snapshot docs | Remove | None |
| `deployment_guide.md` | Remove or banner | Low |
| `WIKI.md` | **Diff against `docs/` first, then archive** | Medium |
| `plan.md` | Split: discard 2/3, verify 1/3, then remove | Low–Medium |
| `homepage.md` | Remove | None |
| `FIX.MD` | Remove | Low |
| `smart_attendance_view.sql`, `FIX_ATTENDANCE_SQL.sql` | Remove after migration 004 confirmed live | Low |
| `audit.md` | Keep, relocate to `docs/history/` | Do not remove |
| Duplicate `002` migration numbering | Renumber | Cosmetic |
| `storage_policies.sql` | Fold into a numbered migration | Cosmetic |
| `supabase/.temp/` tracked in git | Gitignore + untrack | Cosmetic |

**Nothing above is removed by this report.** Confirm which rows to act on.
