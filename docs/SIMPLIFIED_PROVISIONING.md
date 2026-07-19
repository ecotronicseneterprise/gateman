# Simplified Provisioning Flow - Final Implementation

## User Experience (3 Simple Steps)

### Step 1: Device Setup
- User powers on device
- Device creates WiFi hotspot: `GATEMAN-SETUP-XXXX`
- LED blinks slowly

### Step 2: WiFi Configuration
1. User connects phone to `GATEMAN-SETUP-XXXX`
2. Browser opens automatically (captive portal)
3. Portal shows:
   - Device MAC address (real MAC, not 0.0.0.0)
   - WiFi SSID input
   - WiFi Password input
4. User enters WiFi credentials and clicks "Generate Pairing Code"
5. Portal displays **12-character pairing code** (e.g., `30C6F7982B14`)
6. User clicks "Copy Code" button

### Step 3: Dashboard Binding
1. User goes to dashboard
2. Clicks "Add Device"
3. Enters:
   - Device name (e.g., "Main Entrance")
   - Pairing code (paste from clipboard)
4. Clicks "Add Device"
5. Dashboard validates code and binds device to account
6. Device automatically provisions and reboots

---

## Technical Implementation

### Pairing Code Format
- **Source**: Device MAC address without colons
- **Example**: MAC `30:C6:F7:98:2B:14` → Code `30C6F7982B14`
- **Length**: 12 characters (uppercase hex)
- **Uniqueness**: Every device has unique MAC = unique code

### Device Side (Firmware)
1. Device boots unprovisionned → Enters AP mode
2. User submits WiFi credentials → Device saves to NVS
3. Device generates pairing code from MAC address
4. Device shows code to user (stays in AP mode)
5. Device waits for dashboard to provision it via API
6. When provisioned, device reboots and connects to WiFi

### Dashboard Side (To Implement)
1. User pastes pairing code
2. Dashboard calls Edge Function: `pair-device`
3. Edge Function:
   - Validates pairing code format (12 hex chars)
   - Checks if device with that MAC exists in pending state
   - Creates device record in database
   - Generates provision token
   - Returns success
4. Device polls or receives provision signal
5. Device provisions itself and reboots

---

## Next Steps

### 1. Upload New Firmware ✅
- Firmware updated with simplified provisioning
- MAC address now displayed correctly
- Pairing code generation implemented

### 2. Create `pair-device` Edge Function
```typescript
// supabase/functions/pair-device/index.ts
// Accepts: { pairing_code, device_name }
// Validates code, creates device, returns provision token
```

### 3. Update Dashboard UI
- Add "Add Device" modal with pairing code input
- Call `pair-device` Edge Function
- Show success/error feedback

### 4. Test End-to-End
1. Power on device → Connect to AP
2. Enter WiFi → Copy pairing code
3. Paste in dashboard → Device binds
4. Test enrollment and attendance

---

## Advantages

✅ **No manual token generation** - User never sees tokens  
✅ **Simple 3-step process** - WiFi → Copy → Paste  
✅ **Unique per device** - MAC-based code  
✅ **Copy button** - No typing errors  
✅ **Clear instructions** - Portal guides user  
✅ **Works offline** - Device doesn't need internet for setup  

---

## Current Status

- ✅ Firmware updated with pairing code generation
- ✅ Portal shows MAC address correctly
- ✅ Portal generates and displays pairing code
- ✅ Copy button implemented
- ⏳ Need to create `pair-device` Edge Function
- ⏳ Need to update dashboard UI
- ⏳ Need to test complete flow

---

## Upload Firmware Now!

The firmware is ready. Upload `wroom_brain.ino` and test:

1. Device boots → Shows real MAC address
2. Connect to AP → Enter WiFi
3. Click "Generate Pairing Code"
4. Portal shows code with copy button
5. Copy code → Ready to paste in dashboard

**Next: Create dashboard integration to accept pairing codes!**
