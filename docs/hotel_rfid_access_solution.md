# EcoTronic Hotel RFID Access Control — Solution Architecture & Implementation Plan

**Document Version:** 1.0
**Date:** March 1, 2026
**Author:** EcoTronic Enterprise
**Client Use Case:** Hotel Revenue Leakage Prevention via RFID Room Access Logging

---

## 1. EXECUTIVE SUMMARY

A hotel is experiencing **revenue leakage** — guests overstay, unauthorized room access occurs, and there is no reliable audit trail of who accessed which room and when. The solution: replace existing door hardware with **RFID-enabled smart locks**, deploy a **central reception logger**, and integrate everything with our existing multi-tenant SaaS platform (Gateman).

### Core Concept

```
┌─────────────────────────────────────────────────────────────────────┐
│                        HOTEL PROPERTY                               │
│                                                                     │
│   ┌──────────────┐     ┌──────────────┐     ┌──────────────┐       │
│   │  ROOM 101    │     │  ROOM 102    │     │  ROOM 103    │       │
│   │  RFID Lock   │     │  RFID Lock   │     │  RFID Lock   │       │
│   │  (Battery)   │     │  (Battery)   │     │  (Battery)   │       │
│   │  SD Offline  │     │  SD Offline  │     │  SD Offline  │       │
│   └──────┬───────┘     └──────┬───────┘     └──────┬───────┘       │
│          │ WiFi/Collector     │                     │               │
│          ▼                    ▼                     ▼               │
│   ┌─────────────────────────────────────────────────────────┐      │
│   │              CENTRAL RECEPTION LOGGER                    │      │
│   │   ESP32-WROOM + RFID Reader + ESP32-CAM                 │      │
│   │   - Key card tap before guest gets key                   │      │
│   │   - Photo capture of guest at check-in                   │      │
│   │   - Real-time sync OR SD card offline buffer             │      │
│   └──────────────────────┬──────────────────────────────────┘      │
│                          │ WiFi / Ethernet                          │
└──────────────────────────┼──────────────────────────────────────────┘
                           ▼
              ┌────────────────────────┐
              │   SUPABASE CLOUD       │
              │   (Existing SaaS)      │
              │   - Edge Functions     │
              │   - PostgreSQL + RLS   │
              │   - Storage (Photos)   │
              │   - Realtime           │
              └────────────┬───────────┘
                           ▼
              ┌────────────────────────┐
              │   HOTEL DASHBOARD      │
              │   - Room access logs   │
              │   - Guest management   │
              │   - Revenue reports    │
              │   - Occupancy tracking │
              └────────────────────────┘
```

### Revenue Leakage Points Addressed

| Leakage Point | How We Fix It |
|---|---|
| Guest overstays past checkout | Real-time alert when key used after checkout time |
| Unauthorized room access | Only registered RFID keys work; full audit trail |
| Staff entering rooms without logging | Staff cards logged separately with reason codes |
| No proof of occupancy for billing disputes | Timestamped access logs + reception photos |
| Key cards shared with non-guests | Reception logger captures photo at key issuance |
| Rooms marked "occupied" but empty | Access frequency analysis reveals unused rooms |

---

## 2. SYSTEM ARCHITECTURE — MAPPING TO EXISTING CODEBASE

The hotel solution is a **clone + refactor** of the existing Gateman attendance system. Here's the mapping:

### 2.1 Concept Mapping

| Gateman (Attendance) | Hotel (Access Control) | Database Table |
|---|---|---|
| Organization | Hotel Property | `organizations` |
| Employee | Guest / Staff | `users` |
| RFID Card | Room Key Card | `users.rfid_uid` |
| Attendance Log | Room Access Log | `attendance_logs` → `access_logs` |
| Device (Gateman unit) | Door Controller / Reception Logger | `devices` |
| Check-in / Check-out | Room Entry / Room Exit | `access_logs.action` |
| Photo (face capture) | Guest photo at reception | `access_logs.photo_url` |
| Department | Room / Floor / Zone | New: `rooms` table |

### 2.2 What We Reuse As-Is

- **ESP32-WROOM firmware** — Reception logger (identical to current Gateman brain)
- **ESP32-CAM slave** — Photo capture at reception (identical)
- **Supabase Edge Functions** — `submit-log`, `get-users`, `device-provision`, `create-provision-token`
- **Auth system** — `_shared/auth.ts` device authentication (SHA-256 secret)
- **Dashboard structure** — Single-page HTML + Supabase JS SDK
- **Provisioning flow** — Token-based with QR code scanning
- **Offline-first design** — SD card buffer + sync when network available
- **Multi-tenant RLS** — Organization-scoped data isolation

