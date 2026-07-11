# Gateman — API Reference

All endpoints are Supabase Edge Functions (Deno), deployed under:

```
https://<project-ref>.supabase.co/functions/v1/<function-name>
```

All are `POST`-only (CORS preflight `OPTIONS` is handled by every function via `handleCors()`/inline equivalents; no other HTTP methods are implemented). All responses are `application/json`.

This is a hand-written reference derived directly from `supabase/functions/*/index.ts`. For schema-level detail (tables, RLS, RPCs) see `docs/DATABASE.md`. For the auth/authorization narrative and known gaps, see `docs/SECURITY.md`.

## Conventions used below

- **Auth header requirements** — every request to a Supabase Edge Function must pass the Supabase gateway's own check. In this codebase that is always an `apikey` header carrying the project's anon key, confirmed from actual firmware/dashboard call sites (`firmware/wroom_brain/wroom_brain.ino`, `dashboard/index.html`), which send both `apikey` and `Authorization: Bearer <anon-key-or-session-token>` on every call. This is **separate from and in addition to** each function's own application-level authentication described per-endpoint below (device secret in body, or a real user session JWT).
- `TODO: Needs verification` is used wherever the repo does not contain enough information to state a fact with confidence — no `supabase/config.toml` is present in this repo, so per-function gateway JWT-verification settings (`verify_jwt`) are not visible in source and could not be confirmed.
- Only status codes actually `return`ed by the function's code are listed.

---

## Device Endpoints

Called by the ESP32 firmware (`firmware/wroom_brain/wroom_brain.ino`). Application-level identity is `device_uid` + `device_secret`, sent as JSON body fields and checked against the `devices` table by `authenticateDevice()` (plaintext comparison — see `docs/SECURITY.md`). Gateway header: `apikey: <SUPABASE_ANON_KEY>`, `Authorization: Bearer <SUPABASE_ANON_KEY>` (the firmware uses the anon key, not a per-device credential, to satisfy the gateway layer).

### `POST /functions/v1/device-provision`

First-boot provisioning. No device credentials exist yet — authorization is possession of a valid single-use token.

**Request body**
```json
{
  "device_uid": "AA:BB:CC:DD:EE:FF",
  "provisioning_token": "a1b2c3d4e5f6..."
}
```

**Response `200`**
```json
{
  "device_secret": "550e8400-...-...-...  -...-...",
  "device_id": "uuid",
  "supabase_url": "https://<project-ref>.supabase.co"
}
```
`device_secret` is plaintext and returned exactly once; the firmware persists it to NVS.

| Status | Meaning |
|---|---|
| 200 | Provisioned; secret issued |
| 400 | Missing `device_uid` or `provisioning_token` |
| 401 | Token invalid or expired |
| 403 | No active subscription / trial expired / device limit reached |
| 409 | Device already provisioned, or token already used (lost a provisioning race) |
| 500 | Internal error |

### `POST /functions/v1/submit-log`

Per-event attendance sync. Idempotent on `(device_uid → device_id, device_event_id)`.

**Request body**
```json
{
  "device_uid": "AA:BB:CC:DD:EE:FF",
  "device_secret": "...",
  "device_event_id": "AA:BB:CC:DD:EE:FF-1720000000-42",
  "credential_value": "04A1B2C3",
  "event_time": "2026-07-11T08:15:00.000Z",
  "action": "check_in",
  "photo_base64": "/9j/4AAQSk...(optional)",
  "photo_mime": "image/jpeg"
}
```
`action` must be exactly `"check_in"` or `"check_out"`. `event_time` must be within 7 days in the past and 5 minutes in the future of server time.

**Response `200`**
```json
{ "status": "ok", "inserted": true, "log_id": "uuid" }
```
or, on a duplicate replay of the same `device_event_id`:
```json
{ "status": "ok", "inserted": false, "log_id": null }
```

| Status | Meaning |
|---|---|
| 200 | Accepted (new insert or idempotent duplicate) |
| 400 | Missing required field, or `action` not `check_in`/`check_out`, or invalid `event_time` |
| 401 | Invalid device credentials |
| 403 | Subscription inactive/expired |
| 422 | `event_time` outside the accepted window |
| 429 | Rate limit exceeded (60 submissions/minute/device) |
| 500 | Internal error |

### `POST /functions/v1/get-users`

Bulk roster + RFID credential sync for the device's local cache.

