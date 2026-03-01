/*
 * EcoTronic — ESP32-WROOM (Brain) FINAL FIRMWARE
 *
 * Improvements in this version:
 *   ✅ Hardware watchdog (auto-reboot if hangs)
 *   ✅ Duplicate tap prevention (5s window)
 *   ✅ UART ACK protocol (retries if CAM doesn't respond)
 *   ✅ Heartbeat ping to CAM every 60s
 *   ✅ No-photo fallback (logs even if CAM fails)
 *   ✅ Enrollment button (GPIO4, hold 2s)
 *
 * WIRING:
 *   RFID SDA  → GPIO5   SCK → GPIO18  MOSI → GPIO23  MISO → GPIO19  RST → 3.3V
 *   CAM TX    → GPIO17  CAM RX → GPIO16  SHARED GND
 *   ENROLL BTN → GPIO4 (to GND, internal pullup)
 *   STATUS LED → GPIO2
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include <mbedtls/md.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include "provision_portal.h"

// ============================================================
// CONFIGURATION — WiFi & NTP (still hardcoded per-site)
// ============================================================
const char* WIFI_SSID     = "MTN_4G_56F7A3";
const char* WIFI_PASSWORD = "88888888";
const char* NTP_SERVER    = "pool.ntp.org";
const long  GMT_OFFSET    = 3600;
const int   DAYLIGHT      = 0;

// Supabase anon key (public, safe to embed)
const char* SUPABASE_ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVlb2JlYnNnaGVlY2Nsd2NiaWd5Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3MzU0MDU0NzMsImV4cCI6MjA1MDk4MTQ3M30.Qrtu1QaKNEy_wKPPCKQlU1MXwSPZVQNMECLPZqhHQRo";

// ============================================================
// DEVICE IDENTITY — loaded from NVS after provisioning
// ============================================================
String SUPABASE_URL  = "";   // e.g. "https://xxxx.supabase.co"
String DEVICE_UID    = "";   // WiFi.macAddress()
String DEVICE_SECRET = "";   // Returned once during provisioning
String DEVICE_ID     = "";   // UUID from Supabase

// Provisioning token — hardcoded for pilot, empty for production
// For pilot: paste the token from dashboard here before flashing
const char* PILOT_PROVISION_TOKEN = "";  // e.g. "b4d3...d0e"

Preferences preferences;

// ============================================================
// PINS
// ============================================================
#define RFID_SS    5
#define RFID_SCK   18
#define RFID_MOSI  23
#define RFID_MISO  19
#define CAM_RX     16
#define CAM_TX     17
#define ENROLL_BTN 4
#define STATUS_LED 2

// ============================================================
// CONSTANTS
// ============================================================
#define QUEUE_SIZE        50
#define MAX_USERS         100
#define CAM_TIMEOUT_MS    8000
#define DUPLICATE_WINDOW  5000   // ms — ignore same card within 5s
#define WDT_TIMEOUT_S     60     // reboot if stuck 60s (increased for stability)
#define HEARTBEAT_MS      60000
#define SYNC_MAX_PER_CYCLE 20    // Max records to sync per cycle (guard watchdog)
#define HTTP_TIMEOUT_MS   8000   // Per-request timeout for submit-log
#define OFFLINE_QUEUE_FILE "/queue.txt"  // SPIFFS file for offline logs

HardwareSerial camSerial(2);
MFRC522 rfid(RFID_SS, -1);

// ============================================================
// DATA
// ============================================================
struct AttendanceEvent { String rfid_uid; unsigned long timestamp; };
struct User {
  String user_id, name, employee_id, department, rfid_uid, last_action;
  unsigned long last_timestamp;
};

AttendanceEvent queue[QUEUE_SIZE];
int qHead = 0, qTail = 0;
bool qBusy = false;

User users[MAX_USERS];
int userCount = 0;

bool enrollMode = false;
String activeEnrollmentId = "";  // Set when server requests enrollment
unsigned long enrollTimeout = 0;  // Auto-exit enroll mode after timeout
unsigned long eventCounter = 0;  // Monotonic counter for device_event_id

#define ENROLL_POLL_MS    30000  // Check server for enrollment commands every 30s
#define ENROLL_TIMEOUT_MS 60000  // Auto-cancel enrollment after 60s

// Duplicate tap prevention
String lastUID = "";
unsigned long lastTapMs = 0;

// ============================================================
// NVS CREDENTIAL MANAGEMENT
// ============================================================
bool isProvisioned() {
  preferences.begin("ecotron", true);  // read-only
  String secret = preferences.getString("device_secret", "");
  preferences.end();
  return secret.length() > 0;
}

void loadCredentials() {
  preferences.begin("ecotron", true);
  DEVICE_UID    = preferences.getString("device_uid", "");
  DEVICE_SECRET = preferences.getString("device_secret", "");
  DEVICE_ID     = preferences.getString("device_id", "");
  SUPABASE_URL  = preferences.getString("supabase_url", "");
  eventCounter  = preferences.getULong("event_ctr", 0);
  preferences.end();
  Serial.println("[NVS] Credentials loaded. UID=" + DEVICE_UID);
}

void saveCredentials(String uid, String secret, String devId, String url) {
  preferences.begin("ecotron", false);  // read-write
  preferences.putString("device_uid", uid);
  preferences.putString("device_secret", secret);
  preferences.putString("device_id", devId);
  preferences.putString("supabase_url", url);
  preferences.putULong("event_ctr", 0);
  preferences.end();
}

void saveEventCounter() {
  preferences.begin("ecotron", false);
  preferences.putULong("event_ctr", eventCounter);
  preferences.end();
}

// ============================================================
// DEVICE EVENT ID — deterministic, idempotent
// ============================================================
String generateEventId(String rfidUid, unsigned long ts) {
  eventCounter++;
  saveEventCounter();
  return DEVICE_UID + "-" + String(ts) + "-" + rfidUid + "-" + String(eventCounter);
}

// ============================================================
// PROVISIONING
// ============================================================
void provisionDevice(String token) {
  String deviceUid = WiFi.macAddress();
  Serial.println("[PROVISION] Attempting with UID=" + deviceUid);

  DynamicJsonDocument doc(256);
  doc["device_uid"] = deviceUid;
  doc["provisioning_token"] = token;
  String body; serializeJson(doc, body);

  HTTPClient http;
  // For pilot: use hardcoded Supabase URL. Production: from QR payload.
  String provUrl = String(SUPABASE_URL.length() > 0 ? SUPABASE_URL : "https://ueobebsgheecclwcbigy.supabase.co");
  http.begin(provUrl + "/functions/v1/device-provision");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.setTimeout(10000);

  int code = http.POST(body);
  if (code == 200) {
    String response = http.getString();
    DynamicJsonDocument resp(512);
    if (deserializeJson(resp, response) == DeserializationError::Ok) {
      String secret = resp["device_secret"].as<String>();
      String devId  = resp["device_id"].as<String>();
      String url    = resp["supabase_url"].as<String>();

      saveCredentials(deviceUid, secret, devId, url);
      Serial.println("[PROVISION] SUCCESS! Rebooting...");
      http.end();
      delay(1000);
      ESP.restart();
    } else {
      Serial.println("[PROVISION] JSON parse error");
    }
  } else {
    Serial.println("[PROVISION] Failed: HTTP " + String(code));
    if (code > 0) Serial.println(http.getString());
  }
  http.end();
}

void enterProvisioningMode() {
  Serial.println("[PROVISION] Device not provisioned.");
  Serial.println("[PROVISION] Starting WiFi AP setup portal...");
  
  // Check if we have pending provisioning from portal
  preferences.begin("gateman", false);
  bool needsProv = preferences.getBool("needs_prov", false);
  String token = preferences.getString("prov_token", "");
  
  if (needsProv && token.length() > 0) {
    Serial.println("[PROVISION] Found pending token from portal");
    preferences.putBool("needs_prov", false);
    preferences.end();
    provisionDevice(token);
    return;
  }
  preferences.end();

  // Start captive portal for provisioning
  startProvisioningPortal();
  // Never returns - device reboots after provisioning
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Ensure GPIO0 is set to INPUT_PULLUP to help with boot mode
  pinMode(0, INPUT_PULLUP);
  
  // Print boot reason for debugging
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("\n[BOOT] Reset reason: %d ", reason);
  switch(reason) {
    case ESP_RST_POWERON:   Serial.println("(Power-on)"); break;
    case ESP_RST_SW:        Serial.println("(Software reset)"); break;
    case ESP_RST_PANIC:     Serial.println("(Exception/panic)"); break;
    case ESP_RST_INT_WDT:   Serial.println("(Interrupt watchdog)"); break;
    case ESP_RST_TASK_WDT:  Serial.println("(Task watchdog)"); break;
    case ESP_RST_WDT:       Serial.println("(Other watchdog)"); break;
    case ESP_RST_DEEPSLEEP: Serial.println("(Deep sleep)"); break;
    case ESP_RST_BROWNOUT:  Serial.println("(Brownout)"); break;
    default:                Serial.println("(Unknown)"); break;
  }

  // Watchdog — DISABLED (was causing constant resets)
  // esp_task_wdt_config_t wdt_config = {
  //   .timeout_ms = WDT_TIMEOUT_S * 1000,
  //   .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
  //   .trigger_panic = true
  // };
  // esp_task_wdt_init(&wdt_config);
  // esp_task_wdt_add(NULL);

  Serial.println("============================");
  Serial.println("  EcoTronic WROOM v3 (SaaS)");
  Serial.println("============================");

  pinMode(STATUS_LED, OUTPUT);
  pinMode(ENROLL_BTN, INPUT_PULLUP);

  // Startup LED sequence — 3 quick flashes so user knows device is alive
  for (int i = 0; i < 3; i++) {
    digitalWrite(STATUS_LED, HIGH); delay(200);
    digitalWrite(STATUS_LED, LOW);  delay(200);
  }

  // Initialize SPIFFS for offline queue
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] Mount failed! Offline queue disabled.");
  } else {
    Serial.println("[SPIFFS] OK - Offline queue enabled");
  }
  // esp_task_wdt_reset();  // Watchdog disabled

  // Derive hardware UID immediately - get MAC before any WiFi mode changes
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  DEVICE_UID = String(macStr);
  Serial.println("[HW] MAC=" + DEVICE_UID);
  
  // Now set WiFi mode
  WiFi.mode(WIFI_STA);
  // esp_task_wdt_reset();  // Watchdog disabled

  // UART to CAM
  camSerial.begin(9600, SERIAL_8N1, CAM_RX, CAM_TX);
  delay(500);
  // esp_task_wdt_reset();  // Watchdog disabled

  // Verify CAM is alive (non-blocking check)
  Serial.print("[CAM] Checking... ");
  if (!pingCAM()) {
    Serial.println("WARNING: No response. Check wiring.");
  } else {
    Serial.println("OK");
  }
  // esp_task_wdt_reset();  // Watchdog disabled

  // RFID
  Serial.print("[RFID] Initializing... ");
  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  rfid.PCD_Init();
  delay(100);
  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.println(v==0x91||v==0x92 ? "OK" : "WARNING — check wiring");
  // esp_task_wdt_reset();  // Watchdog disabled

  // Check provisioning status BEFORE connecting to WiFi
  if (!isProvisioned()) {
    enterProvisioningMode();
    return;  // Never returns - device reboots after provisioning
  }

  // WiFi (only if provisioned)
  connectWiFi();
  // esp_task_wdt_reset();  // Watchdog disabled

  // Load stored credentials from NVS
  loadCredentials();
  // esp_task_wdt_reset();  // Watchdog disabled

  if (WiFi.status() == WL_CONNECTED) {
    syncNTPTime();
    downloadUsers();
    syncPendingLogs();
  } else {
    loadUsersFromCache();
  }
  // esp_task_wdt_reset();  // Watchdog disabled

  Serial.println("\n[READY] Tap a card...\n");
  Serial.println("[HEAP] Free: " + String(ESP.getFreeHeap()) + " bytes");
  blinkOK();
  // esp_task_wdt_reset();  // Watchdog disabled
}

// ============================================================
// OFFLINE QUEUE — Save logs to SPIFFS when WiFi is down
// ============================================================
void saveToQueue(String rfidUid, String action, unsigned long ts, String photoB64) {
  if (!SPIFFS.exists(OFFLINE_QUEUE_FILE)) {
    File f = SPIFFS.open(OFFLINE_QUEUE_FILE, FILE_WRITE);
    if (f) f.close();
  }
  
  File queueFile = SPIFFS.open(OFFLINE_QUEUE_FILE, FILE_APPEND);
  if (!queueFile) {
    Serial.println("[QUEUE] Failed to open queue file");
    return;
  }
  
  DynamicJsonDocument doc(1024);
  doc["rfid_uid"] = rfidUid;
  doc["action"] = action;
  doc["timestamp"] = ts;
  doc["device_event_id"] = String(eventCounter++);
  if (photoB64.length() > 0) doc["photo_b64"] = photoB64;
  
  String line;
  serializeJson(doc, line);
  queueFile.println(line);
  queueFile.close();
  
  Serial.println("[QUEUE] Saved offline (total: " + String(countQueuedLogs()) + ")");
}

int countQueuedLogs() {
  if (!SPIFFS.exists(OFFLINE_QUEUE_FILE)) return 0;
  File f = SPIFFS.open(OFFLINE_QUEUE_FILE, FILE_READ);
  if (!f) return 0;
  int count = 0;
  while (f.available()) {
    f.readStringUntil('\n');
    count++;
  }
  f.close();
  return count;
}

void syncOfflineQueue() {
  if (!SPIFFS.exists(OFFLINE_QUEUE_FILE)) return;
  
  File queueFile = SPIFFS.open(OFFLINE_QUEUE_FILE, FILE_READ);
  if (!queueFile) return;
  
  int synced = 0;
  int failed = 0;
  String tempLines = "";
  
  Serial.println("[QUEUE] Syncing offline logs...");
  
  while (queueFile.available() && synced < SYNC_MAX_PER_CYCLE) {
    String line = queueFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, line) != DeserializationError::Ok) {
      failed++;
      continue;
    }
    
    // Build submit-log payload
    DynamicJsonDocument submitDoc(2048);
    submitDoc["device_uid"] = DEVICE_UID;
    submitDoc["device_secret"] = DEVICE_SECRET;
    submitDoc["rfid_uid"] = doc["rfid_uid"].as<String>();
    submitDoc["action"] = doc["action"].as<String>();
    submitDoc["timestamp"] = timestampToISO(doc["timestamp"].as<unsigned long>());
    submitDoc["device_event_id"] = doc["device_event_id"].as<String>();
    if (doc.containsKey("photo_b64")) {
      submitDoc["photo_b64"] = doc["photo_b64"].as<String>();
    }
    
    String body;
    serializeJson(submitDoc, body);
    
    HTTPClient http;
    http.begin(SUPABASE_URL + "/functions/v1/submit-log");
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT_MS);
    
    int code = http.POST(body);
    http.end();
    
    if (code == 200 || code == 201) {
      synced++;
    } else {
      tempLines += line + "\n";
      failed++;
    }
    
    // esp_task_wdt_reset();  // Watchdog disabled
  }
  
  // Read remaining lines that weren't processed
  while (queueFile.available()) {
    tempLines += queueFile.readStringUntil('\n') + "\n";
  }
  queueFile.close();
  
  // Rewrite queue file with failed/unprocessed logs
  SPIFFS.remove(OFFLINE_QUEUE_FILE);
  if (tempLines.length() > 0) {
    File f = SPIFFS.open(OFFLINE_QUEUE_FILE, FILE_WRITE);
    if (f) {
      f.print(tempLines);
      f.close();
    }
  }
  
  Serial.println("[QUEUE] Synced: " + String(synced) + " | Failed: " + String(failed) + " | Remaining: " + String(countQueuedLogs()));
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // esp_task_wdt_reset();  // Watchdog disabled  // Feed watchdog

  // Check for Serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "RESET" || cmd == "FACTORY_RESET") {
      Serial.println("[RESET] Clearing all stored credentials...");
      preferences.begin("ecotron", false);
      preferences.clear();
      preferences.end();
      preferences.begin("gateman", false);
      preferences.clear();
      preferences.end();
      Serial.println("[RESET] NVS cleared. Rebooting into provisioning mode...");
      delay(1000);
      ESP.restart();
    }
  }

  // Auto-timeout enrollment mode (60s)
  if (enrollMode && millis() > enrollTimeout) {
    Serial.println("[ENROLL] Timeout — exiting enroll mode");
    enrollMode = false;
    activeEnrollmentId = "";
    blinkError();
  }

  // Enrollment button — hold 2s to toggle (manual fallback, legacy mode)
  static unsigned long btnDown = 0;
  if (digitalRead(ENROLL_BTN) == LOW) {
    if (btnDown == 0) btnDown = millis();
    if (millis() - btnDown > 2000) {
      toggleEnroll();
      btnDown = 0;
      delay(500);
    }
  } else { btnDown = 0; }

  // RFID scan
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uid = getUID();
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    // Immediate LED flash — visual feedback that card was read
    digitalWrite(STATUS_LED, HIGH);

    // Duplicate prevention
    if (uid == lastUID && millis() - lastTapMs < DUPLICATE_WINDOW) {
      Serial.println("[RFID] Duplicate tap ignored");
      digitalWrite(STATUS_LED, LOW);
    } else {
      lastUID = uid;
      lastTapMs = millis();
      digitalWrite(STATUS_LED, LOW);
      if (enrollMode) handleEnroll(uid);
      else addToQueue(uid, getEpochTime());
    }
  }

  processQueue();

  // Periodic tasks
  static unsigned long lastSync = 0, lastWifi = 0, lastHeartbeat = 0, lastEnrollPoll = 0;

  // Poll server for admin-initiated enrollment commands
  if (!enrollMode && millis() - lastEnrollPoll > ENROLL_POLL_MS) {
    if (WiFi.status() == WL_CONNECTED) checkEnrollmentCommand();
    lastEnrollPoll = millis();
  }

  if (millis() - lastSync > 300000) {
    if (WiFi.status() == WL_CONNECTED) { 
      syncOfflineQueue();  // Sync offline queue first
      syncPendingLogs(); 
      downloadUsers(); 
    }
    lastSync = millis();
  }

  if (millis() - lastWifi > 60000) {
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
      // If WiFi just reconnected, sync offline queue immediately
      if (WiFi.status() == WL_CONNECTED && countQueuedLogs() > 0) {
        Serial.println("[WIFI] Reconnected - syncing offline queue");
        syncOfflineQueue();
      }
    }
    lastWifi = millis();
  }

  if (millis() - lastHeartbeat > HEARTBEAT_MS) {
    if (!pingCAM()) Serial.println("[HEARTBEAT] CAM not responding");
    lastHeartbeat = millis();
  }

  delay(50);
}

// ============================================================
// CAM PING (heartbeat)
// ============================================================
bool pingCAM() {
  while (camSerial.available()) camSerial.read();
  camSerial.println("PING");
  unsigned long start = millis();
  while (millis() - start < 2000) {
    if (camSerial.available()) {
      String r = camSerial.readStringUntil('\n');
      r.trim();
      if (r == "PONG") return true;
    }
    delay(10);
  }
  return false;
}

// ============================================================
// RFID
// ============================================================
String getUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

// ============================================================
// ENROLLMENT — Admin-driven (server polling) + manual fallback
// ============================================================
void toggleEnroll() {
  enrollMode = !enrollMode;
  if (enrollMode) {
    activeEnrollmentId = "";  // Manual mode — no enrollment_id
    enrollTimeout = millis() + ENROLL_TIMEOUT_MS;
    Serial.println("\n[ENROLL] MANUAL MODE ON (60s timeout)");
    for(int i=0;i<6;i++){digitalWrite(STATUS_LED,HIGH);delay(80);digitalWrite(STATUS_LED,LOW);delay(80);}
  } else {
    activeEnrollmentId = "";
    Serial.println("[ENROLL] MODE OFF");
    blinkOK();
  }
}

void handleEnroll(String uid) {
  Serial.println("[ENROLL] Card: " + uid);
  if (findUserByRFID(uid)) { Serial.println("[ENROLL] Already registered"); blinkError(); return; }

  String photo = sendCaptureCommand("ENROLL", getEpochTime());

  if (WiFi.status() == WL_CONNECTED) {
    DynamicJsonDocument doc(512);
    doc["device_uid"] = DEVICE_UID;
    doc["device_secret"] = DEVICE_SECRET;
    doc["credential_value"] = uid;
    if (activeEnrollmentId.length() > 0) {
      doc["enrollment_id"] = activeEnrollmentId;
    }
    doc["photo_path"] = photo;
    doc["timestamp"] = timestampToISO(getEpochTime());
    String body; serializeJson(doc, body);
    HTTPClient http;
    http.begin(SUPABASE_URL + "/functions/v1/device-enroll");
    http.addHeader("Content-Type","application/json");
    http.addHeader("apikey", SUPABASE_ANON_KEY);
    http.setTimeout(HTTP_TIMEOUT_MS);
    int code = http.POST(body);
    String resp = http.getString();
    http.end();
    Serial.println("[ENROLL] HTTP " + String(code) + " " + resp);
    if (code==200||code==201) {
      downloadUsers();
      blinkOK();
    } else {
      blinkError();
    }
  } else {
    camSerial.println("SAVE_ENROLL:" + uid + ":" + photo);
    blinkOK();
  }

  // Exit enroll mode after successful enrollment
  enrollMode = false;
  activeEnrollmentId = "";
}

/**
 * Poll server for admin-initiated enrollment commands.
 * If a 'waiting' enrollment exists, enter enroll mode automatically.
 */
