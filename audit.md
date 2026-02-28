# ECOTRONICS — CODEBASE DELTA AUDIT
## Single-Tenant → Multi-Tenant SaaS Migration (Supabase)

**Date:** 2026-02-28
**Auditor:** Cascade (Senior IoT SaaS Architect)
**Scope:** All firmware, backend, and dashboard code in `gateman/`

---

## 1. CURRENT ARCHITECTURE SNAPSHOT

### 1.1 Files Audited

| File | Lines | Role |
|------|-------|------|
| `backend/server.js` | 431 | Express API server — all endpoints |
| `backend/package.json` | 19 | Node.js dependencies |
| `backend/env.example` | 21 | Environment config template |
| `firmware/wroom_brain/wroom_brain.ino` | 487 | Brain firmware — RFID, WiFi, HTTP sync |
| `firmware/esp32cam_slave/esp32cam_slave.ino` | 600 | Slave firmware — camera, SD, UART |
| `dashboard/index.html` | 742 | Single-file SPA dashboard |

### 1.2 Current API Contract Map

| # | Endpoint | Method | Auth | Consumer | Purpose |
|---|----------|--------|------|----------|---------|
| 1 | `/api/auth/login` | POST | None | Dashboard | Admin login → JWT |
| 2 | `/api/users/:deviceId` | GET | Device Bearer | Brain firmware (line 396) | Download employee list |
| 3 | `/api/attendance/bulk` | POST | Device Bearer | Brain firmware (line 377) | Bulk sync attendance logs |
| 4 | `/api/enroll` | POST | Device Bearer | Brain firmware (line 248) | Submit enrollment |
| 5 | `/api/events` | GET | Admin JWT | Dashboard (line 419) | SSE real-time stream |
| 6 | `/api/dashboard/stats` | GET | Admin JWT | Dashboard (line 481) | Summary statistics |
| 7 | `/api/dashboard/feed` | GET | Admin JWT | Dashboard (line 490, 651) | Attendance feed |
| 8 | `/api/dashboard/hourly` | GET | Admin JWT | Dashboard (line 528) | Hourly chart data |
| 9 | `/api/dashboard/weekly` | GET | Admin JWT | Dashboard (line 551) | Weekly chart data |
| 10 | `/api/dashboard/departments` | GET | Admin JWT | Dashboard (line 571) | Department breakdown |
| 11 | `/api/employees` | GET | Admin JWT | Dashboard (line 591) | List employees |
| 12 | `/api/employees` | POST | Admin JWT | Dashboard (line 632) | Add employee |
| 13 | `/api/employees/:id/assign-rfid` | PATCH | Admin JWT | Dashboard (line 705) | Assign RFID card |
| 14 | `/api/enrollments/pending` | GET | Admin JWT | Dashboard (line 674) | Pending enrollments |
| 15 | `/api/export/csv` | GET | Admin JWT | Dashboard (line 718) | CSV export |
| 16 | `/health` | GET | None | Monitoring | Health check |

### 1.3 Current Authentication Model

**Admin (Dashboard):**
- `server.js:136-143` — `authAdmin()` verifies JWT from `Authorization: Bearer <token>`
- JWT signed with `JWT_SECRET` (env) with **fallback `'fallback_secret'`** ← VULNERABILITY
- JWT payload: `{ id, name, email }` — company ID is the JWT subject
- 7-day expiry, no refresh mechanism

**Device (Firmware):**
- `server.js:145-153` — `authDevice()` matches raw `secret` against `devices.secret` column (plaintext)
- `wroom_brain.ino:35` — `DEVICE_SECRET` hardcoded as `const char*`
- Bearer token sent in `Authorization` header
- Secret lookup: `SELECT * FROM devices WHERE secret = $1` — **plaintext comparison**

### 1.4 Current Database Schema (from `initDB()`, server.js:44-102)

| Table | Tenant-Scoped? | Notes |
|-------|---------------|-------|
| `companies` | N/A (IS the tenant) | Acts as both auth user AND org |
| `devices` | Yes (`company_id`) | Secret stored plaintext |
| `employees` | Yes (`company_id`) | `rfid_uid UNIQUE` — **global, not org-scoped** |
| `attendance` | Yes (`company_id`) | UUID primary key, `ON CONFLICT (id) DO NOTHING` |
| `enrollment_queue` | **No** | No `company_id` — orphaned design |

---

## 2. RISK ANALYSIS