**Request body**
```json
{ "device_uid": "AA:BB:CC:DD:EE:FF", "device_secret": "..." }
```

**Response `200`**
```json
{
  "users": [
    { "user_id": "uuid", "name": "Jane Doe", "employee_id": "EMP001", "department": "Ops", "rfid_uid": "04A1B2C3" }
  ],
  "device_id": "uuid"
}
```
Only users with an assigned RFID credential are included; active users without one are omitted.

| Status | Meaning |
|---|---|
| 200 | OK |
| 400 | Missing `device_uid`/`device_secret` |
| 401 | Invalid device credentials |
| 403 | Subscription inactive |
| 500 | Internal error |

### `POST /functions/v1/device-enroll`

Registers a new RFID credential — either completing an admin-initiated `waiting` enrollment (pass `enrollment_id`) or creating a legacy `pending` enrollment (omit it).

**Request body**
```json
{
  "device_uid": "AA:BB:CC:DD:EE:FF",
  "device_secret": "...",
  "credential_value": "04A1B2C3",
  "credential_type": "rfid",
  "photo_base64": "(optional)",
  "enrollment_id": "uuid (optional)"
}
```

**Response `200`** — one of:
```json
{ "status": "already_assigned", "credential_value": "04A1B2C3" }
{ "status": "enrolled", "enrollment_id": "uuid", "credential_value": "04A1B2C3", "assigned_to": "uuid" }
{ "status": "already_pending", "enrollment_id": "uuid" }
{ "status": "ok", "enrollment_id": "uuid" }
```

| Status | Meaning |
|---|---|
| 200 | See response variants above |
| 400 | Missing `device_uid`/`device_secret`/`credential_value` |
| 401 | Invalid device credentials |
| 403 | `enrollment_id` belongs to a different organization than the device |
| 404 | `enrollment_id` given but no matching `waiting` row found |
| 500 | Insert failure / internal error |

### `POST /functions/v1/check-enrollment`

Device polls this to discover an admin-initiated `waiting` enrollment.

**Request body**
```json
{ "device_uid": "AA:BB:CC:DD:EE:FF", "device_secret": "..." }
```

**Response `200`**
```json
{ "enroll": false }
```
or
```json
{ "enroll": true, "enrollment_id": "uuid", "assigned_to": "uuid", "credential_type": "rfid" }
```

| Status | Meaning |
|---|---|
| 200 | OK (either shape above) |
| 400 | Missing `device_uid`/`device_secret` |
| 401 | Invalid device credentials |
| 500 | Internal error |

---

## Dashboard Endpoints

Called by the dashboard client (`dashboard/index.html`) using a real Supabase Auth session. Header: `apikey: <SUPABASE_ANON_KEY>`, `Authorization: Bearer <session.access_token>`. Each function independently verifies the JWT via `supabase.auth.getUser()` and then checks `org_members.role IN ('owner', 'admin')` for the `organization_id` in the request body — this membership check is enforced in application code on every call, not only at the database layer.

### `POST /functions/v1/create-provision-token`

Confirmed call site: `dashboard/index.html`. Mints a single-use, 10-minute device provisioning token.

**Request body**
```json
{ "organization_id": "uuid", "device_name": "Front Entrance (optional)" }
```

**Response `200`**
```json
{
  "token": "32-hex-char-string",
  "expires_at": "2026-07-11T08:25:00.000Z",
  "qr_payload": "{\"token\":\"...\",\"url\":\"https://<project-ref>.supabase.co\"}",
  "provision_url": "https://<project-ref>.supabase.co/functions/v1/device-provision"
}
```

| Status | Meaning |
|---|---|
| 200 | Token issued |
| 400 | Missing `organization_id` |
| 401 | Missing/invalid Authorization header or session |
| 403 | Caller is not owner/admin of the org, no active subscription, trial expired, or device limit reached |
| 429 | Rate limit exceeded (10 tokens/org/10min, Phase 4 addition) |
| 500 | Internal error |

### `POST /functions/v1/start-enrollment`

Confirmed call site: `dashboard/index.html`. Admin initiates card enrollment for a named employee on a named device.

**Request body**
```json
{ "user_id": "uuid", "device_id": "uuid", "organization_id": "uuid" }
```

**Response `200`**
```json
{ "status": "waiting", "enrollment_id": "uuid" }
```

