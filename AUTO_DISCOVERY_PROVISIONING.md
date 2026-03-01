# Auto-Discovery Provisioning - User Guide

## Overview

The new auto-discovery provisioning makes adding devices **incredibly simple** - no manual token copying, no complex steps.

---

## User Experience (3 Simple Steps)

### Step 1: Power On Device
- Device creates WiFi hotspot: `GATEMAN-SETUP-XXXX`
- LED blinks slowly (waiting for setup)

### Step 2: Connect & Configure
1. **Connect phone to** `GATEMAN-SETUP-XXXX` WiFi
2. **Browser opens automatically** (captive portal)
3. **Enter WiFi credentials:**
   - WiFi SSID: Your network name
   - WiFi Password: Your network password
4. **Click "Save WiFi & Wait for Claim"**
5. **Portal shows:** "Click here to open dashboard"

### Step 3: Claim from Dashboard
1. **Click the dashboard link** (or go to dashboard manually)
2. **Dashboard shows:** "New device detected: XX:XX:XX:XX:XX:XX"
3. **Click "Claim Device"**
4. **Enter device name** (e.g., "Main Entrance")
5. **Done!** Device auto-provisions and reboots

---

## How It Works (Technical)

### Device Side:
1. Device boots unprovisionned → Creates WiFi AP
2. User connects and enters WiFi credentials
3. Device saves WiFi to NVS
4. Device polls Supabase every 5 seconds for claim status
5. When claimed, device retrieves provision token automatically
6. Device provisions itself and reboots into normal mode

### Dashboard Side:
1. User clicks "Claim Device" button
2. Dashboard calls `claim-device` Edge Function with:
   - Device MAC address
   - Device name
   - WiFi credentials (already saved by device)
3. Edge Function creates provision token and stores claim
4. Device polls `poll-claim` endpoint and retrieves token
5. Device provisions itself

### Database:
- New table: `device_claims`
- Stores pending claims with 10-minute expiry
- Device polls this table via Edge Function

---

## Deployment Steps

### 1. Run Database Migration
```sql
-- Run in Supabase SQL Editor
-- File: supabase/migrations/002_device_claims.sql
```

### 2. Deploy Edge Functions
```bash
npx supabase functions deploy claim-device --no-verify-jwt
npx supabase functions deploy poll-claim --no-verify-jwt
```

### 3. Upload Firmware
- Upload `firmware/wroom_brain/wroom_brain.ino` to ESP32

### 4. Update Dashboard (Next Step)
- Add "Claim Device" interface to dashboard
- Show pending devices waiting for claim
- One-click claim button

---

## Advantages Over Old Flow

### Old Flow (Complex):
1. Dashboard: Generate token
2. Copy token to clipboard
3. Connect to device WiFi
4. Paste token in portal
5. Enter WiFi credentials
6. Submit and wait

### New Flow (Simple):
1. Connect to device WiFi
2. Enter WiFi credentials
3. Click dashboard link
4. Click "Claim Device"
5. Done!

**50% fewer steps, no manual copy/paste!**

---

## Next Steps

1. **Run migration SQL** in Supabase
2. **Deploy Edge Functions**
3. **Update dashboard** to add claim interface
4. **Test end-to-end** provisioning flow
5. **Deploy to production**

---

## Security Notes

- Claims expire after 10 minutes
- Only authenticated users can claim devices
- Device limit enforced per subscription plan
- WiFi credentials stored securely in device NVS
- Provision tokens are single-use and expire

---

## Troubleshooting

**Device not appearing in dashboard:**
- Check device is in AP mode (LED blinking slowly)
- Verify WiFi credentials were saved
- Check device MAC address matches

**Claim not working:**
- Verify Edge Functions are deployed
- Check database migration ran successfully
- Ensure user has active subscription
- Check device limit not exceeded

**Device not provisioning after claim:**
- Verify WiFi credentials are correct
- Check device can reach internet
- Monitor Serial output for errors
- Ensure provision token is valid