### 2.3 What We Add / Modify

| Component | Change |
|---|---|
| `rooms` table | New — room_number, floor, type, status, rate |
| `reservations` table | New — guest_id, room_id, check_in, check_out, status |
| `access_logs` schema | Rename from `attendance_logs`, add `room_id`, `access_type` |
| Door lock firmware | New — Standalone RFID lock controller per door |
| Dashboard | Refactor — Room grid, occupancy view, revenue reports |
| Edge Functions | Add `check-access` (validates key against reservation) |

---

## 3. DOOR HARDWARE OPTIONS — DETAILED COMPARISON

This is the most critical decision. The door hardware must:
1. Use **13.56 MHz Mifare Classic 1K** (same cards as our RFID reader)
2. Support **offline operation** (battery powered, no wiring to each door)
3. Provide an **audit trail** (log who opened when)
4. Be **affordable** at scale (20-200+ rooms)
5. Allow **data extraction** (via data collector card, WiFi, or Bluetooth)

### 3.1 Option A — Traditional Standalone Hotel RFID Lock (RECOMMENDED FOR PHASE 1)

**How it works:** Self-contained lock unit. Battery powered (4× AA). Reads Mifare cards. Stores audit trail in internal memory. Data extracted via special S70 collector card or portable reader.

**Integration with our system:** The reception logger handles all cloud sync. Door locks operate independently. Data is collected periodically and fed into our system.

#### Recommended Models

| Model | Material | Price/Unit | Audit Records | Card Type | Battery Life | Data Collection |
|---|---|---|---|---|---|---|
| **ShineACS SL-HL8011-3** | Stainless Steel | $55–75 | 500+ events | Mifare 13.56MHz | 15,000 openings (~1 year) | S70 collector card |
| **ShineACS SL-HL8505** | Zinc Alloy | $45–65 | 500+ events | Mifare 13.56MHz | 12,000 openings (~1 year) | S70 collector card |
| **ZKTeco LH1000** | Stainless Steel | $80–120 | 224 events | Mifare 13.56MHz | 10,000 openings | ZKBiolock software |
| **ZKTeco LH3600** | Stainless Steel | $90–140 | 500+ events | Mifare 13.56MHz | 15,000 openings | ZKBiolock software |
| **Locstar LS-8015** | Zinc Alloy | $35–55 | 300+ events | Mifare 13.56MHz | 10,000 openings | S70 collector card |
| **Generic Alibaba OEM** | Zinc Alloy | $25–45 | 200+ events | Mifare 13.56MHz | 8,000 openings | S70 collector card |

**Accessories needed per property:**
| Accessory | Price | Qty Needed |
|---|---|---|
| Desktop Mifare Card Encoder | $90–140 | 1 |
| S70 Data Collector Card | $5–10 | 2–3 |
| Mifare 1K Guest Key Cards | $2–5 each | 3× room count |
| Mifare 1K Staff Master Cards | $2–5 each | 10–20 |
| Lock Management Software | $0 (included) | 1 license |
| Registration Code (annual) | $0–500/yr | Depends on vendor |

**Pros:**
- Cheapest per-door cost
- No wiring needed (battery powered)
- Proven hotel-grade reliability (millions deployed worldwide)
- Simple installation (fits standard US/EU mortise)
- Guest experience identical to any major hotel chain

**Cons:**
- Data collection is manual (walk to each door with collector card)
- No real-time access alerts
- Audit trail limited to lock memory (200–500 events)
- Lock vendor software is separate from our SaaS (bridging needed)

**Total cost for 50-room hotel:**
| Item | Cost |
|---|---|
| 50× SL-HL8011-3 locks @ $65 | $3,250 |
| 1× Desktop encoder | $120 |
| 3× S70 collector cards | $24 |
| 200× Guest key cards | $600 |
| 20× Staff master cards | $60 |
| 1× Reception Logger (ESP32 + CAM) | $45 |
| Installation labor (est.) | $1,500 |
| **TOTAL** | **$5,599** |

---

### 3.2 Option B — WiFi/Bluetooth Smart Hotel Lock (RECOMMENDED FOR REAL-TIME)

