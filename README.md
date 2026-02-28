# EcoTronic Attendance System — Deployment Guide
C:\gateman>npx http-server c:\gateman -p 8080 --cors -c-1
Need to install the following packages:
http-server@14.1.1




## Project Structure
```
ecotronic/
├── backend/
│   ├── server.js       ← Node.js API + SQLite
│   ├── package.json
│   └── .env.example    ← Copy to .env and fill in
├── dashboard/
│   └── index.html      ← Served by backend automatically
└── firmware/
    ├── wroom_brain/    ← Upload to ESP32-WROOM
    └── esp32cam_slave/ ← Upload to ESP32-CAM
```

---

## Backend Setup (Local)

```bash
cd backend
cp .env.example .env
# Edit .env with your values

npm install
npm start
# Dashboard: http://localhost:3000
# Login: admin@ecotronic.com / changeme123
```

---

## Backend Deploy (Railway — Free)

1. Push project to GitHub
2. railway.app → New Project → Deploy from GitHub
3. Set environment variables from .env
4. Copy the public URL → paste into firmware as API_ENDPOINT

---

## Firmware Upload Order

1. Flash esp32cam_slave first (via FTDI)
2. Flash wroom_brain second (hold BOOT button)
3. Edit wroom_brain config at top:
   - WIFI_SSID / WIFI_PASSWORD
   - API_ENDPOINT (your Railway URL)
   - DEVICE_SECRET (must match backend .env)

---

## Enrollment Workflow

1. Dashboard → Add Employee → fill name, ID, department
2. Press ENROLL button on WROOM for 2 seconds (LED flashes rapidly)
3. Tap RFID card
4. Dashboard → Enrollment → card appears as pending
5. Assign card to employee
6. Done — card now works for attendance

---

## Add Test User (No Backend)

In wroom_brain.ino, add inside setup() after WiFi block:
```cpp
users[0].user_id = "USR001";
users[0].name = "Test User";
users[0].employee_id = "EMP001";
users[0].rfid_uid = "778D7506";  // Your card's UID
users[0].last_action = "";
userCount = 1;
```

---

## API Endpoints

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| POST | /api/auth/login | None | Admin login |
| GET | /api/users/:deviceId | Device | Get user list |
| POST | /api/attendance/bulk | Device | Sync logs |
| POST | /api/enroll | Device | Register card |
| GET | /api/dashboard/stats | Admin | Stats |
| GET | /api/dashboard/feed | Admin | Live feed |
| GET | /api/events | Admin | SSE stream |
| GET | /api/export/csv | Admin | Export |
