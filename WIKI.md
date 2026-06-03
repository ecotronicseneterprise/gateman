# EcoTronics Gateman — Technical Wiki

> **Last generated:** 2026-05-08  
> **Codebase state:** v3 WROOM firmware, v2 CAM firmware, Supabase Edge Functions (Deno)

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Repository Structure](#2-repository-structure)
3. [Backend — Supabase Edge Functions](#3-backend--supabase-edge-functions)
4. [Firmware — WROOM Brain](#4-firmware--wroom-brain-wroom_brainino)
5. [Firmware — ESP32-CAM Slave](#5-firmware--esp32-cam-slave-esp32cam_slaveino)
6. [Dashboard](#6-dashboard-indexhtml)
7. [Data Flow — End to End](#7-data-flow--end-to-end)
8. [Deployment](#8-deployment)
9. [Known Limitations & TODOs](#9-known-limitations--todos)

---

## 1. Project Overview

### What the system does

Gateman is a multi-tenant SaaS employee attendance system built around commodity ESP32 hardware and Supabase as the cloud backend. When an employee taps an RFID card:

1. The **ESP32-WROOM** reads the card UID and tells the **ESP32-CAM** to take a photo.
2. The WROOM logs the event (locally if offline, to the CAM's SD card otherwise) and syncs it to **Supabase** via HTTPS.
3. Supabase resolves the RFID UID to an employee record, stores the timestamped attendance log, and uploads the photo to object storage.
4. The **web dashboard** shows the event in real time, lets admins manage employees, run reports, and export CSV.

### System Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                        FIELD HARDWARE                        │
│                                                              │
│  ┌─────────────────────────────────────────────────┐         │
│  │            ESP32-WROOM (Brain)                  │         │
│  │  RFID reader ── GPIO SPI ──► MFRC522            │         │
│  │  Status LED  ── GPIO2                           │         │
│  │  Enroll btn  ── GPIO4                           │         │
│  │  UART TX/RX  ── GPIO16/17 ──► ESP32-CAM         │         │
│  │  WiFi (STA)  ──────────────────────────────┐    │         │
│  │  SPIFFS      ── offline queue              │    │         │
│  │  NVS         ── credentials / time cache   │    │         │
│  └─────────────────────────────────────────────┘   │         │
│                                                    │         │
│  ┌──────────────────────────────┐                  │         │
│  │     ESP32-CAM (Slave)        │                  │         │
│  │  OV2640 ── 160×120 JPEG      │                  │         │
│  │  SD card ── /pending         │                  │         │
│  │           ── /synced         │                  │         │
│  │           ── /photos         │                  │         │
│  │  UART RX/TX ── GPIO12/13     │                  │         │
│  └──────────────────────────────┘                  │         │
└────────────────────────────────────────────────────┼─────────┘
                                                     │ HTTPS
┌────────────────────────────────────────────────────▼─────────┐
│                     SUPABASE CLOUD                            │
│                                                              │
│  Edge Functions (Deno)          PostgreSQL + RLS             │
│  ├── device-provision           ├── organizations            │
│  ├── device-login               ├── org_members              │
│  ├── submit-log ◄── per tap     ├── subscriptions            │
│  ├── get-users                  ├── devices                  │
│  ├── check-enrollment           ├── users (employees)        │
│  ├── device-enroll              ├── user_credentials         │
│  ├── start-enrollment           ├── attendance_logs          │
│  ├── create-provision-token     ├── enrollment_queue         │
│  ├── create-checkout            ├── audit_logs               │
│  └── paystack-webhook           └── payment_references       │
│                                                              │
│  Supabase Auth (JWT)     Storage: attendance-photos          │
└──────────────────────────────────────────────────────────────┘
                                    │ Supabase JS / REST
┌───────────────────────────────────▼──────────────────────────┐
│              DASHBOARD  (dashboard/index.html)                │
│  Vanilla JS + Supabase JS + Chart.js + QRCode.js             │
│  Pages: Dashboard · Employees · Attendance · Enrollment      │
│         Devices · Admin                                      │
└──────────────────────────────────────────────────────────────┘
```

### Technology Stack

| Layer | Technology |
|---|---|
| Hardware — brain | ESP32-WROOM-32 |
| Hardware — camera | ESP32-CAM (AI-Thinker, OV2640) |
| RFID reader | MFRC522 (SPI) |
| Firmware language | Arduino C++ (ESP-IDF underneath) |
| Local storage | SPIFFS (WROOM offline queue), SD MMC (CAM) |
| Cloud database | Supabase (PostgreSQL 15 + RLS) |
| Cloud functions | Supabase Edge Functions (Deno/TypeScript) |
| Auth | Supabase Auth (JWT, email/password) |
| Object storage | Supabase Storage (`attendance-photos` bucket) |
| Frontend | Vanilla JS SPA, no build step |
| Charts | Chart.js 4.4.1 |
| QR codes | QRCode.js 1.0.0 |
| Payments | Paystack (NGN) |
| Hosting (dashboard) | `serve` npm package on port 3000 / VPS at 46.225.186.103 |

---

## 2. Repository Structure

```
gateman/
│
├── package.json                  Root project — serves dashboard on port 3000 via `serve`
├── README.md                     Quick-start deployment guide
├── FIX.MD                        Live schema dump, deployment commands, status notes (working doc)
├── WIKI.md                       ← This file
│
├── dashboard/
│   └── index.html                Single-file SPA (~4 000 lines). All UI, auth, and API calls.
│
├── firmware/
│   ├── wroom_brain/
│   │   ├── wroom_brain.ino       ESP32-WROOM main controller firmware (1 192 lines)
│   │   └── provision_portal.h    Captive-portal HTML + DNS/HTTP server for first-time WiFi setup
│   └── esp32cam_slave/
│       └── esp32cam_slave.ino    ESP32-CAM slave firmware (669 lines)
│
├── supabase/
│   ├── migrations/
│   │   └── 001_complete_schema.sql   Complete Postgres schema: tables, indexes, RLS, triggers, RPCs
│   └── functions/
│       ├── _shared/
│       │   ├── auth.ts           Shared helpers: authenticateDevice, auditLog, rateLimiting, subscriptionCheck
│       │   └── cors.ts           CORS headers + jsonResponse / errorResponse utilities
│       ├── device-provision/     One-time token-based device registration
│       ├── device-login/         Device heartbeat (updates last_seen)
│       ├── submit-log/           Per-tap attendance log submission (idempotent)
│       ├── get-users/            Employee list download for firmware cache
│       ├── start-enrollment/     Admin initiates card enrollment command
│       ├── device-enroll/        Firmware submits captured card UID (admin-driven + legacy)
│       ├── check-enrollment/     Firmware polls for pending enrollment commands
│       ├── create-provision-token/  Dashboard generates QR provision token
│       ├── claim-device/         Device claim workflow
│       ├── pair-device/          Device pairing
│       ├── create-checkout/      Paystack checkout session creation
│       └── paystack-webhook/     Payment event handler (HMAC-verified)
│
└── public/
    ├── gateman_brand.html        Brand identity guide
    ├── gateman_primary.svg       Full logo (primary)
    ├── gateman_dark.svg          Dark-mode logo variant
    └── gateman_icon.svg          Icon/favicon
```

---

## 3. Backend — Supabase Edge Functions

All Edge Functions run on Deno in Supabase's managed environment. They are deployed with `--no-verify-jwt` so device credentials (not Supabase JWTs) can be used for authentication. The shared helpers in `_shared/` are imported by every function.

### 3.1 Authentication Mechanism

**Two authentication paths exist:**

| Path | Used by | Mechanism |
|---|---|---|
| Device auth | All firmware → cloud calls | `device_uid` (WiFi MAC) + `device_secret` (UUID pair) in JSON body |
| User auth | Dashboard → cloud calls | Supabase JWT (`Authorization: Bearer <token>`) |

Device auth is validated by `authenticateDevice()` in `_shared/auth.ts`, which queries the `devices` table and compares the plaintext secret. There is no hashing — the secret is stored and transmitted as plaintext.

### 3.2 API Endpoints

All base URLs are `{SUPABASE_URL}/functions/v1/`. The project URL is `https://ueobebsgheecclwcbigy.supabase.co`.

---

#### `POST /device-provision`

Register a new device using a single-use provisioning token.

**Auth:** Provisioning token (not JWT, not device secret)

**Request body:**
```json
{
  "device_uid": "AA:BB:CC:DD:EE:FF",
  "provisioning_token": "b4d3...d0e"
}
```

**Success response `200`:**
```json
{
  "device_secret": "uuid-uuid",
  "device_id": "uuid",
  "supabase_url": "https://ueobebsgheecclwcbigy.supabase.co"
}
```

**Logic:**
1. Validate token exists, not expired (`expires_at`), not used (`used_at IS NULL`)
2. Check device not already provisioned
3. Check org has not exceeded `device_limit`
4. Generate plaintext `device_secret` (UUID-UUID format)
5. Insert device record
6. Mark token as used (race-condition guard with `used_at` CAS)
7. Return credentials — only opportunity to receive the secret

---

#### `POST /device-login`

Lightweight heartbeat that updates `last_seen` on the device row.

**Auth:** Device (uid + secret in body)

**Request body:**
```json
{ "device_uid": "AA:BB:CC:DD:EE:FF", "device_secret": "uuid-uuid" }
```

**Success response `200`:**
```json
{ "status": "ok", "device_id": "uuid", "organization_id": "uuid" }
```

**Rate limit:** 5 failed attempts per 5 minutes.

---

#### `POST /submit-log`

Core attendance sync endpoint. Called once per tap event.

**Auth:** Device (uid + secret in body)

**Request body:**
```json
{
  "device_uid": "AA:BB:CC:DD:EE:FF",
  "device_secret": "uuid-uuid",
  "device_event_id": "AA:BB:CC:DD:EE:FF-1735000000-A1B2C3-42",
  "credential_value": "778D7506",
  "event_time": "2026-01-15T08:30:00Z",
  "action": "check_in",
  "photo_base64": "(optional JPEG base64, max ~20KB)",
  "photo_mime": "image/jpeg"
}
```

**Success responses:**
```json
{ "status": "ok", "inserted": true,  "log_id": "uuid" }   // new record
{ "status": "ok", "inserted": false, "log_id": null  }    // duplicate — safe to retry
```

**Error codes:** `400` missing fields, `401` bad credentials, `403` subscription inactive, `422` timestamp >7 days old or >5 min future, `429` rate limited (60/min), `500` DB error.

**Logic:**
1. Validate required fields + timestamp window
2. Authenticate device
3. Rate-limit check (60 submissions/min per device)
4. Check subscription active
5. Resolve `credential_value` → `user_id` via `user_credentials` table (returns `null` if unknown card — log still recorded)
6. `UPSERT` into `attendance_logs` with `ON CONFLICT (device_id, device_event_id) DO NOTHING` — safe to retry
7. Fire-and-forget: decode base64, upload to `attendance-photos/{org_id}/{device_id}/{log_id}.jpg`, update `photo_url`
8. Audit log

---

#### `POST /get-users`

Download the employee + RFID credential list for a device.

**Auth:** Device (uid + secret in body)

**Request body:**
```json
{ "device_uid": "AA:BB:CC:DD:EE:FF", "device_secret": "uuid-uuid" }
```

**Success response `200`:**
```json
{
  "users": [
    {
      "user_id": "uuid",
      "name": "Cherry Okafor",
      "employee_id": "EMP001",
      "department": "Engineering",
      "rfid_uid": "778D7506"
    }
  ],
  "device_id": "uuid"
}
```

---

#### `POST /check-enrollment`

Device polls every 30 seconds for admin-initiated enrollment commands.

**Auth:** Device (uid + secret in body)

**Request body:**
```json
{ "device_uid": "AA:BB:CC:DD:EE:FF", "device_secret": "uuid-uuid" }
```

**Response when command waiting:**
```json
{ "enroll": true, "enrollment_id": "uuid", "assigned_to": "uuid", "credential_type": "rfid" }
```

**Response when nothing pending:**
```json
{ "enroll": false }
```

---

#### `POST /device-enroll`

Firmware submits the card UID after catching a tap in enroll mode.

**Auth:** Device (uid + secret in body)

**Request body:**
```json
{
  "device_uid": "AA:BB:CC:DD:EE:FF",
  "device_secret": "uuid-uuid",
  "credential_value": "A1B2C3D4",
  "enrollment_id": "uuid",
  "photo_base64": "(optional)"
}
```

**Admin-driven flow:** Looks up the `enrollment_queue` record by `enrollment_id`, inserts a `user_credentials` row linking the card to the pre-assigned employee, sets `enrollment_queue.status = 'assigned'`.

**Legacy (device-initiated) flow:** If no `enrollment_id` provided, inserts an `enrollment_queue` record with `status = 'pending'` for admin to assign later.

---

#### `POST /start-enrollment`

Dashboard triggers an enrollment session on a specific device.

**Auth:** Supabase JWT (admin or owner role required)

**Request body:**
```json
{
  "user_id": "uuid",
  "device_id": "uuid",
  "organization_id": "uuid",
  "caller_user_id": "uuid"
}
```

**Success response `200`:**
```json
{ "status": "waiting", "enrollment_id": "uuid" }
```

**Logic:** Cancels any existing `status = 'waiting'` enrollments for this device, then inserts a new `enrollment_queue` row. The device discovers it on the next `/check-enrollment` poll (within 30 s).

---

#### `POST /create-provision-token`

Dashboard generates a short-lived token and QR code to provision a new device.

**Auth:** Supabase JWT (admin or owner)

**Request body:**
```json
{
  "device_name": "Main Entrance",
  "organization_id": "uuid",
  "user_id": "uuid"
}
```

**Success response `200`:**
```json
{
  "token": "a3f9...b1c2",
  "expires_at": "2026-01-15T08:40:00Z",
  "qr_payload": "...",
  "provision_url": "..."
}
```

Token is a 32-char hex string, valid for 10 minutes, single-use.

---

#### `POST /paystack-webhook`

Handles Paystack payment events. HMAC-SHA512 signature verified before processing.

**Events handled:**

| Event | Action |
|---|---|
| `charge.success` | Activate or renew subscription, update plan limits |
| `subscription.disable` | Set `status = 'cancelled'` |
| `invoice.payment_failed` | Set `status = 'past_due'` |

**Plan limits applied on activation:**

| Plan | Devices | Users | Retention |
|---|---|---|---|
| `starter` | 1 | 50 | 180 days |
| `growth` | 5 | 500 | 365 days |
| `enterprise` | 999 | 99 999 | 730 days |

Idempotency is enforced by inserting into `payment_references` before processing — duplicate webhook deliveries are silently rejected.

---

### 3.3 Database Schema

All tables live in the `public` schema. Every table has Row Level Security enabled. The `auth.users` table is managed by Supabase Auth.

#### `organizations`
| Column | Type | Notes |
|---|---|---|
| `id` | UUID PK | `gen_random_uuid()` |
| `name` | TEXT NOT NULL | Display name |
| `slug` | TEXT NOT NULL UNIQUE | URL-friendly identifier |
| `settings` | JSONB | Default `{}` |
| `created_at` | TIMESTAMPTZ | |
| `updated_at` | TIMESTAMPTZ | Auto-updated by trigger |

#### `org_members`
| Column | Type | Notes |
|---|---|---|
| `id` | UUID PK | |
| `organization_id` | UUID FK → organizations | CASCADE delete |
| `user_id` | UUID FK → auth.users | CASCADE delete |
| `role` | TEXT | `owner` / `admin` / `viewer` |
| `created_at` | TIMESTAMPTZ | |

Unique constraint: `(organization_id, user_id)`.

#### `subscriptions`
| Column | Type | Notes |
|---|---|---|
| `id` | UUID PK | |
| `organization_id` | UUID FK | |
| `plan_type` | TEXT | `starter` / `growth` / `enterprise` |
| `status` | TEXT | `active` / `past_due` / `cancelled` / `trial` |
| `device_limit` | INTEGER | Default 1 |
| `user_limit` | INTEGER | Default 50 |
| `retention_days` | INTEGER | Default 180 |
| `trial_ends_at` | TIMESTAMPTZ | Nullable |
| `current_period_start` | TIMESTAMPTZ | |
| `current_period_end` | TIMESTAMPTZ | Default now+30d |
| `paystack_reference` | TEXT | Nullable |
| `created_at` / `updated_at` | TIMESTAMPTZ | |

Unique partial index: one active/trial subscription per org.

#### `devices`
| Column | Type | Notes |
|---|---|---|
| `id` | UUID PK | |
| `organization_id` | UUID FK | |
| `device_uid` | TEXT UNIQUE | WiFi MAC address |
| `device_secret` | TEXT | Plaintext UUID-UUID pair |
| `name` | TEXT | Default `'New Device'` |
| `location` | TEXT | Nullable |
| `status` | TEXT | `active` / `inactive` / `revoked` |
| `firmware_version` | TEXT | Nullable |
| `last_seen` | TIMESTAMPTZ | Updated on heartbeat |
| `created_at` / `updated_at` | TIMESTAMPTZ | |

#### `provision_tokens`
| Column | Type | Notes |
|---|---|---|
| `id` | UUID PK | |
| `organization_id` | UUID FK | |
| `token` | TEXT UNIQUE | 32-char hex |
| `device_name` | TEXT | Nullable |
| `expires_at` | TIMESTAMPTZ | Default now+10min |
| `used_at` | TIMESTAMPTZ | Null = unused |
| `used_by_device_id` | UUID FK → devices | Nullable |
| `created_by` | UUID FK → auth.users | |
| `created_at` | TIMESTAMPTZ | |

#### `users` (employees)
| Column | Type | Notes |
|---|---|---|
| `id` | UUID PK | |
| `organization_id` | UUID FK | |
| `employee_id` | TEXT | Human-readable ID, unique per org |
| `name` | TEXT NOT NULL | |
| `department` | TEXT | Nullable |
| `email` | TEXT | Nullable |
| `active` | BOOLEAN | Default TRUE; soft-delete sets FALSE |
| `created_at` / `updated_at` | TIMESTAMPTZ | |

#### `user_credentials`
| Column | Type | Notes |
|---|---|---|
| `id` | UUID PK | |
| `user_id` | UUID FK → users | CASCADE delete |
| `organization_id` | UUID FK | |
| `type` | TEXT | `rfid` / `pin` / `fingerprint` / `face` |
| `value` | TEXT | The credential (e.g., RFID UID `778D7506`) |
| `created_at` | TIMESTAMPTZ | |

Unique constraint: `(organization_id, type, value)` — one card per org, prevents sharing.

#### `attendance_logs`
| Column | Type | Notes |
|---|---|---|
| `id` | UUID PK | |
| `organization_id` | UUID FK | |
| `device_id` | UUID FK → devices | `ON DELETE SET NULL` (preserves history) |
| `user_id` | UUID FK → users | `ON DELETE SET NULL` (preserves history) |
| `credential_value` | TEXT NOT NULL | Raw RFID UID |
| `action` | TEXT | `check_in` / `check_out` |
| `device_event_id` | TEXT NOT NULL | Format: `{mac}-{epoch}-{uid}-{counter}` |
| `timestamp` | TIMESTAMPTZ | Device-reported event time |
| `photo_url` | TEXT | Path in `attendance-photos` storage bucket |
| `synced_at` | TIMESTAMPTZ | When received by server |

Unique constraint: `(device_id, device_event_id)` — idempotency key.

#### `enrollment_queue`
| Column | Type | Notes |
|---|---|---|
| `id` | UUID PK | |
| `organization_id` | UUID FK | |
| `device_id` | UUID FK → devices | |
| `credential_type` | TEXT | Default `rfid` |
| `credential_value` | TEXT | The RFID UID (filled after card tap) |
| `photo_url` | TEXT | Nullable |
| `status` | TEXT | `pending` / `waiting` / `assigned` / `rejected` |
| `assigned_to` | UUID FK → users | Nullable |
| `created_at` | TIMESTAMPTZ | |
| `resolved_at` | TIMESTAMPTZ | Nullable |

Status lifecycle: `waiting` (admin requested) → `assigned` (card captured) or `rejected` (timeout/cancel). Legacy flow uses `pending` → `assigned`.

#### `audit_logs`
| Column | Type | Notes |
|---|---|---|
| `id` | UUID PK | |
| `organization_id` | UUID FK | |
| `actor_type` | TEXT | `user` / `device` / `system` |
| `actor_id` | TEXT | UUID of the actor |
| `action` | TEXT | e.g. `attendance.submitted`, `device.provisioned` |
| `resource_type` | TEXT | Nullable |
| `resource_id` | TEXT | Nullable |
| `metadata` | JSONB | Arbitrary context |
| `ip_address` | TEXT | Nullable |
| `created_at` | TIMESTAMPTZ | |

No UPDATE or DELETE RLS policies — immutable by design.

#### `payment_references`
| Column | Type | Notes |
|---|---|---|
| `id` | UUID PK | |
| `reference` | TEXT UNIQUE | Paystack transaction reference |
| `organization_id` | UUID FK | |
| `event_type` | TEXT | e.g. `charge.success` |
| `created_at` | TIMESTAMPTZ | |

No client RLS — service role only.

---

### 3.4 Database Functions (RPCs)

All RPCs are `SECURITY DEFINER` and validate org membership before executing.

| Function | Arguments | Returns | Purpose |
|---|---|---|---|
| `get_hourly_stats` | `org_id UUID` | `TABLE(hour TEXT, action TEXT, count BIGINT)` | Today's tap count grouped by hour and action |
| `get_weekly_stats` | `org_id UUID` | `TABLE(date DATE, unique_staff BIGINT, total_taps BIGINT)` | Last 7 days summary |
| `get_department_presence` | `org_id UUID` | `TABLE(department TEXT, present BIGINT)` | Today's check-ins by department |
| `get_dashboard_stats` | `org_id UUID` | `JSON` | `{total_employees, today_records, checked_in, devices}` |
| `get_smart_attendance` | `org_id UUID, from_date, to_date` | TABLE | First check-in and last check-out per employee per day, with `hours_worked` |

---

### 3.5 Triggers

| Trigger | Table | Event | Action |
|---|---|---|---|
| `on_org_created` | `organizations` | AFTER INSERT | Creates a 14-day trial `starter` subscription (1 device, 50 users, 180d retention) |
| `trg_organizations_updated` | `organizations` | BEFORE UPDATE | Sets `updated_at = NOW()` |
| `trg_devices_updated` | `devices` | BEFORE UPDATE | Sets `updated_at = NOW()` |
| `trg_users_updated` | `users` | BEFORE UPDATE | Sets `updated_at = NOW()` |
| `trg_subscriptions_updated` | `subscriptions` | BEFORE UPDATE | Sets `updated_at = NOW()` |

---

### 3.6 Environment Variables

These must be set in the Supabase Edge Function secrets panel:

| Variable | Used in | Description |
|---|---|---|
| `SUPABASE_URL` | All functions | Project URL (auto-injected by Supabase) |
| `SUPABASE_SERVICE_ROLE_KEY` | All functions | Service role key for bypassing RLS (auto-injected) |
| `PAYSTACK_SECRET_KEY` | `paystack-webhook`, `create-checkout` | Paystack secret for HMAC verification and API calls |

The `SUPABASE_ANON_KEY` is also embedded in the firmware and the dashboard (it is safe to expose — it only grants anon/authenticated access per RLS).

---

### 3.7 Storage

**Bucket:** `attendance-photos`  
**Access:** Private (signed URLs required for dashboard display)  
**Max file size:** 50 KB  
**Allowed MIME types:** `image/jpeg`  
**Path format:** `{org_id}/{device_id}/{log_id}.jpg`

---

## 4. Firmware — WROOM Brain (`wroom_brain.ino`)

### 4.1 Pin Assignments

| Pin | GPIO | Connected to |
|---|---|---|
| RFID SS (SDA) | 5 | MFRC522 SDA |
| RFID SCK | 18 | MFRC522 SCK |
| RFID MOSI | 23 | MFRC522 MOSI |
| RFID MISO | 19 | MFRC522 MISO |
| RFID RST | — | Tied to 3.3V (not software-controlled) |
| CAM RX | 16 | ESP32-CAM GPIO13 (CAM TX) |
| CAM TX | 17 | ESP32-CAM GPIO12 (CAM RX) |
| Enroll button | 4 | Push-button to GND, internal pullup |
| Status LED | 2 | On-board blue LED |

### 4.2 Key Constants

| Constant | Value | Meaning |
|---|---|---|
| `QUEUE_SIZE` | 50 | Circular in-RAM attendance queue |
| `MAX_USERS` | 100 | Max employees cached in RAM |
| `CAM_TIMEOUT_MS` | 8 000 ms | Timeout waiting for CAM CAPTURE response |
| `DUPLICATE_WINDOW` | 5 000 ms | Ignore same card within 5 s |
| `HEARTBEAT_MS` | 60 000 ms | Interval between CAM PING/PONG checks |
| `ENROLL_POLL_MS` | 30 000 ms | How often to poll `/check-enrollment` |
| `ENROLL_TIMEOUT_MS` | 60 000 ms | Auto-exit enroll mode after 60 s |
| `HTTP_TIMEOUT_MS` | 8 000 ms | Per-request HTTP timeout |
| `SYNC_MAX_PER_CYCLE` | 20 | Max records synced per `syncPendingLogs()` call |
| `OFFLINE_QUEUE_FILE` | `/queue.txt` | SPIFFS file for offline event buffer |
| `FALLBACK_EPOCH` | 1 767 225 600 | 2026-01-01 00:00:00 UTC — minimum valid timestamp |

### 4.3 NVS Credential Storage

Credentials are stored in two NVS namespaces:

**Namespace `"ecotron"`** (provisioning data):
| Key | Type | Content |
|---|---|---|
| `device_uid` | String | WiFi MAC (e.g. `AA:BB:CC:DD:EE:FF`) |
| `device_secret` | String | UUID-UUID secret from provisioning |
| `device_id` | String | Supabase device UUID |
| `supabase_url` | String | Full Supabase project URL |
| `event_ctr` | ULong | Monotonic counter for `device_event_id` generation |
| `last_epoch` | ULong64 | Last known good UTC epoch (time cache) |

**Namespace `"gateman"`** (portal data):
| Key | Type | Content |
|---|---|---|
| `wifi_ssid` | String | WiFi SSID entered in captive portal |
| `wifi_pass` | String | WiFi password entered in captive portal |
| `prov_token` | String | Provision token entered in captive portal |
| `needs_prov` | Bool | Flag: portal has data ready to provision |

`isProvisioned()` checks `device_secret` length > 0 in the `"ecotron"` namespace.

### 4.4 WiFi Connection Logic

```
connectWiFi():
  1. Load saved SSID/password from "gateman" NVS namespace
  2. Fall back to hardcoded WIFI_SSID / WIFI_PASSWORD if NVS empty
  3. WiFi.begin(ssid, pass)
  4. Poll up to 20 × 500ms (10 s max) for WL_CONNECTED
  5. On connect: syncNTPTime()
  6. On fail: continue offline

Reconnect loop (in main loop, every 60 s):
  if WiFi.status() != WL_CONNECTED → connectWiFi()
  if just reconnected AND SPIFFS queue has entries → syncOfflineQueue() immediately
```

### 4.5 Time Synchronization

The firmware uses a three-tier time strategy to ensure reliable timestamps without a real-time clock chip:

1. **NTP** (`pool.ntp.org`) — synced on every WiFi connect, up to 20 retries × 1 s
2. **HTTP `Date` header** — every Supabase HTTP response is inspected; if the `Date` header parses to a valid epoch it updates the system clock
3. **NVS cache** — last known good epoch is saved to NVS on every update; restored on boot before WiFi connects

Minimum valid epoch: `1 767 225 600` (2026-01-01). Any value below this is ignored.

### 4.6 RFID Read Flow

```
loop() → rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()
  1. Read UID bytes → uppercase hex string (e.g. "778D7506")
  2. rfid.PICC_HaltA() + rfid.PCD_StopCrypto1()
  3. Immediate LED HIGH (visual feedback)
  4. Duplicate check: uid == lastUID AND millis()-lastTapMs < 5000 → ignore
  5. Update lastUID, lastTapMs
  6. LED LOW
  7. If enrollMode → handleEnroll(uid)
     Else → addToQueue(uid, getEpochTime()) → processQueue()
```

### 4.7 Enrollment Mode

**Two triggers:**

| Trigger | How |
|---|---|
| Manual | Hold GPIO4 button LOW for >2 s |
| Admin-driven | `checkEnrollmentCommand()` poll returns `"enroll": true` |

**Manual mode** sets `enrollMode = true`, `activeEnrollmentId = ""` (no server-side ID — legacy flow). Rapid 6-flash LED pattern.

**Admin-driven mode** sets `enrollMode = true`, `activeEnrollmentId = enrollment_id from server`. Same LED pattern.

**`handleEnroll(uid)` flow:**
1. Check if UID already in `users[]` RAM cache → reject with error blink if so
2. Send `CAPTURE:ENROLL:{epoch}` to CAM → get photo path
3. If WiFi connected: `POST /device-enroll` with `{device_uid, device_secret, credential_value, enrollment_id, photo_path, timestamp}`
   - On HTTP 200/201: `downloadUsers()` (refresh cache), `blinkOK()`
   - On failure: `blinkError()`
4. If WiFi down: send `SAVE_ENROLL:{uid}:{photo_path}` to CAM for offline storage
5. Exit enroll mode (`enrollMode = false`, `activeEnrollmentId = ""`)

**Auto-timeout:** 60 s after entering enroll mode, exit with `blinkError()` if no card tapped.

### 4.8 Attendance Logging

**`handleAttendance(uid, ts)` flow:**
1. Find user in RAM `users[]` by RFID UID → `blinkError()` and return if unknown
2. Determine action: `check_in` if `last_action == ""` or `"check_out"`, else `check_out`
3. `sendCaptureCommand(employee_id, ts)` — 2 attempts, 8 s timeout each
4. `logAttendance(user, action, photo, ts)`:
   - **If WiFi down:** write JSON line to SPIFFS `/queue.txt`
   - **If WiFi up:** send `LOG:{json}` to CAM SD card via UART, then immediately call `syncPendingLogs()`
5. Update `user->last_action` and `user->last_timestamp` in RAM

### 4.9 Offline Queue (SPIFFS)

When WiFi is unavailable, events are appended to `/queue.txt` on SPIFFS as JSONL:

```json
{"rfid_uid":"778D7506","action":"check_in","timestamp":1735000000,"device_event_id":"42","photo_b64":"..."}
```

**`syncOfflineQueue()`:**
- Called on WiFi reconnect and every 5 min
- Reads up to `SYNC_MAX_PER_CYCLE` (20) lines
- For each: `POST /submit-log` with full payload
- Rewrites file with only failed/unprocessed lines
- Successful or duplicate records are removed from the queue

### 4.10 CAM Sync Protocol (UART)

The WROOM communicates with the CAM over `HardwareSerial(2)` at **9600 baud, 8N1** on GPIO16 (RX) / GPIO17 (TX). All messages are newline-terminated strings.

**`syncPendingLogs()` flow:**
```
WROOM → CAM : "GET_PENDING\n"
CAM  → WROOM: "BEGIN_LOGS\n"
              "{json line 1}\n"     (photo_b64 injected if image_path found)
              "{json line 2}\n"
              ...
              "END_LOGS\n"

WROOM processes up to 20 records (POST /submit-log for each):
  if submitted > 0 OR duplicates > 0:
    WROOM → CAM : "MARK_SYNCED\n"
  elif failed > 10:
    WROOM → CAM : "CLEAR_ALL\n"    (safety valve for stuck queue)
```

**`downloadUsers()` flow:**
```
WROOM → Supabase: POST /get-users → JSON with users[]
WROOM → CAM: "SAVE_USERS:{full JSON}\n"
CAM  → WROOM: "USERS_SAVED\n"
```

**`loadUsersFromCache()` (offline fallback):**
```
WROOM → CAM: "GET_USERS\n"
CAM  → WROOM: "USERS:{json}\n"
WROOM: parse and populate users[] RAM array
```

### 4.11 Periodic Tasks (main loop)

| Task | Interval | Trigger |
|---|---|---|
| CAM heartbeat (PING/PONG) | 60 s | `lastHeartbeat` timer |
| Sync + user download | 300 s (5 min) | `lastSync` timer |
| WiFi reconnect check | 60 s | `lastWifi` timer |
| Enrollment command poll | 30 s | `lastEnrollPoll` timer |
| Queue processing | Every loop iteration | `processQueue()` |
| Enrollment timeout | 60 s from mode entry | `enrollTimeout` check |

### 4.12 LED Patterns

| Pattern | Meaning |
|---|---|
| 3 × slow flash (startup) | Device booting |
| 2 × short flash | Success (`blinkOK`) |
| 5 × rapid flash | Error (`blinkError`) |
| 6 × rapid flash | Enroll mode activated |
| Steady ON during RFID read | Card being read |

### 4.13 Serial Debug Commands

Type in Arduino Serial Monitor (115200 baud):

| Command | Effect |
|---|---|
| `STATUS` | Print device UID, secret (first 8 chars), URL, WiFi state, heap, user count |
| `RESET` or `FACTORY_RESET` | Clear all NVS (both namespaces), reboot into provisioning mode |
| `PROVISION:{secret}` | Manually write a device secret to NVS and reboot |
| `CLEAR_PENDING` or `CLEAR_CAM` | Send `CLEAR_ALL` to CAM (removes all pending logs from SD) |

### 4.14 Provisioning Portal (`provision_portal.h`)

When the device is not provisioned, `enterProvisioningMode()` is called. If no pending portal data exists in NVS, `startProvisioningPortal()` launches:

1. WiFi AP mode: SSID `GATEMAN-Setup-XXXX` (last 4 chars of MAC)
2. DNS server: redirects all DNS queries to the device IP (captive portal)
3. HTTP server on port 80: serves a styled HTML form with fields for **WiFi SSID**, **WiFi Password**, and **Provision Token** (can be typed or scanned from QR)
4. On form submit: saves credentials to `"gateman"` NVS namespace, sets `needs_prov = true`, reboots
5. On reboot: `enterProvisioningMode()` detects `needs_prov` flag, calls `provisionDevice(token)` → `POST /device-provision` → saves returned credentials to `"ecotron"` NVS → reboots into normal operation

---

## 5. Firmware — ESP32-CAM Slave (`esp32cam_slave.ino`)

### 5.1 Pin Assignments

**Camera (OV2640 — AI-Thinker layout):**

| Signal | GPIO |
|---|---|
| PWDN | 32 |
| RESET | -1 (not connected) |
| XCLK | 0 |
| SIOD (I2C SDA) | 26 |
| SIOC (I2C SCL) | 27 |
| Y2–Y9 (data) | 5, 18, 19, 21, 36, 39, 34, 35 |
| VSYNC | 25 |
| HREF | 23 |
| PCLK | 22 |

**Other:**

| Signal | GPIO |
|---|---|
| UART RX (from WROOM) | 12 |
| UART TX (to WROOM) | 13 |
| LED (flash) | 33 |

### 5.2 Camera Configuration

```
Resolution : FRAMESIZE_QQVGA (160×120 pixels)
Format     : PIXFORMAT_JPEG
Quality    : 20 (0–63, lower = better quality, larger file)
Effect     : 2 (grayscale — set_special_effect)
Frame count: 1 (single buffer)
XCLK freq  : 20 MHz
```

### 5.3 SD Card

Initialized in 1-bit MMC mode (`SD_MMC.begin("/sdcard", true)`). Directory structure:

```
/sdcard/
├── pending/           Unsynced attendance JSONL files (one per day: YYYY-MM-DD.jsonl)
├── synced/            Moved here after MARK_SYNCED
├── photos/            JPEGs named {employee_id}_{YYYYMMDD}_{HHMMSS}.jpg
├── users.json         Cached employee list (full JSON from get-users response)
└── enrollments.jsonl  Offline enrollment records (legacy fallback)
```

### 5.4 UART Command Protocol

All commands arrive on `HardwareSerial(1)` at 9600 baud from the WROOM. Commands are newline-terminated. The CAM always sends a response (also newline-terminated).

| Command | Format | Response | Description |
|---|---|---|---|
| `PING` | `PING` | `PONG` | Heartbeat — WROOM polls every 60 s |
| `CAPTURE` | `CAPTURE:{empId}:{epoch}` | `DONE:{/photos/...jpg}` or `FAIL` | Take photo, save to SD, return path |
| `LOG` | `LOG:{json}` | *(none)* | Append JSON line to `/pending/YYYY-MM-DD.jsonl` |
| `GET_PENDING` | `GET_PENDING` | `BEGIN_LOGS\n...lines...\nEND_LOGS` | Stream all pending JSONL lines (with photo base64 injected) |
| `MARK_SYNCED` | `MARK_SYNCED` | *(none)* | Rename all `/pending/*.jsonl` → `/synced/`; run `cleanOldPhotos()` + `checkStorage()` |
| `SAVE_USERS` | `SAVE_USERS:{json}` | `USERS_SAVED` or `USERS_FAIL` | Overwrite `/users.json` |
| `GET_USERS` | `GET_USERS` | `USERS:{json}` | Read and return `/users.json` |
| `SAVE_ENROLL` | `SAVE_ENROLL:{json}` | `ENROLL_SAVED` or `ENROLL_FAIL` | Append to `/enrollments.jsonl` |
| `DELETE_USER` | `DELETE_USER:{userId}` | `USER_DELETED` or `DELETE_FAIL` | Remove user entry from `/users.json` by string search |
| `UPDATE_USER` | `UPDATE_USER:{userId}:{json}` | `USER_UPDATED` or `UPDATE_FAIL` | Replace user object in `/users.json` |
| `GET_HEALTH` | `GET_HEALTH` | `HEALTH:{json}` | Returns `{heap, uptime, sd_used_pct}` |
| `CLEAR_ALL` | `CLEAR_ALL` | `CLEARED` | Delete all files in `/pending/` |

### 5.5 Photo Capture Flow (`handleCapture`)

1. Parse `empId` and `ts` from command string
2. `digitalWrite(LED_PIN, HIGH)` — flash LED
3. `esp_camera_fb_get()` — capture one frame
4. Open `/photos/{empId}_{YYYYMMDD_HHMMSS}.jpg` for write
5. Write frame buffer bytes; close file
6. `esp_camera_fb_return(fb)` — release frame buffer
7. `digitalWrite(LED_PIN, LOW)`
8. Send `DONE:{path}` or `FAIL`

### 5.6 Photo Base64 Injection (`handleGetPending`)

When streaming pending logs, for each JSON line containing `"image_path":"{path}"`:
1. Check if the file exists on SD and is < 20 000 bytes
2. Allocate buffer, read file into it
3. Base64-encode using mbedTLS `mbedtls_base64_encode()`
4. Inject `,"photo_b64":"..."` before the closing `}` of the JSON object
5. Free buffer

This means photos are only sent inline if they were captured and saved to SD. If the CAM failed to capture, the `image_path` field will be empty and no injection occurs.

### 5.7 Storage Management

**`checkStorage()`:** Called after every `MARK_SYNCED`. If SD usage > 90%, call `deleteOldSynced()`.

**`deleteOldSynced()`:** Collects all `.jsonl` files in `/synced/`, sorts by `getLastWrite()` timestamp (oldest first), deletes oldest until usage drops to 85%.

**`cleanOldPhotos()`:** Called after every `MARK_SYNCED`. Deletes photos from `/photos/` older than 30 days. Skips cleanup if system time < `1700000000` (clock not set).

---

## 6. Dashboard (`dashboard/index.html`)

A single HTML file (~4 000 lines) containing all CSS, HTML, and JavaScript. No build step required — served directly by `serve` or any static file server.

**External dependencies (CDN):**
- `Chart.js 4.4.1` (UMD build from cdnjs)
- `QRCode.js 1.0.0` (from cdnjs)
- `@supabase/supabase-js@2` (UMD build from jsDelivr)

**Supabase project constants (hardcoded in HTML):**
- `SUPABASE_URL = "https://ueobebsgheecclwcbigy.supabase.co"`
- `SUPABASE_ANON_KEY = "eyJhbGci..."` (safe to expose — RLS enforces access)

### 6.1 Pages and Views

| Page | Nav item | What it shows |
|---|---|---|
| **Dashboard** | Dashboard | 4 stat cards; hourly bar chart; department doughnut; 7-day line chart; live attendance feed |
| **Employees** | Employees | Searchable table of active employees with RFID status; Add Employee modal; Assign Card modal; Delete button |
| **Attendance** | Attendance | Date-filtered smart attendance table (first check-in / last check-out per person per day) with photos; CSV export |
| **Enrollment** | Enrollment | Employee + device selectors; Enroll Card button; live enrollment status with countdown; recent enrollments table |
| **Devices** | Devices | List of devices (UID, location, status, last seen); Add Device → provision token + QR code |
| **Admin** | Admin | Subscription info (plan, limits, period end); org stats; last 50 audit trail events |

### 6.2 Authentication Flow

**Login:**
1. `sb.auth.signInWithPassword({email, password})`
2. On success: Supabase stores JWT in `localStorage`
3. `onAuthStateChange` fires → `loadUserOrg()` → query `org_members` to find `organization_id` and `role`
4. Store `currentOrgId`, `currentUserRole` in module-level variables
5. Show `#app`, hide `#loginPage`, navigate to Dashboard page

**Sign Up:**
1. `sb.auth.signUp({email, password})`
2. `sb.from('organizations').insert({name, slug}).select('id').single()`
3. `sb.from('org_members').insert({organization_id, user_id, role: 'owner'})`
4. Database trigger `on_org_created` auto-creates a trial subscription
5. Auto-login follows the same flow as above

**Sign Out:** `sb.auth.signOut()` → hide `#app`, show `#loginPage`

**Session persistence:** Supabase JS client automatically refreshes the JWT and stores it in `localStorage`.

### 6.3 Real-Time Updates

The dashboard subscribes to Supabase Realtime for live attendance feed updates:

```javascript
sb.channel('attendance-feed')
  .on('postgres_changes', {
    event: 'INSERT',
    schema: 'public',
    table: 'attendance_logs',
    filter: `organization_id=eq.${currentOrgId}`
  }, handleNewLog)
  .subscribe()
```

On new insert: prepend a feed item to the live feed card, update stat card counters, load a signed URL for the photo thumbnail.

### 6.4 Enrollment Polling

While an enrollment is in progress, the dashboard polls `enrollment_queue` every **2 seconds** using `setInterval`:

```javascript
sb.from('enrollment_queue')
  .select('status, credential_value')
  .eq('id', activeEnrollId)
  .single()
```

If `status === 'assigned'`: clear timers, show success, reload tables.  
If `status === 'rejected'`: clear timers, show error.  
Auto-timeout at 65 s (5 s grace after the device's 60 s timeout).

Cancel button: sets `status = 'rejected'` directly via client-side Supabase call.

### 6.5 API Calls Made from Dashboard

| Operation | Method | Target |
|---|---|---|
| Load employees | `sb.from('users').select(...)` | Supabase PostgREST |
| Add employee | `sb.from('users').insert(...)` | Supabase PostgREST |
| Assign RFID | `sb.from('user_credentials').insert(...)` | Supabase PostgREST |
| Delete employee | `sb.from('users').update({active: false})` | Supabase PostgREST |
| Load attendance | `sb.rpc('get_smart_attendance', ...)` | Supabase RPC |
| Export CSV | `sb.from('attendance_logs').select(...)` | Supabase PostgREST |
| Load devices | `sb.from('devices').select(...)` | Supabase PostgREST |
| Load subscription | `sb.from('subscriptions').select(...)` | Supabase PostgREST |
| Load audit trail | `sb.from('audit_logs').select(...)` | Supabase PostgREST |
| Dashboard stats | `sb.rpc('get_dashboard_stats', ...)` | Supabase RPC |
| Hourly chart | `sb.rpc('get_hourly_stats', ...)` | Supabase RPC |
| Weekly chart | `sb.rpc('get_weekly_stats', ...)` | Supabase RPC |
| Dept presence | `sb.rpc('get_department_presence', ...)` | Supabase RPC |
| Start enrollment | `fetch(SUPABASE_URL + '/functions/v1/start-enrollment', ...)` | Edge Function |
| Generate token | `fetch(SUPABASE_URL + '/functions/v1/create-provision-token', ...)` | Edge Function |
| Photo thumbnails | `sb.storage.from('attendance-photos').createSignedUrl(...)` | Supabase Storage |

### 6.6 Export Functionality

CSV export queries up to 5 000 `attendance_logs` records with joined `users` and `devices`. Columns:

```
Name, Employee ID, Department, Action, Timestamp, Device, RFID
```

The file is generated client-side using a `Blob` + `URL.createObjectURL()` and downloaded as `attendance_{YYYY-MM-DD}.csv`.

---

## 7. Data Flow — End to End

### 7.1 Normal Tap → Dashboard

```
1. Employee taps RFID card
   [wroom_brain.ino:552] rfid.PICC_IsNewCardPresent()
   [wroom_brain.ino:553] rfid.PICC_ReadCardSerial()
   [wroom_brain.ino:634] getUID() → "778D7506"

2. Duplicate check
   [wroom_brain.ino:561] if uid == lastUID AND < 5s → skip

3. Add to circular queue
   [wroom_brain.ino:569] addToQueue(uid, getEpochTime())

4. Process queue
   [wroom_brain.ino:755] processQueue() → handleAttendance(uid, ts)

5. Look up employee
   [wroom_brain.ino:783] findUserByRFID(uid) → User* (or null → blinkError)

6. Determine action
   [wroom_brain.ino:771] last_action="" or "check_out" → "check_in", else "check_out"

7. Capture photo (2 attempts, 8s timeout each)
   [wroom_brain.ino:791] sendCaptureCommand(employee_id, ts)
   WROOM → CAM: "CAPTURE:EMP001:1735000000\n"       [UART GPIO17→12]
   CAM: esp_camera_fb_get() → save to /photos/EMP001_20260115_083000.jpg
   CAM → WROOM: "DONE:/photos/EMP001_20260115_083000.jpg\n"

8. Log the event
   [wroom_brain.ino:814] logAttendance(user, action, photo, ts)

   IF WiFi DOWN:
     [wroom_brain.ino:818] saveToQueue(rfid_uid, action, ts, photo)
     → append JSONL line to SPIFFS /queue.txt

   IF WiFi UP:
     [wroom_brain.ino:831] WROOM → CAM: "LOG:{json}\n"
     CAM: append to /pending/2026-01-15.jsonl
     [wroom_brain.ino:780] syncPendingLogs()

9. Sync to Supabase
   [wroom_brain.ino:838] WROOM → CAM: "GET_PENDING\n"
   CAM: stream BEGIN_LOGS ... (with photo_b64 injected) ... END_LOGS
   [wroom_brain.ino:891] for each log line: POST /submit-log
   
   Supabase Edge Function [submit-log/index.ts]:
     → authenticateDevice()
     → checkRateLimit()
     → checkSubscriptionActive()
     → lookup RFID → user_id via user_credentials
     → UPSERT attendance_logs (idempotent on device_event_id)
     → fire-and-forget: upload photo to attendance-photos storage
     → auditLog()
   
   [wroom_brain.ino:923] WROOM → CAM: "MARK_SYNCED\n"
   CAM: rename /pending/*.jsonl → /synced/
        cleanOldPhotos()
        checkStorage()

10. Real-time update to dashboard
    Supabase Realtime: INSERT on attendance_logs fires postgres_changes event
    [dashboard/index.html:~800] handleNewLog() → prepend feed item
                                                → reload stat cards
                                                → load signed URL for photo thumbnail
```

### 7.2 Offline Tap → Later Sync

```
1–6. Same as above (tap, read, queue, lookup, action)

7. Capture attempt (may fail if CAM also has issues, but photo is not required)

8. logAttendance() detects WiFi DOWN
   → saveToQueue() appends to SPIFFS /queue.txt

9. On WiFi reconnect (checked every 60s):
   [wroom_brain.ino:597] syncOfflineQueue()
   → read SPIFFS lines (up to 20 per cycle)
   → POST /submit-log for each
   → rewrite /queue.txt with only failed entries

10. Supabase upsert is idempotent → safe if same event retried
```

### 7.3 Admin-Driven Enrollment

```
1. Admin opens Dashboard → Enrollment page
   → selects employee + device
   → clicks "Enroll Card"

2. Dashboard [index.html:startEnrollment()]
   POST /functions/v1/start-enrollment
   Body: {user_id, device_id, organization_id, caller_user_id}
   
   Edge Function [start-enrollment/index.ts]:
     → validate admin JWT
     → cancel existing 'waiting' enrollments for device
     → INSERT enrollment_queue {status:'waiting', assigned_to:user_id}
   Returns: {status:'waiting', enrollment_id}

3. Dashboard starts 2s polling of enrollment_queue.id

4. Device (within 30s) polls [wroom_brain.ino:710] checkEnrollmentCommand()
   POST /functions/v1/check-enrollment
   Body: {device_uid, device_secret}
   Response: {enroll:true, enrollment_id, assigned_to}

5. Device enters enroll mode (6-flash LED)
   activeEnrollmentId = enrollment_id
   enrollMode = true, timeout = now + 60s

6. Employee taps card → handleEnroll(uid) [wroom_brain.ino:661]
   → sendCaptureCommand("ENROLL", ts) → photo path
   POST /functions/v1/device-enroll
   Body: {device_uid, device_secret, credential_value:uid, enrollment_id, photo_path, timestamp}
   
   Edge Function [device-enroll/index.ts]:
     → lookup enrollment_queue by enrollment_id (status must be 'waiting')
     → INSERT user_credentials {user_id:assigned_to, type:'rfid', value:uid}
     → UPDATE enrollment_queue SET status='assigned', credential_value=uid
   Returns: 200 OK

7. Device: downloadUsers() (refreshes RAM cache), blinkOK(), exits enroll mode

8. Dashboard poll detects status='assigned' (within 2s)
   → clearEnrollTimers()
   → showEnrollStatus('success', 'Card enrolled! UID: {uid}')
   → loadEnrollments(), loadEmployees()
```

### 7.4 Device Provisioning (First Boot)

```
1. Fresh ESP32 boots; isProvisioned() returns false

2. enterProvisioningMode():
   → no pending NVS data → startProvisioningPortal()

3. Device creates WiFi AP: "GATEMAN-Setup-XXXX"
   DNS server captures all DNS → device IP
   HTTP server serves setup HTML form

4. Admin/installer:
   a. Connects phone/laptop to "GATEMAN-Setup-XXXX"
   b. Browser auto-opens captive portal (or navigates to 192.168.4.1)
   c. Enters WiFi SSID + password + provision token (from Dashboard QR)
   d. Submits form

5. Portal saves to NVS "gateman" namespace:
   wifi_ssid, wifi_pass, prov_token, needs_prov=true
   → ESP.restart()

6. On reboot: enterProvisioningMode() detects needs_prov=true
   → provisionDevice(token) [wroom_brain.ino:175]
   POST /functions/v1/device-provision
   Body: {device_uid: WiFi.macAddress(), provisioning_token: token}
   
   Edge Function [device-provision/index.ts]:
     → validate token (exists, not expired, not used)
     → check org device limit
     → generate device_secret (UUID-UUID)
     → INSERT devices record
     → mark token used_at = NOW()
   Returns: {device_secret, device_id, supabase_url}

7. saveCredentials() → NVS "ecotron" namespace
   ESP.restart() → normal operation
```

---

## 8. Deployment

### 8.1 Local Development

```bash
# 1. Install dashboard dependencies
cd "c:\Ecotronics Enterprise\gateman"
npm install

# 2. Start dashboard server
npm start
# → http://localhost:3000
```

The dashboard talks directly to the live Supabase project (URL is hardcoded). No local backend is required.

### 8.2 Supabase Setup

```bash
# Install Supabase CLI
npm install -g supabase

# Run the schema migration (Supabase SQL Editor or CLI)
# Paste contents of supabase/migrations/001_complete_schema.sql into
# Supabase Dashboard → SQL Editor → Run

# Deploy Edge Functions (--no-verify-jwt is required for device auth)
npx supabase functions deploy device-provision      --project-ref ueobebsgheecclwcbigy --no-verify-jwt
npx supabase functions deploy device-login          --project-ref ueobebsgheecclwcbigy --no-verify-jwt
npx supabase functions deploy submit-log            --project-ref ueobebsgheecclwcbigy --no-verify-jwt
npx supabase functions deploy get-users             --project-ref ueobebsgheecclwcbigy --no-verify-jwt
npx supabase functions deploy check-enrollment      --project-ref ueobebsgheecclwcbigy --no-verify-jwt
npx supabase functions deploy device-enroll         --project-ref ueobebsgheecclwcbigy --no-verify-jwt
npx supabase functions deploy start-enrollment      --project-ref ueobebsgheecclwcbigy
npx supabase functions deploy create-provision-token --project-ref ueobebsgheecclwcbigy
npx supabase functions deploy create-checkout       --project-ref ueobebsgheecclwcbigy
npx supabase functions deploy paystack-webhook      --project-ref ueobebsgheecclwcbigy

# Set secrets
npx supabase secrets set PAYSTACK_SECRET_KEY=sk_live_xxxxx --project-ref ueobebsgheecclwcbigy

# Create storage bucket (Supabase Dashboard → Storage → New Bucket)
# Name: attendance-photos
# Public: OFF
# File size limit: 50KB
# Allowed MIME: image/jpeg
```

### 8.3 VPS / Production Deployment

Current production server: `46.225.186.103`

```bash
# SSH into server
ssh deploy@46.225.186.103
cd /var/www/gateman

# Pull latest code
git pull origin main

# Dashboard is served statically — no restart needed
# If using a process manager:
pm2 restart gateman-dashboard
```

A custom domain is planned; currently accessed by IP.

### 8.4 Firmware Flash Order

**Always flash ESP32-CAM first, WROOM second.**

#### ESP32-CAM (`esp32cam_slave.ino`)

1. Wire FTDI adapter: FTDI TX→CAM GPIO3, FTDI RX→CAM GPIO1, GND→GND, 5V→5V
2. Connect IO0 to GND (boot mode)
3. In Arduino IDE: Board = `AI Thinker ESP32-CAM`, Port = FTDI COM port
4. Upload
5. Remove IO0 jumper, press Reset

#### WROOM Brain (`wroom_brain.ino`)

1. Hold BOOT button (GPIO0) while connecting USB
2. In Arduino IDE: Board = `ESP32 Dev Module`, Port = WROOM COM port
3. Configure at top of `wroom_brain.ino` (lines 37–40):
   ```cpp
   const char* WIFI_SSID     = "your_ssid";
   const char* WIFI_PASSWORD = "your_password";
   const long  GMT_OFFSET    = 3600;    // UTC+1 for WAT
   ```
   Or leave blank to use the captive portal for first-time WiFi setup.
4. Upload
5. Open Serial Monitor at 115200 baud
6. Type `STATUS` to confirm MAC address, connectivity, and credential state

#### Provisioning a new device

1. Dashboard → Devices → "+ Add Device"
2. Enter a device name → click "Generate Token"
3. Copy or print the QR code (valid 10 min)
4. Connect to `GATEMAN-Setup-XXXX` WiFi AP on phone/laptop
5. Browser opens portal → enter WiFi credentials + token (or scan QR)
6. Device reboots, provisions automatically, appears in Devices page within ~30 s

---

## 9. Known Limitations & TODOs

### Hardcoded Values

| Location | Value | Risk |
|---|---|---|
| `wroom_brain.ino:37` | `WIFI_SSID = "MTN_4G_56F7A3"` | Must be changed per site; captive portal can override |
| `wroom_brain.ino:38` | `WIFI_PASSWORD = "88888888"` | Same |
| `wroom_brain.ino:44` | Supabase anon key hardcoded | Low risk (it's a public key, RLS enforces access) |
| `wroom_brain.ino:185` | Supabase URL hardcoded as fallback in `provisionDevice()` | OK for single-project deployment |
| `wroom_brain.ino:56` | `PILOT_PROVISION_TOKEN = ""` | Intended for pilot builds; leave empty in production |
| `dashboard/index.html` | `SUPABASE_URL` and `SUPABASE_ANON_KEY` hardcoded | Acceptable for single-project deployment |

### Disabled Features

| Feature | Location | Reason disabled |
|---|---|---|
| Hardware watchdog | `wroom_brain.ino:268–276` | Was causing constant resets; commented out |
| Hardware watchdog | `esp32cam_slave.ino:70–77` | Same reason |
| CAM light sleep | `esp32cam_slave.ino:150–160` | Disabled to keep CAM responsive |

### Security Considerations

- **Device secret is plaintext** in NVS and in the database `device_secret` column. If a device is physically compromised, the secret must be manually revoked by setting `devices.status = 'revoked'`.
- **`--no-verify-jwt` on device functions**: required because devices use their own auth (uid+secret), not Supabase JWTs. Re-deploying without this flag would break all device auth.
- **RFID UIDs as credentials**: standard Mifare UIDs are not encrypted and can be cloned with a card duplicator. The system does not detect cloned cards.
- **Photo base64 over UART**: at 9600 baud, a 20 KB photo takes ~20 s to transmit. This is the primary sync bottleneck.
- **Timestamp window (7 days / 5 min)**: events older than 7 days are rejected by `/submit-log`. Long offline periods exceeding 7 days will cause log loss on sync.

### TODOs / Gaps

- `submit-log` validates timestamps to within 7 days, but the SPIFFS offline queue has no maximum age check — very old queued events will be rejected by the server when eventually synced.
- `handleDeleteUser` and `handleUpdateUser` in `esp32cam_slave.ino` use naive string-search JSON manipulation, which can corrupt `/users.json` on malformed input.
- The `claim-device` and `pair-device` Edge Functions exist in the repo but are not documented or called anywhere in the current firmware or dashboard.
- The `get_smart_attendance` RPC is called by the dashboard but is not in `001_complete_schema.sql` — it must have been applied separately or is missing from the migration file.
- No automated retention enforcement: the `retention_days` field on subscriptions is stored but no cron job or trigger deletes old `attendance_logs` based on it.
- No monitoring or alerting when a device reports `0` users loaded after a sync (potential credential-table gap).
- All Edge Functions deployed with `--no-verify-jwt` for device functions — this must be documented in any CI/CD deploy script to avoid accidental breakage.
- No HTTPS pinning in firmware — a compromised network could MITM device-to-Supabase traffic (mitigated by plaintext device secret not being particularly sensitive for data-exfiltration purposes).
- The dashboard CSV export is capped at 5 000 records with no pagination warning.