| Status | Meaning |
|---|---|
| 200 | Waiting enrollment created |
| 400 | Missing `user_id`/`device_id`/`organization_id` |
| 401 | Missing/invalid Authorization header or session |
| 403 | Caller is not owner/admin of the org |
| 404 | `device_id` not found, not in this org, or not `active` |
| 429 | Rate limit exceeded (20 starts/org/5min, Phase 4 addition) |
| 500 | Internal error |

### `POST /functions/v1/create-checkout`

Same JWT + membership pattern as above. Initializes a Paystack transaction for a subscription plan.

**Request body**
```json
{
  "organization_id": "uuid",
  "plan_type": "starter",
  "billing": "monthly",
  "callback_url": "https://app.example.com/billing/callback (optional)"
}
```
`plan_type` ∈ `starter` | `growth` | `enterprise`; `billing` ∈ `monthly` | `yearly` (defaults to `monthly`).

**Response `200`**
```json
{
  "checkout_url": "https://checkout.paystack.com/...",
  "reference": "T123456789",
  "access_code": "..."
}
```

| Status | Meaning |
|---|---|
| 200 | Checkout session created |
| 400 | Missing `organization_id`/`plan_type`, or invalid plan+billing combination |
| 401 | Missing/invalid Authorization header or session |
| 403 | Caller is not owner/admin of the org |
| 500 | `PAYSTACK_SECRET_KEY` not configured server-side |
| 502 | Paystack rejected the initialize request |

`TODO: Needs verification` — no call site for this endpoint was found in `dashboard/index.html`; it may be invoked from a billing surface outside this file, or currently unused. Documented here because its request/response contract is stable and well-defined regardless.

---

## Webhook Endpoints

### `POST /functions/v1/paystack-webhook`

Called by Paystack's servers, not by the dashboard or firmware. Auth is an HMAC-SHA512 signature over the raw request body, keyed by `PAYSTACK_SECRET_KEY`, sent as the `x-paystack-signature` header. `TODO: Needs verification` whether Supabase's gateway-level JWT check is disabled for this function (no `supabase/config.toml` is present in this repo to confirm `verify_jwt` settings) — if it is not disabled, Paystack would additionally need to supply a valid `apikey`/`Authorization`, which is not evidenced in this codebase.

**Request headers**
```
x-paystack-signature: <hex-encoded HMAC-SHA512 of the raw body>
```

**Request body** — raw Paystack event payload, e.g.:
```json
{
  "event": "charge.success",
  "data": {
    "reference": "T123456789",
    "status": "success",
    "amount": 390000,
    "metadata": { "organization_id": "uuid", "plan_type": "starter", "billing": "monthly", "user_id": "uuid" }
  }
}
```

**Response `200`**
```json
{ "success": true }
```
Returned once signature verification passes, regardless of whether the specific `event` value was one of the three handled types (unrecognized events are logged and ignored, not rejected).

**Events handled**: `charge.success` (re-verified against Paystack's `transaction/verify/{reference}` API before activating/renewing the subscription; idempotent via the `payment_references` table), `subscription.disable` / `subscription.not_renew` (marks `cancelled`), `invoice.payment_failed` (marks `past_due`).

| Status | Meaning |
|---|---|
| 200 | Signature valid; event processed or ignored |
| 400 | Missing `x-paystack-signature` header |
| 401 | Signature does not match computed HMAC |
| 500 | `PAYSTACK_SECRET_KEY` not configured, or internal error |

---

## Unused Endpoints (not called by any current client)

The following exist in `supabase/functions/` but have no confirmed caller in `dashboard/index.html` or `firmware/wroom_brain/wroom_brain.ino`. They are documented here only so an engineer grepping the codebase doesn't mistake them for active surface area. See `docs/EDGE_FUNCTIONS.md` for full detail.

| Endpoint | Intended auth | Note |
|---|---|---|
| `POST /functions/v1/device-login` | `device_uid`+`device_secret` in body | Rate limited (5 failed/5min); fully functional but unreferenced by firmware, which authenticates per-call instead of via a separate login step |
| `POST /functions/v1/pair-device` | Supabase JWT | **Would error if invoked** — queries `organizations.subscription_status`/`organizations.plan_type`, columns that do not exist in the schema (subscription data lives in the separate `subscriptions` table) |
| `POST /functions/v1/claim-device` | Supabase JWT | Writes device pairing info, including plaintext WiFi password, to the `device_claims` table |
| `POST /functions/v1/poll-claim` | none | Unauthenticated; returns plaintext WiFi credentials from `device_claims` for a given MAC address |