**How it works:** Same as Option A but with Bluetooth or WiFi connectivity. Can sync access logs wirelessly via a gateway. Some support TTLock or Tuya cloud platforms.

**Integration with our system:** Gateway collects door events and our system ingests them via webhook or API polling. Real-time alerts possible.

#### Recommended Models

| Model | Material | Price/Unit | Connectivity | Card Type | Battery Life | Cloud Platform |
|---|---|---|---|---|---|---|
| **TTLock-based BLE Lock** | Zinc Alloy | $60–120 | Bluetooth + WiFi Gateway | Mifare 13.56MHz | 10,000 openings | TTLock Cloud (free API) |
| **Tuya-based WiFi Lock** | Zinc Alloy | $70–130 | WiFi Direct | Mifare 13.56MHz | 6,000 openings (WiFi drains faster) | Tuya Cloud (API available) |
| **ShineACS SL-H8181** | Stainless Steel | $80–130 | Bluetooth + Gateway | Mifare 13.56MHz | 12,000 openings | Custom SDK |
| **iLockey BLE Lock** | Stainless Steel | $90–150 | Bluetooth + WiFi Gateway | Mifare 13.56MHz | 12,000 openings | TTHotel system |

**Additional hardware:**
| Accessory | Price | Qty Needed |
|---|---|---|
| WiFi/BLE Gateway (per floor) | $30–60 | 1 per floor |
| Desktop Mifare Card Encoder | $90–140 | 1 |
| Mifare 1K Guest Key Cards | $2–5 each | 3× room count |

**Pros:**
- Real-time access logs (no manual data collection)
- Remote lock/unlock capability
- Over-the-air firmware updates
- Integrates with TTLock/Tuya APIs for webhook-based ingestion
- Guest can use phone as key (Bluetooth)

**Cons:**
- Higher per-unit cost
- WiFi locks drain batteries faster (6,000 vs 15,000 openings)
- Depends on gateway reliability
- TTLock/Tuya cloud adds a dependency layer
- More complex installation (gateway placement, WiFi coverage)

**Total cost for 50-room hotel:**
| Item | Cost |
|---|---|
| 50× TTLock BLE locks @ $90 | $4,500 |
| 5× WiFi Gateways (10 rooms/floor) | $200 |
| 1× Desktop encoder | $120 |
| 200× Guest key cards | $600 |
| 20× Staff master cards | $60 |
| 1× Reception Logger (ESP32 + CAM) | $45 |
| Installation labor (est.) | $2,000 |
| **TOTAL** | **$7,525** |

---

### 3.3 Option C — DIY ESP32 Door Controller + Electric Strike (MAXIMUM CONTROL)

**How it works:** We build our own door controller using ESP32 + RFID reader + electric strike/magnetic lock. Each door has our custom firmware. Full control over data, real-time sync, identical architecture to reception logger.

**Integration:** Perfect — same firmware stack, same Supabase Edge Functions, same device provisioning. Each door IS a Gateman device.

#### Bill of Materials Per Door

| Component | Price | Notes |
|---|---|---|
| ESP32-WROOM-32 Dev Board | $4–6 | WiFi + BLE |
| MFRC522 RFID Reader (13.56MHz) | $2–3 | Same as current system |
| Electric Strike Lock (12V DC) | $15–30 | Fail-secure, ANSI standard |
| 5V/12V Power Supply | $5–8 | Wall-mounted, dedicated |
| Relay Module (5V) | $1–2 | Controls strike lock |
| Buzzer + LED | $1 | Feedback |
| Waterproof Enclosure | $5–10 | IP65 rated |
| PCB / Wiring / Connectors | $5–8 | Custom PCB recommended |
| **Total per door** | **$38–67** | |

**Additional infrastructure:**
| Item | Price | Notes |
|---|---|---|
| 12V Power runs to each door | $10–20/door | Electrician needed |
| WiFi Access Points (if needed) | $30–80 each | Ensure coverage |
| Central UPS/Battery backup | $50–150 | Keeps locks powered during outage |

**Pros:**
- **Cheapest per-door hardware** ($38–67 vs $55–150)
- Full control — our firmware, our cloud, our data
- Real-time sync (WiFi built-in)
- No vendor lock-in
- Identical architecture to reception logger — one codebase
- Each door is a provisioned device in our SaaS
- OTA firmware updates possible

