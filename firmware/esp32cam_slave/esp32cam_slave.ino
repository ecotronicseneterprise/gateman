/*
 * EcoTronic — ESP32-CAM (Slave) FINAL FIRMWARE — FIELD PILOT SAFE
 *
 * Final fixes Applied:
 *   ✅ Time validity guard
 *   ✅ entry.close() in all directory loops
 *   ✅ UART sleep guard
 * Earlier Improvements:
 *   ✅ PING/PONG heartbeat response
 *   ✅ Photo cleanup — delete photos older than 30 days after sync
 *   ✅ ACK on every command
 *   ✅ Light sleep between commands
 *   ✅ Storage health auto-runs after every sync
 *
 * WIRING:
 *   GPIO12 ← WROOM GPIO17 (receive commands)
 *   GPIO13 → WROOM GPIO16 (send responses)
 *   Shared GND — CRITICAL
 *   5V from dedicated 2A supply
 */

#include <esp_camera.h>
#include <SD_MMC.h>
#include <FS.h>
#include <time.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>

#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM   0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM     5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22

#define WROOM_RX  12
#define WROOM_TX  13
#define LED_PIN   33

#define STORAGE_MAX_PCT 90.0
#define KEEP_SYNCED_DAYS 30
#define PHOTO_KEEP_DAYS  30
#define WDT_TIMEOUT_S   60
#define IDLE_SLEEP_MS  10000

HardwareSerial wroomSerial(1);
unsigned long lastCmdMs = 0;

void handleDeleteUser(String cmd);
void handleUpdateUser(String cmd);
void handleGetHealth();

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200); delay(1000);

  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);

  Serial.println("\n============================");
  Serial.println("  EcoTronic CAM v2 (Pilot)");
  Serial.println("============================");

  pinMode(LED_PIN, OUTPUT);

  // Startup LED sequence — 3 quick flashes so user knows CAM is alive
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH); delay(200);
    digitalWrite(LED_PIN, LOW);  delay(200);
  }

  wroomSerial.begin(9600, SERIAL_8N1, WROOM_RX, WROOM_TX);

  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("[SD] FAILED"); return;
  }

  ensureDirectories();
  checkStorage();

  camera_config_t cfg;
  cfg.ledc_channel=LEDC_CHANNEL_0; cfg.ledc_timer=LEDC_TIMER_0;
  cfg.pin_d0=Y2_GPIO_NUM; cfg.pin_d1=Y3_GPIO_NUM; cfg.pin_d2=Y4_GPIO_NUM;
  cfg.pin_d3=Y5_GPIO_NUM; cfg.pin_d4=Y6_GPIO_NUM; cfg.pin_d5=Y7_GPIO_NUM;
  cfg.pin_d6=Y8_GPIO_NUM; cfg.pin_d7=Y9_GPIO_NUM;
  cfg.pin_xclk=XCLK_GPIO_NUM; cfg.pin_pclk=PCLK_GPIO_NUM;
  cfg.pin_vsync=VSYNC_GPIO_NUM; cfg.pin_href=HREF_GPIO_NUM;
  cfg.pin_sscb_sda=SIOD_GPIO_NUM; cfg.pin_sscb_scl=SIOC_GPIO_NUM;
  cfg.pin_pwdn=PWDN_GPIO_NUM; cfg.pin_reset=RESET_GPIO_NUM;
  cfg.xclk_freq_hz=20000000; cfg.pixel_format=PIXFORMAT_JPEG;
  cfg.frame_size=FRAMESIZE_QQVGA; cfg.jpeg_quality=20; cfg.fb_count=1;

  if (esp_camera_init(&cfg)==ESP_OK) {
    sensor_t* s=esp_camera_sensor_get();
    if(s){s->set_special_effect(s,2);s->set_quality(s,20);}
    Serial.println("[CAM] OK — 160x120 grayscale");
  } else {
    Serial.println("[CAM] FAILED");
  }

  Serial.println("[READY] Waiting for commands...\n");
  lastCmdMs = millis();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  esp_task_wdt_reset();

  if (wroomSerial.available()) {
    String cmd = wroomSerial.readStringUntil('\n');
    cmd.trim();
    lastCmdMs = millis();
    if (cmd.length()==0) return;

    if (cmd=="PING")                       wroomSerial.println("PONG");
    else if (cmd.startsWith("CAPTURE:"))   handleCapture(cmd);
    else if (cmd.startsWith("LOG:"))       handleLog(cmd);
    else if (cmd=="GET_PENDING")           handleGetPending();
    else if (cmd=="MARK_SYNCED")           { handleMarkSynced(); cleanOldPhotos(); checkStorage(); }
    else if (cmd.startsWith("SAVE_USERS:"))handleSaveUsers(cmd);
    else if (cmd=="GET_USERS")             handleGetUsers();
    else if (cmd.startsWith("SAVE_ENROLL:"))handleSaveEnroll(cmd);
    else if(cmd.startsWith("DELETE_USER:")) handleDeleteUser(cmd);
    else if(cmd.startsWith("UPDATE_USER:")) handleUpdateUser(cmd);
    else if(cmd == "GET_HEALTH") handleGetHealth();
  }

  // Sleep disabled - keep CAM always responsive for UART commands
  // if (millis()-lastCmdMs > IDLE_SLEEP_MS) {
  //   if (!wroomSerial.available()) {
  //     Serial.println("[SLEEP]");
  //     Serial.flush();
  //     esp_sleep_enable_uart_wakeup(1);
  //     esp_light_sleep_start();
  //     lastCmdMs = millis();
  //     Serial.println("[WAKE]");
  //   }
  // }

  delay(10);
}

