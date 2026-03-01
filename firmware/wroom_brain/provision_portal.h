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
      <strong>Step 1:</strong> Enter your WiFi credentials below<br>
      <strong>Step 2:</strong> Copy the pairing code shown<br>
      <strong>Step 3:</strong> Go to dashboard and paste code to add device
    </div>

    <div class="form-group">
      <label>Device MAC Address</label>
      <input type="text" id="mac" value="%MAC%" readonly>
    </div>

    <form id="provisionForm" onsubmit="submitForm(event)">
      <div class="form-group">
        <label>WiFi SSID *</label>
        <input type="text" id="ssid" name="ssid" placeholder="Your WiFi network name" required>
      </div>

      <div class="form-group">
        <label>WiFi Password *</label>
        <input type="password" id="password" name="password" placeholder="WiFi password" required>
      </div>

      <button type="submit">Generate Pairing Code</button>
    </form>

    <div id="status" class="info"></div>
    
    <div id="pairingCode" style="display:none;margin-top:20px;padding:20px;background:#f0fdf4;border:2px solid #22c55e;border-radius:8px;text-align:center">
      <div style="font-size:14px;color:#15803d;margin-bottom:10px;font-weight:600">✓ WiFi Saved! Your Pairing Code:</div>
      <div style="font-size:32px;font-weight:700;color:#15803d;letter-spacing:4px;margin:15px 0;font-family:monospace" id="codeDisplay"></div>
      <button onclick="copyCode()" style="background:#22c55e;color:white;border:none;padding:12px 24px;border-radius:6px;font-size:14px;font-weight:600;cursor:pointer;margin-top:10px">📋 Copy Code</button>
      <div style="margin-top:15px;font-size:13px;color:#15803d">
        Go to your dashboard → Add Device → Paste this code
      </div>
    </div>
  </div>

  <script>
    async function submitForm(e) {
      e.preventDefault();
      const btn = e.target.querySelector('button');
      const status = document.getElementById('status');
      const mac = document.getElementById('mac').value;
      
      btn.disabled = true;
      btn.textContent = 'Generating...';
      status.style.display = 'block';
      status.className = 'info';
      status.textContent = 'Saving WiFi credentials...';

      const formData = new FormData(e.target);
      const data = {
        mac: mac,
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

        if (response.ok && result.pairing_code) {
          status.style.display = 'none';
          document.getElementById('provisionForm').style.display = 'none';
          document.getElementById('pairingCode').style.display = 'block';
          document.getElementById('codeDisplay').textContent = result.pairing_code;
        } else {
          status.className = 'info error';
          status.textContent = '✗ Failed to generate pairing code';
          btn.disabled = false;
          btn.textContent = 'Generate Pairing Code';
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

void handleRoot() {
  String html = PROVISION_HTML;
  html.replace("%MAC%", WiFi.macAddress());
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
  deviceMAC = WiFi.macAddress();

  if (savedSSID.length() == 0) {
    provisionServer.send(400, "application/json", "{\"error\":\"Missing SSID\"}");
    return;
  }

  // Save WiFi credentials to NVS
  preferences.begin("gateman", false);
  preferences.putString("wifi_ssid", savedSSID);
  preferences.putString("wifi_pass", savedPassword);
  preferences.end();

  // Generate pairing code (MAC address without colons + last 4 chars as suffix)
  String pairingCode = deviceMAC;
  pairingCode.replace(":", "");
  pairingCode.toUpperCase();

  Serial.println("[AP] WiFi saved. Pairing code: " + pairingCode);

  // Return pairing code to user
  String response = "{\"status\":\"success\",\"pairing_code\":\"" + pairingCode + "\"}";
  provisionServer.send(200, "application/json", response);
}

void handleNotFound() {
  // Redirect all unknown requests to root (captive portal behavior)
  provisionServer.sendHeader("Location", "/", true);
  provisionServer.send(302, "text/plain", "");
}

void startProvisioningPortal() {
  String apSSID = "GATEMAN-SETUP-" + WiFi.macAddress().substring(12);
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

  deviceMAC = WiFi.macAddress();

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
