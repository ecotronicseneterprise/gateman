/*
 * GATEMAN — Simplified Provisioning Portal
 *
 * Flow:
 *   1. Device boots unpaired → starts WiFi AP "GATEMAN-SETUP-XXXXXX"
 *   2. User connects to AP → opens browser → sees DEVICE CODE
 *   3. User enters WiFi SSID + password → submits
 *   4. Device connects to WiFi + reboots
 *   5. User copies DEVICE CODE → goes to dashboard → Add Device → pastes code
 *   6. Dashboard registers device_uid = code, secret = code
 *   Done. No tokens. No Edge Functions for provisioning.
 */

#ifndef PROVISION_PORTAL_H
#define PROVISION_PORTAL_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

extern Preferences preferences;
extern String DEVICE_UID;

#ifndef STATUS_LED
#define STATUS_LED 2
#endif

WebServer provisionServer(80);
DNSServer  dnsServer;

// ============================================================
// DEVICE CODE — MAC without colons, uppercase
// This is both the device_uid AND the secret.
// e.g. MAC 30:C6:F7:98:2B:14 → code "30C6F7982B14"
// ============================================================
String getDeviceCode() {
  String code = DEVICE_UID;
  code.replace(":", "");
  code.toUpperCase();
  return code;
}

// ============================================================
// PORTAL HTML
// ============================================================
const char PROVISION_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>GATEMAN Device Setup</title>
  <style>
    * { margin:0; padding:0; box-sizing:border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #0f172a;
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 20px;
    }
    .card {
      background: #1e293b;
      border: 1px solid #334155;
      border-radius: 20px;
      max-width: 400px;
      width: 100%;
      padding: 36px 32px;
    }
    .logo {
      text-align: center;
      margin-bottom: 28px;
    }
    .logo-mark {
      display: inline-flex;
      align-items: flex-end;
      gap: 12px;
      margin-bottom: 4px;
    }
    .logo-name {
      font-size: 26px;
      font-weight: 700;
      background: linear-gradient(135deg, #6366f1, #22d3ee);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
      background-clip: text;
    }
    .logo-sub {
      font-size: 12px;
      color: #64748b;
      letter-spacing: 2px;
      text-transform: uppercase;
    }

    /* Device Code Box */
    .code-box {
      background: #0f172a;
      border: 1px solid #334155;
      border-radius: 12px;
      padding: 20px;
      margin-bottom: 28px;
      text-align: center;
    }
    .code-label {
      font-size: 11px;
      color: #64748b;
      letter-spacing: 2px;
      text-transform: uppercase;
      margin-bottom: 10px;
    }
    .code-value {
      font-family: 'SF Mono', 'Courier New', monospace;
      font-size: 28px;
      font-weight: 700;
      color: #22d3ee;
      letter-spacing: 3px;
      margin-bottom: 12px;
      word-break: break-all;
    }
    .copy-btn {
      background: #1e293b;
      border: 1px solid #475569;
      color: #94a3b8;
      padding: 6px 16px;
      border-radius: 6px;
      font-size: 12px;
      cursor: pointer;
      transition: .2s;
    }
    .copy-btn:hover { border-color: #22d3ee; color: #22d3ee; }
    .copy-btn.copied { border-color: #10b981; color: #10b981; }

    .step-hint {
      background: rgba(99,102,241,0.1);
      border: 1px solid rgba(99,102,241,0.2);
      border-radius: 8px;
      padding: 12px 14px;
      font-size: 12px;
      color: #94a3b8;
      margin-bottom: 24px;
      line-height: 1.6;
    }
    .step-hint strong { color: #e2e8f0; }

    label {
      display: block;
      font-size: 12px;
      font-weight: 600;
      color: #64748b;
      letter-spacing: .5px;
      margin-bottom: 6px;
    }
    input {
      width: 100%;
      background: #0f172a;
      border: 1px solid #334155;
      border-radius: 8px;
      padding: 12px 14px;
      color: #e2e8f0;
      font-size: 14px;
      outline: none;
      transition: .2s;
      margin-bottom: 16px;
    }
    input:focus { border-color: #6366f1; }

    .submit-btn {
      width: 100%;
      background: linear-gradient(135deg, #6366f1, #4f46e5);
      color: white;
      border: none;
      border-radius: 10px;
      padding: 14px;
      font-size: 15px;
      font-weight: 600;
      cursor: pointer;
      transition: .2s;
      margin-top: 4px;
    }
    .submit-btn:hover { opacity: .9; transform: translateY(-1px); }
    .submit-btn:disabled { opacity: .5; cursor: not-allowed; transform: none; }

    #status {
      display: none;
      margin-top: 16px;
      border-radius: 8px;
      padding: 12px 14px;
      font-size: 13px;
    }
    #status.ok  { background: rgba(16,185,129,.1); border: 1px solid rgba(16,185,129,.3); color: #34d399; }
    #status.err { background: rgba(239,68,68,.1);  border: 1px solid rgba(239,68,68,.3);  color: #f87171; }
  </style>
</head>
<body>
<div class="card">

  <div class="logo">
    <!-- Gate mark SVG -->
    <div class="logo-mark">
      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 44 54" fill="none" width="32" height="39">
        <defs>
          <linearGradient id="g" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stop-color="#6366f1"/>
            <stop offset="100%" stop-color="#22d3ee"/>
          </linearGradient>
        </defs>
        <rect x="0"  y="0" width="8"  height="54" rx="4" fill="url(#g)"/>
        <rect x="36" y="0" width="8"  height="54" rx="4" fill="url(#g)"/>
        <rect x="0"  y="0" width="44" height="8"  rx="4" fill="url(#g)"/>
      </svg>
      <span class="logo-name">GATEMAN</span>
    </div>
    <div class="logo-sub">Device Setup</div>
  </div>

  <!-- Step 1: Copy the code -->
  <div class="code-box">
    <div class="code-label">Your Device Code</div>
    <div class="code-value" id="codeDisplay">%DEVICE_CODE%</div>
    <button class="copy-btn" onclick="copyCode()">Copy Code</button>
  </div>

  <div class="step-hint">
    <strong>1.</strong> Copy the code above.<br>
    <strong>2.</strong> Enter your WiFi below and tap <em>Connect</em>.<br>
    <strong>3.</strong> Go to the Gateman dashboard → Devices → Add Device → paste the code.
  </div>

  <!-- Step 2: WiFi credentials -->
  <label>WiFi Network Name (SSID)</label>
  <input type="text" id="ssid" placeholder="e.g. MyHomeWiFi" autocomplete="off">

  <label>WiFi Password</label>
  <input type="password" id="pass" placeholder="WiFi password">

  <button class="submit-btn" id="submitBtn" onclick="connect()">Connect Device</button>

  <div id="status"></div>
</div>

<script>
  function copyCode() {
    const code = document.getElementById('codeDisplay').textContent.trim();
    navigator.clipboard.writeText(code).then(() => {
      const btn = document.querySelector('.copy-btn');
      btn.textContent = '✓ Copied!';
      btn.classList.add('copied');
      setTimeout(() => { btn.textContent = 'Copy Code'; btn.classList.remove('copied'); }, 2000);
    }).catch(() => {
      // Fallback for older browsers
      const el = document.getElementById('codeDisplay');
      const range = document.createRange();
      range.selectNodeContents(el);
      window.getSelection().removeAllRanges();
      window.getSelection().addRange(range);
      document.execCommand('copy');
      window.getSelection().removeAllRanges();
    });
  }

  async function connect() {
    const ssid = document.getElementById('ssid').value.trim();
    const pass = document.getElementById('pass').value;
    const btn  = document.getElementById('submitBtn');
    const st   = document.getElementById('status');

    if (!ssid) { showStatus('Enter your WiFi network name.', false); return; }

    btn.disabled = true;
    btn.textContent = 'Connecting...';
    showStatus('Saving credentials and connecting to WiFi...', true);

    try {
      const r = await fetch('/save-wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid, password: pass })
      });
      const d = await r.json();
      if (r.ok) {
        showStatus('✓ Connected! Device is rebooting. You can close this page.\n\nNow go to Gateman dashboard → Devices → Add Device and paste your code.', true);
      } else {
        showStatus('✗ ' + (d.error || 'Failed'), false);
        btn.disabled = false;
        btn.textContent = 'Connect Device';
      }
    } catch(e) {
      showStatus('✗ Error: ' + e.message, false);
      btn.disabled = false;
      btn.textContent = 'Connect Device';
    }
  }

  function showStatus(msg, ok) {
    const st = document.getElementById('status');
    st.style.display = 'block';
    st.className = ok ? 'ok' : 'err';
    st.textContent = msg;
  }
</script>
</body>
</html>
)rawliteral";

// ============================================================
// HANDLERS
// ============================================================
void handleProvisionRoot() {
  String html = PROVISION_HTML;
  html.replace("%DEVICE_CODE%", getDeviceCode());
  provisionServer.send(200, "text/html", html);
}

void handleSaveWiFi() {
  if (!provisionServer.hasArg("plain")) {
    provisionServer.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }

  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, provisionServer.arg("plain")) != DeserializationError::Ok) {
    provisionServer.send(400, "application/json", "{\"error\":\"Bad JSON\"}");
    return;
  }

  String ssid = doc["ssid"].as<String>();
  String pass = doc["password"].as<String>();

  if (ssid.length() == 0) {
    provisionServer.send(400, "application/json", "{\"error\":\"SSID required\"}");
    return;
  }

  // Save WiFi credentials to NVS
  preferences.begin("gateman", false);
  preferences.putString("wifi_ssid", ssid);
  preferences.putString("wifi_pass", pass);
  // Mark as provisioned — device code IS the secret (MAC without colons)
  preferences.putString("device_secret", getDeviceCode());
  preferences.putString("device_uid",    getDeviceCode());
  preferences.putString("supabase_url",  "https://ueobebsgheecclwcbigy.supabase.co");
  preferences.end();

  // Also save to ecotron namespace so isProvisioned() returns true
  preferences.begin("ecotron", false);
  preferences.putString("device_secret", getDeviceCode());
  preferences.putString("device_uid",    getDeviceCode());
  preferences.putString("supabase_url",  "https://ueobebsgheecclwcbigy.supabase.co");
  preferences.end();

  Serial.println("[PORTAL] WiFi saved. SSID=" + ssid);
  Serial.println("[PORTAL] Device code: " + getDeviceCode());

  provisionServer.send(200, "application/json", "{\"status\":\"ok\"}");

  delay(800);
  dnsServer.stop();
  provisionServer.stop();
  WiFi.softAPdisconnect(true);
  delay(300);

  Serial.println("[PORTAL] Rebooting into normal mode...");
  ESP.restart();
}

void handleProvisionNotFound() {
  provisionServer.sendHeader("Location", "/", true);
  provisionServer.send(302, "text/plain", "");
}

// ============================================================
// START PORTAL
// ============================================================
void startProvisioningPortal() {
  String code   = getDeviceCode();
  String apSSID = "GATEMAN-" + code.substring(6); // Last 6 chars of MAC

  Serial.println("\n[PORTAL] ================================");
  Serial.println("[PORTAL] DEVICE NOT PROVISIONED");
  Serial.println("[PORTAL] Starting setup AP...");
  Serial.println("[PORTAL] Connect to WiFi: " + apSSID);
  Serial.println("[PORTAL] Open browser:    http://192.168.4.1");
  Serial.println("[PORTAL] DEVICE CODE:     " + code);
  Serial.println("[PORTAL] ================================\n");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID.c_str()); // No password — easy to connect
  delay(500);

  IPAddress ip = WiFi.softAPIP();
  Serial.println("[PORTAL] AP IP: " + ip.toString());

  dnsServer.start(53, "*", ip);

  provisionServer.on("/",          handleProvisionRoot);
  provisionServer.on("/save-wifi", HTTP_POST, handleSaveWiFi);
  provisionServer.onNotFound(handleProvisionNotFound);
  provisionServer.begin();

  Serial.println("[PORTAL] Waiting for setup...");

  // Slow blink = waiting for setup
  while (true) {
    dnsServer.processNextRequest();
    provisionServer.handleClient();
    digitalWrite(STATUS_LED, HIGH); delay(600);
    digitalWrite(STATUS_LED, LOW);  delay(600);
  }
  // Never returns — handleSaveWiFi calls ESP.restart()
}