**Cons:**
- **Requires 12V wiring to every door** (major installation cost)
- Not a "hotel-grade" lock — it's an access controller + electric strike
- No deadbolt/mechanical key backup (fire code concern)
- Aesthetics are industrial, not hospitality-grade
- Fail-secure mode means door locks when power fails (safety concern without battery backup)
- More maintenance (power supplies, WiFi connectivity per door)
- Regulatory/fire safety certification needed

**Total cost for 50-room hotel:**
| Item | Cost |
|---|---|
| 50× ESP32 door controllers @ $50 | $2,500 |
| 50× Electric strike installation + wiring | $5,000 |
| 5× WiFi APs | $250 |
| 200× Mifare key cards | $600 |
| 1× Reception Logger (ESP32 + CAM) | $45 |
| 1× UPS / Battery backup | $100 |
| Electrician labor (est.) | $3,000 |
| **TOTAL** | **$11,495** |

---

### 3.4 Option D — Hybrid: Commercial Lock + ESP32 Reception Logger (BEST VALUE)

**How it works:** Use affordable standalone Mifare locks on doors (Option A) for physical security, but make the **reception logger** the intelligent hub. All key issuance, guest registration, photo capture, and cloud sync happen at reception. Door audit trails are collected periodically via S70 card.

**This is the approach that best matches our current architecture** because:
1. The reception logger IS the Gateman device (ESP32 + CAM) — zero firmware changes
2. Door locks are dumb but reliable — no firmware to maintain
3. All intelligence is in the cloud (Supabase) and at reception
4. Offline-first: reception logger buffers on SD, syncs when WiFi available

```
┌──────────────────────────────────────────────────────────────┐
│                      RECEPTION DESK                           │
│                                                              │
│  ┌────────────────────┐   ┌──────────────────────┐          │
│  │ ESP32-WROOM Brain  │   │  ESP32-CAM Slave     │          │
│  │ - RFID Reader      │   │  - Guest photo       │          │
│  │ - NTP time sync    │   │  - SD card buffer    │          │
│  │ - WiFi sync        │   │  - Offline queue     │          │
│  │ - User cache       │   │                      │          │
│  └────────┬───────────┘   └──────────┬───────────┘          │
│           │ UART                      │                      │
│           └──────────────────────────┘                       │
│                                                              │
│  ┌──────────────────────────────────────────┐               │
│  │ Desktop Card Encoder (USB to PC)         │               │
│  │ - Issues guest key cards                 │               │
│  │ - Programs room + dates + permissions    │               │
│  └──────────────────────────────────────────┘               │
│                                                              │
│  WORKFLOW:                                                   │
│  1. Receptionist creates reservation in dashboard            │
│  2. Guest arrives → tap blank card on RFID reader            │
│  3. ESP32 logs: guest_id + card_uid + timestamp + photo      │
│  4. Receptionist programs card via encoder for room access    │
│  5. Guest uses card at room door (standalone lock)            │
│  6. Door lock stores access in internal memory                │
│  7. Periodic: staff collects door logs via S70 card           │
│  8. S70 data imported to dashboard for full audit trail       │
└──────────────────────────────────────────────────────────────┘
```

**Total cost for 50-room hotel:**
| Item | Cost |
|---|---|
| 50× Locstar LS-8015 locks @ $45 | $2,250 |
| 1× Reception Logger (ESP32 + CAM) | $45 |
| 1× Desktop Mifare encoder | $120 |
| 3× S70 collector cards | $24 |
| 200× Mifare 1K guest cards | $600 |
| 20× Staff master cards | $60 |
| Installation labor | $1,500 |
| **TOTAL** | **$4,599** |

---

## 4. RECOMMENDATION MATRIX

| Criteria | Option A (Standalone) | Option B (WiFi/BLE) | Option C (DIY ESP32) | Option D (Hybrid) |
|---|---|---|---|---|
| **Per-door cost** | $45–75 | $60–150 | $38–67 + wiring | $35–55 |
| **Total 50-room cost** | $5,599 | $7,525 | $11,495 | **$4,599** |
| **Installation complexity** | Low | Medium | High | Low |
| **Real-time alerts** | ❌ | ✅ | ✅ | ❌ (reception only) |
| **Wiring required** | None | None | Yes (12V) | None |
| **Architecture fit** | Good | Good | Perfect | **Best** |
| **Maintenance** | Low | Medium | High | Low |
| **Guest experience** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Fire code compliance** | ✅ | ✅ | ⚠️ Needs review | ✅ |
| **Scalability** | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| **Time to deploy** | 1–2 weeks | 2–3 weeks | 4–6 weeks | **1–2 weeks** |
| **Vendor dependency** | Lock vendor | TTLock/Tuya | None | Lock vendor |

