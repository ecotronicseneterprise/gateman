# Lessons Learned

This document exists because the incidents below are visible in `git log` and worth learning from explicitly, rather than quietly fixing and moving on. Each entry: what happened, the concrete evidence, and the practice that would have prevented it.

## 1. A debugging workaround shipped to production and stayed there for months

**What happened**: while fighting a Supabase gateway issue (the `Authorization` header being stripped somewhere in the request path), a commit titled `REMOVE ALL AUTH - just require organization_id to generate token` removed authentication from `create-provision-token` entirely, to unblock debugging. It was never reverted. It stayed live — reachable by anyone, no login required — from 2026-02-28 until this documentation/security pass caught it.

**Evidence**: `git log` on `supabase/functions/create-provision-token/index.ts`.

**Practice going forward**: a debugging workaround that removes a security check needs a tracked follow-up (an issue, a `TODO` with a date, anything) before the debugging session ends — not just a mental note. If the fix works and the auth check doesn't come back in the same session, that's the moment to write down "restore auth here" somewhere that survives context-switching.

## 2. Three provisioning UX designs were built before one was chosen

**What happened**: token+QR provisioning → auto-discovery/claim flow → MAC-based pairing codes → reverted back to token+QR, all within about 48 hours per commit timestamps. Each abandoned attempt left Edge Functions (`pair-device`, `claim-device`, `poll-claim`), a database table (`device_claims`), and documentation (`AUTO_DISCOVERY_PROVISIONING.md`, `SIMPLIFIED_PROVISIONING.md`, `FINAL_PROVISIONING_FLOW.md`) behind — none of it removed when the direction changed.

**Evidence**: `git log` timestamps on those files; confirmed dead by grepping actual call sites in the current dashboard and firmware.

**Practice going forward**: when an approach is abandoned, remove or explicitly archive its code and docs in the same session — not "later." The cost of leaving it wasn't just clutter: `pair-device` also turned out to reference schema columns that don't exist, meaning it was broken as well as unused, and nobody noticed because nothing exercised it. See [`CLEANUP_REPORT.md`](CLEANUP_REPORT.md) for the actual removal, done properly this time — inventoried and approved before deletion, not silently dropped.

## 3. Documentation drifted from the code it described

**What happened**: at least three separate documents describe how the dashboard is deployed, and they contradict each other (`deployment_guide.md` says Nginx or PM2, `WIKI.md` implies PM2, the actual Caddyfile in `VPS_RECOVERY_GUIDE.md` shows neither). Similarly, `ESP32-WROOM BRAIN FIRMWARE.md` and `ESP32-CAM SLAVE FIRMWARE.md` are verbatim snapshots of a firmware generation that predates the current Supabase backend entirely — different auth scheme, different endpoints, watchdog enabled where it's now disabled.

**Evidence**: direct comparison of each doc against the live Caddyfile and the current `.ino` source, done as part of this documentation pass.

**Practice going forward**: a doc that's a snapshot of code at a point in time (rather than living documentation) should say so explicitly, with a date, so nobody mistakes it for current. Better: don't paste code snapshots into markdown files at all — link to the source instead.

## 4. A security incident's own root cause included pasting secrets into an AI chat

**What happened**: `VPS_RECOVERY_GUIDE.md` documents that the previous VPS was compromised, and lists among the contributing factors that real `.env` secrets were pasted into an AI chat session during troubleshooting, flagged in the document itself as needing rotation.

**Evidence**: `VPS_RECOVERY_GUIDE.md`, written by the repo owner during incident recovery.

**Practice going forward**: this document itself is being produced by an AI assistant with read access to the full repository, which makes this lesson directly relevant rather than abstract — real secrets (service role keys, Paystack keys, VPS credentials) should never appear in a prompt, a pasted file, or a chat transcript, ever, regardless of how much faster it would make debugging. Environment variables stay in `.env` files (gitignored) and provider secret managers, referenced by name in conversation, never by value.

## 5. RLS is not automatically applied everywhere — `SECURITY DEFINER` opts out of it

**What happened**: the core RLS policy design (org-scoped, checked on every table) is sound. But two RPCs and one Edge Function bypassed it — `SECURITY DEFINER` functions and service-role Edge Functions both run with elevated privilege by design, which means each one is individually responsible for re-implementing the access check that RLS would otherwise provide for free. One RPC (`get_smart_attendance`) was written directly in the SQL editor, outside the pattern the other four RPCs used, and simply never got that check.

**Practice going forward**: any new `SECURITY DEFINER` function or service-role Edge Function needs its membership/ownership check reviewed as carefully as if RLS didn't exist at all — because for that function, it doesn't.

## 6. What actually went right, worth repeating

Not everything here is a mistake to avoid — some patterns are worth deliberately reusing:

- The Paystack webhook (`paystack-webhook/index.ts`) does signature verification, independent re-verification against Paystack's API, and idempotency correctly, from the start. It's the template other webhook-style integrations should follow.
- The attendance-log idempotency design (`UNIQUE(device_id, device_event_id)` + `ON CONFLICT DO NOTHING`) correctly anticipates that firmware will retry syncs, and handles it without duplicate records — this was itself a fix for an earlier broken version (a fresh UUID on every insert meant the original `ON CONFLICT` could never trigger), and the fixed version is solid.
- The no-photo-fallback in the attendance flow (2 capture attempts, then proceed without a photo rather than blocking) is a good resilience pattern: prefer a degraded result over no result.