### 2.1 Critical Risks (Must Fix Before Migration)

| # | Risk | Location | Impact | Mitigation |
|---|------|----------|--------|------------|
| R1 | **Firmware blocking POST loop** | `wroom_brain.ino:362-388` — `syncPendingLogs()` builds entire JSON in memory, single blocking POST | Device stalls if server slow; watchdog reboot after 30s | Switch to per-record POST with timeout per request |
| R2 | **8KB JSON allocation** | `wroom_brain.ino:362` — `DynamicJsonDocument payload(8192)` for bulk records | Heap fragmentation on ESP32 (520KB total SRAM) | Per-record sync eliminates need for large buffer |
| R3 | **No device_event_id** | `server.js:236-240` — duplicate protection uses `ON CONFLICT (id)` where `id = uuidv4()` | **Every retry generates new UUID = duplicates possible** | Add `device_event_id` field; use `ON CONFLICT (device_id, device_event_id)` |
| R4 | **Global RFID uniqueness** | `server.js:70` — `rfid_uid TEXT UNIQUE` on `employees` | Company A blocks Company B from using same RFID tag | Move to `UNIQUE(organization_id, type, value)` on `user_credentials` |
| R5 | **JWT fallback secret** | `server.js:140,197` — `process.env.JWT_SECRET \|\| 'fallback_secret'` | Predictable secret if env not set | Supabase Auth eliminates this entirely |
| R6 | **Plaintext device secrets** | `server.js:117` — stored as-is in `devices.secret` | DB compromise = all device secrets exposed | bcrypt hash at provisioning time |
| R7 | **No RLS** | Entire `server.js` | Data isolation relies solely on app logic; one bug = data leak | Supabase RLS with org-scoped policies |
| R8 | **enrollment_queue not tenant-scoped** | `server.js:89-96` | Cross-tenant enrollment leakage possible | Redesigned in Supabase schema |

### 2.2 Medium Risks

| # | Risk | Location | Impact |
|---|------|----------|--------|
| R9 | WiFi reconnection blocks loop | `wroom_brain.ino:442-448` — `connectWiFi()` blocks up to 10s | RFID scanning paused during reconnect |
| R10 | HMAC key = device secret | `wroom_brain.ino:335` — `hmacSHA256(raw, String(DEVICE_SECRET))` | Signature and auth use same key |
| R11 | SSE memory leak potential | `server.js:158-184` — `sseClients` Map grows if clients disconnect ungracefully | Server memory pressure |
| R12 | Photo path served directly | `dashboard/index.html:514-516` — `<img src="/photos/${filename}">` | Photos served from Express static; no auth on photo access |

### 2.3 Low Risks

| # | Risk | Location |
|---|------|----------|
| R13 | `loadUsersFromCache()` has no freshness check | `wroom_brain.ino:417-437` |
| R14 | `deleteOldSynced()` uses bubble sort (O(n²)) | `esp32cam_slave.ino:282-294` — acceptable for n≤100 |
| R15 | Dashboard has hardcoded default credentials in HTML | `dashboard/index.html:146-150` |

---

## 3. REQUIRED REFACTORS

### 3.1 Firmware Changes (Brain Only)

**Slave firmware: ZERO changes. Confirmed.**
UART protocol, SD logic, camera, sleep — all untouched.

#### Line-Level Changes in `wroom_brain.ino`:

| # | Lines | Current Code | New Code | Complexity |
|---|-------|-------------|----------|------------|
| F1 | **31-35** | `const char* API_ENDPOINT`, `DEVICE_ID`, `DEVICE_SECRET` hardcoded | `const char* SUPABASE_URL`; `String DEVICE_UID` from `WiFi.macAddress()`; `String DEVICE_SECRET` from NVS | Low |
| F2 | **90-136** | `setup()` — calls `downloadUsers()` and `syncPendingLogs()` directly | Add NVS credential load, provisioning check before normal boot | Medium |
| F3 | **244** | `doc["device_id"] = DEVICE_ID` | `doc["device_uid"] = DEVICE_UID` | Low |
| F4 | **248** | `http.begin(String(API_ENDPOINT) + "/enroll")` | `http.begin(String(SUPABASE_URL) + "/functions/v1/device-enroll")` | Low |
| F5 | **250** | `http.addHeader("Authorization","Bearer " + String(DEVICE_SECRET))` | Send `device_uid` + `device_secret` in JSON body (Edge Function model) | Low |
| F6 | **328-338** | `logAttendance()` — builds JSON with `doc["device_id"]=DEVICE_ID` | Add `device_event_id` field; use `DEVICE_UID` | Low |
| F7 | **333** | `doc["device_id"]=DEVICE_ID` | `doc["device_uid"]=DEVICE_UID` | Low |
| F8 | **335** | `doc["signature"] = hmacSHA256(raw, String(DEVICE_SECRET))` | Keep HMAC but ensure key loaded from NVS, not hardcoded constant | Low |
| F9 | **343-389** | `syncPendingLogs()` — bulk POST with 8KB JSON buffer | Per-record POST loop; 512-byte buffer per record; individual HTTP calls | **High** |
| F10 | **376-381** | `http.begin(String(API_ENDPOINT)+"/attendance/bulk")` with Bearer auth | `http.begin(String(SUPABASE_URL)+"/functions/v1/submit-log")` with JSON body auth | Medium |
| F11 | **394-415** | `downloadUsers()` — GET with Bearer token | POST to `/functions/v1/get-users` with JSON body containing `device_uid` + `device_secret` | Medium |
| F12 | **396** | `http.begin(String(API_ENDPOINT)+"/users/"+DEVICE_ID)` | `http.begin(String(SUPABASE_URL)+"/functions/v1/get-users")` | Low |
| F13 | **N/A** | Does not exist | New function: `generateEventId(unsigned long ts)` → `"{DEVICE_UID}-{ts}-{counter}"` | Low |
| F14 | **N/A** | Does not exist | New function: `provisionDevice(String token)` — POST to `/functions/v1/device-provision` | Medium |
| F15 | **N/A** | Does not exist | NVS load/save via `<Preferences.h>` for `device_uid` and `device_secret` | Low |

**Total estimated lines changed:** ~150 of 487 (31%)
**Functions rewritten:** `syncPendingLogs()`, `downloadUsers()`, `logAttendance()`, `handleEnroll()`, `setup()`
**Functions added:** `generateEventId()`, `provisionDevice()`, `isProvisioned()`, `saveCredentials()`
**Functions untouched:** `loop()`, `processQueue()`, `handleAttendance()`, `findUserByRFID()`, `sendCaptureCommand()`, `pingCAM()`, `getUID()`, `connectWiFi()`, `syncNTPTime()`, all LED/HMAC/time utilities

#### Critical Firmware Stability Concerns:

1. **`syncPendingLogs()` rewrite (F9) is the highest-risk change.** Current implementation sends one bulk POST. New implementation sends N sequential POSTs. If N=50, that's 50 HTTP connections. Each must:
   - Open connection
   - POST ~512 bytes
   - Wait for 200 response
   - Close connection
   - Feed watchdog between iterations

   **Stress test required:** 100 pending logs, 2G network, mid-sync power cut.

2. **Memory fragmentation.** Current bulk approach allocates one 8KB buffer. Per-record approach allocates/deallocates 512-byte buffers N times. ESP32 heap may fragment over time. Monitor `ESP.getFreeHeap()` during extended runs.

3. **Watchdog timeout.** `WDT_TIMEOUT_S = 30`. If 50 records × 2s timeout each = 100s → watchdog reboot. Must call `esp_task_wdt_reset()` inside sync loop.

---

### 3.2 Backend Changes (Complete Replacement)

**The entire `backend/server.js` (431 lines) is replaced by Supabase infrastructure.**

#### API Contract Migration Map:

| Current Endpoint | Supabase Replacement | Type |
|-----------------|---------------------|------|
| `POST /api/auth/login` | `supabase.auth.signInWithPassword()` | Client-side SDK |
| `GET /api/users/:deviceId` | Edge Function: `get-users` | New code |
| `POST /api/attendance/bulk` | Edge Function: `submit-log` (per record) | New code |
| `POST /api/enroll` | Edge Function: `device-enroll` | New code |
| `GET /api/events` (SSE) | `supabase.channel().on('postgres_changes')` | Client-side SDK |
| `GET /api/dashboard/stats` | Direct Supabase query with RLS | Client-side SDK |
| `GET /api/dashboard/feed` | Direct Supabase query with RLS | Client-side SDK |
| `GET /api/dashboard/hourly` | Direct Supabase query or DB function | Client-side SDK / RPC |
| `GET /api/dashboard/weekly` | Direct Supabase query or DB function | Client-side SDK / RPC |
| `GET /api/dashboard/departments` | Direct Supabase query or DB function | Client-side SDK / RPC |
| `GET /api/employees` | Direct Supabase query with RLS | Client-side SDK |
| `POST /api/employees` | Direct Supabase insert with RLS | Client-side SDK |
| `PATCH /api/employees/:id/assign-rfid` | Direct Supabase update with RLS | Client-side SDK |
| `GET /api/enrollments/pending` | Direct Supabase query with RLS | Client-side SDK |
| `GET /api/export/csv` | Client-side CSV generation from Supabase query | Client-side logic |
| `GET /health` | Supabase built-in health | N/A |