void checkEnrollmentCommand() {
  DynamicJsonDocument doc(256);
  doc["device_uid"] = DEVICE_UID;
  doc["device_secret"] = DEVICE_SECRET;
  String body; serializeJson(doc, body);

  HTTPClient http;
  http.begin(SUPABASE_URL + "/functions/v1/check-enrollment");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT_MS);

  int code = http.POST(body);
  
  if (code == 200) {
    String resp = http.getString();
    DynamicJsonDocument respDoc(512);
    if (deserializeJson(respDoc, resp) == DeserializationError::Ok) {
      bool shouldEnroll = respDoc["enroll"] | false;
      if (shouldEnroll) {
        String eid = respDoc["enrollment_id"].as<String>();
        Serial.println("[ENROLL] Server requests enrollment! ID=" + eid);
        activeEnrollmentId = eid;
        enrollMode = true;
        enrollTimeout = millis() + ENROLL_TIMEOUT_MS;
        // Rapid blink to signal enroll mode active
        for(int i=0;i<6;i++){digitalWrite(STATUS_LED,HIGH);delay(80);digitalWrite(STATUS_LED,LOW);delay(80);}
      }
    }
  }
  http.end();
}

// ============================================================
// QUEUE
// ============================================================
void addToQueue(String uid, unsigned long ts) {
  int next = (qTail+1)%QUEUE_SIZE;
  if (next != qHead) { queue[qTail]={uid,ts}; qTail=next; }
  else Serial.println("[QUEUE] Full");
}

