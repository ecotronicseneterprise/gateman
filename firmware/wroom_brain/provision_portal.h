/*
 * WiFi AP Provisioning Portal
 * Creates a captive portal for easy device setup
 */

#ifndef PROVISION_PORTAL_H
#define PROVISION_PORTAL_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// External references to main sketch variables
extern Preferences preferences;

// STATUS_LED will be defined in main sketch, but we need it available here
#ifndef STATUS_LED
#define STATUS_LED 2
#endif

WebServer provisionServer(80);
DNSServer dnsServer;

const char PROVISION_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>GATEMAN Setup</title>
  <style>
    * { margin:0; padding:0; box-sizing:border-box; }
    body { 
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 20px;
    }
    .container {
      background: white;
      border-radius: 16px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
      max-width: 420px;
      width: 100%;
      padding: 32px;
    }
    .logo {
      text-align: center;
      margin-bottom: 24px;
    }
    .logo svg {
      width: 48px;
      height: 48px;
      margin-bottom: 12px;
    }
    h1 {
      font-size: 24px;
      font-weight: 700;
      color: #1a202c;
      text-align: center;
      margin-bottom: 8px;
    }
    .subtitle {
      text-align: center;
      color: #718096;
      font-size: 14px;
      margin-bottom: 32px;
    }
    .form-group {
      margin-bottom: 20px;
    }
    label {
      display: block;
      font-size: 13px;
      font-weight: 600;
      color: #4a5568;
      margin-bottom: 8px;
    }
    input, textarea {
      width: 100%;
      padding: 12px 16px;
      border: 2px solid #e2e8f0;
      border-radius: 8px;
      font-size: 14px;
      transition: all 0.2s;
      font-family: inherit;
    }
    input:focus, textarea:focus {
      outline: none;
      border-color: #667eea;
      box-shadow: 0 0 0 3px rgba(102,126,234,0.1);
    }
    input[readonly] {
      background: #f7fafc;
      color: #718096;
    }
    textarea {
      resize: vertical;
      min-height: 80px;
      font-family: monospace;
      font-size: 12px;
    }
    button {
      width: 100%;
      padding: 14px;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      border: none;
      border-radius: 8px;
      font-size: 15px;
      font-weight: 600;
      cursor: pointer;
      transition: transform 0.2s;
    }
    button:hover {
      transform: translateY(-2px);
    }
    button:active {
      transform: translateY(0);
    }
    .info {
      background: #ebf8ff;
      border: 1px solid #90cdf4;
      border-radius: 8px;
      padding: 12px;
      font-size: 13px;
      color: #2c5282;
      margin-bottom: 20px;
    }
    .success {
      background: #c6f6d5;
      border-color: #68d391;
      color: #22543d;
    }
    .error {
      background: #fed7d7;
      border-color: #fc8181;
      color: #742a2a;
    }
    #status {
      display: none;
      margin-top: 16px;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="logo">
      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 72 88" fill="url(#grad)">
        <defs>
          <linearGradient id="grad" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stop-color="#667eea"/>
            <stop offset="100%" stop-color="#764ba2"/>
          </linearGradient>
        </defs>
        <rect x="0" y="0" width="14" height="88" rx="7"/>
        <rect x="58" y="0" width="14" height="88" rx="7"/>
        <rect x="0" y="0" width="72" height="14" rx="7"/>
      </svg>
      <h1>GATEMAN Setup</h1>
      <div class="subtitle">Configure your attendance device</div>
    </div>

    <div class="info">
      <strong>Step 1:</strong> Go to dashboard → Devices → Add Device → Generate Token<br>
      <strong>Step 2:</strong> Scan QR code OR paste token manually<br>
      <strong>Step 3:</strong> Enter WiFi credentials and submit
    </div>

    <div class="form-group">
      <label>Device MAC Address</label>
      <input type="text" id="mac" value="%MAC%" readonly>
    </div>

    <form id="provisionForm" onsubmit="submitForm(event)">
      <div class="form-group">
        <label>Provisioning Token *</label>
        <div style="display:flex;gap:8px;margin-bottom:8px">
          <button type="button" onclick="startQRScan()" style="width:auto;padding:8px 16px;font-size:13px">📷 Scan QR Code</button>
          <button type="button" onclick="stopQRScan()" id="stopBtn" style="width:auto;padding:8px 16px;font-size:13px;display:none;background:#dc2626">Stop Camera</button>
        </div>
        <video id="qrVideo" style="width:100%;max-width:300px;border-radius:8px;display:none;margin-bottom:8px"></video>
        <canvas id="qrCanvas" style="display:none"></canvas>
        <input type="text" id="token" name="token" placeholder="Paste token or scan QR code" required style="font-family:monospace">
      </div>

      <div class="form-group">
        <label>WiFi SSID *</label>
        <input type="text" id="ssid" name="ssid" placeholder="Your WiFi network name" required>
      </div>

      <div class="form-group">
        <label>WiFi Password *</label>
        <input type="password" id="password" name="password" placeholder="WiFi password" required>
      </div>

      <button type="submit">Provision Device</button>
    </form>

    <div id="status" class="info"></div>
  </div>

  <script src="https://cdn.jsdelivr.net/npm/jsqr@1.4.0/dist/jsQR.min.js"></script>
  <script>
    let qrStream = null;
    let qrScanning = false;

    async function startQRScan() {
      const video = document.getElementById('qrVideo');
      const canvas = document.getElementById('qrCanvas');
      const ctx = canvas.getContext('2d');
      const stopBtn = document.getElementById('stopBtn');
      
      try {
        qrStream = await navigator.mediaDevices.getUserMedia({ 
          video: { facingMode: 'environment' } 
        });
        video.srcObject = qrStream;
        video.style.display = 'block';
        stopBtn.style.display = 'inline-block';
        await video.play();
        
        qrScanning = true;
        scanQRCode(video, canvas, ctx);
      } catch(err) {
        alert('Camera access denied or not available: ' + err.message);
      }
    }

    function stopQRScan() {
      qrScanning = false;
      const video = document.getElementById('qrVideo');
      const stopBtn = document.getElementById('stopBtn');
      
      if (qrStream) {
        qrStream.getTracks().forEach(track => track.stop());
        qrStream = null;
      }
      video.style.display = 'none';
      stopBtn.style.display = 'none';
    }

    function scanQRCode(video, canvas, ctx) {
      if (!qrScanning) return;
      
      canvas.width = video.videoWidth;
      canvas.height = video.videoHeight;
      ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
      
      const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
      const code = jsQR(imageData.data, imageData.width, imageData.height);
      
      if (code) {
        document.getElementById('token').value = code.data;
        stopQRScan();
        alert('✓ QR Code scanned successfully!');
      } else {
        requestAnimationFrame(() => scanQRCode(video, canvas, ctx));
      }
    }

    async function submitForm(e) {
      e.preventDefault();
      const btn = e.target.querySelector('button');
      const status = document.getElementById('status');
      
      btn.disabled = true;
      btn.textContent = 'Provisioning...';
      status.style.display = 'block';
      status.className = 'info';
      status.textContent = 'Connecting to WiFi and provisioning device...';

      const formData = new FormData(e.target);
      const data = {
        token: formData.get('token'),
        ssid: formData.get('ssid'),
        password: formData.get('password')
      };

      try {
        const response = await fetch('/save-wifi', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(data)
        });

        const result = await response.json();

        if (response.ok) {
          status.className = 'info';
          status.textContent = '✓ Success! Device is provisioning and will reboot...';
          setTimeout(() => {
            status.textContent = 'You can close this page. Device will connect to WiFi shortly.';
          }, 3000);
        } else {
          status.className = 'info error';
          status.textContent = '✗ ' + (result.error || 'Provisioning failed');
          btn.disabled = false;
          btn.textContent = 'Provision Device';
        }
      } catch (err) {
        status.className = 'info error';
        status.textContent = '✗ Error: ' + err.message;
        btn.disabled = false;
        btn.textContent = 'Generate Pairing Code';
      }
    }

    function copyCode() {
      const code = document.getElementById('codeDisplay').textContent;
      navigator.clipboard.writeText(code).then(() => {
        const btn = event.target;
        const originalText = btn.textContent;
        btn.textContent = '✓ Copied!';
        btn.style.background = '#15803d';
        setTimeout(() => {
          btn.textContent = originalText;
          btn.style.background = '#22c55e';
        }, 2000);
      });
    }
  </script>