### Primary Recommendation: **Option D (Hybrid)** for immediate deployment

### Growth Path: **Option D → Option B** (upgrade doors to WiFi/BLE locks over time for real-time capability, the reception logger and cloud remain unchanged)

---

## 5. DATABASE SCHEMA CHANGES

### 5.1 New Tables

```sql
-- Rooms table
CREATE TABLE rooms (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id),
  room_number VARCHAR(20) NOT NULL,
  floor INTEGER NOT NULL DEFAULT 1,
  room_type VARCHAR(50) NOT NULL DEFAULT 'standard',  -- standard, deluxe, suite, etc.
  rate_per_night DECIMAL(10,2),
  status VARCHAR(20) NOT NULL DEFAULT 'available',     -- available, occupied, maintenance, blocked
  max_occupancy INTEGER DEFAULT 2,
  device_id UUID REFERENCES devices(id),               -- optional: linked door controller
  created_at TIMESTAMPTZ DEFAULT now(),
  UNIQUE(organization_id, room_number)
);

-- Reservations table
CREATE TABLE reservations (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id),
  room_id UUID NOT NULL REFERENCES rooms(id),
  guest_id UUID REFERENCES users(id),                  -- linked after check-in
  guest_name VARCHAR(255) NOT NULL,
  guest_email VARCHAR(255),
  guest_phone VARCHAR(50),
  guest_id_number VARCHAR(100),                        -- passport / national ID
  check_in TIMESTAMPTZ NOT NULL,
  check_out TIMESTAMPTZ NOT NULL,
  actual_check_in TIMESTAMPTZ,                         -- when key was actually issued
  actual_check_out TIMESTAMPTZ,                        -- when key was actually returned
  key_card_uid VARCHAR(50),                            -- RFID UID of issued key card
  num_keys_issued INTEGER DEFAULT 1,
  rate_per_night DECIMAL(10,2) NOT NULL,
  total_amount DECIMAL(10,2),
  payment_status VARCHAR(20) DEFAULT 'pending',        -- pending, paid, partial, refunded
  status VARCHAR(20) DEFAULT 'confirmed',              -- confirmed, checked_in, checked_out, cancelled, no_show
  notes TEXT,
  created_at TIMESTAMPTZ DEFAULT now(),
  updated_at TIMESTAMPTZ DEFAULT now()
);

-- Access logs (replaces attendance_logs for hotel context)
CREATE TABLE access_logs (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id),
  device_id UUID REFERENCES devices(id),               -- which device logged this
  room_id UUID REFERENCES rooms(id),                   -- which room (NULL for reception logs)
  user_id UUID REFERENCES users(id),                   -- guest or staff
  credential_value VARCHAR(255) NOT NULL,              -- RFID UID
  action VARCHAR(20) NOT NULL,                         -- key_issue, key_return, room_entry, room_exit, denied
  access_type VARCHAR(20) DEFAULT 'guest',             -- guest, staff, master, emergency
  device_event_id VARCHAR(255) UNIQUE,                 -- idempotency key
  timestamp TIMESTAMPTZ NOT NULL,
  photo_url TEXT,                                      -- photo from reception camera
  source VARCHAR(20) DEFAULT 'reception',              -- reception, door_lock, collector
  metadata JSONB DEFAULT '{}',                         -- extra context
  synced_at TIMESTAMPTZ DEFAULT now(),
  created_at TIMESTAMPTZ DEFAULT now()
);

-- RLS Policies
ALTER TABLE rooms ENABLE ROW LEVEL SECURITY;
ALTER TABLE reservations ENABLE ROW LEVEL SECURITY;
ALTER TABLE access_logs ENABLE ROW LEVEL SECURITY;

-- Organization-scoped read
CREATE POLICY rooms_org_read ON rooms FOR SELECT 
  USING (organization_id IN (
    SELECT organization_id FROM org_members WHERE user_id = auth.uid()
  ));

CREATE POLICY reservations_org_read ON reservations FOR SELECT
  USING (organization_id IN (
    SELECT organization_id FROM org_members WHERE user_id = auth.uid()
  ));

CREATE POLICY access_logs_org_read ON access_logs FOR SELECT
  USING (organization_id IN (
    SELECT organization_id FROM org_members WHERE user_id = auth.uid()
  ));

-- Service role insert (Edge Functions)
CREATE POLICY access_logs_service_insert ON access_logs FOR INSERT
  WITH CHECK (true);  -- Edge Functions use service role

-- Indexes for performance
CREATE INDEX idx_rooms_org ON rooms(organization_id);
CREATE INDEX idx_reservations_org_dates ON reservations(organization_id, check_in, check_out);
CREATE INDEX idx_reservations_status ON reservations(organization_id, status);
CREATE INDEX idx_reservations_card ON reservations(key_card_uid);
CREATE INDEX idx_access_logs_org_ts ON access_logs(organization_id, timestamp DESC);
CREATE INDEX idx_access_logs_room ON access_logs(room_id, timestamp DESC);
CREATE INDEX idx_access_logs_credential ON access_logs(credential_value, timestamp DESC);
```