// ============================================================
// CAPTURE
// ============================================================
void handleCapture(String cmd) {
  int c1=cmd.indexOf(':'), c2=cmd.indexOf(':',c1+1);
  if (c1==-1||c2==-1) { wroomSerial.println("FAIL"); return; }

  String empId = cmd.substring(c1+1,c2);
  unsigned long ts = cmd.substring(c2+1).toInt();

  digitalWrite(LED_PIN,HIGH); delay(100);

  camera_fb_t* fb=esp_camera_fb_get();
  if (!fb) { wroomSerial.println("FAIL"); digitalWrite(LED_PIN,LOW); return; }

  String fname="/photos/"+empId+"_"+tsString(ts)+".jpg";
  File f=SD_MMC.open(fname,FILE_WRITE);
  if (f) { f.write(fb->buf,fb->len); f.close(); wroomSerial.println("DONE:"+fname); }
  else   wroomSerial.println("FAIL");

  esp_camera_fb_return(fb);
  digitalWrite(LED_PIN,LOW);
}

// ============================================================
// LOG
// ============================================================
void handleLog(String cmd) {
  String json=cmd.substring(4);
  if (json.length()==0) return;
  File f=SD_MMC.open("/pending/"+dateStr()+".jsonl",FILE_APPEND);
  if(f){f.println(json);f.close();}
}

// ============================================================
// GET_PENDING
// ============================================================
void handleGetPending() {
  wroomSerial.println("BEGIN_LOGS");
  File dir=SD_MMC.open("/pending");
  if (dir&&dir.isDirectory()) {
    File entry=dir.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        String name=String(entry.name());
        if (name.endsWith(".jsonl")) {
          File f=SD_MMC.open("/pending/"+name,FILE_READ);
          if(f){
            while(f.available()){
              String line=f.readStringUntil('\n'); line.trim();
              if(line.length()>0){wroomSerial.println(line);delay(20);}
            }
            f.close();
          }
        }
      }
      entry.close();
      entry=dir.openNextFile();
    }
    dir.close();
  }
  wroomSerial.println("END_LOGS");
}

// ============================================================
// MARK_SYNCED
// ============================================================
void handleMarkSynced() {
  File dir=SD_MMC.open("/pending");
  if(!dir||!dir.isDirectory()) return;
  File entry=dir.openNextFile();
  while(entry){
    if(!entry.isDirectory()){
      String name=String(entry.name());
      if(name.endsWith(".jsonl")){
        SD_MMC.rename("/pending/"+name,"/synced/"+name);
      }
    }
    entry.close();
    entry=dir.openNextFile();
  }
  dir.close();
}

// ============================================================
// STORAGE HEALTH
// ============================================================
void checkStorage() {
  uint64_t total=SD_MMC.totalBytes(), used=SD_MMC.usedBytes();
  if (!total) return;
  float pct=(float)used/(float)total*100.0;
  if (pct>STORAGE_MAX_PCT) deleteOldSynced();
}