#endif


// Supabase Edge Function: get-users
// Deploy: supabase functions deploy get-users
//
// Device authenticates with device_uid + device_secret
// Both are the same value: MAC without colons e.g. "30C6F7982B14"

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

Deno.serve(async (req) => {
  try {
    const { device_uid, device_secret } = await req.json()

    if (!device_uid || !device_secret) {
      return new Response(JSON.stringify({ error: 'Missing credentials' }), { status: 400 })
    }

    const sb = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
    )

    // Look up device — uid and secret are both the MAC-derived code
    const { data: device, error: devErr } = await sb
      .from('devices')
      .select('id, organization_id, status')
      .eq('device_uid', device_uid)
      .eq('secret', device_secret)
      .single()

    if (devErr || !device) {
      console.log('Device not found:', device_uid, devErr?.message)
      return new Response(JSON.stringify({ error: 'Device not registered. Add device in dashboard first.' }), { status: 401 })
    }

    // Update last_seen
    await sb.from('devices')
      .update({ last_seen: new Date().toISOString(), status: 'active' })
      .eq('id', device.id)

    // Get all active users with RFID credentials for this org
    const { data: users, error: usersErr } = await sb
      .from('users')
      .select('id, name, employee_id, department, user_credentials(value)')
      .eq('organization_id', device.organization_id)
      .eq('active', true)

    if (usersErr) {
      return new Response(JSON.stringify({ error: usersErr.message }), { status: 500 })
    }

    // Flatten — one entry per RFID card
    const mapped = (users || [])
      .filter(u => u.user_credentials && u.user_credentials.length > 0)
      .map(u => ({
        user_id:     u.id,
        name:        u.name,
        employee_id: u.employee_id,
        department:  u.department || '',
        rfid_uid:    u.user_credentials[0].value
      }))

    return new Response(JSON.stringify({ users: mapped, device_id: device.id }), {
      headers: { 'Content-Type': 'application/json' }
    })

  } catch (e) {
    return new Response(JSON.stringify({ error: e.message }), { status: 500 })
  }
})



// Supabase Edge Function: submit-log
// Deploy: supabase functions deploy submit-log

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

Deno.serve(async (req) => {
  try {
    const body = await req.json()
    const { device_uid, device_secret, credential_value, action, event_time, device_event_id } = body

    if (!device_uid || !device_secret) {
      return new Response(JSON.stringify({ error: 'Missing credentials' }), { status: 400 })
    }

    const sb = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
    )

    // Verify device
    const { data: device } = await sb
      .from('devices')
      .select('id, organization_id')
      .eq('device_uid', device_uid)
      .eq('secret', device_secret)
      .single()

    if (!device) {
      return new Response(JSON.stringify({ error: 'Unauthorized' }), { status: 401 })
    }

    // Find user by RFID value
    const { data: cred } = await sb
      .from('user_credentials')
      .select('user_id')
      .eq('value', credential_value)
      .eq('organization_id', device.organization_id)
      .single()

    if (!cred) {
      return new Response(JSON.stringify({ error: 'Unknown card', inserted: false }), { status: 200 })
    }

    // Idempotency — skip if same device_event_id already exists
    if (device_event_id) {
      const { data: dup } = await sb
        .from('attendance_logs')
        .select('id')
        .eq('device_id', device.id)
        .eq('credential_value', credential_value)
        .gte('timestamp', new Date(Date.now() - 8000).toISOString())
        .limit(1)
        .single()

      if (dup) {
        return new Response(JSON.stringify({ inserted: false, duplicate: true }), { status: 200 })
      }
    }

    // Insert attendance log
    const { error: insertErr } = await sb.from('attendance_logs').insert({
      organization_id:  device.organization_id,
      device_id:        device.id,
      user_id:          cred.user_id,
      action:           action || 'check_in',
      timestamp:        event_time || new Date().toISOString(),
      credential_value: credential_value,
    })

    if (insertErr) {
      return new Response(JSON.stringify({ error: insertErr.message }), { status: 500 })
    }

    return new Response(JSON.stringify({ inserted: true }), { status: 200 })

  } catch (e) {
    return new Response(JSON.stringify({ error: e.message }), { status: 500 })
  }
})




<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>EcoTronic — Attendance Dashboard</title>

<!-- FIX 1: Use UMD/browser builds that expose globals, NOT ES module skypack imports -->
<script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/qrcodejs/1.0.0/qrcode.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2/dist/umd/supabase.js"></script>

<style>
:root {
  --bg: #0f1117;
  --surface: #1a1d27;
  --surface2: #22263a;
  --border: #2d3148;
  --accent: #6366f1;
  --accent2: #22d3ee;
  --green: #10b981;
  --red: #ef4444;
  --yellow: #f59e0b;
  --text: #e2e8f0;
  --muted: #64748b;
  --radius: 12px;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: 'Segoe UI', system-ui, sans-serif; background: var(--bg); color: var(--text); min-height: 100vh; }