### 5.2 Key Queries

```sql
-- Current room occupancy
SELECT r.room_number, r.floor, r.room_type, r.status,
       res.guest_name, res.check_in, res.check_out,
       res.key_card_uid, res.status as reservation_status
FROM rooms r
LEFT JOIN reservations res ON r.id = res.room_id 
  AND res.status = 'checked_in'
WHERE r.organization_id = $1
ORDER BY r.floor, r.room_number;

-- Revenue leakage: Access after checkout
SELECT al.timestamp, r.room_number, al.credential_value,
       res.guest_name, res.check_out,
       al.timestamp - res.check_out AS overstay_duration
FROM access_logs al
JOIN rooms r ON al.room_id = r.id
JOIN reservations res ON res.key_card_uid = al.credential_value
WHERE al.organization_id = $1
  AND al.action = 'room_entry'
  AND al.timestamp > res.check_out
ORDER BY al.timestamp DESC;

-- Room access frequency (detect unused rooms billed as occupied)
SELECT r.room_number, res.guest_name,
       COUNT(al.id) AS access_count,
       MIN(al.timestamp) AS first_access,
       MAX(al.timestamp) AS last_access,
       res.check_in, res.check_out
FROM reservations res
JOIN rooms r ON res.room_id = r.id
LEFT JOIN access_logs al ON al.credential_value = res.key_card_uid
  AND al.room_id = r.id
WHERE res.organization_id = $1
  AND res.status IN ('checked_in', 'checked_out')
GROUP BY r.room_number, res.guest_name, res.check_in, res.check_out
HAVING COUNT(al.id) = 0  -- rooms with zero door access
ORDER BY res.check_in;

-- Staff access audit
SELECT al.timestamp, r.room_number, u.name AS staff_name,
       al.action, al.access_type
FROM access_logs al
JOIN rooms r ON al.room_id = r.id
JOIN users u ON al.user_id = u.id
WHERE al.organization_id = $1
  AND al.access_type IN ('staff', 'master')
ORDER BY al.timestamp DESC;
```

---

## 6. FIRMWARE CHANGES (MINIMAL)

### 6.1 Reception Logger (WROOM Brain)

**Changes from Gateman → Hotel:**
- Rename `action` values: `check_in` → `key_issue`, `check_out` → `key_return`
- Add serial command `ISSUE_KEY:room_number:guest_name` from reception PC
- Add `room_id` field to log payload
- Everything else (WiFi, NTP, sync, offline queue, provisioning) stays identical

### 6.2 Door Controllers (If Option C chosen)

**Simplified firmware (subset of WROOM Brain):**
- No ESP32-CAM slave needed (no photos at doors)
- RFID read → check local cache → unlock electric strike (or deny)
- Log to SPIFFS (no SD card needed, smaller log volume)
- Sync via WiFi when connected
- Sleep between taps to conserve power

### 6.3 ESP32-CAM Slave (Reception)

**Zero changes.** Photo capture at reception works exactly as current attendance system.

---

## 7. EDGE FUNCTION CHANGES

### 7.1 Modified: `submit-log`

```typescript
// Add room_id resolution from credential_value
// If card is linked to a reservation, auto-populate room_id
// Add access_type detection (guest vs staff vs master)
// Add overstay alert: if access after checkout, flag in metadata
```

### 7.2 New: `check-access` (for Option C WiFi doors)