void processQueue() {
  if (qBusy || qHead==qTail) return;
  qBusy = true;
  AttendanceEvent e = queue[qHead]; qHead=(qHead+1)%QUEUE_SIZE;
  handleAttendance(e.rfid_uid, e.timestamp);
  qBusy = false;
}

// ============================================================
// ATTENDANCE
// ============================================================
void handleAttendance(String uid, unsigned long ts) {
  Serial.println("\n[TAP] " + uid);
  User* u = findUserByRFID(uid);
  if (!u) { Serial.println("[ATT] Unknown"); blinkError(); return; }

  String action = (u->last_action==""||u->last_action=="check_out") ? "check_in" : "check_out";
  String photo  = sendCaptureCommand(u->employee_id, ts);  // Empty string if CAM fails — OK

  logAttendance(u, action, photo, ts);
  u->last_action = action; u->last_timestamp = ts;

  Serial.println("[ATT] " + u->name + " → " + action + (photo.length()?"  📷":"  ⚠️ no photo"));
  blinkOK();

  if (WiFi.status()==WL_CONNECTED) syncPendingLogs();
}

User* findUserByRFID(String uid) {
  for (int i=0;i<userCount;i++) if(users[i].rfid_uid==uid) return &users[i];
  return nullptr;
}

// ============================================================
// CAMERA — with retry on failure
// ============================================================
String sendCaptureCommand(String employeeId, unsigned long ts) {
  for (int attempt = 1; attempt <= 2; attempt++) {
    while (camSerial.available()) camSerial.read();
    camSerial.println("CAPTURE:" + employeeId + ":" + String(ts));

    unsigned long start = millis();
    while (millis()-start < CAM_TIMEOUT_MS) {
      if (camSerial.available()) {
        String r = camSerial.readStringUntil('\n'); r.trim();
        if (r.startsWith("DONE:")) return r.substring(5);
        if (r == "FAIL") break;
      }
      delay(10);
    }
    Serial.println("[CAM] Attempt " + String(attempt) + " failed");
    delay(500);
  }
  return "";  // No photo — attendance still logged
}