#### New Edge Functions Required (4):

| Function | Replaces | Auth Model | Key Logic |
|----------|----------|-----------|-----------|
| `device-login` | Part of `authDevice()` | Service role key; manual bcrypt | Validate `device_uid`+`device_secret`, update `last_seen` |
| `submit-log` | `/api/attendance/bulk` | Service role key; manual bcrypt | Resolve RFID→user_id via `user_credentials`, insert with `ON CONFLICT DO NOTHING` |
| `get-users` | `/api/users/:deviceId` | Service role key; manual bcrypt | Return org-scoped users + credentials for device's org |
| `device-provision` | Does not exist | Service role key; token validation | Validate provision token, generate+hash secret, create device record |

#### Supabase DB Functions (RPCs) for Complex Queries (3):

The following dashboard queries use aggregations that are easier as server-side RPCs:

| RPC Name | Replaces | Reason |
|----------|----------|--------|
| `get_hourly_stats(org_id)` | `/api/dashboard/hourly` (server.js:320-328) | `to_char()` + `GROUP BY` aggregation |
| `get_weekly_stats(org_id)` | `/api/dashboard/weekly` (server.js:330-340) | Date grouping + interval |
| `get_department_presence(org_id)` | `/api/dashboard/departments` (server.js:342-350) | `DISTINCT` + JOIN aggregation |

These can alternatively be done client-side with multiple queries, but RPCs are cleaner and more performant.

---

### 3.3 Dashboard Changes

**The entire `dashboard/index.html` (742 lines) must be refactored.**

#### Change Categories:

| Category | Current (Lines) | New Approach | Effort |
|----------|----------------|-------------|--------|
| **Auth** | `login()` lines 376-394 | `supabase.auth.signInWithPassword()` | Low |
| **Token storage** | `localStorage.getItem('eco_token')` line 368 | Supabase session management (automatic) | Low |
| **API helper** | `api()` function lines 447-454 | Supabase client queries with RLS | Medium |
| **SSE** | `initSSE()` lines 417-442 | `supabase.channel().on('postgres_changes')` | Medium |
| **Stats** | `loadStats()` lines 480-487 | Supabase RPC or multi-query | Medium |
| **Feed** | `loadFeed()` lines 489-495 | Supabase query with joins | Low |
| **Charts** | lines 527-585 | Supabase RPCs or client-side aggregation | Medium |
| **Employees CRUD** | lines 590-643 | Direct Supabase queries | Low |
| **Enrollment** | lines 673-710 | Supabase query (redesigned table) | Medium |
| **CSV Export** | `exportCSV()` lines 715-719 | Client-side generation from Supabase query | Medium |
| **Photo display** | lines 514, 663, 687 | Supabase Storage signed URLs | Medium |
| **Org context** | Does not exist | New: org selector, membership awareness | **New feature** |
| **Signup page** | Does not exist | New: company registration flow | **New feature** |
| **Device provisioning UI** | Does not exist | New: generate token, display QR | **New feature** |

---

## 4. BREAKING CHANGES

### 4.1 Firmware Breaking Changes

| # | Change | Impact | Rollback Plan |
|---|--------|--------|--------------|
| B1 | API endpoint URL changes | Device cannot talk to old backend AND new backend simultaneously | Keep old backend alive; firmware can only point to one |
| B2 | Auth model change (Bearer header → JSON body) | Incompatible with old `authDevice()` middleware | Old backend must stay up until all devices are migrated |
| B3 | Bulk sync → per-record sync | Response format changes; MARK_SYNCED timing changes | If per-record fails, partial sync means some logs marked synced on CAM but not all sent |
| B4 | `DEVICE_ID` → `DEVICE_UID` (MAC address) | Old backend expects `DEVICE_001`; new expects `AA:BB:CC:DD:EE:FF` format | Device record must be created with MAC address in new DB |
| B5 | New `device_event_id` field | Old backend ignores it (harmless); new backend requires it | Forward-compatible — add to firmware before backend switch |