```typescript
// Called by door controller before granting access
// Input: device_uid, device_secret, credential_value
// Logic:
//   1. Authenticate device
//   2. Look up credential_value → reservation
//   3. Check reservation dates (is it still valid?)
//   4. Check room matches device's assigned room
//   5. Return: { allowed: true/false, guest_name, room_number }
```

### 7.3 New: `import-door-logs` (for Option A/D collector card)

```typescript
// Called by dashboard when S70 collector data is uploaded
// Input: CSV/JSON of door events from vendor software export
// Logic:
//   1. Parse door events (timestamp, card_uid, door_id)
//   2. Match card_uid → reservation
//   3. Match door_id → room
//   4. Bulk insert into access_logs with source='collector'
//   5. Detect anomalies (overstay, unauthorized)
```

---

## 8. DASHBOARD CHANGES

### 8.1 New Pages

| Page | Purpose |
|---|---|
| **Room Grid** | Visual grid of all rooms, color-coded by status (available/occupied/maintenance) |
| **Reservations** | CRUD for reservations, link to room, issue key card |
| **Guest Check-in** | Streamlined flow: select reservation → tap card → capture photo → issue key |
| **Access Logs** | Filterable log of all room access events |
| **Revenue Reports** | Overstay detection, occupancy rates, room utilization |
| **Key Management** | Active keys, revoke keys, master key audit |
| **Door Data Import** | Upload collector card data from vendor software |

### 8.2 Real-time Features

- **Reception tap alert** — Realtime subscription on `access_logs` for live feed
- **Overstay notification** — When key used after checkout, alert on dashboard
- **Room status change** — Automatic when key is issued or returned

---

## 9. IMPLEMENTATION PHASES

### Phase 1 — Core Reception System (Week 1–2) — Option D

| Task | Duration | Owner |
|---|---|---|
| Clone Gateman repo → `gateman-hotel` | 1 hour | Dev |
| Add `rooms`, `reservations`, `access_logs` tables | 2 hours | Dev |
| Refactor dashboard: Room grid + Reservations page | 2 days | Dev |
| Refactor dashboard: Guest check-in flow | 1 day | Dev |
| Modify `submit-log` for hotel context | 2 hours | Dev |
| Deploy Supabase schema + Edge Functions | 1 hour | Dev |
| Order hardware (locks + encoder + cards) | 1–2 weeks | Procurement |
| **Deliverable:** Reception logger operational, dashboard ready | | |

### Phase 2 — Door Lock Installation (Week 2–3)

| Task | Duration | Owner |
|---|---|---|
| Install standalone RFID locks on all doors | 2–3 days | Installer |
| Program master cards, floor cards, staff cards | 1 day | Hotel IT |
| Test all doors with test key cards | 1 day | QA |
| Train reception staff on check-in workflow | Half day | Dev |
| **Deliverable:** All doors operational, reception flow tested | | |

### Phase 3 — Data Collection & Revenue Reports (Week 3–4)

| Task | Duration | Owner |
|---|---|---|
| Build door log import function (collector → dashboard) | 1 day | Dev |
| Build revenue leakage reports (overstay, unused rooms) | 2 days | Dev |
| Build staff access audit report | 1 day | Dev |
| First data collection run (S70 cards on all doors) | Half day | Staff |
| Validate imported data matches reality | Half day | QA |
| **Deliverable:** Full audit trail, revenue reports operational | | |

### Phase 4 — Scale & Optimize (Month 2+)

| Task | Duration | Owner |
|---|---|---|
| Upgrade select doors to WiFi/BLE locks (Option B) | As budget allows | Installer |
| Add PMS integration (if hotel uses Opera, etc.) | 1–2 weeks | Dev |
| Mobile app for staff (room status, access logs) | 2–3 weeks | Dev |
| Multi-property support (chain hotels) | Existing SaaS multi-tenant | Dev |

---

## 10. PRICING TIERS FOR HOTEL SaaS

Reusing existing subscription model from Gateman:

| Plan | Monthly Price | Rooms | Devices | Features |
|---|---|---|---|---|
| **Starter** | $39/mo | Up to 20 | 2 | Reception logger + basic reports |
| **Professional** | $129/mo | Up to 100 | 10 | Full reports + API + door log import |
| **Enterprise** | Custom | Unlimited | Unlimited | Multi-property + PMS integration + SLA |