void deleteOldSynced(){

  const float TARGET_PCT = 85.0;   // stop deleting when below this
  uint64_t total = SD_MMC.totalBytes();
  if (!total) return;

  // Collect files
  String files[100];
  time_t fileTimes[100];
  int count = 0;

  File dir = SD_MMC.open("/synced");
  if(!dir || !dir.isDirectory()) return;

  File entry = dir.openNextFile();
  while(entry && count < 100){
    if(!entry.isDirectory()){
      String name = String(entry.name());
      if(name.endsWith(".jsonl")){
        files[count] = name;
        fileTimes[count] = entry.getLastWrite();
        count++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  if(count == 0) return;

  // Sort files by oldest first (simple bubble sort — small N)
  for(int i=0;i<count-1;i++){
    for(int j=i+1;j<count;j++){
      if(fileTimes[j] < fileTimes[i]){
        time_t ttmp = fileTimes[i];
        fileTimes[i] = fileTimes[j];
        fileTimes[j] = ttmp;

        String stmp = files[i];
        files[i] = files[j];
        files[j] = stmp;
      }
    }
  }

  // Delete oldest until below threshold
  for(int i=0;i<count;i++){

    uint64_t used = SD_MMC.usedBytes();
    float pct = (float)used / (float)total * 100.0;
    if(pct <= TARGET_PCT) break;

    Serial.println("[STORAGE] Deleting old synced: " + files[i]);
    SD_MMC.remove("/synced/" + files[i]);
  }
}

// ============================================================
// PHOTO CLEANUP (TIME GUARDED)
// ============================================================
void cleanOldPhotos(){
  time_t now; time(&now);

  if (now < 1700000000) {
    Serial.println("[TIME] Not set — skipping cleanup");
    return;
  }

  unsigned long cutoff = (unsigned long)now - (PHOTO_KEEP_DAYS*86400UL);

  File dir=SD_MMC.open("/photos");
  if(!dir||!dir.isDirectory()) return;

  File entry=dir.openNextFile();
  while(entry){
    if(!entry.isDirectory()){
      String name=String(entry.name());
      int us=name.lastIndexOf('_');
      int ds=name.lastIndexOf('_',us-1);
      if(ds>0&&us>ds){
        String datePart=name.substring(ds+1,us);
        String timePart=name.substring(us+1,us+7);
        if(datePart.length()==8&&timePart.length()==6){
          struct tm t={};
          t.tm_year=datePart.substring(0,4).toInt()-1900;
          t.tm_mon =datePart.substring(4,6).toInt()-1;
          t.tm_mday=datePart.substring(6,8).toInt();
          t.tm_hour=timePart.substring(0,2).toInt();
          t.tm_min =timePart.substring(2,4).toInt();
          t.tm_sec =timePart.substring(4,6).toInt();
          unsigned long fileTs=(unsigned long)mktime(&t);
          if(fileTs<cutoff){
            SD_MMC.remove("/photos/"+name);
          }
        }
      }
    }
    entry.close();
    entry=dir.openNextFile();
  }
  dir.close();
}

// ============================================================
// SAVE_USERS
// ============================================================
void handleSaveUsers(String cmd) {

  int idx = cmd.indexOf(':');
  if (idx == -1) {
    wroomSerial.println("USERS_FAIL");
    return;
  }

  String json = cmd.substring(idx + 1);
  json.trim();

  if (json.length() == 0) {
    wroomSerial.println("USERS_FAIL");
    return;
  }

  File f = SD_MMC.open("/users.json", FILE_WRITE);
  if (!f) {
    wroomSerial.println("USERS_FAIL");
    return;
  }

  f.print(json);
  f.close();

  wroomSerial.println("USERS_SAVED");
}

// ============================================================
// GET_USERS
// ============================================================
void handleGetUsers() {

  if (!SD_MMC.exists("/users.json")) {
    wroomSerial.println("USERS:{}");
    return;
  }

  File f = SD_MMC.open("/users.json", FILE_READ);
  if (!f) {
    wroomSerial.println("USERS:{}");
    return;
  }

  String content = f.readString();
  f.close();

  content.trim();
  if (content.length() == 0) content = "{}";

  wroomSerial.println("USERS:" + content);
}

// ============================================================
// SAVE_ENROLL
// ============================================================
void handleSaveEnroll(String cmd) {

  int idx = cmd.indexOf(':');
  if (idx == -1) {
    wroomSerial.println("ENROLL_FAIL");
    return;
  }

  String json = cmd.substring(idx + 1);
  json.trim();

  if (json.length() == 0) {
    wroomSerial.println("ENROLL_FAIL");
    return;
  }

  File f = SD_MMC.open("/enrollments.jsonl", FILE_APPEND);
  if (!f) {
    wroomSerial.println("ENROLL_FAIL");
    return;
  }

  f.println(json);
  f.close();

  wroomSerial.println("ENROLL_SAVED");
}

void handleDeleteUser(String cmd){

  int idx = cmd.indexOf(':');
  if(idx == -1){
    wroomSerial.println("DELETE_FAIL");
    return;
  }

  String userId = cmd.substring(idx + 1);
  userId.trim();

  if(!SD_MMC.exists("/users.json")){
    wroomSerial.println("DELETE_FAIL");
    return;
  }

  File f = SD_MMC.open("/users.json", FILE_READ);
  if(!f){
    wroomSerial.println("DELETE_FAIL");
    return;
  }

  String content = f.readString();
  f.close();

  int start = content.indexOf("\"" + userId + "\"");
  if(start == -1){
    wroomSerial.println("DELETE_FAIL");
    return;
  }

  int commaBefore = content.lastIndexOf(',', start);
  int braceAfter = content.indexOf("}", start);

  if(braceAfter == -1){
    wroomSerial.println("DELETE_FAIL");
    return;
  }

  int end = content.indexOf(",", braceAfter);
  if(end == -1) end = braceAfter;

  if(commaBefore != -1)
    content.remove(commaBefore, end - commaBefore);
  else
    content.remove(start, end - start);

  File wf = SD_MMC.open("/users.json", FILE_WRITE);
  if(!wf){
    wroomSerial.println("DELETE_FAIL");
    return;
  }

  wf.print(content);
  wf.close();

  wroomSerial.println("USER_DELETED");
}

void handleUpdateUser(String cmd){

  int first = cmd.indexOf(':');
  int second = cmd.indexOf(':', first + 1);

  if(first == -1 || second == -1){
    wroomSerial.println("UPDATE_FAIL");
    return;
  }

  String userId = cmd.substring(first + 1, second);
  String newData = cmd.substring(second + 1);

  userId.trim();
  newData.trim();

  if(!SD_MMC.exists("/users.json")){
    wroomSerial.println("UPDATE_FAIL");
    return;
  }

  File f = SD_MMC.open("/users.json", FILE_READ);
  if(!f){
    wroomSerial.println("UPDATE_FAIL");
    return;
  }

  String content = f.readString();
  f.close();

  int start = content.indexOf("\"" + userId + "\"");
  if(start == -1){
    wroomSerial.println("UPDATE_FAIL");
    return;
  }

  int braceStart = content.indexOf("{", start);
  int braceEnd = content.indexOf("}", braceStart);

  if(braceStart == -1 || braceEnd == -1){
    wroomSerial.println("UPDATE_FAIL");
    return;
  }

  content = content.substring(0, braceStart) + newData + content.substring(braceEnd + 1);

  File wf = SD_MMC.open("/users.json", FILE_WRITE);
  if(!wf){
    wroomSerial.println("UPDATE_FAIL");
    return;
  }

  wf.print(content);
  wf.close();

  wroomSerial.println("USER_UPDATED");
}

void handleGetHealth(){

  uint64_t total = SD_MMC.totalBytes();
  uint64_t used  = SD_MMC.usedBytes();

  float pct = total ? (float)used / total * 100.0 : 0;

  String health = "{";
  health += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  health += "\"uptime\":" + String(millis()/1000) + ",";
  health += "\"sd_used_pct\":" + String(pct,1);
  health += "}";

  wroomSerial.println("HEALTH:" + health);
}


// ============================================================
// UTILITIES
// ============================================================
void ensureDirectories(){
  if(!SD_MMC.exists("/pending")) SD_MMC.mkdir("/pending");
  if(!SD_MMC.exists("/synced"))  SD_MMC.mkdir("/synced");
  if(!SD_MMC.exists("/photos"))  SD_MMC.mkdir("/photos");
}

String tsString(unsigned long e){
  if(e<1000000) return "00000000_000000";
  struct tm* t=gmtime((time_t*)&e);
  char buf[20];
  strftime(buf,20,"%Y%m%d_%H%M%S",t);
  return String(buf);
}

String dateStr(){
  time_t now; time(&now);
  if(now < 1700000000) return "1970-01-01";
  struct tm* t=localtime(&now);
  char buf[12];
  strftime(buf,12,"%Y-%m-%d",t);
  return String(buf);
}