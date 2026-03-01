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
      <strong>Step 1:</strong> Open dashboard and click "Claim Device"<br>
      <strong>Step 2:</strong> Enter WiFi credentials below<br>
      <strong>Step 3:</strong> Device will auto-provision when claimed
    </div>

    <div class="form-group">
      <label>Device MAC Address</label>
      <input type="text" id="mac" value="%MAC%" readonly>
    </div>

    <div class="info" style="background:#fef3c7;border-color:#fbbf24;color:#78350f;margin-bottom:20px">
      <strong>📱 Claim this device from your dashboard:</strong><br>
      <a href="http://dashboard.gateman.app/claim?mac=%MAC%" target="_blank" style="color:#78350f;font-weight:600;text-decoration:underline">
        Click here to open dashboard
      </a>
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

      <button type="submit">Save WiFi & Wait for Claim</button>
    </form>

    <div id="status" class="info"></div>
  </div>

  <script>
    async function submitForm(e) {
      e.preventDefault();
      const btn = e.target.querySelector('button');
      const status = document.getElementById('status');
      const mac = document.getElementById('mac').value;
      
      btn.disabled = true;
      btn.textContent = 'Saving...';
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

        if (response.ok) {
          status.className = 'info';
          status.textContent = '✓ WiFi saved! Waiting for you to claim device from dashboard...';
          btn.textContent = 'Waiting for Claim...';
          
          // Start polling for claim
          pollForClaim(mac);
        } else {
          status.className = 'info error';
          status.textContent = '✗ Failed to save WiFi credentials';
          btn.disabled = false;
          btn.textContent = 'Save WiFi & Wait for Claim';
        }
      } catch (err) {
        status.className = 'info error';
        status.textContent = '✗ Error: ' + err.message;
        btn.disabled = false;
        btn.textContent = 'Save WiFi & Wait for Claim';
      }
    }

    async function pollForClaim(mac) {
      const status = document.getElementById('status');
      let attempts = 0;
      const maxAttempts = 60; // 5 minutes (5s intervals)

      const interval = setInterval(async () => {
        attempts++;
        
        try {
          const response = await fetch('/check-claim');
          const result = await response.json();

          if (result.claimed) {
            clearInterval(interval);
            status.className = 'info success';
            status.textContent = '✓ Device claimed! Provisioning now...';
            
            setTimeout(() => {
              status.textContent = '✓ Provisioning complete. Device rebooting...';
            }, 2000);
          } else if (attempts >= maxAttempts) {
            clearInterval(interval);
            status.className = 'info error';
            status.textContent = '✗ Timeout waiting for claim. Please try again.';
            document.querySelector('button').disabled = false;
            document.querySelector('button').textContent = 'Save WiFi & Wait for Claim';
          } else {
            status.textContent = `⏳ Waiting for claim from dashboard... (${Math.floor((maxAttempts - attempts) * 5 / 60)}m ${((maxAttempts - attempts) * 5) % 60}s remaining)`;
          }
        } catch (err) {
          console.error('Poll error:', err);
        }
      }, 5000); // Poll every 5 seconds
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

// Global variables for claim polling
String savedSSID = "";
String savedPassword = "";
String deviceMAC = "";
bool claimReceived = false;
String claimToken = "";

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

  if (savedSSID.length() == 0) {
    provisionServer.send(400, "application/json", "{\"error\":\"Missing SSID\"}");
    return;
  }

  // Save WiFi credentials to NVS
  preferences.begin("gateman", false);
  preferences.putString("wifi_ssid", savedSSID);
  preferences.putString("wifi_pass", savedPassword);
  preferences.end();

  provisionServer.send(200, "application/json", "{\"status\":\"success\"}");
}

void handleCheckClaim() {
  // Return claim status
  if (claimReceived) {
    provisionServer.send(200, "application/json", "{\"claimed\":true}");
    
    // Trigger provisioning
    preferences.begin("gateman", false);
    preferences.putString("prov_token", claimToken);
    preferences.putBool("needs_prov", true);
    preferences.end();
    
    delay(2000);
    ESP.restart();
  } else {
    provisionServer.send(200, "application/json", "{\"claimed\":false}");
  }
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
  provisionServer.on("/check-claim", handleCheckClaim);
  provisionServer.onNotFound(handleNotFound);
  
  provisionServer.begin();
  Serial.println("[AP] Web server started");
  Serial.println("[AP] Waiting for WiFi credentials and claim...");

  deviceMAC = WiFi.macAddress();
  unsigned long lastPollMs = 0;
  const unsigned long POLL_INTERVAL = 5000; // Poll cloud every 5 seconds

  // Main loop - handle web requests and poll for claims
  while (true) {
    dnsServer.processNextRequest();
    provisionServer.handleClient();
    
    // Poll Supabase for claim if WiFi credentials are saved
    if (savedSSID.length() > 0 && millis() - lastPollMs > POLL_INTERVAL) {
      lastPollMs = millis();
      
      // Try to connect to WiFi temporarily to check for claim
      WiFi.mode(WIFI_AP_STA);
      WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
      
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(250);
        attempts++;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        // Check for claim from Supabase
        HTTPClient http;
        http.begin("https://ueobebsgheecclwcbigy.supabase.co/functions/v1/poll-claim");
        http.addHeader("Content-Type", "application/json");
        
        String payload = "{\"device_mac\":\"" + deviceMAC + "\"}";
        int code = http.POST(payload);
        
        if (code == 200) {
          String response = http.getString();
          DynamicJsonDocument doc(512);
          if (deserializeJson(doc, response) == DeserializationError::Ok) {
            if (doc["claimed"] == true) {
              claimReceived = true;
              claimToken = doc["provision_token"].as<String>();
              Serial.println("[AP] Device claimed! Provisioning...");
            }
          }
        }
        http.end();
        
        // Disconnect WiFi, go back to AP-only mode
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
      }
    }
    
    // Slow blink = waiting for claim
    digitalWrite(STATUS_LED, HIGH);
    delay(500);
    digitalWrite(STATUS_LED, LOW);
    delay(500);
  }
}

#endif