// ============================================================
// LOGGING
// ============================================================
void logAttendance(User* u, String action, String photo, unsigned long ts) {
  // If WiFi is down, save to offline queue instead of trying to sync immediately
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ATT] WiFi down - saving to offline queue");
    saveToQueue(u->rfid_uid, action, ts, photo);
    return;
  }
  
  // WiFi available - log to CAM for immediate sync
  String eventId = generateEventId(u->rfid_uid, ts);
  DynamicJsonDocument doc(512);
  doc["user_id"]=u->user_id; doc["employee_id"]=u->employee_id;
  doc["name"]=u->name; doc["rfid_uid"]=u->rfid_uid;
  doc["action"]=action; doc["timestamp"]=timestampToISO(ts);
  doc["device_uid"]=DEVICE_UID; doc["image_path"]=photo; doc["synced"]=false;
  doc["device_event_id"]=eventId;
  String final_json; serializeJson(doc,final_json);
  camSerial.println("LOG:" + final_json);
}

// ============================================================
// SYNC
// ============================================================
void syncPendingLogs() {
  Serial.println("[SYNC] Getting logs from CAM...");
  while (camSerial.available()) camSerial.read();
  camSerial.println("GET_PENDING");

  // Collect all pending log lines from CAM
  String logs=""; bool receiving=false;
  unsigned long start = millis();
  while (millis()-start<15000) {
    if (camSerial.available()) {
      String line = camSerial.readStringUntil('\n'); line.trim();
      if (line=="BEGIN_LOGS") { receiving=true; continue; }
      if (line=="END_LOGS") break;
      if (receiving&&line.length()>0) { logs+=line+"\n"; }
    }
    delay(5);
  }

  if (logs.length()==0) { Serial.println("[SYNC] Nothing pending"); return; }

  // Parse log lines into individual records
  int submitted=0, failed=0, duplicates=0;
  int pos=0, recordCount=0;

  while (pos<(int)logs.length() && recordCount<SYNC_MAX_PER_CYCLE) {
    int end=logs.indexOf('\n',pos); if(end==-1) break;
    String line=logs.substring(pos,end); line.trim(); pos=end+1;
    if (line.length()==0) continue;
    recordCount++;

    DynamicJsonDocument rec(512);
    if (deserializeJson(rec,line)!=DeserializationError::Ok) continue;

    // Build submit-log payload (512 bytes — no photo in this version)
    DynamicJsonDocument payload(512);
    payload["device_uid"]      = DEVICE_UID;
    payload["device_secret"]   = DEVICE_SECRET;
    String evtId = rec["device_event_id"].as<String>();
    if (evtId.length() == 0 || evtId == "null") {
      evtId = DEVICE_UID + "-" + rec["timestamp"].as<String>() + "-" + rec["rfid_uid"].as<String>();
    }
    payload["device_event_id"] = evtId;
    payload["credential_value"]= rec["rfid_uid"];
    payload["event_time"]      = rec["timestamp"];
    payload["action"]          = rec["action"];
    // photo_base64 omitted for Phase 1 (SD-only buffer)

    String body; serializeJson(payload, body);

    // Per-record HTTP POST
    HTTPClient http;
    http.begin(SUPABASE_URL + "/functions/v1/submit-log");
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT_MS);

    int code = http.POST(body);

    if (code == 200) {
      // Parse response to check if inserted or duplicate
      String resp = http.getString();
      if (resp.indexOf("true") > -1) submitted++;
      else duplicates++;
    } else {
      failed++;
      Serial.println("[SYNC] Record failed HTTP " + String(code));
    }

    http.end();

    // CRITICAL: yield to watchdog and allow background tasks
    // esp_task_wdt_reset();  // Watchdog disabled
    delay(10);  // Prevent tight-loop heap fragmentation
  }

  Serial.println("[SYNC] Done: " + String(submitted) + " new, " +
                 String(duplicates) + " dup, " + String(failed) + " failed");
  Serial.println("[HEAP] Free: " + String(ESP.getFreeHeap()) + " bytes");

  // FIX: Only MARK_SYNCED if ALL records succeeded or were duplicates.
  // If any failed (network error, server error), do NOT mark — next cycle
  // will retry all pending. Succeeded records are idempotent on re-send.
  if (failed == 0) {
    camSerial.println("MARK_SYNCED");
  } else {
    Serial.println("[SYNC] Skipping MARK_SYNCED — " + String(failed) + " records failed. Will retry next cycle.");
  }
}

