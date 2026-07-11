# Business Model

## Model: Hardware + Subscription

Decided direction (confirmed by the repo owner): customers buy a hardware kit, then pay a recurring subscription for the software/cloud platform behind it. Not a source-code sale, not hardware-only, not a pure managed-SaaS-with-owned-hardware model. This mirrors how Ubiquiti or Verkada sell to SMEs — hardware gets you in the door, subscription is the durable revenue.

```
Customer buys hardware kit → scans QR → device provisions → works in minutes → pays monthly
```

The subscription infrastructure for this is **already built**, not aspirational — `organizations`, `subscriptions`, and the Paystack billing integration are live in the schema and Edge Functions (see [`DATABASE.md`](DATABASE.md), [`EDGE_FUNCTIONS.md`](EDGE_FUNCTIONS.md)). This document describes what's already implemented, then makes recommendations for what isn't decided yet — the two are labeled separately throughout.

## Subscription tiers — already implemented

Pulled directly from `supabase/functions/create-checkout/index.ts` (`planPricing`) and `supabase/functions/paystack-webhook/index.ts` (`PLAN_CONFIG`), currency NGN (Paystack, Nigeria):

| Tier | Device limit | User limit | Retention | Monthly | Yearly |
|---|---|---|---|---|---|
| Starter | 1 | 50 | 180 days | ₦3,900 | ₦39,000 |
| Growth | 5 | 500 | 365 days | ₦12,900 | ₦129,000 |
| Enterprise | 999 | 99,999 | 730 days | ₦49,900 | ₦499,000 |

New organizations get a 14-day trial on the Starter tier automatically (`handle_new_organization()` trigger, `supabase/migrations/001_complete_schema.sql`). Yearly pricing is roughly a 17% discount versus monthly ×12 across all three tiers — consistent, not ad hoc.

**Observation, not yet a recommendation**: Starter's 1-device limit means a customer literally cannot test multi-device behavior without upgrading — worth knowing before offering trials to prospects who might want to demo more than one door/entrance, and worth remembering as the explanation the next time "device limit reached" looks like a bug during testing (see [`TESTING.md`](TESTING.md)).

## Hardware pricing — not yet decided, no figures exist in the codebase

The system prompt driving this documentation phase asked for pricing recommendations here. Being explicit about the distinction: everything in the table above is **already implemented and real**; everything below is a **recommendation**, not a decision, and should be treated as a starting point for the repo owner to price against actual component and assembly cost, which this document cannot see.

Recommended structure:
- **One-time hardware fee at cost-plus-margin**, not subsidized by subscription revenue — keeps unit economics simple for a solo operator without inventory financing. Once real BOM cost is known (see `TODO: Needs verification` items in [`HARDWARE.md`](HARDWARE.md)), price the kit at roughly 2–3× landed component + assembly cost, which is a common floor for low-volume hardware with support burden attached, not a formula specific to this product.
- **Bundle the first month or two of subscription into the hardware price** to reduce the perceived up-front cost and get the customer immediately into the recurring-revenue funnel rather than treating hardware and subscription as two separate purchase decisions.
- Consider whether the 14-day trial should require a card on file (reduces trial abuse, standard SaaS practice) — not currently enforced anywhere in the code; `subscriptions.status = 'trial'` is set automatically with no payment method requirement.

## Licensing — explicitly ruled out as the primary model

Per the repo owner's direction, this is not a source-code-licensing business. If white-labeling to other integrators is ever revisited, it would be a secondary channel, not a replacement for the hardware+subscription model above — not designed here, since it isn't the current direction.

## Future integrations (not built, listed for prioritization later)

Ordered by rough proximity to what already exists in the schema, not by business priority (that's the repo owner's call once there's real customer feedback):

- **Payroll export** — `attendance_logs` and the `get_smart_attendance` RPC already compute hours worked per day; a CSV/API export tailored to a specific payroll provider's import format is a relatively small addition on top of data that already exists.
- **SMS/email notifications** — no notification infrastructure exists today (no email/SMS provider integration in any Edge Function).
- **Microsoft Entra / Google Workspace sync** — would map onto the existing `users` table but requires a new sync mechanism; nothing today reads from or writes to any external directory.
- **Multi-vertical expansion** (hotel access, visitor management, etc.) — see [`ROADMAP.md`](ROADMAP.md) and `docs/hotel_rfid_access_solution.md` (a speculative, unimplemented proposal). This is a product-scope decision with real engineering cost (new tables, new firmware relay/lock control that doesn't exist today — see [`FIRMWARE.md`](FIRMWARE.md)), not a checkbox feature.

## What this document is not

Not a fundraising pitch, not a go-to-market plan, not a competitive analysis. It exists to keep pricing and packaging decisions traceable to what the code actually does, so a future collaborator (or the repo owner six months from now) can tell at a glance what's shipped versus what's still an idea.