</body>
</html>
)rawliteral";

// External reference to DEVICE_UID from main sketch
extern String DEVICE_UID;

void handleRoot() {
  String html = PROVISION_HTML;
  html.replace("%MAC%", DEVICE_UID);
  provisionServer.send(200, "text/html", html);
}

// Global variables for WiFi setup
String savedSSID = "";
String savedPassword = "";
String deviceMAC = "";

void handleSaveWiFi() {
  if (!provisionServer.hasArg("plain")) {
    provisionServer.send(400, "application/json", "{\"error\":\"No data\"}");
    return;
  }

  String body = provisionServer.arg("plain");
  DynamicJsonDocument doc(512);
  
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    provisionServer.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  savedSSID = doc["ssid"].as<String>();
  savedPassword = doc["password"].as<String>();
  String token = doc["token"].as<String>();

  if (savedSSID.length() == 0) {
    provisionServer.send(400, "application/json", "{\"error\":\"Missing SSID\"}");
    return;
  }

  if (token.length() == 0) {
    provisionServer.send(400, "application/json", "{\"error\":\"Missing provisioning token\"}");
    return;
  }

  // Save WiFi credentials to NVS
  preferences.begin("gateman", false);
  preferences.putString("wifi_ssid", savedSSID);
  preferences.putString("wifi_pass", savedPassword);
  preferences.end();

  Serial.println("[AP] WiFi and token saved. Connecting to WiFi...");

  // Return success to user
  provisionServer.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Connecting to WiFi...\"}");
  
  delay(1000);
  
  // Stop AP and DNS server
  dnsServer.stop();
  provisionServer.stop();
  WiFi.softAPdisconnect(true);
  
  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  
  Serial.print("[WiFi] Connecting to " + savedSSID + "...");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Connected!");
    Serial.println("[WiFi] IP: " + WiFi.localIP().toString());
    
    // Now provision device
    delay(1000);
    provisionDevice(token);
  } else {
    Serial.println(" FAILED!");
    Serial.println("[ERROR] Could not connect to WiFi. Rebooting...");
    delay(2000);
    ESP.restart();
  }
}