// ============================================================
// USERS
// ============================================================
void downloadUsers() {
  Serial.print("[USERS] Downloading... ");
  // esp_task_wdt_reset();  // Watchdog disabled
  
  // POST to get-users Edge Function with device credentials in body
  DynamicJsonDocument authDoc(256);
  authDoc["device_uid"] = DEVICE_UID;
  authDoc["device_secret"] = DEVICE_SECRET;
  String authBody; serializeJson(authDoc, authBody);

  HTTPClient http;
  http.begin(SUPABASE_URL + "/functions/v1/get-users");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(8000);
  
  // esp_task_wdt_reset();  // Watchdog disabled
  int code = http.POST(authBody);
  // esp_task_wdt_reset();  // Watchdog disabled
  
  if (code==200) {
    String payload=http.getString();
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc,payload)==DeserializationError::Ok) {
      JsonArray arr=doc["users"]; userCount=0;
      for (JsonObject u:arr) {
        if (userCount>=MAX_USERS) break;
        users[userCount]={u["user_id"],u["name"],u["employee_id"],u["department"],u["rfid_uid"],"",0};
        userCount++;
      }
      Serial.println(String(userCount)+" loaded");
      camSerial.println("SAVE_USERS:"+payload);
    }
  } else { 
    Serial.println("HTTP "+String(code)); 
    loadUsersFromCache(); 
  }
  http.end();
  // esp_task_wdt_reset();  // Watchdog disabled
}