### 4.2 Dashboard Breaking Changes

| # | Change | Impact |
|---|--------|--------|
| B6 | Token format changes (custom JWT → Supabase JWT) | Complete auth rewrite |
| B7 | API URLs change (Express routes → Supabase client SDK) | Every `fetch()` call replaced |
| B8 | SSE → Supabase Realtime | Complete real-time rewrite |
| B9 | Photo URLs change (local static → Supabase Storage) | All `<img src>` paths change |
| B10 | Employee table renamed to `users`; `rfid_uid` moved to `user_credentials` | All employee queries restructured |

### 4.3 Migration Order to Minimize Breakage

**Critical insight:** Firmware and dashboard can be migrated independently because they don't communicate directly. The backend is the shared dependency.

```
Week 1: Deploy Supabase backend (schema + RLS + Edge Functions)
         Keep old Express backend running in parallel
         
Week 2: Migrate firmware to Supabase endpoints
         Old backend still running (dashboard still uses it)
         
Week 3: Migrate dashboard to Supabase client SDK
         Old backend can be decommissioned after verification
         
Week 4: Add provisioning, signup, polish
```

**Zero-downtime migration is achievable** because old and new backends coexist.

---

## 5. HIDDEN COUPLING DETECTED

### 5.1 Dashboard ↔ Backend Coupling

| Coupling | Location | Risk |
|----------|----------|------|
| **Photo serving** | `server.js:128` — `express.static(path.join(__dirname, '../dashboard'))` serves dashboard AND photos from same origin | Dashboard expects photos at `/photos/{filename}`. In Supabase, photos will be in Storage with signed URLs. Every `<img>` tag must change. |
| **SSE token passing** | `dashboard/index.html:419` — `EventSource(/api/events?token=${token})` | SSE uses query param for auth (non-standard). Supabase Realtime uses WebSocket with session auth. |
| **CSV export via redirect** | `dashboard/index.html:718` — `window.location.href = /api/export/csv?...&token=${token}` | Cannot do browser redirect to Supabase. Must fetch data and generate CSV client-side. |
| **Company context from JWT** | `server.js:140,280` — JWT `id` field IS the company ID | In Supabase, company context comes from `org_members` lookup, not JWT payload directly. Dashboard must query `org_members` after login. |

### 5.2 Firmware ↔ Backend Coupling

| Coupling | Location | Risk |
|----------|----------|------|
| **MARK_SYNCED timing** | `wroom_brain.ino:385` — sends `MARK_SYNCED` only after successful bulk POST | With per-record sync, MARK_SYNCED should only fire after ALL records succeed. If 48/50 succeed, the 2 failures will be lost. Must track per-record success. |
| **User list format** | `server.js:209-215` — returns `{ users: [{user_id, name, employee_id, department, rfid_uid}] }` | New Edge Function must return same JSON shape or firmware parsing breaks. |
| **Enrollment response** | `server.js:272` — returns `{ success: true, enrollment_id }` | Firmware checks `code==200\|\|code==201` (line 253). New Edge Function must return same HTTP codes. |

### 5.3 Critical Coupling Issue: Partial Sync

**This is the most dangerous hidden coupling in the entire codebase.**

Current flow (`wroom_brain.ino:343-389`):
1. `GET_PENDING` → CAM sends all pending logs
2. Brain builds bulk JSON
3. Single POST to `/attendance/bulk`
4. If `code==200` → `MARK_SYNCED` (moves ALL pending to synced)

New per-record flow:
1. `GET_PENDING` → CAM sends all pending logs
2. Brain POSTs each record individually
3. Some may succeed, some may fail (timeout, server error)
4. **If we send `MARK_SYNCED` → failed records are lost**
5. **If we don't send `MARK_SYNCED` → successful records will be re-sent (but idempotent via `device_event_id`)**

**Safe approach:** Always send `MARK_SYNCED` after the loop. Failed records will be re-synced on next cycle. `device_event_id` + `ON CONFLICT DO NOTHING` guarantees no duplicates. Some records will be submitted twice, but the second attempt is a harmless no-op.

**This requires `device_event_id` to be deterministic** — generated from the log data, not random. Format: `{DEVICE_UID}-{timestamp}-{rfid_uid}` ensures the same event always produces the same ID.

---

## 6. SECURITY VULNERABILITIES

