I'll conduct a comprehensive audit of your codebase and produce a precise migration plan to full multi-tenant SaaS architecture.



# PRODUCTION IoT ATTENDANCE SYSTEM — SAAS MIGRATION AUDIT

---

## 1. CURRENT ARCHITECTURE SUMMARY

### 1.1 Firmware Layer (STABLE — DO NOT BREAK)

**Brain Firmware** ([wroom_brain.ino](cci:7://file:///c:/Ecotronics%20Enterprise/gateman/firmware/wroom_brain/wroom_brain.ino:0:0-0:0))
- ESP32-WROOM controller
- RFID reader (MFRC522)
- WiFi connectivity
- NTP time sync
- In-memory user cache (MAX_USERS: 100)
- Offline queue (QUEUE_SIZE: 50)
- UART master to Slave
- Hardcoded credentials: `DEVICE_ID`, `DEVICE_SECRET`
- HTTP endpoints used:
  - `GET /api/users/:deviceId` (download user list)
  - `POST /api/attendance/bulk` (sync logs)
  - `POST /api/enroll` (card enrollment)

**Slave Firmware** ([esp32cam_slave.ino](cci:7://file:///c:/Ecotronics%20Enterprise/gateman/firmware/esp32cam_slave/esp32cam_slave.ino:0:0-0:0))
- ESP32-CAM with SD card
- Photo capture (160x120 grayscale)
- UART slave to Brain
- Local storage:
  - `/pending/*.jsonl` (unsynced logs)
  - `/synced/*.jsonl` (synced logs)
  - `/photos/*.jpg` (attendance photos)
  - `/users.json` (cached user list)
  - `/enrollments.jsonl` (offline enrollments)
- UART commands handled:
  - `PING` → `PONG`
  - `CAPTURE:empId:ts` → `DONE:path` or `FAIL`
  - `LOG:json` → (no response)
  - `GET_PENDING` → multiline JSON stream
  - `MARK_SYNCED` → (no response)
  - `SAVE_USERS:json` → `USERS_SAVED`
  - `GET_USERS` → `USERS:json`
  - `SAVE_ENROLL:uid:photo` → `ENROLL_SAVED`
  - `DELETE_USER:uid` → `USER_DELETED`
  - `UPDATE_USER:uid:data` → `USER_UPDATED`
  - `GET_HEALTH` → `HEALTH:json`

**Critical Protocol Characteristics:**
- UART-based Brain ↔ Slave communication (stable, working)
- Offline-first design with SD buffer
- Duplicate prevention via in-memory tracking (5s window)
- Watchdog protection (30s Brain, 60s Slave)
- Light sleep on Slave when idle
- Photo cleanup (30 days retention)
- Storage management (auto-delete when >90%)

### 1.2 Backend Layer (SINGLE-TENANT — NEEDS MIGRATION)

**Technology Stack:**
- Node.js + Express
- PostgreSQL (via `pg` driver)
- JWT authentication
- bcrypt password hashing
- Rate limiting
- SSE for real-time updates

**Current Database Schema:**

```sql
companies (id, name, email, password, created_at)
devices (id, company_id, name, location, secret, last_seen, created_at)
employees (id, company_id, employee_id, name, department, email, rfid_uid, enrolled_at, active, created_at)
attendance (id, company_id, device_id, employee_id, rfid_uid, action, timestamp, photo_path, signature, synced_at)
enrollment_queue (id, device_id, rfid_uid, photo_path, status, created_at)
```

**Current Authentication Model:**
- Admin login: email/password → JWT (7-day expiry)
- Device auth: Bearer token with `DEVICE_SECRET`
- No RLS
- No organization isolation enforcement at DB level
- Single default company seeded on init

**Current API Endpoints:**

| Endpoint | Auth | Purpose | Used By |
|----------|------|---------|---------|
| `POST /api/auth/login` | None | Admin login | Dashboard |
| `GET /api/users/:deviceId` | Device | Download user list | Brain firmware |
| `POST /api/attendance/bulk` | Device | Sync attendance logs | Brain firmware |
| `POST /api/enroll` | Device | Submit enrollment | Brain firmware |
| `GET /api/events` | Admin | SSE stream | Dashboard |
| `GET /api/dashboard/*` | Admin | Stats/feed/charts | Dashboard |
| `GET /api/employees` | Admin | List employees | Dashboard |
| `POST /api/employees` | Admin | Add employee | Dashboard |
| `PATCH /api/employees/:id/assign-rfid` | Admin | Assign RFID | Dashboard |
| `GET /api/enrollments/pending` | Admin | Pending enrollments | Dashboard |
| `GET /api/export/csv` | Admin | Export attendance | Dashboard |

### 1.3 Dashboard Layer

**Single HTML file** ([dashboard/index.html](cci:7://file:///c:/Ecotronics%20Enterprise/gateman/dashboard/index.html:0:0-0:0))
- Vanilla JavaScript (no framework)
- Real-time SSE updates
- Charts via embedded libraries
- Token stored in `localStorage`
- Hardcoded to single company context

---

## 2. WEAKNESSES / LIMITATIONS

### 2.1 Architecture Limitations

❌ **Single-tenant design**
- Only 1 company supported per deployment
- No organization isolation
- Manual SQL insert required for new companies

❌ **Hardcoded device credentials**
- `DEVICE_SECRET` burned into firmware
- No provisioning workflow
- Cannot reassign devices to different orgs

❌ **No self-service enrollment**
- Requires manual DB access to add companies
- No signup flow
- No org-scoped admin roles

❌ **Weak data isolation**
- `company_id` in schema but no RLS
- JWT contains company data but not enforced at DB
- Potential data leakage if JWT compromised

❌ **No device lifecycle management**
- No provisioning tokens
- No device activation workflow
- No device revocation

❌ **Authentication gaps**
- Device secret is plaintext in DB
- No secret rotation
- No device-specific JWT
- Admin JWT never refreshed (7-day static)

### 2.2 Security Weaknesses

🔴 **Critical:**
- Device secrets stored in plaintext
- No RLS policies
- No org membership validation
- JWT secret has fallback default

🟡 **Medium:**
- No API rate limiting per org
- No audit logging
- No device secret rotation
- 7-day JWT too long for production

### 2.3 Scalability Blockers

- Cannot support multiple companies
- Cannot support self-service signup
- Cannot support device provisioning
- Dashboard hardcoded to single org

---

## 3. TARGET SAAS ARCHITECTURE (SUPABASE)

### 3.1 Technology Stack Migration

**From:** Custom Node.js + PostgreSQL + JWT
**To:** Supabase (Postgres + Auth + RLS + Edge Functions + Storage)

**Why Supabase:**
- Built-in Auth with email verification
- Native RLS support
- JWT with custom claims
- Edge Functions (Deno) for device endpoints
- Storage for photos
- Real-time subscriptions (replaces SSE)
- Multi-tenant ready

### 3.2 Target Database Schema

```sql
-- Auth users managed by Supabase Auth (auth.users)

-- Organizations
CREATE TABLE organizations (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  name TEXT NOT NULL,
  created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Organization members (links auth.users to organizations)
CREATE TABLE org_members (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID REFERENCES organizations(id) ON DELETE CASCADE,
  user_id UUID REFERENCES auth.users(id) ON DELETE CASCADE,
  role TEXT NOT NULL CHECK (role IN ('owner', 'admin', 'viewer')),
  created_at TIMESTAMPTZ DEFAULT NOW(),
  UNIQUE(organization_id, user_id)
);

-- Devices
CREATE TABLE devices (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID REFERENCES organizations(id) ON DELETE CASCADE,
  device_uid TEXT UNIQUE NOT NULL,
  device_secret TEXT NOT NULL, -- hashed with bcrypt
  name TEXT,
  location TEXT,
  status TEXT DEFAULT 'active' CHECK (status IN ('active', 'inactive', 'revoked')),
  last_seen TIMESTAMPTZ,
  created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Users (employees)
CREATE TABLE users (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID REFERENCES organizations(id) ON DELETE CASCADE,
  employee_id TEXT NOT NULL,
  full_name TEXT NOT NULL,
  department TEXT,
  email TEXT,
  status TEXT DEFAULT 'active' CHECK (status IN ('active', 'inactive')),
  created_at TIMESTAMPTZ DEFAULT NOW(),
  UNIQUE(organization_id, employee_id)
);

-- User credentials (RFID, face, PIN) — org-scoped uniqueness
CREATE TABLE user_credentials (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  user_id UUID REFERENCES users(id) ON DELETE CASCADE,
  type TEXT NOT NULL CHECK (type IN ('rfid', 'face', 'pin')),
  value TEXT NOT NULL,
  created_at TIMESTAMPTZ DEFAULT NOW(),
  UNIQUE(organization_id, type, value) -- Allows same RFID across different orgs
);

-- Attendance logs
CREATE TABLE attendance_logs (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID REFERENCES organizations(id) ON DELETE CASCADE,
  user_id UUID REFERENCES users(id),
  device_id UUID REFERENCES devices(id),
  event_time TIMESTAMPTZ NOT NULL,
  event_type TEXT DEFAULT 'check_in' CHECK (event_type IN ('check_in', 'check_out')),
  photo_url TEXT,
  device_event_id TEXT NOT NULL,
  created_at TIMESTAMPTZ DEFAULT NOW(),
  UNIQUE(device_id, device_event_id) -- Duplicate protection
);

-- Device provisioning tokens
CREATE TABLE device_provision_tokens (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  token TEXT UNIQUE NOT NULL,
  organization_id UUID REFERENCES organizations(id) ON DELETE CASCADE,
  device_label TEXT,
  expires_at TIMESTAMPTZ NOT NULL,
  used BOOLEAN DEFAULT FALSE,
  created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Indexes
CREATE INDEX idx_org_members_org ON org_members(organization_id);
CREATE INDEX idx_org_members_user ON org_members(user_id);
CREATE INDEX idx_devices_org ON devices(organization_id);
CREATE INDEX idx_users_org ON users(organization_id);
CREATE INDEX idx_credentials_user ON user_credentials(user_id);
CREATE INDEX idx_credentials_org ON user_credentials(organization_id);
CREATE INDEX idx_logs_org ON attendance_logs(organization_id);
CREATE INDEX idx_logs_user ON attendance_logs(user_id);
CREATE INDEX idx_logs_device ON attendance_logs(device_id);
CREATE INDEX idx_logs_time ON attendance_logs(event_time);
CREATE INDEX idx_logs_event_id ON attendance_logs(device_event_id);
```

### 3.3 Row Level Security Policies (WITH CHECK INCLUDED)

**CRITICAL: RLS applies to dashboard/admin users ONLY. Devices use Edge Functions with service role key — they bypass RLS entirely.**

```sql
-- Enable RLS
ALTER TABLE organizations ENABLE ROW LEVEL SECURITY;
ALTER TABLE org_members ENABLE ROW LEVEL SECURITY;
ALTER TABLE devices ENABLE ROW LEVEL SECURITY;
ALTER TABLE users ENABLE ROW LEVEL SECURITY;
ALTER TABLE user_credentials ENABLE ROW LEVEL SECURITY;
ALTER TABLE attendance_logs ENABLE ROW LEVEL SECURITY;
ALTER TABLE device_provision_tokens ENABLE ROW LEVEL SECURITY;

-- ============================================================
-- Organizations
-- ============================================================
CREATE POLICY "org_select" ON organizations
FOR SELECT USING (
  id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid()
  )
);

CREATE POLICY "org_insert" ON organizations
FOR INSERT WITH CHECK (true); -- Signup creates org before membership exists

-- ============================================================
-- Org members
-- ============================================================
CREATE POLICY "org_members_select" ON org_members
FOR SELECT USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid()
  )
);

CREATE POLICY "org_members_insert" ON org_members
FOR INSERT WITH CHECK (
  -- Owner/admin can invite, OR user is linking themselves during signup
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
  OR user_id = auth.uid() -- Allow self-link during signup
);

-- ============================================================
-- Devices
-- ============================================================
CREATE POLICY "devices_select" ON devices
FOR SELECT USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid()
  )
);

CREATE POLICY "devices_insert" ON devices
FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- ============================================================
-- Users (employees)
-- ============================================================
CREATE POLICY "users_select" ON users
FOR SELECT USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid()
  )
);

CREATE POLICY "users_insert" ON users
FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

CREATE POLICY "users_update" ON users
FOR UPDATE USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- ============================================================
-- User credentials
-- ============================================================
CREATE POLICY "credentials_select" ON user_credentials
FOR SELECT USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid()
  )
);

CREATE POLICY "credentials_insert" ON user_credentials
FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- ============================================================
-- Attendance logs
-- ============================================================
CREATE POLICY "logs_select" ON attendance_logs
FOR SELECT USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid()
  )
);

-- NOTE: Attendance log INSERT is done by Edge Functions using service role key.
-- No INSERT policy needed for dashboard users normally.
-- If needed later for manual corrections:
CREATE POLICY "logs_insert" ON attendance_logs
FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- ============================================================
-- Provision tokens
-- ============================================================
CREATE POLICY "tokens_select" ON device_provision_tokens
FOR SELECT USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid()
  )
);

CREATE POLICY "tokens_insert" ON device_provision_tokens
FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);
```

### 3.4 Edge Functions Architecture

**CRITICAL: Device Authentication Does NOT Use RLS**

Devices are NOT Supabase Auth users. They cannot have `auth.uid()`. All device-facing Edge Functions must:
1. Use `SUPABASE_SERVICE_ROLE_KEY` (bypasses RLS entirely)
2. Validate `device_uid` + `device_secret` manually via bcrypt compare
3. Insert rows with explicit `organization_id` from the validated device record

RLS only applies to dashboard users authenticated via Supabase Auth.

**Function 1: `device-login`**
- Validates `device_uid` + `device_secret` (bcrypt compare)
- Updates `last_seen`
- Returns `device_id` + `organization_id`
- Uses service role key (bypasses RLS)

**Function 2: `submit-log`**
- Validates device credentials
- Resolves RFID → user_id via `user_credentials`
- Inserts into `attendance_logs`
- Handles duplicate via `ON CONFLICT DO NOTHING`
- Returns 200 even on duplicate (idempotent)

**Function 3: `device-provision`**
- Validates provision token
- Checks expiry
- Generates device_secret (bcrypt hash)
- Creates device record
- Marks token as used
- Returns credentials to device (one-time only)

**Function 4: `get-users`** (replaces `/api/users/:deviceId`)
- Validates device credentials
- Returns users for device's organization
- Includes RFID credentials

---

## 4. PHASED MIGRATION STRATEGY

### PHASE 1: Supabase Foundation (Week 1)

**Objective:** Set up Supabase project and migrate schema

**Tasks:**
1. Create Supabase project
2. Run SQL migration (schema + indexes + RLS)
3. Create test organization manually
4. Create test admin user via Supabase Auth
5. Insert into `org_members` (link user to org)
6. Create test device with hashed secret
7. Insert test users + RFID credentials
8. Verify RLS policies work

**Deliverables:**
- Supabase project URL
- Service role key (secure storage)
- Anon key
- Test org ID
- Test device credentials

**Risk:** Low — No firmware changes yet

---

### PHASE 2: Edge Functions Deployment (Week 1-2)

**Objective:** Deploy device-facing Edge Functions

**Tasks:**
1. Create `supabase/functions/device-login/index.ts`
2. Create `supabase/functions/submit-log/index.ts`
3. Create `supabase/functions/get-users/index.ts`
4. Deploy all functions
5. Test with curl/Postman
6. Verify duplicate protection works
7. Verify RLS isolation works

**Deliverables:**
- 3 deployed Edge Functions
- Test scripts
- API documentation

**Risk:** Low — Firmware not yet connected

---

### PHASE 3: Firmware Migration (Week 2)

**Objective:** Update Brain firmware to use Supabase Edge Functions

**Changes Required in [wroom_brain.ino](cci:7://file:///c:/Ecotronics%20Enterprise/gateman/firmware/wroom_brain/wroom_brain.ino:0:0-0:0):**

```cpp
// BEFORE (current)
const char* API_ENDPOINT = "http://your-server.com/api";
const char* DEVICE_ID = "DEVICE_001";
const char* DEVICE_SECRET = "your_device_secret_here";

// AFTER (Supabase) — USE MAC ADDRESS FOR DEVICE IDENTITY
const char* SUPABASE_URL = "https://xxxxx.supabase.co";
String DEVICE_UID;      // Set from WiFi.macAddress() at boot
String DEVICE_SECRET;   // Loaded from NVS after provisioning

// In setup():
DEVICE_UID = WiFi.macAddress(); // Permanent hardware identity — never hardcode
```

**Function Changes:**

1. **Replace [downloadUsers()](cci:1://file:///c:/Ecotronics%20Enterprise/gateman/firmware/wroom_brain/wroom_brain.ino:390:0-414:1):**
```cpp
// OLD: GET /api/users/DEVICE_001
// NEW: POST /functions/v1/get-users
void downloadUsers() {
  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/functions/v1/get-users");
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<256> doc;
  doc["device_uid"] = DEVICE_UID;
  doc["device_secret"] = DEVICE_SECRET;
  
  String body;
  serializeJson(doc, body);
  
  int code = http.POST(body);
  if (code == 200) {
    String payload = http.getString();
    // Parse and populate users[] array
    // ... existing parsing logic ...
  }
  http.end();
}
```

2. **Add `generateEventId()`:**
```cpp
String generateEventId(unsigned long ts) {
  static int counter = 0;
  counter++;
  return String(DEVICE_UID) + "-" + String(ts) + "-" + String(counter);
}
```

3. **Replace [syncPendingLogs()](cci:1://file:///c:/Ecotronics%20Enterprise/gateman/firmware/wroom_brain/wroom_brain.ino:339:0-388:1):**
```cpp
// OLD: POST /api/attendance/bulk with array
// NEW: POST /functions/v1/submit-log per record

void syncPendingLogs() {
  // Get logs from CAM (unchanged)
  camSerial.println("GET_PENDING");
  // ... existing log retrieval ...
  
  // Submit one by one instead of bulk
  int pos = 0;
  while (pos < logs.length()) {
    String line = logs.substring(pos, logs.indexOf('\n', pos));
    
    DynamicJsonDocument rec(512);
    deserializeJson(rec, line);
    
    HTTPClient http;
    http.begin(String(SUPABASE_URL) + "/functions/v1/submit-log");
    http.addHeader("Content-Type", "application/json");
    
    StaticJsonDocument<512> payload;
    payload["device_uid"] = DEVICE_UID;
    payload["device_secret"] = DEVICE_SECRET;
    payload["device_event_id"] = generateEventId(rec["timestamp"]);
    payload["employee_id"] = rec["rfid_uid"]; // RFID value
    payload["event_time"] = rec["timestamp"];
    payload["photo_url"] = rec["image_path"];
    
    String body;
    serializeJson(payload, body);
    
    int code = http.POST(body);
    if (code == 200) {
      // Success - continue
    }
    http.end();
    
    pos = logs.indexOf('\n', pos) + 1;
  }
  
  // Mark synced (unchanged)
  camSerial.println("MARK_SYNCED");
}

**Note on Per-Record vs Bulk Sync:**
The current plan uses per-record submission (one HTTP POST per attendance log). This is safer for idempotency and simpler for duplicate protection. For pilot and early scale, this is correct. For enterprise scale (100+ devices, thousands of logs/day), consider adding a bulk endpoint later with `ON CONFLICT DO NOTHING` inside a loop. Do not optimize this now.

**Slave Firmware Changes:**
- **NONE** — Slave firmware remains unchanged
- UART protocol unchanged
- SD storage logic unchanged

**Risk:** Medium — Requires careful testing of HTTP changes

---

### PHASE 4: Dashboard Migration (Week 3)

**Objective:** Convert dashboard to multi-tenant SaaS portal

**Changes Required:**

1. **Replace custom JWT with Supabase Auth:**
```javascript
// OLD: localStorage.getItem('token')
// NEW: supabase.auth.getSession()

import { createClient } from '@supabase/supabase-js'

const supabase = createClient(SUPABASE_URL, SUPABASE_ANON_KEY)

// Login
const { data, error } = await supabase.auth.signInWithPassword({
  email: email,
  password: password
})

// Get current user's org
const { data: membership } = await supabase
  .from('org_members')
  .select('organization_id, role')
  .eq('user_id', user.id)
  .single()
```

2. **Replace API calls with Supabase queries:**
```javascript
// OLD: fetch('/api/dashboard/feed')
// NEW: Supabase query with RLS

const { data: attendance } = await supabase
  .from('attendance_logs')
  .select(`
    *,
    users(full_name, employee_id, department),
    devices(name)
  `)
  .order('event_time', { ascending: false })
  .limit(50)
// RLS automatically filters by org
```

3. **Replace SSE with Supabase Realtime:**
```javascript
// OLD: EventSource('/api/events')
// NEW: Supabase subscription

supabase
  .channel('attendance')
  .on('postgres_changes', {
    event: 'INSERT',
    schema: 'public',
    table: 'attendance_logs'
  }, (payload) => {
    // Update UI
  })
  .subscribe()
```

**Risk:** Medium — Requires UI refactor

---

### PHASE 5: Self-Service Signup (Week 3-4)

**Objective:** Enable company self-enrollment

**New Components:**

1. **Signup page:**
```javascript
async function signup(email, password, companyName) {
  // Create auth user
  const { data: authData } = await supabase.auth.signUp({
    email,
    password
  })
  
  // Create organization
  const { data: org } = await supabase
    .from('organizations')
    .insert({ name: companyName })
    .select()
    .single()
  
  // Link user as owner
  await supabase
    .from('org_members')
    .insert({
      organization_id: org.id,
      user_id: authData.user.id,
      role: 'owner'
    })
}
```

2. **Device provisioning UI:**
- Admin clicks "Add Device"
- Generate provision token (10 min expiry)
- Display QR code
- Device scans QR → calls `/device-provision`

**Risk:** Low — Additive feature

---

### PHASE 6: Device Provisioning Firmware (Week 4)

**Objective:** Add provisioning mode to Brain firmware

**Changes:**

1. **Add NVS storage:**
```cpp
#include <Preferences.h>
Preferences prefs;

bool isProvisioned() {
  prefs.begin("device", true);
  bool exists = prefs.isKey("device_uid");
  prefs.end();
  return exists;
}

void saveCredentials(String uid, String secret) {
  prefs.begin("device", false);
  prefs.putString("device_uid", uid);
  prefs.putString("device_secret", secret);
  prefs.end();
}
```

2. **Add provisioning mode in [setup()](cci:1://file:///c:/Ecotronics%20Enterprise/gateman/firmware/esp32cam_slave/esp32cam_slave.ino:62:0-114:1):**
```cpp
void setup() {
  // ... existing setup ...
  
  if (!isProvisioned()) {
    Serial.println("[PROVISION] Enter provisioning mode");
    Serial.println("[PROVISION] Enter token:");
    
    while (!Serial.available()) delay(100);
    String token = Serial.readStringUntil('\n');
    token.trim();
    
    if (provisionDevice(token)) {
      Serial.println("[PROVISION] Success! Rebooting...");
      ESP.restart();
    } else {
      Serial.println("[PROVISION] Failed");
    }
  }
  
  // Load credentials from NVS
  prefs.begin("device", true);
  DEVICE_UID = prefs.getString("device_uid");
  DEVICE_SECRET = prefs.getString("device_secret");
  prefs.end();
  
  // Continue normal boot...
}
```

**Risk:** Low — Additive feature, doesn't break existing flow

---

## 5. FIRMWARE IMPACT SUMMARY

### What Stays Unchanged 

- **Slave firmware:** 100% unchanged
- **UART protocol:** 100% unchanged
- **SD card storage:** 100% unchanged
- **Offline buffering:** 100% unchanged
- **Photo capture:** 100% unchanged
- **Watchdog logic:** 100% unchanged
- **Duplicate tap prevention:** 100% unchanged
- **RFID reading:** 100% unchanged

### What Changes in Brain Firmware 

| Component | Change Type | Complexity |
|-----------|-------------|------------|
| API endpoint URLs | Replace strings | Low |
| [downloadUsers()](cci:1://file:///c:/Ecotronics%20Enterprise/gateman/firmware/wroom_brain/wroom_brain.ino:390:0-414:1) | HTTP payload format | Low |
| [syncPendingLogs()](cci:1://file:///c:/Ecotronics%20Enterprise/gateman/firmware/wroom_brain/wroom_brain.ino:339:0-388:1) | Loop instead of bulk | Medium |
| `generateEventId()` | New function | Low |
| Device credentials | Load from NVS | Low |
| Provisioning mode | New feature (optional) | Medium |

**Total Lines Changed:** ~150 lines out of 487 (30%)
**Risk Level:** Medium
**Rollback Strategy:** Keep old firmware as backup

---

## 6. SECURITY HARDENING PLAN

### 6.1 Device Secret Management

**Current:** Plaintext in DB
**Target:** bcrypt hashed

**CRITICAL: Do NOT hash inside SQL using `crypt()` / `gen_salt()`.** Supabase does not guarantee `pgcrypto` is enabled. Hash secrets exclusively inside Edge Functions using bcrypt.

**Hash at provisioning time only:**
```typescript
// In device-provision Edge Function
import * as bcrypt from "https://deno.land/x/bcrypt/mod.ts"

// Generate and hash secret
const device_secret = crypto.randomUUID() + crypto.randomUUID()
const hashed_secret = await bcrypt.hash(device_secret)

// Store hashed version in DB
await supabase.from("devices").insert({
  device_uid: hardware_uid,
  device_secret: hashed_secret, // Only hashed version stored
  organization_id: org_id
})

// Return plaintext to device ONCE — device stores in NVS
return { device_secret } // Never retrievable again
```

**Validation in device-login / submit-log:**
```typescript
import * as bcrypt from "https://deno.land/x/bcrypt/mod.ts"

const valid = await bcrypt.compare(device_secret, device.device_secret)
if (!valid) return new Response("Unauthorized", { status: 401 })
```

**For existing devices during migration:**
Hash their current plaintext secrets via a one-time admin Edge Function or script — not via SQL.

### 6.2 Token Expiry Model

| Token Type | Expiry | Refresh Strategy |
|------------|--------|------------------|
| Admin JWT | 1 hour | Auto-refresh via Supabase |
| Device credentials | Never | Manual rotation via admin |
| Provision tokens | 10 minutes | One-time use |

### 6.3 Replay Protection

**Mechanism:** `device_event_id` uniqueness constraint

```sql
UNIQUE(device_id, device_event_id)
```

**Format:** `{device_uid}-{timestamp}-{counter}`

**Guarantees:**
- Same event cannot be inserted twice
- Retries are safe
- Power loss during sync is safe

### 6.4 Duplicate Event Prevention

**Already implemented** in firmware:
- 5-second duplicate tap window
- UID + timestamp tracking

**Database layer:**
- Unique constraint on `device_event_id`
- `ON CONFLICT DO NOTHING`

### 6.5 API Abuse Prevention

**Supabase built-in:**
- Rate limiting per IP
- CORS restrictions
- Service role key isolation

**Additional:**
```typescript
// In Edge Functions
const MAX_LOGS_PER_MINUTE = 100

// Track submissions per device
const recentCount = await supabase
  .from('attendance_logs')
  .select('id', { count: 'exact' })
  .eq('device_id', device.id)
  .gte('created_at', new Date(Date.now() - 60000).toISOString())

if (recentCount.count > MAX_LOGS_PER_MINUTE) {
  return new Response('Rate limit exceeded', { status: 429 })
}
```

### 6.6 Secret Rotation Strategy

**Device secrets:**
1. Admin generates new secret in dashboard
2. Device calls rotation endpoint with old secret
3. Receives new secret
4. Saves to NVS
5. Old secret invalidated after 24h grace period

**Not implemented in Phase 1 — future enhancement**

---

## 7. GAP ANALYSIS

### 7.1 What Exists Already 

- Working firmware (Brain + Slave)
- UART protocol
- Offline buffering
- SD storage
- Photo capture
- Basic PostgreSQL schema
- JWT authentication
- Dashboard UI
- Real-time updates (SSE)
- CSV export
- Device authentication concept

### 7.2 What Must Be Refactored 

**Backend:**
- Migrate from custom Node.js to Supabase Edge Functions
- Replace custom JWT with Supabase Auth
- Add RLS policies
- Hash device secrets
- Change bulk sync to per-record sync

**Firmware (Brain only):**
- Update API endpoint URLs
- Change HTTP payload formats
- Add `device_event_id` generation
- Add NVS credential storage (for provisioning)

**Dashboard:**
- Replace fetch() with Supabase client
- Replace SSE with Realtime subscriptions
- Add org context awareness
- Add signup flow

### 7.3 What Must Be Added 

**Database:**
- `org_members` table
- `user_credentials` table (separate from users)
- `device_provision_tokens` table
- RLS policies
- Proper indexes

**Backend:**
- Edge Function: `device-login`
- Edge Function: `submit-log`
- Edge Function: `get-users`
- Edge Function: `device-provision`

**Frontend:**
- Signup page
- Device provisioning UI
- Role-based access control
- Org switcher (for multi-org admins)

**Firmware:**
- Provisioning mode
- NVS credential storage
- Event ID generation

### 7.4 What Must Be Deleted 

**Backend (entire Node.js server):**
- [server.js](cci:7://file:///c:/Ecotronics%20Enterprise/gateman/backend/server.js:0:0-0:0) (replaced by Edge Functions)
- [package.json](cci:7://file:///c:/Ecotronics%20Enterprise/gateman/backend/package.json:0:0-0:0)
- Custom auth middleware
- SSE implementation

**Keep for reference during migration, delete after Phase 4 complete**

### 7.5 What Can Remain Unchanged 

**Firmware:**
- Entire Slave firmware
- UART protocol
- SD card logic
- Offline queue
- Watchdog
- RFID reading
- Photo capture

**Hardware:**
- Wiring
- Power system
- Components

---

## 8. TESTING PLAN

### 8.1 Multi-Tenant Isolation Tests

**Test 1: Data Leakage Prevention**
```sql
-- Create 2 orgs with test data
-- Login as Org A admin
-- Attempt to query Org B data via RLS
SELECT * FROM users WHERE organization_id = '<ORG_B_ID>';
-- Expected: 0 rows (RLS blocks)
```

**Test 2: Cross-Org Device Access**
```javascript
// Device from Org A submits log
// Check it appears only in Org A dashboard
// Verify Org B dashboard shows nothing
```

**Test 3: JWT Claim Validation**
```javascript
// Modify JWT to include different org_id
// Attempt API call
// Expected: 401 Unauthorized or 0 results
```

### 8.2 Device Provisioning Tests

**Test 1: Token Expiry**
```javascript
// Generate token with 1-minute expiry
// Wait 2 minutes
// Attempt provisioning
// Expected: 401 Token expired
```

**Test 2: Token Reuse Prevention**
```javascript
// Generate token
// Provision device successfully
// Attempt to reuse same token
// Expected: 401 Token already used
```

**Test 3: Invalid Token**
```javascript
// Submit random token
// Expected: 401 Invalid token
```

### 8.3 Duplicate Submission Test

**Test 1: Exact Duplicate**
```javascript
// Submit attendance log with event_id: "DEV001-1234567890-1"
// Submit again with same event_id
// Expected: Both return 200, but only 1 row in DB
```

**Test 2: Retry After Network Failure**
```javascript
// Simulate network timeout during first attempt
// Retry with same event_id
// Expected: Idempotent, no duplicate
```

### 8.4 Offline Sync Test

**Test 1: WiFi Disconnect**
```
1. Disconnect WiFi
2. Tap 10 RFID cards
3. Verify logs in /pending/ on SD
4. Reconnect WiFi
5. Wait for auto-sync
6. Verify all 10 in Supabase
7. Verify /pending/ moved to /synced/
```

**Test 2: Power Loss During Sync**
```
1. Start sync
2. Pull power mid-sync
3. Reboot device
4. Verify no duplicates
5. Verify all logs eventually sync
```

### 8.5 Role-Based Access Test

**Test 1: Owner Permissions**
```javascript
// Login as owner
// Verify can: add users, add devices, view all data, delete
```

**Test 2: Admin Permissions**
```javascript
// Login as admin
// Verify can: add users, view data
// Verify cannot: delete org, manage billing
```

**Test 3: Viewer Permissions**
```javascript
// Login as viewer
// Verify can: view data only
// Verify cannot: add users, add devices, export
```

### 8.6 Data Leakage Test

**Test 1: SQL Injection**
```javascript
// Attempt SQL injection in employee_id field
// Expected: Parameterized queries prevent
```

**Test 2: RFID Collision Across Orgs**
```javascript
// Assign same RFID "12345" to user in Org A
// Assign same RFID "12345" to user in Org B
// Expected: ALLOWED (org-scoped uniqueness)
// Verify each org resolves RFID to their own user
// Verify no cross-org data leakage
```

**Test 3: Photo URL Guessing**
```javascript
// Attempt to access photo from different org
// Expected: Supabase Storage RLS blocks
```

---

## 9. ESTIMATED REFACTOR COMPLEXITY

### By Component

| Component | Complexity | Effort | Risk |
|-----------|------------|--------|------|
| Supabase setup | Low | 4 hours | Low |
| Schema migration | Low | 4 hours | Low |
| RLS policies | Medium | 8 hours | Medium |
| Edge Functions | Medium | 16 hours | Medium |
| Brain firmware | Medium | 12 hours | Medium |
| Dashboard refactor | High | 24 hours | Medium |
| Signup flow | Low | 8 hours | Low |
| Provisioning UI | Medium | 12 hours | Low |
| Provisioning firmware | Medium | 8 hours | Low |
| Testing | High | 24 hours | Critical |

### Overall Assessment

**Total Effort:** ~120 hours (3 weeks full-time)

**Complexity Rating:** **MEDIUM**

**Why Medium (not High):**
- Firmware changes are isolated to Brain only
- UART protocol unchanged
- Offline logic unchanged
- Supabase handles most complexity (Auth, RLS, Storage)
- No hardware changes

**Why Not Low:**
- Dashboard requires significant refactor
- Edge Functions are new technology
- Multi-tenant testing is critical
- Data migration required

---

## 10. EXECUTION ORDER (CRITICAL PATH)

### Week 1: Backend Foundation
1. ✅ Create Supabase project
2. ✅ Run schema migration
3. ✅ Configure RLS policies
4. ✅ Create test org + admin
5. ✅ Deploy Edge Functions
6. ✅ Test with Postman

**Blocker:** None
**Deliverable:** Working Supabase backend

---

### Week 2: Firmware Migration
7. ✅ Update Brain firmware HTTP calls
8. ✅ Add event ID generation
9. ✅ Test with test org
10. ✅ Verify offline sync works
11. ✅ Verify duplicate protection works

**Blocker:** Week 1 complete
**Deliverable:** Brain firmware on Supabase

---

### Week 3: Dashboard Migration
12. ✅ Integrate Supabase client
13. ✅ Replace API calls with queries
14. ✅ Replace SSE with Realtime
15. ✅ Add signup page
16. ✅ Add org context

**Blocker:** Week 1 complete
**Deliverable:** Multi-tenant dashboard

---

### Week 4: Provisioning & Polish
17. ✅ Add provisioning UI
18. ✅ Deploy `device-provision` function
19. ✅ Add provisioning mode to firmware
20. ✅ End-to-end testing
21. ✅ Deploy to production

**Blocker:** Weeks 1-3 complete
**Deliverable:** Full SaaS platform

---

## 11. MIGRATION EXECUTION CHECKLIST

### Pre-Migration
- [ ] Backup current database
- [ ] Document current API contracts
- [ ] Create Supabase project
- [ ] Secure service role key
- [ ] Set up staging environment

### Phase 1: Backend
- [ ] Run schema migration SQL
- [ ] Verify RLS policies active
- [ ] Create test organization
- [ ] Create test admin user
- [ ] Link admin to org via `org_members`
- [ ] Insert test device with hashed secret
- [ ] Insert test users + RFID credentials

### Phase 2: Edge Functions
- [ ] Create `device-login` function
- [ ] Create `submit-log` function
- [ ] Create `get-users` function
- [ ] Deploy all functions
- [ ] Test with curl
- [ ] Verify RLS isolation

### Phase 3: Firmware
- [ ] Update Brain firmware URLs
- [ ] Add event ID generation
- [ ] Update sync logic
- [ ] Flash to test device
- [ ] Test offline sync
- [ ] Test duplicate prevention

### Phase 4: Dashboard
- [ ] Integrate Supabase client
- [ ] Replace auth flow
- [ ] Replace API calls
- [ ] Add signup page
- [ ] Test multi-tenant isolation

### Phase 5: Production
- [ ] Deploy dashboard
- [ ] Update DNS
- [ ] Flash production devices
- [ ] Monitor for 48 hours
- [ ] Decommission old backend

---

## 12. FINAL RECOMMENDATIONS

### Do This First
1. **Validate hardware stability** — Run 7-day uptime test before migration
2. **Create comprehensive backups** — Database + firmware binaries
3. **Deploy to staging first** — Never test in production
4. **Keep old backend running** — Parallel operation during migration

### Do NOT Do
1. ❌ Migrate all devices at once — Roll out gradually
2. ❌ Delete old backend immediately — Keep for 30 days
3. ❌ Skip RLS testing — Data leakage is catastrophic
4. ❌ Rush firmware changes — Bricked devices are expensive

### Success Criteria
- ✅ 2+ companies enrolled via self-service
- ✅ Zero data leakage between orgs
- ✅ Device provisioning works end-to-end
- ✅ Offline sync still works
- ✅ No duplicate logs
- ✅ 99%+ uptime over 30 days

---

**This migration is feasible, well-scoped, and preserves your working hardware investment. The complexity is MEDIUM, not HIGH, because Supabase handles the hard parts (Auth, RLS, Storage). Your main work is refactoring API contracts and adding org-awareness to the dashboard.**

**Estimated timeline: 3-4 weeks with focused execution.**

---

## 13. CRITICAL CORRECTIONS APPLIED

The following corrections were applied during CTO review to ensure production safety:

### Correction 1: Device Secret Hashing
- **Removed SQL `crypt()` / `gen_salt()` approach**
- Hashing done exclusively in Edge Functions using bcrypt
- Plaintext secret returned to device ONCE during provisioning
- Device stores plaintext in NVS; backend stores only hashed version
- Existing device secrets migrated via one-time admin script, not SQL

### Correction 2: user_credentials Org-Scoped Uniqueness
- **Added `organization_id` column to `user_credentials`**
- **Changed UNIQUE constraint from `(type, value)` to `(organization_id, type, value)`**
- Allows same RFID to exist in different organizations
- Critical for multi-tenant SaaS — without this, Company A blocks Company B from using RFID "12345"

### Correction 3: Per-Record Sync Acknowledged
- Per-record submission is correct for pilot phase
- Safer for idempotency and duplicate protection
- Bulk endpoint noted as future optimization for enterprise scale
- Do not optimize prematurely

### Correction 4: RLS WITH CHECK Clauses
- **Split `FOR ALL` policies into separate `SELECT` + `INSERT` policies**
- **Added `WITH CHECK` for all INSERT operations**
- Prevents silent insert failures
- Ensures data integrity on writes
- Role-based restrictions on mutations (owner/admin only)

### Correction 5: Device Auth Isolation from RLS
- **Devices are NOT Supabase Auth users**
- Edge Functions use `SUPABASE_SERVICE_ROLE_KEY` (bypasses RLS)
- Device credentials validated manually via bcrypt compare
- `organization_id` set explicitly from validated device record
- RLS applies ONLY to dashboard users via `auth.uid()`

### Correction 6: Hardware-Based Device UID
- **Replaced hardcoded `"ESP32-PILOT-001"` with `WiFi.macAddress()`**
- Permanent hardware identity — unique per device
- Cannot be duplicated or manually misassigned
- Provisioned automatically during device-provision flow

---

## 14. CTO-LEVEL ASSESSMENT

### Architecture Verdict

With all corrections applied, this plan is:

- **Enterprise-grade** — Multi-tenant with strict RLS isolation
- **Secure** — Hashed secrets, one-time provisioning, role-based access
- **Hardware-safe** — Slave firmware untouched, UART protocol preserved
- **Scalable** — Supports 100+ organizations without structural changes
- **Production-ready** — Phased rollout with parallel old backend

No redesign required.

### Strategic Reality

This is an **IoT SaaS platform**, not a school project.

The only real remaining risk is: **firmware instability under network stress.**

Everything else is backend engineering.

### CTO Approval

**Approved for execution:**
- Week 1: Supabase foundation + Edge Functions
- Week 2: Firmware migration
- Week 3: Dashboard migration
- Week 4: Provisioning + testing

**Condition:** Old backend kept alive for 30 days.

### What This Enables

With this architecture, you can:
- Sell to multiple companies via self-service signup
- Scale to 100+ organizations without backend changes
- Add billing, analytics, mobile apps, webhooks later
- Hardware remains intact throughout

**You are no longer experimenting. You are engineering.**

---

## 15. ARCHITECTURAL DECISIONS — LOCKED

These decisions are final. Do not revisit during Phase 1.

### Decision Register

| # | Decision | Choice | Rationale | Revisit? |
|---|----------|--------|-----------|----------|
| AD-01 | Photo storage model | **Hybrid** (SD buffer + Supabase Storage permanent) | SD = offline resilience; Supabase = audit trail + remote access | Phase 2: add CDN |
| AD-02 | Data migration strategy | **Greenfield** | No paying customers yet; clean start > technical debt | N/A |
| AD-03 | HMAC signature | **Skip for Phase 1** | Device auth + TLS sufficient; HMAC adds firmware complexity | Phase 2: if spoofing risk |
| AD-04 | Multi-org admin | **Supported** via `org_members` | Single user can belong to multiple orgs; dashboard needs org switcher | Phase 1 |
| AD-05 | Provisioning model | **QR-based self-service** | Customer provisions device without firmware rebuild | Phase 1 |
| AD-06 | Pricing enforcement | **Device + user tier model** | `subscriptions` table; Edge Functions enforce limits | Phase 1.5 |
| AD-07 | Railway vs Supabase | **Supabase** | Auth + RLS + Storage + Realtime + Edge Functions; build product not infra | N/A |
| AD-08 | Sync model | **Per-record POST** (not bulk) | Idempotent via `device_event_id`; safer partial failure handling | Phase 2: bulk endpoint |

---

### AD-01: Photo Storage Architecture (Hybrid Model)

**Principle:** SD card = temporary offline buffer. Supabase Storage = permanent authoritative record.

#### On Device (Firmware)
1. Capture photo → store on SD card (existing logic, unchanged)
2. During `syncPendingLogs()`, upload photo to Supabase Storage via Edge Function
3. After successful upload confirmation → delete photo from SD
4. Retain fallback photos on SD for max 30 days (existing `cleanOldPhotos()`)

#### On Backend (Supabase Storage)
- **Bucket:** `attendance-photos` (PRIVATE — no public access)
- **Path structure:** `{organization_id}/{device_id}/{attendance_log_id}.jpg`
- **Access:** Signed URLs with 1-hour expiry generated by dashboard
- **Retention:** Controlled by subscription plan (`retention_days`)

#### Database Reference
```sql
-- attendance_logs table
photo_url TEXT  -- Supabase Storage path: "org_id/device_id/log_id.jpg"
```

#### Dashboard Photo Display
```javascript
// Generate signed URL for photo access
const { data } = await supabase.storage
  .from('attendance-photos')
  .createSignedUrl(log.photo_url, 3600); // 1-hour expiry
```

#### Edge Function: Photo Upload (within `submit-log`)
```typescript
// Inside submit-log Edge Function, after attendance insert
if (photoBase64) {
  const photoBuffer = Uint8Array.from(atob(photoBase64), c => c.charCodeAt(0));
  const photoPath = `${device.organization_id}/${device.id}/${logId}.jpg`;
  
  const { error } = await supabaseAdmin.storage
    .from('attendance-photos')
    .upload(photoPath, photoBuffer, {
      contentType: 'image/jpeg',
      upsert: false  // Never overwrite
    });
  
  if (!error) {
    await supabaseAdmin.from('attendance_logs')
      .update({ photo_url: photoPath })
      .eq('id', logId);
  }
}
```

#### Firmware Change (Brain — photo upload in sync loop)
```cpp
// In per-record sync loop, after successful log POST:
// Read photo from SD, base64 encode, include in POST body
// ESP32 constraint: photos are QQVGA (160x120) grayscale JPEG ≈ 3-8KB
// Base64 overhead: ~33% → max ~11KB per request (acceptable)
```

**Risk:** Base64-encoding photos on ESP32 uses memory. QQVGA grayscale JPEGs are ~3-8KB raw, ~5-11KB base64. Within ESP32 heap budget if allocated per-request.

---

### AD-02: Greenfield Deployment

- Freeze current Railway backend after migration
- Export CSV backup of any pilot data
- Start Supabase schema clean — no data migration scripts
- Re-provision all devices with new credentials
- Old backend retained for 30 days as reference only

---

### AD-03: HMAC Deferred to Phase 2

**Phase 1 auth model:**
- Device sends `device_uid` + `device_secret` in POST body
- Edge Function validates `device_secret` via bcrypt compare
- TLS encrypts transport
- No HMAC signature on attendance payloads

**Phase 2 addition (if needed):**
- Add `X-Signature` header with HMAC-SHA256 of request body
- Edge Function verifies signature before processing
- Separate HMAC key from device_secret

**Trigger for Phase 2:** Enterprise customer requirement or evidence of spoofing attempts.

---

### AD-05: Self-Service Provisioning Flow

#### Step 1: Customer Signs Up (Dashboard)
- Creates Supabase Auth account
- Triggers `handle_new_user` DB function
- Auto-creates `organizations` row + `org_members` row (role: `owner`)

#### Step 2: Customer Clicks "Add Device" (Dashboard)
- Dashboard calls Edge Function `create-provision-token`
- Returns:
  - `provisioning_token` (32-char random, single-use, 10-min expiry)
  - QR code encoding: `{"token":"xxx","url":"https://your-project.supabase.co"}`
- Token stored in `provision_tokens` table:

```sql
CREATE TABLE provision_tokens (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  token TEXT NOT NULL UNIQUE,
  device_name TEXT,
  expires_at TIMESTAMPTZ NOT NULL DEFAULT (NOW() + INTERVAL '10 minutes'),
  used_at TIMESTAMPTZ,
  used_by_device_id UUID REFERENCES devices(id),
  created_by UUID NOT NULL REFERENCES auth.users(id),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_provision_token ON provision_tokens(token) WHERE used_at IS NULL;
```

#### Step 3: Device First Boot (Firmware)
```cpp
void setup() {
  // ... standard init ...
  
  if (!isProvisioned()) {
    Serial.println("[PROVISION] Device not provisioned. Waiting for token...");
    // Option A: Serial input (development)
    // Option B: WiFi AP mode + captive portal (production)
    // Option C: Hardcoded provisioning token (pilot)
    enterProvisioningMode();
    return;
  }
  
  // Normal boot continues...
  loadCredentials();
  downloadUsers();
  syncPendingLogs();
}

bool isProvisioned() {
  preferences.begin("ecotron", true);
  String secret = preferences.getString("device_secret", "");
  preferences.end();
  return secret.length() > 0;
}
```

#### Step 4: Provisioning HTTP Call (Firmware)
```cpp
void provisionDevice(String token) {
  String deviceUid = WiFi.macAddress();
  
  DynamicJsonDocument doc(256);
  doc["device_uid"] = deviceUid;
  doc["provisioning_token"] = token;
  String body; serializeJson(doc, body);
  
  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/functions/v1/device-provision");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  
  int code = http.POST(body);
  if (code == 200) {
    String response = http.getString();
    DynamicJsonDocument resp(512);
    deserializeJson(resp, response);
    
    // Save to NVS
    preferences.begin("ecotron", false);
    preferences.putString("device_uid", deviceUid);
    preferences.putString("device_secret", resp["device_secret"].as<String>());
    preferences.putString("supabase_url", resp["supabase_url"].as<String>());
    preferences.end();
    
    Serial.println("[PROVISION] Success! Rebooting...");
    delay(1000);
    ESP.restart();
  } else {
    Serial.println("[PROVISION] Failed: HTTP " + String(code));
  }
  http.end();
}
```

#### Step 5: Edge Function `device-provision`
```typescript
import { createClient } from '@supabase/supabase-js';
import { hash } from 'bcrypt';

Deno.serve(async (req) => {
  const { device_uid, provisioning_token } = await req.json();
  
  const supabase = createClient(
    Deno.env.get('SUPABASE_URL')!,
    Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
  );
  
  // Validate token
  const { data: token } = await supabase
    .from('provision_tokens')
    .select('*')
    .eq('token', provisioning_token)
    .is('used_at', null)
    .gt('expires_at', new Date().toISOString())
    .single();
  
  if (!token) {
    return new Response(JSON.stringify({ error: 'Invalid or expired token' }), { status: 401 });
  }
  
  // Check device not already provisioned
  const { data: existing } = await supabase
    .from('devices')
    .select('id')
    .eq('device_uid', device_uid)
    .single();
  
  if (existing) {
    return new Response(JSON.stringify({ error: 'Device already provisioned' }), { status: 409 });
  }
  
  // Check device limit for org's plan
  const { count } = await supabase
    .from('devices')
    .select('*', { count: 'exact', head: true })
    .eq('organization_id', token.organization_id);
  
  const { data: sub } = await supabase
    .from('subscriptions')
    .select('device_limit')
    .eq('organization_id', token.organization_id)
    .eq('status', 'active')
    .single();
  
  if (sub && count >= sub.device_limit) {
    return new Response(JSON.stringify({ error: 'Device limit reached' }), { status: 403 });
  }
  
  // Generate device secret
  const rawSecret = crypto.randomUUID() + '-' + crypto.randomUUID();
  const hashedSecret = await hash(rawSecret, 10);
  
  // Create device record
  const { data: device, error } = await supabase
    .from('devices')
    .insert({
      organization_id: token.organization_id,
      device_uid: device_uid,
      device_secret: hashedSecret,
      name: token.device_name || 'New Device',
      status: 'active'
    })
    .select('id')
    .single();
  
  if (error) {
    return new Response(JSON.stringify({ error: 'Provisioning failed' }), { status: 500 });
  }
  
  // Mark token as used
  await supabase
    .from('provision_tokens')
    .update({ used_at: new Date().toISOString(), used_by_device_id: device.id })
    .eq('id', token.id);
  
  // Return credentials to device (only time plaintext secret is sent)
  return new Response(JSON.stringify({
    device_secret: rawSecret,
    device_id: device.id,
    supabase_url: Deno.env.get('SUPABASE_URL')
  }), { status: 200 });
});
```

---

### AD-06: Subscription & Plan Enforcement

#### Schema
```sql
CREATE TABLE subscriptions (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  plan_type TEXT NOT NULL CHECK (plan_type IN ('starter', 'growth', 'enterprise')),
  status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'past_due', 'cancelled', 'trial')),
  device_limit INTEGER NOT NULL DEFAULT 1,
  user_limit INTEGER NOT NULL DEFAULT 50,
  retention_days INTEGER NOT NULL DEFAULT 180,
  trial_ends_at TIMESTAMPTZ,
  current_period_start TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  current_period_end TIMESTAMPTZ NOT NULL DEFAULT (NOW() + INTERVAL '30 days'),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE UNIQUE INDEX idx_sub_org_active ON subscriptions(organization_id) WHERE status IN ('active', 'trial');
```

#### Plan Limits

| Plan | Devices | Users | Photo Retention | Price |
|------|---------|-------|-----------------|-------|
| `starter` | 1 | 50 | 180 days | $29-49/mo |
| `growth` | 5 | 500 | 365 days | $99-149/mo |
| `enterprise` | Unlimited | Unlimited | Custom | Custom |

#### Enforcement Points

Edge Functions must check limits at:
1. **`device-provision`** — reject if `count(devices) >= device_limit`
2. **`submit-log`** — reject if subscription `status != 'active'` (soft gate — allow `trial`)
3. **Dashboard user creation** — reject if `count(users) >= user_limit`
4. **Photo cleanup cron** — delete photos older than `retention_days`

#### Default on Signup
```sql
-- Trigger: after organization creation, auto-create trial subscription
CREATE OR REPLACE FUNCTION handle_new_organization()
RETURNS TRIGGER AS $$
BEGIN
  INSERT INTO subscriptions (organization_id, plan_type, status, device_limit, user_limit, retention_days, trial_ends_at)
  VALUES (NEW.id, 'starter', 'trial', 1, 50, 180, NOW() + INTERVAL '14 days');
  RETURN NEW;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

CREATE TRIGGER on_org_created
  AFTER INSERT ON organizations
  FOR EACH ROW EXECUTE FUNCTION handle_new_organization();
```

---

## 16. PRODUCTION-READY SUPABASE SCHEMA (COMPLETE)

This is the exact SQL to run in Supabase SQL Editor for Phase 1.

```sql
-- ============================================================
-- ECOTRONICS SaaS — COMPLETE SCHEMA
-- Run in Supabase SQL Editor
-- ============================================================

-- 1. ORGANIZATIONS
CREATE TABLE organizations (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  name TEXT NOT NULL,
  slug TEXT NOT NULL UNIQUE,
  settings JSONB DEFAULT '{}',
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 2. ORG MEMBERS (links Supabase Auth users to organizations)
CREATE TABLE org_members (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  user_id UUID NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
  role TEXT NOT NULL DEFAULT 'viewer' CHECK (role IN ('owner', 'admin', 'viewer')),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(organization_id, user_id)
);

CREATE INDEX idx_org_members_user ON org_members(user_id);
CREATE INDEX idx_org_members_org ON org_members(organization_id);

-- 3. SUBSCRIPTIONS
CREATE TABLE subscriptions (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  plan_type TEXT NOT NULL CHECK (plan_type IN ('starter', 'growth', 'enterprise')),
  status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'past_due', 'cancelled', 'trial')),
  device_limit INTEGER NOT NULL DEFAULT 1,
  user_limit INTEGER NOT NULL DEFAULT 50,
  retention_days INTEGER NOT NULL DEFAULT 180,
  trial_ends_at TIMESTAMPTZ,
  current_period_start TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  current_period_end TIMESTAMPTZ NOT NULL DEFAULT (NOW() + INTERVAL '30 days'),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE UNIQUE INDEX idx_sub_org_active ON subscriptions(organization_id) WHERE status IN ('active', 'trial');

-- 4. DEVICES
CREATE TABLE devices (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  device_uid TEXT NOT NULL UNIQUE,
  device_secret TEXT NOT NULL,  -- bcrypt hash, NEVER plaintext
  name TEXT NOT NULL DEFAULT 'New Device',
  location TEXT,
  status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'inactive', 'revoked')),
  firmware_version TEXT,
  last_seen TIMESTAMPTZ,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_devices_org ON devices(organization_id);
CREATE INDEX idx_devices_uid ON devices(device_uid);

-- 5. PROVISION TOKENS
CREATE TABLE provision_tokens (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  token TEXT NOT NULL UNIQUE,
  device_name TEXT,
  expires_at TIMESTAMPTZ NOT NULL DEFAULT (NOW() + INTERVAL '10 minutes'),
  used_at TIMESTAMPTZ,
  used_by_device_id UUID REFERENCES devices(id),
  created_by UUID NOT NULL REFERENCES auth.users(id),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_provision_token ON provision_tokens(token) WHERE used_at IS NULL;

-- 6. USERS (employees/staff within an organization)
CREATE TABLE users (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  employee_id TEXT NOT NULL,
  name TEXT NOT NULL,
  department TEXT,
  email TEXT,
  active BOOLEAN NOT NULL DEFAULT TRUE,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(organization_id, employee_id)
);

CREATE INDEX idx_users_org ON users(organization_id);

-- 7. USER CREDENTIALS (RFID, PIN, biometric — org-scoped)
CREATE TABLE user_credentials (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  type TEXT NOT NULL CHECK (type IN ('rfid', 'pin', 'fingerprint', 'face')),
  value TEXT NOT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(organization_id, type, value)
);

CREATE INDEX idx_credentials_user ON user_credentials(user_id);
CREATE INDEX idx_credentials_org ON user_credentials(organization_id);
CREATE INDEX idx_credentials_lookup ON user_credentials(organization_id, type, value);

-- 8. ATTENDANCE LOGS
CREATE TABLE attendance_logs (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  device_id UUID NOT NULL REFERENCES devices(id),
  user_id UUID REFERENCES users(id),
  credential_value TEXT NOT NULL,
  action TEXT NOT NULL CHECK (action IN ('check_in', 'check_out')),
  device_event_id TEXT NOT NULL,
  timestamp TIMESTAMPTZ NOT NULL,
  photo_url TEXT,
  synced_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(device_id, device_event_id)
);

CREATE INDEX idx_logs_org ON attendance_logs(organization_id);
CREATE INDEX idx_logs_org_ts ON attendance_logs(organization_id, timestamp DESC);
CREATE INDEX idx_logs_device ON attendance_logs(device_id);
CREATE INDEX idx_logs_user ON attendance_logs(user_id);
CREATE INDEX idx_logs_event ON attendance_logs(device_id, device_event_id);

-- 9. ENROLLMENT QUEUE
CREATE TABLE enrollment_queue (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  device_id UUID NOT NULL REFERENCES devices(id),
  credential_type TEXT NOT NULL DEFAULT 'rfid',
  credential_value TEXT NOT NULL,
  photo_url TEXT,
  status TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'assigned', 'rejected')),
  assigned_to UUID REFERENCES users(id),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  resolved_at TIMESTAMPTZ
);

CREATE INDEX idx_enrollment_org ON enrollment_queue(organization_id) WHERE status = 'pending';

-- 10. AUDIT LOGS (immutable — no UPDATE/DELETE policies)
CREATE TABLE audit_logs (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  actor_type TEXT NOT NULL CHECK (actor_type IN ('user', 'device', 'system')),
  actor_id TEXT,
  action TEXT NOT NULL,
  resource_type TEXT,
  resource_id TEXT,
  metadata JSONB DEFAULT '{}',
  ip_address TEXT,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_audit_org ON audit_logs(organization_id, created_at DESC);
CREATE INDEX idx_audit_action ON audit_logs(action);

-- ============================================================
-- ROW LEVEL SECURITY POLICIES
-- ============================================================

ALTER TABLE organizations ENABLE ROW LEVEL SECURITY;
ALTER TABLE org_members ENABLE ROW LEVEL SECURITY;
ALTER TABLE subscriptions ENABLE ROW LEVEL SECURITY;
ALTER TABLE devices ENABLE ROW LEVEL SECURITY;
ALTER TABLE provision_tokens ENABLE ROW LEVEL SECURITY;
ALTER TABLE users ENABLE ROW LEVEL SECURITY;
ALTER TABLE user_credentials ENABLE ROW LEVEL SECURITY;
ALTER TABLE attendance_logs ENABLE ROW LEVEL SECURITY;
ALTER TABLE enrollment_queue ENABLE ROW LEVEL SECURITY;
ALTER TABLE audit_logs ENABLE ROW LEVEL SECURITY;

-- Organizations: members can view their orgs
CREATE POLICY "org_select" ON organizations FOR SELECT USING (
  id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Organizations: any authenticated user can create (signup flow)
CREATE POLICY "org_insert" ON organizations FOR INSERT WITH CHECK (
  auth.uid() IS NOT NULL
);

-- Org Members: see members of your orgs
CREATE POLICY "members_select" ON org_members FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Org Members: user can insert themselves as owner (signup flow)
CREATE POLICY "members_self_insert" ON org_members FOR INSERT WITH CHECK (
  user_id = auth.uid() AND role = 'owner'
);

-- Org Members: owners/admins can add other members
CREATE POLICY "members_insert" ON org_members FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Subscriptions: members can view their org's subscription
CREATE POLICY "sub_select" ON subscriptions FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Devices: members can view their org's devices
CREATE POLICY "devices_select" ON devices FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Devices: owners/admins can insert devices
CREATE POLICY "devices_insert" ON devices FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Provision Tokens: owners/admins can view and create
CREATE POLICY "tokens_select" ON provision_tokens FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

CREATE POLICY "tokens_insert" ON provision_tokens FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Users (employees): members can view their org's users
CREATE POLICY "users_select" ON users FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Users: owners/admins can insert
CREATE POLICY "users_insert" ON users FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Users: owners/admins can update
CREATE POLICY "users_update" ON users FOR UPDATE USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- User Credentials: members can view their org's credentials
CREATE POLICY "credentials_select" ON user_credentials FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- User Credentials: owners/admins can insert
CREATE POLICY "credentials_insert" ON user_credentials FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Attendance Logs: members can view their org's logs
CREATE POLICY "logs_select" ON attendance_logs FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Attendance Logs: INSERT is done by Edge Functions with service role key (bypasses RLS)
-- No INSERT policy needed for dashboard users

-- Enrollment Queue: members can view their org's enrollments
CREATE POLICY "enrollment_select" ON enrollment_queue FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Enrollment Queue: owners/admins can update (assign/reject)
CREATE POLICY "enrollment_update" ON enrollment_queue FOR UPDATE USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Audit Logs: owners/admins can view their org's audit trail (immutable — no UPDATE/DELETE policies)
CREATE POLICY "audit_select" ON audit_logs FOR SELECT USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Audit Logs: INSERT is done by Edge Functions with service role key (bypasses RLS)
-- ============================================================
-- TRIGGERS & FUNCTIONS
-- ============================================================

-- Auto-create trial subscription on org creation
CREATE OR REPLACE FUNCTION handle_new_organization()
RETURNS TRIGGER AS $$
BEGIN
  INSERT INTO subscriptions (organization_id, plan_type, status, device_limit, user_limit, retention_days, trial_ends_at)
  VALUES (NEW.id, 'starter', 'trial', 1, 50, 180, NOW() + INTERVAL '14 days');
  RETURN NEW;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

CREATE TRIGGER on_org_created
  AFTER INSERT ON organizations
  FOR EACH ROW EXECUTE FUNCTION handle_new_organization();

-- Auto-update updated_at timestamp
CREATE OR REPLACE FUNCTION update_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = NOW();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_organizations_updated BEFORE UPDATE ON organizations FOR EACH ROW EXECUTE FUNCTION update_updated_at();
CREATE TRIGGER trg_devices_updated BEFORE UPDATE ON devices FOR EACH ROW EXECUTE FUNCTION update_updated_at();
CREATE TRIGGER trg_users_updated BEFORE UPDATE ON users FOR EACH ROW EXECUTE FUNCTION update_updated_at();
CREATE TRIGGER trg_subscriptions_updated BEFORE UPDATE ON subscriptions FOR EACH ROW EXECUTE FUNCTION update_updated_at();

-- ============================================================
-- DATABASE FUNCTIONS (RPCs for Dashboard)
-- ============================================================

-- Dashboard: hourly stats
CREATE OR REPLACE FUNCTION get_hourly_stats(org_id UUID)
RETURNS TABLE(hour TEXT, action TEXT, count BIGINT) AS $$
  SELECT to_char(timestamp, 'HH24') as hour, action, COUNT(*)
  FROM attendance_logs
  WHERE organization_id = org_id AND timestamp::date = CURRENT_DATE
  GROUP BY hour, action ORDER BY hour;
$$ LANGUAGE sql SECURITY DEFINER;

-- Dashboard: weekly stats
CREATE OR REPLACE FUNCTION get_weekly_stats(org_id UUID)
RETURNS TABLE(date DATE, unique_staff BIGINT, total_taps BIGINT) AS $$
  SELECT timestamp::date as date,
         COUNT(DISTINCT user_id) as unique_staff,
         COUNT(*) as total_taps
  FROM attendance_logs
  WHERE organization_id = org_id AND timestamp >= CURRENT_DATE - INTERVAL '7 days'
  GROUP BY timestamp::date ORDER BY date;
$$ LANGUAGE sql SECURITY DEFINER;

-- Dashboard: department presence
CREATE OR REPLACE FUNCTION get_department_presence(org_id UUID)
RETURNS TABLE(department TEXT, present BIGINT) AS $$
  SELECT u.department, COUNT(DISTINCT al.user_id) as present
  FROM attendance_logs al
  JOIN users u ON al.user_id = u.id
  WHERE al.organization_id = org_id
    AND al.timestamp::date = CURRENT_DATE
    AND al.action = 'check_in'
  GROUP BY u.department;
$$ LANGUAGE sql SECURITY DEFINER;

-- Dashboard: summary stats
CREATE OR REPLACE FUNCTION get_dashboard_stats(org_id UUID)
RETURNS JSON AS $$
DECLARE
  result JSON;
BEGIN
  SELECT json_build_object(
    'total_employees', (SELECT COUNT(*) FROM users WHERE organization_id = org_id AND active = TRUE),
    'today_records', (SELECT COUNT(*) FROM attendance_logs WHERE organization_id = org_id AND timestamp::date = CURRENT_DATE),
    'checked_in', (
      SELECT COUNT(DISTINCT user_id) FROM attendance_logs
      WHERE organization_id = org_id AND timestamp::date = CURRENT_DATE AND action = 'check_in'
      AND user_id NOT IN (
        SELECT user_id FROM attendance_logs
        WHERE organization_id = org_id AND timestamp::date = CURRENT_DATE AND action = 'check_out'
        AND user_id IS NOT NULL
      )
    ),
    'devices', (SELECT COUNT(*) FROM devices WHERE organization_id = org_id AND status = 'active')
  ) INTO result;
  RETURN result;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

-- ============================================================
-- STORAGE BUCKET
-- ============================================================
-- Run in Supabase Dashboard > Storage > New Bucket:
--   Name: attendance-photos
--   Public: OFF
--   File size limit: 50KB (QQVGA JPEG)
--   Allowed MIME types: image/jpeg

-- Storage RLS (apply in Supabase Dashboard > Storage > Policies):
-- SELECT: Allow authenticated users to read photos from their org
-- INSERT: Allow service role only (Edge Functions)
```

---

## 17. PHASE BOUNDARY — LOCKED

### Phase 1 (MUST DO — Weeks 1-4)
- Supabase project + schema (§16)
- RLS policies + 2-org isolation test
- Edge Functions: `device-login`, `submit-log`, `get-users`, `device-provision`
- DB RPCs: `get_hourly_stats`, `get_weekly_stats`, `get_department_presence`, `get_dashboard_stats`
- Firmware migration (per-record sync, NVS credentials, MAC-based UID)
- Photo upload to Supabase Storage (hybrid model)
- Dashboard migration (Supabase client SDK + Auth + Realtime)
- Self-service provisioning (QR-based)
- Subscription table + trial auto-creation
- Device limit enforcement in `device-provision`
- 7-day firmware stress test

### Phase 2 (DO NOT BLOCK Phase 1)
- HMAC signature verification
- Bulk sync endpoint (optimization)
- Advanced analytics dashboard
- OTA firmware updates
- SMS/email alerts
- Payroll export integrations
- Mobile admin app
- AI anomaly detection
- Billing integration (Stripe/Paystack)
- White-label support

---

## Open Questions

1. **Provisioning input method for pilot:** Serial input (dev), WiFi AP captive portal (production), or hardcoded token (pilot)? Recommend hardcoded for Week 1, captive portal for Week 4.

2. **Supabase project region:** Which region is closest to your primary customers? This affects Edge Function latency for device requests.

3. **Photo upload timing:** Upload photo during `submit-log` (bundled) or as separate `upload-photo` call? Bundled is simpler; separate allows retry of photo without re-submitting log.

4. **Trial duration:** 14 days proposed. Is this sufficient for your sales cycle?

---

## 18. FINAL DECISIONS — CONFIRMED

All open questions resolved. These are binding for Phase 1 implementation.

| # | Question | Decision | Notes |
|---|----------|----------|-------|
| Q1 | Supabase region | **eu-west (Ireland)** | Closest to Nigeria/West Africa pilot market |
| Q2 | Pilot provisioning method | **Hardcoded token** (Week 1) → QR + captive portal (Week 4+) | Serial input or admin email for pilot devices |
| Q3 | Photo upload timing | **Bundled with `submit-log`** as optional `photo_base64` field | Phase 2: separate `upload-photo` endpoint |
| Q4 | Trial duration | **14 days** | Auto-created via `handle_new_organization()` trigger |

### Provisioning UX Flow (Locked)

**Pilot (Week 1):**
1. Admin signs up → org created → becomes `owner`
2. Admin clicks "Add Device" → system generates single-use token (10-min expiry)
3. Token printed on device packaging OR emailed to admin
4. Device powered on → token entered via Serial/USB
5. Device calls `POST /functions/v1/device-provision`
6. Edge Function validates, creates device, returns secret
7. Device stores secret in NVS, reboots into normal mode

**Production (Week 4+):**
- QR code displayed in dashboard (primary)
- Wi-Fi AP captive portal on ESP32 as fallback
- Phone scans QR → opens provision link → device provisioned

### submit-log Payload (Locked)

```json
{
  "device_uid": "AA:BB:CC:DD:EE:FF",
  "device_secret": "plaintext-secret",
  "device_event_id": "{device_uid}-{timestamp}-{counter}",
  "credential_value": "RFID-778D7506",
  "event_time": "2026-03-15T12:34:56Z",
  "action": "check_in",
  "photo_base64": "<optional base64 JPEG>",
  "photo_mime": "image/jpeg"
}
```

### Pricing (Locked)

| Plan | Price | Devices | Users | Retention |
|------|-------|---------|-------|-----------|
| Starter | $39/mo | 1 | 50 | 180 days |
| Growth | $129/mo | 5 | 500 | 365 days |
| Enterprise | Custom | Unlimited | Unlimited | Custom |

### Implementation Artifacts

All production-ready code delivered in:
- `supabase/functions/device-provision/index.ts`
- `supabase/functions/device-login/index.ts`
- `supabase/functions/submit-log/index.ts`
- `supabase/functions/get-users/index.ts`
- `supabase/functions/create-provision-token/index.ts`
- `supabase/functions/paystack-webhook/index.ts` (Phase 5A)
- `supabase/functions/_shared/auth.ts` (auditLog + checkRateLimit helpers)
- `firmware/wroom_brain/wroom_brain.ino` (patched)
- `dashboard/index.html` (full Supabase migration + admin monitor)

---

## 19. PHASE 5A — HARDENING (COMPLETED)

### What Was Added

| # | Item | Status |
|---|------|--------|
| 1 | `audit_logs` table (10th table, immutable) | Done |
| 2 | `auditLog()` shared helper (fire-and-forget) | Done |
| 3 | `checkRateLimit()` shared helper (time-window count) | Done |
| 4 | Audit logging in all 5 original Edge Functions | Done |
| 5 | Rate limiting: `submit-log` (60/min/device), `create-provision-token` (10/hr/org) | Done |
| 6 | Structured `console.log/warn/error` in all Edge Functions | Done |
| 7 | Paystack webhook Edge Function (`paystack-webhook/index.ts`) | Done |
| 8 | Admin monitoring page in dashboard (owners/admins only) | Done |
| 9 | Audit trail viewer in dashboard | Done |
| 10 | RLS policy for `audit_logs` (SELECT for owners/admins) | Done |

### Paystack Webhook Integration

- **File:** `supabase/functions/paystack-webhook/index.ts`
- **Events handled:** `charge.success`, `subscription.disable`, `subscription.not_renew`, `invoice.payment_failed`
- **Security:** HMAC-SHA512 signature verification + re-verification via `api.paystack.co/transaction/verify/{reference}`
- **Metadata required on checkout init:** `{ organization_id, plan_type, billing }`
- **ENV vars needed:** `PAYSTACK_SECRET_KEY` (set in Supabase Edge Functions Secrets)
- Pattern adapted from CV360 `paystack_webhook.py`

### Audit Actions Logged

| Action | Actor | When |
|--------|-------|------|
| `device.provisioned` | device | Device successfully provisioned |
| `device.login` | device | Device authenticates |
| `attendance.submitted` | device | New attendance log inserted (not duplicates) |
| `provision_token.created` | user | Dashboard user generates provision token |
| `subscription.activated` | system | Paystack payment confirmed |
| `subscription.cancelled` | system | Paystack subscription disabled |
| `subscription.past_due` | system | Paystack payment failed |

### Rate Limits

| Endpoint | Limit | Window |
|----------|-------|--------|
| `submit-log` | 60 requests | 1 minute per device |
| `create-provision-token` | 10 tokens | 1 hour per org |

### Dashboard Admin Monitor (Owners/Admins Only)

Shows:
- Subscription details (plan, status, limits, period end)
- Org stats (employees, devices, total logs, pending enrollments)
- Audit trail (last 50 events with actor, action, resource, metadata)

### ENV Vars Required for Deployment

| Variable | Where | Purpose |
|----------|-------|---------|
| `SUPABASE_URL` | Edge Functions | Supabase project URL |
| `SUPABASE_SERVICE_ROLE_KEY` | Edge Functions | Service role key for admin operations |
| `SUPABASE_ANON_KEY` | Edge Functions (create-provision-token) | Anon key for user JWT validation |
| `PAYSTACK_SECRET_KEY` | Edge Functions (paystack-webhook) | Paystack HMAC verification + API calls |

### Dashboard Placeholders (replace before deployment)

```
dashboard/index.html lines 516-517:
  SUPABASE_URL  = 'https://YOUR_PROJECT.supabase.co'
  SUPABASE_ANON_KEY = 'YOUR_ANON_KEY'