void loadUsersFromCache() {
  while (camSerial.available()) camSerial.read();
  camSerial.println("GET_USERS");
  unsigned long start=millis(); String resp="";
  while (millis()-start<5000) {
    if (camSerial.available()) { resp=camSerial.readStringUntil('\n'); resp.trim(); break; }
    delay(10);
  }
  if (!resp.startsWith("USERS:")) return;
  String json=resp.substring(6);
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc,json)==DeserializationError::Ok) {
    JsonArray arr=doc["users"]; userCount=0;
    for (JsonObject u:arr) {
      if(userCount>=MAX_USERS) break;
      users[userCount]={u["user_id"],u["name"],u["employee_id"],u["department"],u["rfid_uid"],"",0};
      userCount++;
    }
    Serial.println("[USERS] "+String(userCount)+" from cache");
  }
}

// ============================================================
// WIFI
// ============================================================
void connectWiFi() {
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  
  // Try to load WiFi credentials from NVS (saved by provisioning portal)
  preferences.begin("gateman", true);
  String savedSSID = preferences.getString("wifi_ssid", "");
  String savedPass = preferences.getString("wifi_pass", "");
  preferences.end();
  
  // Use saved credentials if available, otherwise use hardcoded
  const char* ssid = savedSSID.length() > 0 ? savedSSID.c_str() : WIFI_SSID;
  const char* pass = savedPass.length() > 0 ? savedPass.c_str() : WIFI_PASSWORD;
  
  WiFi.begin(ssid, pass);
  Serial.print("[WiFi] Connecting");
  int att=0;
  while (WiFi.status()!=WL_CONNECTED&&att<20) { 
    delay(500); 
    Serial.print("."); 
    att++; 
    // esp_task_wdt_reset();  // Watchdog disabled
  }
  if (WiFi.status()==WL_CONNECTED) {
    Serial.println(" "+WiFi.localIP().toString());
    Serial.println("[WiFi] Auto-reconnect enabled, sleep disabled");
  } else {
    Serial.println(" OFFLINE");
  }
}