| # | Severity | Vulnerability | Location | Fix |
|---|----------|--------------|----------|-----|
| S1 | **CRITICAL** | JWT fallback secret `'fallback_secret'` | `server.js:140,197` | Eliminated — Supabase Auth manages JWT signing |
| S2 | **CRITICAL** | Device secrets stored plaintext | `server.js:117`, `devices.secret` column | bcrypt hash at provisioning; validate via Edge Function |
| S3 | **CRITICAL** | No RLS — data isolation is app-logic only | Entire `server.js` | Supabase RLS policies with org-scoped access |
| S4 | **HIGH** | `enrollment_queue` not tenant-scoped | `server.js:89-96` | Redesigned in new schema |
| S5 | **HIGH** | Global RFID uniqueness blocks multi-tenant | `server.js:70` — `rfid_uid TEXT UNIQUE` | `UNIQUE(organization_id, type, value)` in `user_credentials` |
| S6 | **HIGH** | Hardcoded credentials in dashboard HTML | `dashboard/index.html:146-150` — `value="admin@ecotronic.com"` / `value="changeme123"` | Remove default values in production |
| S7 | **MEDIUM** | Photos served without auth | `server.js:128` — `express.static()` | Supabase Storage with RLS + signed URLs |
| S8 | **MEDIUM** | No rate limiting per org (only per IP) | `server.js:130-131` | Supabase + Edge Function per-device rate limiting |
| S9 | **MEDIUM** | No audit logging | Entire backend | Add `audit_logs` table or use Supabase Realtime + log drain |
| S10 | **LOW** | `timestamp` in attendance is client-provided | `server.js:225-226` — only validates age, not future | Add server-side timestamp validation in Edge Function |

---

## 7. DUPLICATE PROTECTION VALIDATION

### 7.1 Current Implementation — BROKEN

**Backend (`server.js:235-240`):**
```sql
INSERT INTO attendance (id,...) VALUES ($1,...) ON CONFLICT (id) DO NOTHING
```
Where `$1 = uuidv4()` — **a new random UUID every call.**

**Result:** `ON CONFLICT (id)` will NEVER trigger because every insert has a unique UUID. **Duplicate protection does not work.**

**Firmware (`wroom_brain.ino:162-163`):**
```cpp
if (uid == lastUID && millis() - lastTapMs < DUPLICATE_WINDOW) { ... }
```
This prevents duplicate **taps** (5s window) but NOT duplicate **submissions** during sync.

### 7.2 New Implementation — Correct

**Schema:**
```sql
UNIQUE(device_id, device_event_id)
```

**Edge Function:**
```sql
INSERT INTO attendance_logs (..., device_event_id) VALUES (..., $1)
ON CONFLICT (device_id, device_event_id) DO NOTHING
```

**Firmware generates deterministic ID:**
```cpp
String generateEventId(unsigned long ts) {
  return String(DEVICE_UID) + "-" + String(ts) + "-" + String(rfid_uid);
}
```

**This is correct.** Same event = same ID = safe retry.

---

## 8. OFFLINE SYNC SAFETY VALIDATION

### 8.1 Current Offline Flow (Unchanged by Migration)

1. Card tap → `addToQueue()` → `processQueue()` → `handleAttendance()`
2. `logAttendance()` → `camSerial.println("LOG:" + json)` → Slave writes to `/pending/*.jsonl`
3. If WiFi connected → `syncPendingLogs()` → reads from CAM → POSTs to server
4. If success → `MARK_SYNCED` → Slave moves files from `/pending/` to `/synced/`
5. If offline → logs accumulate in `/pending/` on SD card

### 8.2 What Changes

| Step | Before | After | Safe? |
|------|--------|-------|-------|
| 1-2 | Unchanged | Unchanged | ✅ |
| 3 | Single bulk POST | Per-record POST loop | ⚠️ See §5.3 |
| 4 | MARK_SYNCED after one 200 | MARK_SYNCED after loop completes | ✅ (with idempotent retry) |
| 5 | Unchanged | Unchanged | ✅ |

### 8.3 Power Loss Scenarios

| Scenario | Current Behavior | New Behavior | Safe? |
|----------|-----------------|-------------|-------|
| Power cut before sync | Logs safe on SD | Logs safe on SD | ✅ |
| Power cut during bulk POST | Entire batch fails; retried next cycle | Some records submitted; rest retried | ✅ (with `device_event_id`) |
| Power cut during MARK_SYNCED | Pending files remain; re-synced with duplicates | Same — but duplicates now blocked by `device_event_id` | ✅ **Improved** |
| WiFi drop mid-sync | HTTP timeout; retry next cycle | Per-record timeout; partial progress possible | ✅ **Improved** |