**Hardware is sold separately** — one-time cost to hotel.

---

## 11. ROBUSTNESS & SCALING

### 11.1 Offline Resilience

| Scenario | Behavior |
|---|---|
| WiFi down at reception | SD card buffers all logs, syncs when reconnected |
| Power outage | Door locks are battery-powered, continue working. Reception logger reboots and syncs pending logs |
| Supabase down | Firmware continues logging locally, syncs when cloud returns |
| SD card full | Auto-cleanup of synced logs (>30 days). Alert on dashboard |

### 11.2 Scaling Path

| Scale | Architecture |
|---|---|
| 1 hotel, 20 rooms | 1 reception logger + standalone locks |
| 1 hotel, 100 rooms | 1 reception logger + standalone locks + weekly collection |
| 1 hotel, 200+ rooms | 2 reception loggers (lobby + back entrance) + WiFi locks |
| 5 hotels (chain) | Multi-tenant SaaS, each hotel = 1 organization |
| 50+ hotels | Same architecture, Supabase scales horizontally |

### 11.3 Security

- **Guest key cards are time-limited** — programmed with check-in/check-out dates by encoder
- **Master cards audited** — all staff access logged and reportable
- **Lost card revocation** — receptionist can blacklist a card UID in the lock
- **Data isolation** — RLS ensures Hotel A cannot see Hotel B's data
- **Device authentication** — SHA-256 hashed secrets (same as current system)
- **Photo evidence** — Guest face captured at key issuance (consent required)

---

## 12. SUPPLIER CONTACTS & PROCUREMENT

### Recommended Vendors (Sorted by Value)

1. **ShineACS Locks (acslocks.com)** — Best price-to-quality ratio
   - MOQ: 10 units for wholesale pricing
   - Free management software + lifetime registration code
   - Ships worldwide, 7–15 day delivery
   - Contact: sales@acslocks.com

2. **Locstar (locstar.com)** — Budget option
   - Lowest per-unit price ($35–55)
   - Good for budget-conscious properties
   - Ships from China, 10–20 day delivery

3. **ZKTeco** — Premium option with local support in many countries
   - Higher price but better after-sales support
   - ZKBiolock software is more feature-rich
   - Local distributors in most African countries

4. **Alibaba OEM** — Bulk orders (100+ units)
   - Customizable branding
   - $25–45/unit at scale
   - Higher risk (quality varies, vet supplier carefully)

---

## 13. RISK REGISTER

| Risk | Impact | Probability | Mitigation |
|---|---|---|---|
| Lock vendor software incompatible with our import format | Medium | Medium | Request sample data format before purchase; build flexible CSV parser |
| Guest key card compatibility (Mifare 1K vs other) | High | Low | Confirm 13.56MHz Mifare Classic 1K with vendor before purchase |
| Battery drain on locks faster than expected | Low | Low | Stock replacement batteries; choose locks with >10K opening rating |
| WiFi dead zones in hotel | Medium | Medium | Survey WiFi coverage before deployment; add APs if needed |
| Hotel staff resistance to new system | Medium | High | On-site training; simple workflow; keep parallel manual process for 2 weeks |
| Fire code requirements for electronic locks | High | Medium | Confirm with local fire authority; all recommended locks have mechanical key backup |
| Supabase outage during peak check-in | Medium | Low | Reception logger works offline; SD buffer ensures no data loss |

---

## 14. OPEN QUESTIONS

1. **Hotel size?** — How many rooms? This determines hardware quantity and Option selection.
2. **Existing door type?** — Wooden doors with US or EU mortise? Thickness? This affects lock model compatibility.
3. **WiFi coverage?** — Is there reliable WiFi throughout the property? This determines if Option B is viable immediately.
4. **Budget?** — What is the hotel's total budget for door hardware? This narrows the options.
5. **Existing PMS?** — Does the hotel use any Property Management System (Opera, Hotelogix, etc.)? Integration may be needed.
6. **Timeline?** — How urgent is deployment? Standalone locks (Option A/D) ship in 1–3 weeks, WiFi locks may take longer.
7. **Local regulations?** — Any fire code or building regulations regarding electronic locks in your jurisdiction?
8. **Power reliability?** — How frequent are power outages? This affects whether WiFi-dependent solutions (Option B/C) are viable.
9. **Staff technical skill?** — Can reception staff be trained on the dashboard, or is a simpler dedicated check-in terminal needed?