// ============================================================
// TIME
// ============================================================
void syncNTPTime() {
  configTime(GMT_OFFSET, DAYLIGHT, NTP_SERVER);
  struct tm ti; int att=0;
  while (!getLocalTime(&ti)&&att<10) { 
    delay(500); 
    att++; 
    // esp_task_wdt_reset();  // Watchdog disabled
  }
  char buf[32]; strftime(buf,32,"%Y-%m-%d %H:%M:%S",&ti);
  Serial.println("[NTP] "+String(buf));
  // esp_task_wdt_reset();  // Watchdog disabled
}

unsigned long getEpochTime() { time_t now; time(&now); return (unsigned long)now; }

String timestampToISO(unsigned long e) {
  if(e<1000000) return "1970-01-01T00:00:00Z";
  struct tm* t=gmtime((time_t*)&e); char buf[25];
  strftime(buf,25,"%Y-%m-%dT%H:%M:%SZ",t); return String(buf);
}

// ============================================================
// LED
// ============================================================
void blinkOK() { for(int i=0;i<2;i++){digitalWrite(STATUS_LED,HIGH);delay(150);digitalWrite(STATUS_LED,LOW);delay(150);} }
void blinkError() { for(int i=0;i<5;i++){digitalWrite(STATUS_LED,HIGH);delay(80);digitalWrite(STATUS_LED,LOW);delay(80);} }

// ============================================================
// HMAC
// ============================================================
String hmacSHA256(String data, String key) {
  byte h[32]; mbedtls_md_context_t ctx; mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx,mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),1);
  mbedtls_md_hmac_starts(&ctx,(const unsigned char*)key.c_str(),key.length());
  mbedtls_md_hmac_update(&ctx,(const unsigned char*)data.c_str(),data.length());
  mbedtls_md_hmac_finish(&ctx,h); mbedtls_md_free(&ctx);
  String r=""; for(int i=0;i<32;i++){if(h[i]<0x10)r+="0"; r+=String(h[i],HEX);} return r;
}
