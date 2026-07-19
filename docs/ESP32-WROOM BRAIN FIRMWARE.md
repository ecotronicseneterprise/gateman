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

// ============================================================
// CONFIGURATION
// ============================================================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* API_ENDPOINT  = "http://your-server.com/api";  // No trailing slash
const char* DEVICE_ID     = "DEVICE_001";
const char* DEVICE_SECRET = "your_device_secret_here";
const char* NTP_SERVER    = "pool.ntp.org";
const long  GMT_OFFSET    = 3600;
const int   DAYLIGHT      = 0;

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
#define WDT_TIMEOUT_S     30     // reboot if stuck 30s
#define HEARTBEAT_MS      60000

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

// Duplicate tap prevention
String lastUID = "";
unsigned long lastTapMs = 0;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Watchdog — auto-reboot if code hangs
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);

  Serial.println("\n============================");
  Serial.println("  EcoTronic WROOM v2 (Final)");
  Serial.println("============================");

  pinMode(STATUS_LED, OUTPUT);
  pinMode(ENROLL_BTN, INPUT_PULLUP);
  digitalWrite(STATUS_LED, LOW);

  // UART to CAM
  camSerial.begin(9600, SERIAL_8N1, CAM_RX, CAM_TX);
  delay(500);

  // Verify CAM is alive
  if (!pingCAM()) {
    Serial.println("[CAM] WARNING: No response from ESP32-CAM. Check wiring and power.");
  }

  // RFID
  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  rfid.PCD_Init();
  delay(100);
  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.println(v==0x91||v==0x92 ? "[RFID] OK" : "[RFID] WARNING — check wiring");

  // WiFi
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    syncNTPTime();
    downloadUsers();
    syncPendingLogs();
  } else {
    loadUsersFromCache();
  }

  Serial.println("\n[READY] Tap a card...\n");
  blinkOK();
  esp_task_wdt_reset();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  esp_task_wdt_reset();  // Feed watchdog

  // Enrollment button — hold 2s to toggle
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

    // Duplicate prevention
    if (uid == lastUID && millis() - lastTapMs < DUPLICATE_WINDOW) {
      Serial.println("[RFID] Duplicate tap ignored");
    } else {
      lastUID = uid;
      lastTapMs = millis();
      if (enrollMode) handleEnroll(uid);
      else addToQueue(uid, getEpochTime());
    }
  }

  processQueue();

  // Periodic tasks
  static unsigned long lastSync = 0, lastWifi = 0, lastHeartbeat = 0;

  if (millis() - lastSync > 300000) {
    if (WiFi.status() == WL_CONNECTED) { syncPendingLogs(); downloadUsers(); }
    lastSync = millis();
  }

  if (millis() - lastWifi > 60000) {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
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
// ENROLLMENT
// ============================================================
void toggleEnroll() {
  enrollMode = !enrollMode;
  Serial.println(enrollMode ? "\n[ENROLL] MODE ON" : "[ENROLL] MODE OFF");
  if (enrollMode) { for(int i=0;i<6;i++){digitalWrite(STATUS_LED,HIGH);delay(80);digitalWrite(STATUS_LED,LOW);delay(80);} }
  else blinkOK();
}

void handleEnroll(String uid) {
  Serial.println("[ENROLL] Card: " + uid);
  if (findUserByRFID(uid)) { Serial.println("[ENROLL] Already registered"); blinkError(); return; }

  String photo = sendCaptureCommand("ENROLL", getEpochTime());

  if (WiFi.status() == WL_CONNECTED) {
    DynamicJsonDocument doc(256);
    doc["rfid_uid"] = uid; doc["device_id"] = DEVICE_ID; doc["photo_path"] = photo;
    doc["timestamp"] = timestampToISO(getEpochTime());
    String body; serializeJson(doc, body);
    HTTPClient http;
    http.begin(String(API_ENDPOINT) + "/enroll");
    http.addHeader("Content-Type","application/json");
    http.addHeader("Authorization","Bearer " + String(DEVICE_SECRET));
    int code = http.POST(body); http.end();
    Serial.println("[ENROLL] HTTP " + String(code));
    if (code==200||code==201) downloadUsers();
  } else {
    camSerial.println("SAVE_ENROLL:" + uid + ":" + photo);
  }
  blinkOK();
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
  DynamicJsonDocument doc(512);
  doc["user_id"]=u->user_id; doc["employee_id"]=u->employee_id;
  doc["name"]=u->name; doc["rfid_uid"]=u->rfid_uid;
  doc["action"]=action; doc["timestamp"]=timestampToISO(ts);
  doc["device_id"]=DEVICE_ID; doc["image_path"]=photo; doc["synced"]=false;
  String raw; serializeJson(doc,raw);
  doc["signature"] = hmacSHA256(raw, String(DEVICE_SECRET));
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

  DynamicJsonDocument payload(8192);
  JsonArray recs = payload.createNestedArray("records");
  int pos=0;
  while (pos<logs.length()) {
    int end=logs.indexOf('\n',pos); if(end==-1) break;
    String line=logs.substring(pos,end); line.trim(); pos=end+1;
    if (line.length()==0) continue;
    DynamicJsonDocument rec(512);
    if (deserializeJson(rec,line)==DeserializationError::Ok) recs.add(rec);
  }

  if (recs.size()==0) return;
  String body; serializeJson(payload,body);

  HTTPClient http;
  http.begin(String(API_ENDPOINT)+"/attendance/bulk");
  http.addHeader("Content-Type","application/json");
  http.addHeader("Authorization","Bearer "+String(DEVICE_SECRET));
  http.setTimeout(10000);
  int code=http.POST(body); http.end();

  if (code==200||code==201) {
    Serial.println("[SYNC] ✓ "+String(recs.size())+" records");
    camSerial.println("MARK_SYNCED");
  } else {
    Serial.println("[SYNC] Failed HTTP "+String(code));
  }
}

// ============================================================
// USERS
// ============================================================
void downloadUsers() {
  HTTPClient http;
  http.begin(String(API_ENDPOINT)+"/users/"+DEVICE_ID);
  http.addHeader("Authorization","Bearer "+String(DEVICE_SECRET));
  http.setTimeout(8000);
  int code=http.GET();
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
      Serial.println("[USERS] "+String(userCount)+" loaded");
      camSerial.println("SAVE_USERS:"+payload);
    }
  } else { Serial.println("[USERS] HTTP "+String(code)); loadUsersFromCache(); }
  http.end();
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
  WiFi.setSleep(false); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");
  int att=0;
  while (WiFi.status()!=WL_CONNECTED&&att<20) { delay(500); Serial.print("."); att++; }
  if (WiFi.status()==WL_CONNECTED) Serial.println(" "+WiFi.localIP().toString());
  else Serial.println(" OFFLINE");
}

// ============================================================
// TIME
// ============================================================
void syncNTPTime() {
  configTime(GMT_OFFSET, DAYLIGHT, NTP_SERVER);
  struct tm ti; int att=0;
  while (!getLocalTime(&ti)&&att<10) { delay(500); att++; }
  char buf[32]; strftime(buf,32,"%Y-%m-%d %H:%M:%S",&ti);
  Serial.println("[NTP] "+String(buf));
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