**Verdict:** Offline sync safety is **maintained and improved** by the migration.

---

## 9. BACKEND CODE DELETION SCHEDULE

### Delete After Phase 3 Complete (Dashboard Migrated)

| File | Lines | Reason |
|------|-------|--------|
| `backend/server.js` | 431 | Entirely replaced by Supabase Edge Functions + client SDK |
| `backend/package.json` | 19 | No Node.js backend needed |
| `backend/env.example` | 21 | Supabase config replaces all env vars |
| `backend/node_modules/` | ~thousands | No longer needed |

**Keep copies for 30 days after migration as reference.**

### What NOT to Delete

| File | Reason |
|------|--------|
| `firmware/wroom_brain/wroom_brain.ino` | Actively modified, not deleted |
| `firmware/esp32cam_slave/esp32cam_slave.ino` | **Untouched** — do not modify |
| `dashboard/index.html` | Refactored in place, not deleted |

---

## 10. MIGRATION EXECUTION PLAN

### Phase 1: Supabase Foundation (Days 1-3)

| Step | Action | Risk | Blocks |
|------|--------|------|--------|
| 1.1 | Create Supabase project | None | Everything |
| 1.2 | Run schema migration SQL (from plan.md §3.2) | Low | 1.1 |
| 1.3 | Apply RLS policies (from plan.md §3.3) | Medium | 1.2 |
| 1.4 | Create test org + admin user manually | Low | 1.2 |
| 1.5 | Insert test device with bcrypt-hashed secret | Low | 1.2 |
| 1.6 | Insert test employees + RFID credentials | Low | 1.2 |
| 1.7 | Verify RLS isolation (2-org test) | **Critical** | 1.3, 1.4 |

### Phase 2: Edge Functions (Days 3-7)

| Step | Action | Risk | Blocks |
|------|--------|------|--------|
| 2.1 | Create `device-login` Edge Function | Medium | 1.5 |
| 2.2 | Create `submit-log` Edge Function | Medium | 1.6 |
| 2.3 | Create `get-users` Edge Function | Medium | 1.6 |
| 2.4 | Create `device-provision` Edge Function | Medium | 1.5 |
| 2.5 | Create DB RPCs for dashboard aggregations | Low | 1.2 |
| 2.6 | Test all functions with curl/Postman | **Critical** | 2.1-2.4 |
| 2.7 | Verify duplicate protection (`device_event_id`) | **Critical** | 2.2 |

### Phase 3: Firmware Migration (Days 7-12)

| Step | Action | Risk | Blocks |
|------|--------|------|--------|
| 3.1 | Add `generateEventId()` to Brain firmware | Low | None |
| 3.2 | Add NVS credential storage functions | Low | None |
| 3.3 | Rewrite `downloadUsers()` for Supabase | Medium | 2.3 |
| 3.4 | Rewrite `syncPendingLogs()` for per-record sync | **High** | 2.2 |
| 3.5 | Update `logAttendance()` with `device_event_id` | Low | 3.1 |
| 3.6 | Update `handleEnroll()` for Supabase | Low | 2.4 |
| 3.7 | Add provisioning mode to `setup()` | Medium | 2.4, 3.2 |
| 3.8 | Flash to test device; run 7-day stress test | **Critical** | 3.1-3.7 |

**Test matrix for 3.8:**
- [ ] 1,000 sequential log submissions
- [ ] WiFi drop mid-sync (pull antenna)
- [ ] Power cut during sync (pull power)
- [ ] 50+ pending logs sync in one cycle
- [ ] `ESP.getFreeHeap()` stable after 24 hours
- [ ] Watchdog does not trigger during long sync

### Phase 4: Dashboard Migration (Days 12-18)

| Step | Action | Risk | Blocks |
|------|--------|------|--------|
| 4.1 | Add Supabase JS client to dashboard | Low | 1.1 |
| 4.2 | Replace `login()` with Supabase Auth | Low | 4.1 |
| 4.3 | Replace `api()` helper with Supabase queries | Medium | 4.1 |
| 4.4 | Replace SSE with Supabase Realtime | Medium | 4.1 |
| 4.5 | Replace photo URLs with Supabase Storage signed URLs | Medium | 4.1 |
| 4.6 | Rewrite CSV export as client-side generation | Low | 4.3 |
| 4.7 | Add org context awareness | Medium | 4.2 |
| 4.8 | Test multi-tenant dashboard isolation | **Critical** | 4.7 |