/* LOGIN */
#loginPage { display:flex; align-items:center; justify-content:center; min-height:100vh; background: radial-gradient(ellipse at 50% 0%, #1e1b4b 0%, var(--bg) 70%); }
.login-card { background: var(--surface); border: 1px solid var(--border); border-radius: 20px; padding: 48px 40px; width: 380px; }
.login-logo { font-size: 28px; font-weight: 800; background: linear-gradient(135deg, var(--accent), var(--accent2)); -webkit-background-clip: text; background-clip: text; -webkit-text-fill-color: transparent; margin-bottom: 8px; }
.login-sub { color: var(--muted); font-size: 14px; margin-bottom: 32px; }
.form-group { margin-bottom: 18px; }
.form-group label { display:block; font-size:13px; color:var(--muted); margin-bottom:6px; }
.form-group input { width:100%; background:var(--surface2); border:1px solid var(--border); border-radius:8px; padding:12px 14px; color:var(--text); font-size:14px; outline:none; transition:.2s; }
.form-group input:focus { border-color:var(--accent); }
.btn-primary { width:100%; background: linear-gradient(135deg, var(--accent), #818cf8); color:white; border:none; border-radius:8px; padding:13px; font-size:15px; font-weight:600; cursor:pointer; transition:.2s; }
.btn-primary:hover { opacity:.9; transform:translateY(-1px); }
.login-error { background:#3f1212; border:1px solid var(--red); border-radius:8px; padding:10px 14px; font-size:13px; color:#fca5a5; margin-top:14px; display:none; }

/* APP */
#app { display:none; }
.sidebar { position:fixed; top:0; left:0; width:220px; height:100vh; background:var(--surface); border-right:1px solid var(--border); display:flex; flex-direction:column; z-index:100; }
.logo { padding:24px 20px; font-size:20px; font-weight:800; background: linear-gradient(135deg, var(--accent), var(--accent2)); -webkit-background-clip:text; background-clip:text; -webkit-text-fill-color:transparent; border-bottom:1px solid var(--border); }
.logo span { font-size:11px; font-weight:400; -webkit-text-fill-color:var(--muted); display:block; }
nav { flex:1; padding:16px 10px; }
.nav-item { display:flex; align-items:center; gap:10px; padding:10px 14px; border-radius:8px; cursor:pointer; font-size:14px; color:var(--muted); transition:.15s; margin-bottom:2px; border:none; background:none; width:100%; text-align:left; }
.nav-item:hover { background:var(--surface2); color:var(--text); }
.nav-item.active { background:rgba(99,102,241,.15); color:var(--accent); }
.nav-item svg { width:18px; height:18px; flex-shrink:0; }
.sidebar-footer { padding:16px; border-top:1px solid var(--border); }
.status-dot { width:8px; height:8px; border-radius:50%; background:var(--green); display:inline-block; margin-right:6px; animation:pulse 2s infinite; }
@keyframes pulse { 0%,100%{opacity:1}50%{opacity:.4} }

.main { margin-left:220px; padding:28px; min-height:100vh; }
.page { display:none; }
.page.active { display:block; }

/* HEADER */
.page-header { display:flex; justify-content:space-between; align-items:center; margin-bottom:28px; }
.page-title { font-size:24px; font-weight:700; }
.page-title span { font-size:14px; font-weight:400; color:var(--muted); margin-left:8px; }
.header-actions { display:flex; gap:10px; }
.btn { padding:9px 18px; border-radius:8px; font-size:13px; font-weight:600; cursor:pointer; border:none; transition:.15s; }
.btn-outline { background:transparent; border:1px solid var(--border); color:var(--text); }
.btn-outline:hover { border-color:var(--accent); color:var(--accent); }
.btn-sm { padding:7px 14px; font-size:12px; }
.btn-green { background:var(--green); color:white; }
.btn-red { background:var(--red); color:white; }
.btn-accent { background:var(--accent); color:white; }

/* STAT CARDS */
.stats-grid { display:grid; grid-template-columns:repeat(4, 1fr); gap:16px; margin-bottom:24px; }
.stat-card { background:var(--surface); border:1px solid var(--border); border-radius:var(--radius); padding:22px; position:relative; overflow:hidden; }
.stat-card::before { content:''; position:absolute; top:0; left:0; right:0; height:3px; }
.stat-card.blue::before { background:linear-gradient(90deg, var(--accent), #818cf8); }
.stat-card.cyan::before { background:linear-gradient(90deg, var(--accent2), #06b6d4); }
.stat-card.green::before { background:linear-gradient(90deg, var(--green), #34d399); }
.stat-card.yellow::before { background:linear-gradient(90deg, var(--yellow), #fcd34d); }
.stat-label { font-size:12px; color:var(--muted); text-transform:uppercase; letter-spacing:.5px; margin-bottom:10px; }
.stat-value { font-size:36px; font-weight:800; line-height:1; }
.stat-sub { font-size:12px; color:var(--muted); margin-top:6px; }

/* CHARTS */
.charts-grid { display:grid; grid-template-columns:2fr 1fr; gap:16px; margin-bottom:24px; }
.chart-card { background:var(--surface); border:1px solid var(--border); border-radius:var(--radius); padding:22px; }
.chart-title { font-size:14px; font-weight:600; margin-bottom:18px; color:var(--text); }
.chart-wrap { position:relative; height:200px; }

/* LIVE FEED */
.feed-card { background:var(--surface); border:1px solid var(--border); border-radius:var(--radius); padding:22px; }
.feed-header { display:flex; justify-content:space-between; align-items:center; margin-bottom:16px; }
.live-badge { display:flex; align-items:center; gap:6px; background:rgba(16,185,129,.1); border:1px solid rgba(16,185,129,.3); color:var(--green); padding:4px 10px; border-radius:20px; font-size:12px; font-weight:600; }
.feed-list { max-height: 360px; overflow-y:auto; }
.feed-item { display:flex; align-items:center; gap:14px; padding:12px 0; border-bottom:1px solid var(--border); animation:slideIn .3s ease; }
@keyframes slideIn { from{opacity:0;transform:translateY(-8px)}to{opacity:1;transform:translateY(0)} }
.feed-item:last-child { border-bottom:none; }
.feed-avatar { width:40px; height:40px; border-radius:10px; background:var(--surface2); overflow:hidden; flex-shrink:0; display:flex; align-items:center; justify-content:center; font-weight:700; color:var(--accent); font-size:14px; }
.feed-avatar img { width:100%; height:100%; object-fit:cover; }
.feed-info { flex:1; min-width:0; }
.feed-name { font-size:14px; font-weight:600; }
.feed-meta { font-size:12px; color:var(--muted); }
.feed-badge { padding:4px 10px; border-radius:20px; font-size:11px; font-weight:700; flex-shrink:0; }
.feed-badge.in { background:rgba(16,185,129,.15); color:var(--green); }
.feed-badge.out { background:rgba(239,68,68,.15); color:var(--red); }
.feed-time { font-size:12px; color:var(--muted); flex-shrink:0; }

/* TABLE */
.table-card { background:var(--surface); border:1px solid var(--border); border-radius:var(--radius); overflow:hidden; }
.table-header { padding:18px 22px; display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid var(--border); }
table { width:100%; border-collapse:collapse; }
th { background:var(--surface2); padding:12px 18px; text-align:left; font-size:12px; color:var(--muted); text-transform:uppercase; letter-spacing:.5px; }
td { padding:13px 18px; font-size:14px; border-bottom:1px solid var(--border); }
tr:last-child td { border-bottom:none; }
tr:hover td { background:rgba(99,102,241,.04); }
.badge { padding:3px 10px; border-radius:20px; font-size:11px; font-weight:700; }
.badge.present { background:rgba(16,185,129,.15); color:var(--green); }
.badge.absent { background:rgba(239,68,68,.1); color:var(--red); }
.badge.pending { background:rgba(245,158,11,.1); color:var(--yellow); }

/* MODAL */
.modal-overlay { display:none; position:fixed; inset:0; background:rgba(0,0,0,.7); z-index:1000; align-items:center; justify-content:center; }
.modal-overlay.open { display:flex; }
.modal { background:var(--surface); border:1px solid var(--border); border-radius:16px; padding:32px; width:460px; max-width:90vw; }
.modal h3 { font-size:18px; font-weight:700; margin-bottom:20px; }
.modal-actions { display:flex; gap:10px; justify-content:flex-end; margin-top:24px; }

/* SEARCH */
.search-input { background:var(--surface2); border:1px solid var(--border); border-radius:8px; padding:9px 14px; color:var(--text); font-size:14px; outline:none; width:220px; }
.search-input:focus { border-color:var(--accent); }

/* TOAST */
#toast { position:fixed; bottom:24px; right:24px; background:var(--surface); border:1px solid var(--border); border-radius:10px; padding:14px 20px; font-size:14px; z-index:2000; transform:translateY(80px); opacity:0; transition:.3s; max-width:320px; }
#toast.show { transform:translateY(0); opacity:1; }
#toast.success { border-left:4px solid var(--green); }
#toast.error { border-left:4px solid var(--red); }
</style>

<style>
/* MOBILE RESPONSIVE DESIGN */
.mobile-menu-btn {
  display: none;
  position: fixed;
  top: 16px;
  left: 16px;
  z-index: 1001;
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 12px;
  cursor: pointer;
  color: var(--text);
}
.mobile-menu-btn svg { width: 20px; height: 20px; }
.mobile-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,.5); z-index: 1000; }

@media (max-width: 768px) {
  .mobile-menu-btn { display: block; }
  .sidebar { transform: translateX(-100%); transition: transform .3s ease; z-index: 1002; }
  .sidebar.open { transform: translateX(0); }
  .main { margin-left: 0; padding: 20px 16px; }
  .page-header { flex-direction: column; align-items: flex-start; gap: 16px; }
  .page-title { font-size: 20px; }
  .header-actions { width: 100%; justify-content: space-between; }
  .stats-grid { grid-template-columns: 1fr; gap: 12px; }
  .stat-card { padding: 16px; }
  .stat-value { font-size: 28px; }
  .charts-grid { grid-template-columns: 1fr; gap: 16px; }
  .chart-wrap { height: 250px; }
  .table-card { overflow-x: auto; -webkit-overflow-scrolling: touch; }
  table { min-width: 600px; }
  th, td { white-space: nowrap; padding: 10px 12px; }
  .modal { width: 95vw; max-width: none; margin: 20px; padding: 24px; }
  .modal h3 { font-size: 16px; }
  .login-card { width: 90vw; max-width: 400px; padding: 32px 24px; }
  .login-logo { font-size: 24px; }
  .btn { min-height: 44px; padding: 12px 20px; }
  .nav-item { padding: 12px 16px; min-height: 48px; }
  .feed-item { padding: 16px 0; gap: 12px; }
  .feed-avatar { width: 36px; height: 36px; }
  .feed-name { font-size: 13px; }
  .feed-meta { font-size: 11px; }
}

@media (max-width: 480px) {
  .main { padding: 16px 12px; }
  .login-card { width: 95vw; padding: 24px 20px; }
  .login-logo { font-size: 20px; }
  .login-sub { font-size: 12px; }
  .page-title { font-size: 18px; }
  .stat-card { padding: 12px; }
  .stat-value { font-size: 24px; }
  .stat-label { font-size: 11px; }
  .chart-card { padding: 16px; }
  .chart-title { font-size: 13px; }
  .modal { margin: 10px; padding: 20px; }
  .form-group input { padding: 14px 16px; }
  .header-actions { flex-direction: column; gap: 8px; align-items: stretch; }
  .header-actions .btn { width: 100%; }
  .search-input { width: 100%; }
  #provisionQR { max-width: 200px; padding: 8px; }
  #toast { bottom: 16px; right: 16px; left: 16px; max-width: none; }
}

@media (min-width: 481px) and (max-width: 768px) {
  .stats-grid { grid-template-columns: repeat(2, 1fr); }
  .charts-grid { grid-template-columns: 1fr; }
}
</style>
</head>  <!-- FIX 2: </head> was missing — <body> must come AFTER </head> -->

<body>  <!-- FIX 3: <body> was inside a stray <script> tag — now correctly placed -->

<!-- LOGIN / SIGNUP -->
<div id="loginPage">
  <div class="login-card">
    <div class="login-logo">Gateman</div>
    <div class="login-sub">Smart Attendance System</div>
    <div style="display:flex;gap:0;margin-bottom:24px;border-bottom:1px solid var(--border)">
      <button class="btn" id="tabLogin" onclick="switchAuthTab('login')" style="flex:1;border-radius:0;border-bottom:2px solid var(--accent);color:var(--accent);background:none">Sign In</button>
      <button class="btn" id="tabSignup" onclick="switchAuthTab('signup')" style="flex:1;border-radius:0;border-bottom:2px solid transparent;color:var(--muted);background:none">Sign Up</button>
    </div>
    <!-- LOGIN FORM -->
    <div id="loginForm">
      <div class="form-group">
        <label>Email</label>
        <input type="email" id="loginEmail" placeholder="admin@company.com">
      </div>
      <div class="form-group">
        <label>Password</label>
        <input type="password" id="loginPassword" placeholder="••••••••">
      </div>
      <button class="btn-primary" onclick="login()">Sign In</button>
    </div>
    <!-- SIGNUP FORM -->
    <div id="signupForm" style="display:none">
      <div class="form-group">
        <label>Company Name *</label>
        <input type="text" id="signupCompany" placeholder="Acme Industries">
      </div>
      <div class="form-group">
        <label>Your Name *</label>
        <input type="text" id="signupName" placeholder="Jane Doe">
      </div>
      <div class="form-group">
        <label>Email *</label>
        <input type="email" id="signupEmail" placeholder="jane@acme.com">
      </div>
      <div class="form-group">
        <label>Password *</label>
        <input type="password" id="signupPassword" placeholder="Min 8 characters">
      </div>
      <button class="btn-primary" onclick="signup()">Create Account</button>
    </div>
    <div class="login-error" id="loginError"></div>
  </div>
</div>

<!-- MOBILE MENU BUTTON -->
<button class="mobile-menu-btn" id="mobileMenuBtn" onclick="toggleMobileMenu()">
  <svg fill="none" viewBox="0 0 24 24" stroke="currentColor">
    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 6h16M4 12h16M4 18h16"/>
  </svg>
</button>

<!-- MOBILE OVERLAY -->
<div class="mobile-overlay" id="mobileOverlay" onclick="closeMobileMenu()"></div>

<!-- APP -->
<div id="app">
  <!-- Sidebar -->
  <div class="sidebar">
    <div class="logo">Gateman<span>Attendance System</span></div>
    <nav>
      <button class="nav-item active" onclick="showPage('dashboard', this)">
        <svg fill="none" viewBox="0 0 24 24" stroke="currentColor"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M3 7h18M3 12h18M3 17h18"/></svg>
        Dashboard
      </button>
      <button class="nav-item" onclick="showPage('employees', this)">
        <svg fill="none" viewBox="0 0 24 24" stroke="currentColor"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0"/></svg>
        Employees
      </button>
      <button class="nav-item" onclick="showPage('attendance', this)">
        <svg fill="none" viewBox="0 0 24 24" stroke="currentColor"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 5H7a2 2 0 00-2 2v12a2 2 0 002 2h10a2 2 0 002-2V7a2 2 0 00-2-2h-2M9 5a2 2 0 002 2h2a2 2 0 002-2M9 5a2 2 0 012-2h2a2 2 0 012 2"/></svg>
        Attendance
      </button>
      <button class="nav-item" onclick="showPage('enrollment', this)">
        <svg fill="none" viewBox="0 0 24 24" stroke="currentColor"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M18 9v3m0 0v3m0-3h3m-3 0h-3m-2-5a4 4 0 11-8 0 4 4 0 018 0zM3 20a6 6 0 0112 0v1H3v-1z"/></svg>
        Enrollment
      </button>
      <button class="nav-item" onclick="showPage('devices', this)">
        <svg fill="none" viewBox="0 0 24 24" stroke="currentColor"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 3v2m6-2v2M9 19v2m6-2v2M5 9H3m2 6H3m18-6h-2m2 6h-2M7 19h10a2 2 0 002-2V7a2 2 0 00-2-2H7a2 2 0 00-2 2v10a2 2 0 002 2zM9 9h6v6H9V9z"/></svg>
        Devices
      </button>
      <button class="nav-item" id="navAdmin" style="display:none" onclick="showPage('admin', this)">
        <svg fill="none" viewBox="0 0 24 24" stroke="currentColor"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 6V4m0 2a2 2 0 100 4m0-4a2 2 0 110 4m-6 8a2 2 0 100-4m0 4a2 2 0 110-4m0 4v2m0-6V4m6 6v10m6-2a2 2 0 100-4m0 4a2 2 0 110-4m0 4v2m0-6V4"/></svg>
        Admin
      </button>
    </nav>
    <div class="sidebar-footer">
      <div style="display:flex;align-items:center;gap:6px;margin-bottom:6px">
        <span class="status-dot" id="liveIndicator"></span>
        <span style="font-size:12px;color:var(--muted)" id="liveStatus">Connecting...</span>
      </div>
      <div style="font-size:11px;color:var(--muted);margin-bottom:0" id="orgName"></div>
    </div>
  </div>

  <!-- Main content -->
  <div class="main">

    <!-- DASHBOARD PAGE -->
    <div class="page active" id="page-dashboard">
      <div class="page-header">
        <div class="page-title">Dashboard <span id="todayDate"></span></div>
        <div class="header-actions">
          <button class="btn btn-outline" onclick="exportCSV()">↓ Export CSV</button>
        </div>
      </div>

      <div class="stats-grid">
        <div class="stat-card blue">
          <div class="stat-label">Total Employees</div>
          <div class="stat-value" id="statTotal">—</div>
          <div class="stat-sub">Registered</div>
        </div>
        <div class="stat-card green">
          <div class="stat-label">Currently In</div>
          <div class="stat-value" id="statIn">—</div>
          <div class="stat-sub">Checked in today</div>
        </div>
        <div class="stat-card cyan">
          <div class="stat-label">Today's Taps</div>
          <div class="stat-value" id="statTaps">—</div>
          <div class="stat-sub">Total events</div>
        </div>
        <div class="stat-card yellow">
          <div class="stat-label">Devices</div>
          <div class="stat-value" id="statDevices">—</div>
          <div class="stat-sub">Active readers</div>
        </div>
      </div>

      <div class="charts-grid">
        <div class="chart-card">
          <div class="chart-title">Today's Activity (by hour)</div>
          <div class="chart-wrap"><canvas id="hourlyChart"></canvas></div>
        </div>
        <div class="chart-card">
          <div class="chart-title">Department Presence</div>
          <div class="chart-wrap"><canvas id="deptChart"></canvas></div>
        </div>
      </div>

      <div class="charts-grid" style="grid-template-columns:1fr 1.5fr">
        <div class="chart-card">
          <div class="chart-title">7-Day Trend</div>
          <div class="chart-wrap"><canvas id="weeklyChart"></canvas></div>
        </div>
        <div class="feed-card">
          <div class="feed-header">
            <div style="font-size:14px;font-weight:600">Live Feed</div>
            <div class="live-badge"><span class="status-dot"></span>LIVE</div>
          </div>
          <div class="feed-list" id="feedList"></div>
        </div>
      </div>
    </div>

    <!-- EMPLOYEES PAGE -->
    <div class="page" id="page-employees">
      <div class="page-header">
        <div class="page-title">Employees</div>
        <div class="header-actions">
          <input class="search-input" placeholder="Search employees..." oninput="filterEmployees(this.value)">
          <button class="btn btn-accent" onclick="openAddEmployee()">+ Add Employee</button>
        </div>
      </div>
      <div class="table-card">
        <table>
          <thead>
            <tr>
              <th>Name</th>
              <th>Employee ID</th>
              <th>Department</th>
              <th>RFID Card</th>
              <th>Status</th>
              <th>Last Seen</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody id="employeesTable"></tbody>
        </table>
      </div>
    </div>

    <!-- ATTENDANCE PAGE -->
    <div class="page" id="page-attendance">
      <div class="page-header">
        <div class="page-title">Attendance Records</div>
        <div class="header-actions">
          <input type="date" id="filterFrom" class="search-input" style="width:150px">
          <input type="date" id="filterTo" class="search-input" style="width:150px">
          <button class="btn btn-outline" onclick="loadAttendance()">Filter</button>
          <button class="btn btn-outline" onclick="exportCSV()">↓ Export</button>
        </div>
      </div>
      <div class="table-card">
        <table>
          <thead>
            <tr>
              <th>Employee</th>
              <th>Department</th>
              <th>Action</th>
              <th>Time</th>
              <th>Device</th>
              <th>Photo</th>
            </tr>
          </thead>
          <tbody id="attendanceTable"></tbody>
        </table>
      </div>
    </div>

    <!-- ENROLLMENT PAGE -->
    <div class="page" id="page-enrollment">
      <div class="page-header">
        <div class="page-title">Pending Enrollments</div>
      </div>
      <div class="table-card">
        <div class="table-header" style="border-bottom:1px solid var(--border)">
          <span style="font-size:14px;color:var(--muted)">Cards tapped on device waiting for employee assignment</span>
          <button class="btn btn-outline btn-sm" onclick="loadEnrollments()">↻ Refresh</button>
        </div>
        <table>
          <thead>
            <tr>
              <th>Card UID</th>
              <th>Photo</th>
              <th>Device</th>
              <th>Time</th>
              <th>Assign To</th>
            </tr>
          </thead>
          <tbody id="enrollmentTable"></tbody>
        </table>
      </div>
    </div>

    <!-- DEVICES PAGE -->
    <div class="page" id="page-devices">
      <div class="page-header">
        <div class="page-title">Devices</div>
        <div class="header-actions">
          <button class="btn btn-accent" onclick="openProvisionModal()">+ Add Device</button>
        </div>
      </div>
      <div class="table-card">
        <div class="table-header" style="border-bottom:1px solid var(--border)">
          <span style="font-size:14px;color:var(--muted)">Registered devices for your organization</span>
          <button class="btn btn-outline btn-sm" onclick="loadDevices()">Refresh</button>
        </div>
        <table>
          <thead>
            <tr>
              <th>Name</th>
              <th>Device UID</th>
              <th>Location</th>
              <th>Status</th>
              <th>Last Seen</th>
            </tr>
          </thead>
          <tbody id="devicesTable"></tbody>
        </table>
      </div>

      <div id="provisionResult" style="display:none; margin-top:24px;">
        <div class="chart-card" style="max-width:480px;">
          <div class="chart-title" style="color:var(--green)">✓ Device Registered</div>
          <p style="color:var(--muted);font-size:13px;margin-top:8px">Device has been added. It will appear as active once it connects to WiFi and calls home.</p>
        </div>
      </div>
    </div>

    <!-- ADMIN MONITORING PAGE -->
    <div class="page" id="page-admin">
      <div class="page-header">
        <div class="page-title">Admin Monitor</div>
        <div class="header-actions">
          <button class="btn btn-outline" onclick="loadAdminData()">Refresh</button>
        </div>
      </div>

      <div class="chart-card" style="margin-bottom:20px">
        <div class="chart-title">Subscription</div>
        <div id="adminSubInfo" style="color:var(--muted);font-size:14px">Loading...</div>
      </div>

      <div class="stats-grid" style="margin-bottom:20px">
        <div class="stat-card"><div class="stat-value" id="adminTotalEmployees">-</div><div class="stat-label">Employees</div></div>
        <div class="stat-card"><div class="stat-value" id="adminTotalDevices">-</div><div class="stat-label">Active Devices</div></div>
        <div class="stat-card"><div class="stat-value" id="adminTotalLogs">-</div><div class="stat-label">Total Logs</div></div>
        <div class="stat-card"><div class="stat-value" id="adminPendingEnrollments">-</div><div class="stat-label">Pending Enrollments</div></div>
      </div>

      <div class="table-card">
        <div class="table-header" style="border-bottom:1px solid var(--border)">
          <span style="font-size:14px;font-weight:600">Audit Trail</span>
          <span style="font-size:12px;color:var(--muted)">Last 50 events</span>
        </div>
        <table>
          <thead>
            <tr>
              <th>Time</th>
              <th>Actor</th>
              <th>Action</th>
              <th>Resource</th>
              <th>Details</th>
            </tr>
          </thead>
          <tbody id="auditTable"></tbody>
        </table>
      </div>
    </div>

    <!-- PAGE FOOTER -->
    <div style="margin-top:48px;padding:24px 0 8px;border-top:1px solid var(--border);display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:12px">
      <div style="font-size:11px;color:var(--muted)">Powered by <strong style="color:var(--text)">Ecotronics Enterprise</strong> &nbsp;·&nbsp; © 2026</div>
      <button class="btn btn-outline btn-sm" onclick="logout()">Sign Out</button>
    </div>
  </div><!-- .main -->
</div><!-- #app -->

<!-- ADD EMPLOYEE MODAL -->
<div class="modal-overlay" id="addEmployeeModal">
  <div class="modal">
    <h3>Add Employee</h3>
    <div class="form-group"><label>Full Name *</label><input id="empName" class="search-input" style="width:100%" placeholder="John Doe"></div>
    <div class="form-group"><label>Employee ID *</label><input id="empId" class="search-input" style="width:100%" placeholder="EMP001"></div>
    <div class="form-group"><label>Department</label><input id="empDept" class="search-input" style="width:100%" placeholder="Finance"></div>
    <div class="form-group"><label>Email</label><input id="empEmail" class="search-input" style="width:100%" placeholder="john@company.com"></div>
    <div class="form-group"><label>RFID Card UID (optional)</label><input id="empRfid" class="search-input" style="width:100%" placeholder="778D7506"></div>
    <div class="modal-actions">
      <button class="btn btn-outline" onclick="closeModal('addEmployeeModal')">Cancel</button>
      <button class="btn btn-accent" onclick="addEmployee()">Add Employee</button>
    </div>
  </div>
</div>

<!-- ASSIGN RFID MODAL -->
<div class="modal-overlay" id="assignModal">
  <div class="modal">
    <h3>Assign Card to Employee</h3>
    <p style="color:var(--muted);font-size:14px;margin-bottom:20px">Select which employee this card belongs to</p>
    <input type="hidden" id="assignRfidUid">
    <div class="form-group">
      <label>Employee</label>
      <select id="assignEmployee" class="search-input" style="width:100%"></select>
    </div>
    <div class="modal-actions">
      <button class="btn btn-outline" onclick="closeModal('assignModal')">Cancel</button>
      <button class="btn btn-accent" onclick="completeAssignment()">Assign Card</button>
    </div>
  </div>
</div>

<!-- PROVISION DEVICE MODAL -->
<div class="modal-overlay" id="provisionModal">
  <div class="modal">
    <h3>Add New Device</h3>
    <p style="color:var(--muted);font-size:13px;margin-bottom:20px">Connect to the device WiFi AP, open <strong style="color:var(--accent2)">http://192.168.4.1</strong>, copy the Device Code shown, then paste it below.</p>
    <div class="form-group">
      <label>Device Name</label>
      <input id="provDeviceName" class="search-input" style="width:100%" placeholder="e.g. Main Entrance">
    </div>
    <div class="form-group">
      <label>Device Code (from device portal)</label>
      <input id="provDeviceCode" class="search-input" style="width:100%;font-family:monospace;letter-spacing:2px" placeholder="e.g. 30C6F7982B14">
    </div>
    <div class="modal-actions">
      <button class="btn btn-outline" onclick="closeModal('provisionModal')">Cancel</button>
      <button class="btn btn-accent" id="btnRegisterDevice" onclick="registerDevice()">Register Device</button>
    </div>
  </div>
</div>

<!-- TOAST -->
<div id="toast"></div>

<script>
// ============================================================
// FIX 2: Access supabase via window.supabase (UMD global)
// The UMD build exposes supabase as window.supabase
// ============================================================
const SUPABASE_URL     = 'https://ueobebsgheecclwcbigy.supabase.co';
const SUPABASE_ANON_KEY = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVlb2JlYnNnaGVlY2Nsd2NiaWd5Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzIyNzE3OTMsImV4cCI6MjA4Nzg0Nzc5M30.Qrtu1QKfjJV0RA0wO8OYNDWgH5fr8aNIEqkCr8z8Xxw';
const sb = supabase.createClient(SUPABASE_URL, SUPABASE_ANON_KEY);

let charts = {};
let allEmployees = [];
let currentOrgId = null;
let currentUserId = null;
let currentUserRole = null;
let realtimeChannel = null;

// ============================================================
// AUTH
// ============================================================
function switchAuthTab(tab) {
  document.getElementById('loginForm').style.display   = tab === 'login'  ? 'block' : 'none';
  document.getElementById('signupForm').style.display  = tab === 'signup' ? 'block' : 'none';
  document.getElementById('tabLogin').style.borderBottomColor  = tab === 'login'  ? 'var(--accent)' : 'transparent';
  document.getElementById('tabLogin').style.color               = tab === 'login'  ? 'var(--accent)' : 'var(--muted)';
  document.getElementById('tabSignup').style.borderBottomColor = tab === 'signup' ? 'var(--accent)' : 'transparent';
  document.getElementById('tabSignup').style.color              = tab === 'signup' ? 'var(--accent)' : 'var(--muted)';
  document.getElementById('loginError').style.display = 'none';
}

async function login() {
  const email    = document.getElementById('loginEmail').value;
  const password = document.getElementById('loginPassword').value;
  if (!email || !password) return showAuthError('Email and password required');
  const { data, error } = await sb.auth.signInWithPassword({ email, password });
  if (error) return showAuthError(error.message);
  await loadOrgContext(data.user.id);
  showApp();
}

async function signup() {
  const company  = document.getElementById('signupCompany').value.trim();
  const name     = document.getElementById('signupName').value.trim();
  const email    = document.getElementById('signupEmail').value.trim();
  const password = document.getElementById('signupPassword').value;
  if (!company || !name || !email || !password) return showAuthError('All fields are required');
  if (password.length < 8) return showAuthError('Password must be at least 8 characters');

  // Step 1: Create the auth user
  const { error: authErr } = await sb.auth.signUp({
    email, password, options: { data: { full_name: name } }
  });
  if (authErr) return showAuthError(authErr.message);

  // Step 2: Sign in immediately to get a real session JWT
  // signUp alone doesn't establish a session — RLS blocks DB inserts without it
  const { data: signInData, error: signInErr } = await sb.auth.signInWithPassword({ email, password });
  if (signInErr) {
    return showAuthError('Account created! Please check your email to confirm, then sign in.');
  }

  const userId = signInData.user.id;

  // Step 3: Create org with a real authenticated session
  const slug = company.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/(^-|-$)/g, '');
  const { data: org, error: orgErr } = await sb.from('organizations').insert({
    name: company,
    slug: slug + '-' + Date.now().toString(36)
  }).select('id').single();

  if (orgErr) {
    console.error('Org error:', orgErr);
    return showAuthError('Org setup failed — add INSERT policy for authenticated users on the organizations table in Supabase.');
  }

  // Step 4: Add as owner
  const { error: memErr } = await sb.from('org_members').insert({
    organization_id: org.id, user_id: userId, role: 'owner'
  });
  if (memErr) {
    console.error('Membership error:', memErr);
    return showAuthError('Membership setup failed — check RLS on org_members table.');
  }

  showToast('Account created! Welcome to Gateman.', 'success');
  await loadOrgContext(userId);
  showApp();
}

async function logout() {
  if (realtimeChannel) { sb.removeChannel(realtimeChannel); realtimeChannel = null; }
  await sb.auth.signOut();
  currentOrgId = null; currentUserId = null;
  document.getElementById('app').style.display = 'none';
  document.getElementById('loginPage').style.display = 'flex';
}

function showAuthError(msg) {
  const el = document.getElementById('loginError');
  el.textContent = msg;
  el.style.display = 'block';
}

async function loadOrgContext(userId) {
  currentUserId = userId;
  const { data: membership } = await sb.from('org_members')
    .select('organization_id, role, organizations(name)')
    .eq('user_id', userId).limit(1).single();
  if (membership) {
    currentOrgId = membership.organization_id;
    currentUserRole = membership.role;
    const el = document.getElementById('orgName');
    if (el) el.textContent = membership.organizations?.name || '';
    const navAdmin = document.getElementById('navAdmin');
    if (navAdmin) navAdmin.style.display = (membership.role === 'owner' || membership.role === 'admin') ? '' : 'none';
  }
}

async function showApp() {
  document.getElementById('loginPage').style.display = 'none';
  document.getElementById('app').style.display = 'block';
  document.getElementById('todayDate').textContent = new Date().toLocaleDateString('en-GB', {weekday:'long', day:'numeric', month:'long'});
  initRealtime();
  loadDashboard();
}

// Check existing session on load
(async () => {
  const { data: { session } } = await sb.auth.getSession();
  if (session?.user) {
    await loadOrgContext(session.user.id);
    showApp();
  }
})();

// ============================================================
// REALTIME
// ============================================================
function initRealtime() {
  if (realtimeChannel) sb.removeChannel(realtimeChannel);
  realtimeChannel = sb.channel('dashboard-live')
    .on('postgres_changes',
      { event: 'INSERT', schema: 'public', table: 'attendance_logs', filter: `organization_id=eq.${currentOrgId}` },
      async (payload) => {
        const log = payload.new;
        let userName = 'Unknown', dept = '';
        if (log.user_id) {
          const { data: u } = await sb.from('users').select('name, department').eq('id', log.user_id).single();
          if (u) { userName = u.name; dept = u.department || ''; }
        }
        prependFeedItem({ name: userName, department: dept, action: log.action, timestamp: log.timestamp, photo_url: log.photo_url, credential_value: log.credential_value });
        loadStats();
      }
    )
    .on('postgres_changes',
      { event: 'INSERT', schema: 'public', table: 'enrollment_queue', filter: `organization_id=eq.${currentOrgId}` },
      () => { showToast('New card tapped for enrollment!', 'success'); loadEnrollments(); }
    )
    .subscribe((status) => {
      if (status === 'SUBSCRIBED') {
        document.getElementById('liveStatus').textContent = 'Live';
        document.getElementById('liveIndicator').style.background = 'var(--green)';
      } else if (status === 'CHANNEL_ERROR') {
        document.getElementById('liveStatus').textContent = 'Reconnecting...';
        document.getElementById('liveIndicator').style.background = 'var(--yellow)';
      }
    });
}

// ============================================================
// NAVIGATION
// ============================================================
function showPage(name, btn) {
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));
  document.getElementById(`page-${name}`).classList.add('active');
  btn.classList.add('active');
  closeMobileMenu();
  if (name === 'employees') loadEmployees();
  if (name === 'attendance') loadAttendance();
  if (name === 'enrollment') loadEnrollments();
  if (name === 'devices') loadDevices();
  if (name === 'admin') loadAdminData();
}

function toggleMobileMenu() {
  const sidebar = document.querySelector('.sidebar');
  const overlay = document.getElementById('mobileOverlay');
  const isOpen  = sidebar.classList.contains('open');
  if (isOpen) { closeMobileMenu(); }
  else { sidebar.classList.add('open'); overlay.style.display = 'block'; document.body.style.overflow = 'hidden'; }
}

function closeMobileMenu() {
  document.querySelector('.sidebar').classList.remove('open');
  document.getElementById('mobileOverlay').style.display = 'none';
  document.body.style.overflow = '';
}

// ============================================================
// DASHBOARD
// ============================================================
async function loadDashboard() {
  loadStats(); loadFeed(); loadHourlyChart(); loadWeeklyChart(); loadDeptChart();
}

async function loadStats() {
  if (!currentOrgId) return;
  const { data } = await sb.rpc('get_dashboard_stats', { org_id: currentOrgId });
  if (!data) return;
  document.getElementById('statTotal').textContent   = data.total_employees;
  document.getElementById('statIn').textContent      = data.checked_in;
  document.getElementById('statTaps').textContent    = data.today_records;
  document.getElementById('statDevices').textContent = data.devices;
}

async function loadFeed() {
  if (!currentOrgId) return;
  const { data: records } = await sb.from('attendance_logs')
    .select('*, users(name, department), devices(name)')
    .eq('organization_id', currentOrgId)
    .order('timestamp', { ascending: false }).limit(20);
  if (!records) return;
  const list = document.getElementById('feedList');
  list.innerHTML = '';
  records.forEach(r => appendFeedItem(r, list));
}

function prependFeedItem(r) {
  const list = document.getElementById('feedList');
  list.insertBefore(buildFeedItem(r), list.firstChild);
  if (list.children.length > 30) list.removeChild(list.lastChild);
}

function appendFeedItem(r, container) { container.appendChild(buildFeedItem(r)); }

function buildFeedItem(r) {
  const div = document.createElement('div');
  div.className = 'feed-item';
  const name     = r.users?.name || r.name || 'Unknown';
  const dept     = r.users?.department || r.department || 'Staff';
  const devName  = r.devices?.name || '';
  const initials = name.split(' ').map(w=>w[0]).join('').substring(0,2).toUpperCase();
  const time     = new Date(r.timestamp).toLocaleTimeString('en-GB', {hour:'2-digit', minute:'2-digit'});
  const photoUrl = r.photo_url || '';
  let avatarHtml = initials;
  if (photoUrl) avatarHtml = `<img src="" data-photo="${photoUrl}" onerror="this.parentElement.textContent='${initials}'" style="width:100%;height:100%;object-fit:cover">`;
  div.innerHTML = `
    <div class="feed-avatar">${avatarHtml}</div>
    <div class="feed-info">
      <div class="feed-name">${name}</div>
      <div class="feed-meta">${dept} · ${devName}</div>
    </div>
    <span class="feed-badge ${r.action==='check_in'?'in':'out'}">${r.action==='check_in'?'IN':'OUT'}</span>
    <div class="feed-time">${time}</div>`;
  if (photoUrl) loadSignedUrl(photoUrl).then(url => { const img = div.querySelector('img[data-photo]'); if (img && url) img.src = url; });
  return div;
}

// ============================================================
// SIGNED PHOTO URLS
// ============================================================
const signedUrlCache = {};
async function loadSignedUrl(path) {
  if (!path) return null;
  if (signedUrlCache[path]) return signedUrlCache[path];
  const { data } = await sb.storage.from('attendance-photos').createSignedUrl(path, 300);
  if (data?.signedUrl) { signedUrlCache[path] = data.signedUrl; return data.signedUrl; }
  return null;
}

// ============================================================
// CHARTS
// ============================================================
async function loadHourlyChart() {
  if (!currentOrgId) return;
  const { data } = await sb.rpc('get_hourly_stats', { org_id: currentOrgId });
  if (!data) return;
  const hours = Array.from({length:24}, (_,i) => String(i).padStart(2,'0'));
  const inData  = hours.map(h => { const r = data.find(d=>d.hour===h&&d.action==='check_in');  return Number(r?.count)||0; });
  const outData = hours.map(h => { const r = data.find(d=>d.hour===h&&d.action==='check_out'); return Number(r?.count)||0; });
  if (charts.hourly) charts.hourly.destroy();
  charts.hourly = new Chart(document.getElementById('hourlyChart'), {
    type: 'bar',
    data: { labels: hours.map(h=>h+':00'), datasets: [
      { label:'Check In',  data:inData,  backgroundColor:'rgba(99,102,241,.7)', borderRadius:4 },
      { label:'Check Out', data:outData, backgroundColor:'rgba(239,68,68,.5)',  borderRadius:4 }
    ]},
    options: { responsive:true, maintainAspectRatio:false,
      plugins:{ legend:{ labels:{ color:'#94a3b8', font:{size:11} } } },
      scales:{ x:{ticks:{color:'#475569',font:{size:10}},grid:{color:'#1e2235'}}, y:{ticks:{color:'#475569'},grid:{color:'#1e2235'},beginAtZero:true} } }
  });
}

async function loadWeeklyChart() {
  if (!currentOrgId) return;
  const { data } = await sb.rpc('get_weekly_stats', { org_id: currentOrgId });
  if (!data) return;
  if (charts.weekly) charts.weekly.destroy();
  charts.weekly = new Chart(document.getElementById('weeklyChart'), {
    type: 'line',
    data: { labels: data.map(d => new Date(d.date).toLocaleDateString('en-GB',{weekday:'short',day:'numeric'})),
      datasets: [{ label:'Staff Present', data:data.map(d=>Number(d.unique_staff)),
        borderColor:'#22d3ee', backgroundColor:'rgba(34,211,238,.1)', borderWidth:2, fill:true, tension:.4, pointBackgroundColor:'#22d3ee' }]},
    options: { responsive:true, maintainAspectRatio:false,
      plugins:{ legend:{ labels:{ color:'#94a3b8', font:{size:11} } } },
      scales:{ x:{ticks:{color:'#475569',font:{size:10}},grid:{color:'#1e2235'}}, y:{ticks:{color:'#475569'},grid:{color:'#1e2235'},beginAtZero:true} } }
  });
}

async function loadDeptChart() {
  if (!currentOrgId) return;
  const { data } = await sb.rpc('get_department_presence', { org_id: currentOrgId });
  if (!data || data.length === 0) return;
  const colors = ['#6366f1','#22d3ee','#10b981','#f59e0b','#ef4444','#a78bfa'];
  if (charts.dept) charts.dept.destroy();
  charts.dept = new Chart(document.getElementById('deptChart'), {
    type: 'doughnut',
    data: { labels: data.map(d=>d.department||'Other'),
      datasets: [{ data:data.map(d=>Number(d.present)), backgroundColor:colors, borderWidth:0, hoverOffset:6 }]},
    options: { responsive:true, maintainAspectRatio:false, cutout:'65%',
      plugins:{ legend:{ position:'bottom', labels:{color:'#94a3b8',font:{size:11},padding:12} } } }
  });
}

// ============================================================
// EMPLOYEES
// ============================================================
async function loadEmployees() {
  if (!currentOrgId) return;
  const { data: users } = await sb.from('users')
    .select('id, name, employee_id, department, email, active, user_credentials(type, value)')
    .eq('organization_id', currentOrgId).eq('active', true).order('name');
  if (!users) return;
  allEmployees = users.map(u => {
    const rfid = u.user_credentials?.find(c => c.type === 'rfid');
    return { id:u.id, name:u.name, employee_id:u.employee_id, department:u.department, email:u.email, rfid_uid:rfid?.value||null };
  });
  renderEmployeesTable(allEmployees);
}

function filterEmployees(q) {
  renderEmployeesTable(allEmployees.filter(e =>
    e.name.toLowerCase().includes(q.toLowerCase()) ||
    e.employee_id.toLowerCase().includes(q.toLowerCase()) ||
    (e.department||'').toLowerCase().includes(q.toLowerCase())
  ));
}

function renderEmployeesTable(list) {
  document.getElementById('employeesTable').innerHTML = list.map(e => `
    <tr>
      <td><strong>${e.name}</strong></td>
      <td>${e.employee_id}</td>
      <td>${e.department||'—'}</td>
      <td><code style="font-size:12px;color:var(--accent2)">${e.rfid_uid||'Not assigned'}</code></td>
      <td><span class="badge present">Active</span></td>
      <td style="color:var(--muted);font-size:12px">${e.email||'—'}</td>
      <td><button class="btn btn-outline btn-sm" onclick="openAssignRfid('${e.id}')">Assign Card</button></td>
    </tr>`).join('');
}

function openAddEmployee() { document.getElementById('addEmployeeModal').classList.add('open'); }

async function addEmployee() {
  if (!currentOrgId) return;
  const name  = document.getElementById('empName').value.trim();
  const empId = document.getElementById('empId').value.trim();
  const dept  = document.getElementById('empDept').value.trim();
  const email = document.getElementById('empEmail').value.trim();
  const rfid  = document.getElementById('empRfid').value.trim();
  if (!name || !empId) return showToast('Name and Employee ID required', 'error');

  const { data: newUser, error: userErr } = await sb.from('users').insert({
    organization_id: currentOrgId, name, employee_id: empId, department: dept||null, email: email||null
  }).select('id').single();
  if (userErr) return showToast(userErr.message || 'Failed to add employee', 'error');

  if (rfid && newUser) {
    await sb.from('user_credentials').insert({ user_id: newUser.id, organization_id: currentOrgId, type: 'rfid', value: rfid });
  }

  closeModal('addEmployeeModal');
  ['empName','empId','empDept','empEmail','empRfid'].forEach(id => document.getElementById(id).value = '');
  showToast('Employee added successfully', 'success');
  loadEmployees();
}

async function openAssignRfid(userId) {
  document.getElementById('assignRfidUid').value = userId;
  const select = document.getElementById('assignEmployee');
  select.innerHTML = '';
  if (allEmployees.length === 0) await loadEmployees();
  allEmployees.forEach(e => {
    const opt = document.createElement('option');
    opt.value = e.id; opt.textContent = `${e.name} (${e.employee_id})`;
    if (e.id === userId) opt.selected = true;
    select.appendChild(opt);
  });
  document.getElementById('assignModal').classList.add('open');
}

async function completeAssignment() {
  const userId = document.getElementById('assignEmployee').value;
  if (!userId) return showToast('Select an employee', 'error');
  const rfidValue = prompt('Enter RFID Card UID:');
  if (!rfidValue?.trim()) return showToast('RFID UID is required', 'error');
  const { error } = await sb.from('user_credentials').insert({
    user_id: userId, organization_id: currentOrgId, type: 'rfid', value: rfidValue.trim()
  });
  if (error) return showToast(error.message || 'Failed to assign card', 'error');
  closeModal('assignModal');
  showToast('RFID card assigned successfully', 'success');
  loadEmployees();
}

// ============================================================
// ATTENDANCE
// ============================================================
async function loadAttendance() {
  if (!currentOrgId) return;
  const from = document.getElementById('filterFrom')?.value;
  const to   = document.getElementById('filterTo')?.value;
  let q = sb.from('attendance_logs')
    .select('*, users(name, employee_id, department), devices(name)')
    .eq('organization_id', currentOrgId).order('timestamp', { ascending: false }).limit(200);
  if (from) q = q.gte('timestamp', from + 'T00:00:00');
  if (to)   q = q.lte('timestamp', to   + 'T23:59:59');
  const { data: records } = await q;
  if (!records) return;
  const tb = document.getElementById('attendanceTable');
  tb.innerHTML = '';
  for (const r of records) {
    const name      = r.users?.name || 'Unknown';
    const empId     = r.users?.employee_id || '';
    const dept      = r.users?.department || '—';
    const devName   = r.devices?.name || '';
    const photoUrl  = r.photo_url || '';
    let photoHtml   = '<span style="color:var(--muted);font-size:12px">No photo</span>';
    if (photoUrl) {
      const signed = await loadSignedUrl(photoUrl);
      if (signed) photoHtml = `<img src="${signed}" style="width:36px;height:36px;border-radius:6px;object-fit:cover" onerror="this.style.display='none'">`;
    }
    tb.innerHTML += `
      <tr>
        <td><strong>${name}</strong><br><span style="font-size:11px;color:var(--muted)">${empId}</span></td>
        <td>${dept}</td>
        <td><span class="feed-badge ${r.action==='check_in'?'in':'out'}">${r.action==='check_in'?'CHECK IN':'CHECK OUT'}</span></td>
        <td style="font-size:13px">${new Date(r.timestamp).toLocaleString('en-GB')}</td>
        <td style="color:var(--muted);font-size:13px">${devName}</td>
        <td>${photoHtml}</td>
      </tr>`;
  }
}

// ============================================================
// ENROLLMENT
// ============================================================
async function loadEnrollments() {
  if (!currentOrgId) return;
  const { data: list }      = await sb.from('enrollment_queue').select('*, devices(name)').eq('organization_id', currentOrgId).eq('status', 'pending').order('created_at', { ascending: false });
  const { data: employees } = await sb.from('users').select('id, name, employee_id').eq('organization_id', currentOrgId).eq('active', true).order('name');
  const tb = document.getElementById('enrollmentTable');
  if (!list || list.length === 0) {
    tb.innerHTML = '<tr><td colspan="5" style="text-align:center;color:var(--muted);padding:32px">No pending enrollments</td></tr>';
    return;
  }
  let html = '';
  for (const e of list) {
    let photoHtml = '<span style="color:var(--muted);font-size:12px">—</span>';
    if (e.photo_url) { const s = await loadSignedUrl(e.photo_url); if (s) photoHtml = `<img src="${s}" style="width:40px;height:40px;border-radius:8px;object-fit:cover">`; }
    const empOptions = (employees||[]).map(emp => `<option value="${emp.id}">${emp.name} (${emp.employee_id})</option>`).join('');
    html += `<tr>
      <td><code style="color:var(--accent2)">${e.credential_value}</code></td>
      <td>${photoHtml}</td>
      <td style="color:var(--muted);font-size:13px">${e.devices?.name||''}</td>
      <td style="color:var(--muted);font-size:12px">${new Date(e.created_at).toLocaleString('en-GB')}</td>
      <td>
        <select id="assign_${e.id}" class="search-input" style="width:180px;padding:6px 10px">
          <option value="">Select employee...</option>${empOptions}
        </select>
        <button class="btn btn-green btn-sm" style="margin-left:8px" onclick="assignCard('${e.id}','${e.credential_value}')">Assign</button>
      </td></tr>`;
  }
  tb.innerHTML = html;
}

async function assignCard(enrollmentId, credentialValue) {
  const empId = document.getElementById(`assign_${enrollmentId}`).value;
  if (!empId) return showToast('Select an employee first', 'error');
  const { error } = await sb.from('user_credentials').insert({ user_id: empId, organization_id: currentOrgId, type: 'rfid', value: credentialValue });
  if (error) return showToast(error.message || 'Failed to assign card', 'error');
  await sb.from('enrollment_queue').update({ status: 'assigned', assigned_to: empId, resolved_at: new Date().toISOString() }).eq('id', enrollmentId);
  showToast('Card assigned successfully!', 'success');
  loadEnrollments();
}

// ============================================================
// DEVICES
// ============================================================
async function loadDevices() {
  if (!currentOrgId) return;
  const { data: devices } = await sb.from('devices').select('*').eq('organization_id', currentOrgId).order('created_at', { ascending: false });
  const tb = document.getElementById('devicesTable');
  if (!devices || devices.length === 0) {
    tb.innerHTML = '<tr><td colspan="5" style="text-align:center;color:var(--muted);padding:32px">No devices registered yet. Click "+ Add Device" to provision one.</td></tr>';
    return;
  }
  tb.innerHTML = devices.map(d => `
    <tr>
      <td><strong>${d.name||'Unnamed'}</strong></td>
      <td><code style="font-size:12px;color:var(--accent2)">${d.device_uid||'—'}</code></td>
      <td>${d.location||'—'}</td>
      <td><span class="badge ${d.status==='active'?'present':d.status==='inactive'?'pending':'absent'}">${d.status}</span></td>
      <td style="color:var(--muted);font-size:12px">${d.last_seen ? new Date(d.last_seen).toLocaleString('en-GB') : 'Never'}</td>
    </tr>`).join('');
}

function openProvisionModal() {
  document.getElementById('provDeviceName').value = '';
  document.getElementById('provDeviceCode').value = '';
  document.getElementById('provisionResult').style.display = 'none';
  document.getElementById('provisionModal').classList.add('open');
}

async function registerDevice() {
  const name = document.getElementById('provDeviceName').value.trim();
  const code = document.getElementById('provDeviceCode').value.trim().toUpperCase().replace(/[^A-F0-9]/g, '');
  const btn  = document.getElementById('btnRegisterDevice');

  if (!code || code.length !== 12) {
    showToast('Enter the 12-character device code from the portal', 'error');
    return;
  }

  btn.disabled = true; btn.textContent = 'Registering...';

  try {
    // Insert device — device_uid and secret are both the MAC-derived code
    const { error } = await sb.from('devices').insert({
      organization_id: currentOrgId,
      name:            name || 'New Device',
      device_uid:      code,
      secret:          code,
      status:          'active'
    });

    if (error) {
      if (error.message.includes('unique') || error.message.includes('duplicate')) {
        showToast('Device already registered', 'error');
      } else {
        showToast(error.message, 'error');
      }
      return;
    }

    closeModal('provisionModal');
    document.getElementById('provisionResult').style.display = 'block';
    showToast('Device registered successfully!', 'success');
    loadDevices();

  } catch(e) {
    showToast('Error: ' + e.message, 'error');
  } finally {
    btn.disabled = false; btn.textContent = 'Register Device';
  }
}

// ============================================================
// EXPORT
// ============================================================
async function exportCSV() {
  if (!currentOrgId) return;
  const from = document.getElementById('filterFrom')?.value;
  const to   = document.getElementById('filterTo')?.value;
  let q = sb.from('attendance_logs').select('*, users(name, employee_id, department), devices(name)')
    .eq('organization_id', currentOrgId).order('timestamp', { ascending: false }).limit(5000);
  if (from) q = q.gte('timestamp', from + 'T00:00:00');
  if (to)   q = q.lte('timestamp', to   + 'T23:59:59');
  const { data: records } = await q;
  if (!records || records.length === 0) return showToast('No records to export', 'error');
  const header = 'Name,Employee ID,Department,Action,Timestamp,Device,RFID\n';
  const rows = records.map(r =>
    `"${r.users?.name||'Unknown'}","${r.users?.employee_id||''}","${r.users?.department||''}","${r.action}","${r.timestamp}","${r.devices?.name||''}","${r.credential_value||''}"`
  ).join('\n');
  const blob = new Blob([header + rows], { type: 'text/csv' });
  const url  = URL.createObjectURL(blob);
  const a    = document.createElement('a');
  a.href = url; a.download = `attendance_${new Date().toISOString().split('T')[0]}.csv`;
  a.click(); URL.revokeObjectURL(url);
  showToast(`Exported ${records.length} records`, 'success');
}

// ============================================================
// ADMIN
// ============================================================
async function loadAdminData() {
  if (!currentOrgId || (currentUserRole !== 'owner' && currentUserRole !== 'admin')) return;
  await Promise.all([loadAdminSubscription(), loadAdminStats(), loadAuditTrail()]);
}

async function loadAdminSubscription() {
  const { data: sub } = await sb.from('subscriptions').select('*').eq('organization_id', currentOrgId).in('status', ['active','trial','past_due']).single();
  const el = document.getElementById('adminSubInfo');
  if (!sub) { el.textContent = 'No active subscription found'; return; }
  const colors = { active:'var(--green)', trial:'var(--yellow)', past_due:'var(--red)' };
  const trialInfo  = sub.status === 'trial' && sub.trial_ends_at ? ` — expires ${new Date(sub.trial_ends_at).toLocaleDateString()}` : '';
  const periodEnd  = sub.current_period_end ? new Date(sub.current_period_end).toLocaleDateString() : 'N/A';
  el.innerHTML = `<div style="display:flex;gap:24px;flex-wrap:wrap">
    <div><strong>Plan:</strong> ${sub.plan_type}</div>
    <div><strong>Status:</strong> <span style="color:${colors[sub.status]||'var(--muted)'}">${sub.status.toUpperCase()}</span>${trialInfo}</div>
    <div><strong>Devices:</strong> ${sub.device_limit} max</div>
    <div><strong>Users:</strong> ${sub.user_limit} max</div>
    <div><strong>Retention:</strong> ${sub.retention_days} days</div>
    <div><strong>Period ends:</strong> ${periodEnd}</div>
  </div>`;
}

async function loadAdminStats() {
  const [empRes, devRes, logRes, enrRes] = await Promise.all([
    sb.from('users').select('*',{count:'exact',head:true}).eq('organization_id',currentOrgId).eq('active',true),
    sb.from('devices').select('*',{count:'exact',head:true}).eq('organization_id',currentOrgId).eq('status','active'),
    sb.from('attendance_logs').select('*',{count:'exact',head:true}).eq('organization_id',currentOrgId),
    sb.from('enrollment_queue').select('*',{count:'exact',head:true}).eq('organization_id',currentOrgId).eq('status','pending'),
  ]);
  document.getElementById('adminTotalEmployees').textContent    = empRes.count ?? 0;
  document.getElementById('adminTotalDevices').textContent      = devRes.count ?? 0;
  document.getElementById('adminTotalLogs').textContent         = logRes.count ?? 0;
  document.getElementById('adminPendingEnrollments').textContent = enrRes.count ?? 0;
}

async function loadAuditTrail() {
  const { data: logs } = await sb.from('audit_logs').select('*').eq('organization_id', currentOrgId).order('created_at', { ascending: false }).limit(50);
  const tbody = document.getElementById('auditTable');
  if (!logs || logs.length === 0) {
    tbody.innerHTML = '<tr><td colspan="5" style="text-align:center;color:var(--muted);padding:24px">No audit events yet</td></tr>';
    return;
  }
  tbody.innerHTML = logs.map(l => {
    const actor    = l.actor_type === 'device' ? `Device ${(l.actor_id||'').substring(0,8)}...` : l.actor_type === 'system' ? 'System' : `User ${(l.actor_id||'').substring(0,8)}...`;
    const resource = l.resource_type || '-';
    const details  = l.metadata ? Object.entries(l.metadata).map(([k,v])=>`${k}=${v}`).join(', ').substring(0,80) : '-';
    return `<tr>
      <td style="white-space:nowrap">${new Date(l.created_at).toLocaleString()}</td>
      <td>${actor}</td>
      <td><span style="background:var(--surface2);padding:2px 8px;border-radius:4px;font-size:12px">${l.action}</span></td>
      <td>${resource}</td>
      <td style="font-size:12px;color:var(--muted);max-width:200px;overflow:hidden;text-overflow:ellipsis">${details}</td>
    </tr>`;
  }).join('');
}

// ============================================================
// UTILS
// ============================================================
function closeModal(id) { document.getElementById(id).classList.remove('open'); }
document.querySelectorAll('.modal-overlay').forEach(m => {
  m.addEventListener('click', e => { if (e.target === m) m.classList.remove('open'); });
});

let toastTimer;
function showToast(msg, type='success') {
  const t = document.getElementById('toast');
  t.textContent = msg; t.className = `show ${type}`;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.className = '', 3000);
}

document.getElementById('loginPassword').addEventListener('keydown', e => { if (e.key === 'Enter') login(); });
</script>
</body>
</html>