void handleNotFound() {
  // Redirect all unknown requests to root (captive portal behavior)
  provisionServer.sendHeader("Location", "/", true);
  provisionServer.send(302, "text/plain", "");
}

void startProvisioningPortal() {
  // Use last 4 chars of MAC for SSID (already have DEVICE_UID from setup)
  String apSSID = "GATEMAN-SETUP-" + DEVICE_UID.substring(12);
  apSSID.replace(":", "");
  
  Serial.println("[AP] Starting provisioning portal...");
  Serial.println("[AP] SSID: " + apSSID);
  Serial.println("[AP] Password: (none)");
  Serial.println("[AP] Connect and go to: http://192.168.4.1");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID.c_str());
  
  delay(500);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("[AP] IP: ");
  Serial.println(IP);

  // DNS server for captive portal
  dnsServer.start(53, "*", IP);

  // Web server routes
  provisionServer.on("/", handleRoot);
  provisionServer.on("/save-wifi", HTTP_POST, handleSaveWiFi);
  provisionServer.onNotFound(handleNotFound);
  
  provisionServer.begin();
  Serial.println("[AP] Web server started");
  Serial.println("[AP] Waiting for user to enter WiFi credentials...");

  // Main loop - handle web requests and wait for pairing
  while (true) {
    dnsServer.processNextRequest();
    provisionServer.handleClient();
    
    // Slow blink = waiting for setup
    digitalWrite(STATUS_LED, HIGH);
    delay(500);
    digitalWrite(STATUS_LED, LOW);
    delay(500);
  }
}

#endif