### Phase 5: Self-Service + Provisioning (Days 18-25)

| Step | Action | Risk | Blocks |
|------|--------|------|--------|
| 5.1 | Build signup page | Low | 4.2 |
| 5.2 | Build device provisioning UI | Medium | 2.4 |
| 5.3 | Build QR code generation for provision tokens | Low | 5.2 |
| 5.4 | End-to-end provisioning test (new company + new device) | **Critical** | 5.1-5.3 |
| 5.5 | Decommission old Express backend | Low | All phases verified |

---

## 11. VALIDATION CHECKLIST

### Multi-Tenant Isolation
- [ ] Org A admin cannot see Org B employees
- [ ] Org A admin cannot see Org B attendance logs
- [ ] Org A device cannot submit logs to Org B
- [ ] Same RFID works independently in both orgs
- [ ] JWT from Org A returns zero rows when querying Org B data

### Device Security
- [ ] Device secrets stored as bcrypt hash only
- [ ] Plaintext secret never retrievable from DB
- [ ] Revoked device returns 401 on all Edge Functions
- [ ] Expired provision token returns 401
- [ ] Used provision token returns 401 on reuse

### Duplicate Protection
- [ ] Same `device_event_id` submitted twice → 200 both times, 1 row in DB
- [ ] Power cut mid-sync → no duplicates after resync
- [ ] Network timeout → retry with same ID succeeds idempotently

### Offline Sync
- [ ] 50 logs buffered offline → all sync on reconnect
- [ ] SD card files move from `/pending/` to `/synced/` after sync
- [ ] No data loss during WiFi drop
- [ ] No data loss during power cut

### Dashboard
- [ ] Login works with Supabase Auth
- [ ] Live feed updates via Realtime
- [ ] Charts render with org-scoped data
- [ ] CSV export contains only current org data
- [ ] Signup creates org + owner membership

---

## 12. GO / NO-GO RECOMMENDATION

### ✅ GO — With Conditions

**The migration is approved.** The architecture is sound, the risks are identified and manageable, and the phased approach ensures no single-point-of-failure.

**Conditions for GO:**

1. **Phase 3 (firmware) must include a 7-day stress test** before Phase 4 begins. The `syncPendingLogs()` rewrite (F9) is the highest-risk change. Do not rush this.

2. **Keep old Express backend running for 30 days** after Phase 4 completion. If anything breaks, you can revert firmware to point at old endpoints.

3. **RLS isolation test (Step 1.7) is a hard gate.** Do not proceed to Phase 2 until you have verified with 2 test orgs that data leakage is impossible.

4. **`device_event_id` must be deterministic**, not random. Format: `{DEVICE_UID}-{timestamp}-{rfid_uid}`. This is non-negotiable for offline sync safety.

**What could cause a NO-GO:**
- ESP32 heap fragmentation during per-record sync loop (monitor `getFreeHeap()`)
- Supabase Edge Function cold start latency >5s (test under load)
- RLS policy misconfiguration discovered during testing

**Timeline:** 25 working days (5 weeks) with a single developer.
**Complexity:** MEDIUM.
**Confidence:** HIGH.

---

## Open Questions

1. **Photo storage migration:** Current photos are on the ESP32-CAM SD card and served via Express static files. Will photos be uploaded to Supabase Storage during sync, or remain on the SD card? This affects dashboard photo display.

2. **Enrollment redesign:** The current `enrollment_queue` table has no `company_id`. In the new schema, enrollment should go through the Edge Function. Is the current enrollment UX (tap card → appears in dashboard → admin assigns) still the desired flow?

3. **HMAC signature:** Brain firmware signs attendance logs with `hmacSHA256(raw, DEVICE_SECRET)`. Should the Edge Function verify this signature, or is bcrypt device auth sufficient? Keeping HMAC adds defense-in-depth but increases complexity.

4. **Multi-org admin:** Can a single Supabase Auth user be admin of multiple organizations? The current `org_members` schema supports this, but the dashboard would need an org switcher UI.

5. **Existing data migration:** Is there production data in the current PostgreSQL that needs to be migrated to Supabase, or is this a greenfield deployment?
