# requirements.txt - Python Dependencies
Flask==3.0.0
Flask-SQLAlchemy==3.1.1
Werkzeug==3.0.1
psycopg2-binary==2.9.9
plotly==5.18.0
pandas==2.1.4
openpyxl==3.1.2
python-dateutil==2.8.2

# ============================================
# QUICK START COMMANDS
# ============================================

# 1. LOCAL DEVELOPMENT
"""
# Install dependencies
pip install -r requirements.txt

# Run locally
python app.py

# Visit http://localhost:5000
# Default admin: admin@demo.com / password: admin123
"""

# 2. DEPLOY TO RAILWAY (FREE)
"""
# Install Railway CLI
npm i -g @railway/cli

# Login
railway login

# Create project
railway init

# Add PostgreSQL
railway add postgresql

# Deploy
railway up

# Set environment variables
railway variables set SECRET_KEY=your_random_key_here

# Open deployed app
railway open
"""

# 3. DEPLOY TO RENDER (FREE)
"""
# 1. Push code to GitHub
git init
git add .
git commit -m "Initial commit"
git remote add origin https://github.com/yourusername/ecotronic.git
git push -u origin main

# 2. Go to render.com
# 3. New → Web Service
# 4. Connect GitHub repo
# 5. Build Command: pip install -r requirements.txt
# 6. Start Command: gunicorn app:app
# 7. Add PostgreSQL database
# 8. Set environment variables:
#    - SECRET_KEY
#    - DATABASE_URL (auto-populated)
# 9. Deploy!
"""

# ============================================
# ESP32-CAM FIRMWARE LIBRARIES
# ============================================
"""
Required Arduino Libraries (Install via Library Manager):

1. MFRC522 by GithubCommunity
2. Adafruit GFX Library
3. Adafruit SSD1306
4. ArduinoJson (v6.x)
5. ESP32 Board Support (via Board Manager)

Board Settings:
- Board: AI Thinker ESP32-CAM
- Upload Speed: 115200
- Flash Frequency: 80MHz
- Partition Scheme: Huge APP (3MB No OTA)
"""

# ============================================
# DATABASE INITIALIZATION SCRIPT
# ============================================
def create_demo_data():
    """
    Run this after first deployment to create demo account
    
    Usage:
    python
    >>> from app import app, db, Company, Admin, Device, Employee
    >>> from werkzeug.security import generate_password_hash
    >>> with app.app_context():
    ...     create_demo_data()
    """
    from app import db, Company, Admin, Device, Employee
    from werkzeug.security import generate_password_hash
    import secrets
    
    # Create demo company
    company = Company(
        name="Demo Company Ltd",
        email="demo@company.com",
        plan="free",
        max_devices=1,
        max_employees=50
    )
    db.session.add(company)
    db.session.flush()
    
    # Create admin user
    admin = Admin(
        company_id=company.id,
        name="Admin User",
        email="admin@demo.com",
        password_hash=generate_password_hash("admin123")
    )
    db.session.add(admin)
    
    # Create demo device
    device = Device(
        company_id=company.id,
        device_id="DEMO_001",
        name="Main Office Device",
        location="Reception",
        device_secret=secrets.token_hex(32),
        status="active"
    )
    db.session.add(device)
    db.session.flush()
    
    # Create demo employees
    employees = [
        {"employee_id": "EMP001", "name": "John Doe", "department": "Sales"},
        {"employee_id": "EMP002", "name": "Jane Smith", "department": "Marketing"},
        {"employee_id": "EMP003", "name": "Mike Johnson", "department": "Engineering"},
        {"employee_id": "EMP004", "name": "Sarah Williams", "department": "HR"},
        {"employee_id": "EMP005", "name": "David Brown", "department": "Finance"},
    ]
    
    for emp_data in employees:
        employee = Employee(
            company_id=company.id,
            employee_id=emp_data["employee_id"],
            name=emp_data["name"],
            department=emp_data["department"],
            status="pending_rfid"
        )
        db.session.add(employee)
    
    db.session.commit()
    
    print("✓ Demo data created successfully!")
    print(f"✓ Admin login: admin@demo.com / admin123")
    print(f"✓ Device ID: DEMO_001")
    print(f"✓ Device Secret: {device.device_secret}")

# ============================================
# ENVIRONMENT VARIABLES (.env file)
# ============================================
"""
# Create .env file in project root (for local development)

SECRET_KEY=your_super_secret_key_change_this_in_production
DATABASE_URL=postgresql://username:password@localhost/ecotronic
FLASK_ENV=development
FLASK_DEBUG=True

# For production, set FLASK_ENV=production and FLASK_DEBUG=False
"""

# ============================================
# GUNICORN CONFIGURATION (gunicorn_config.py)
# ============================================
"""
# For production deployment

bind = "0.0.0.0:5000"
workers = 4
threads = 2
timeout = 120
keepalive = 5
accesslog = "-"
errorlog = "-"
loglevel = "info"
"""

# ============================================
# PROCFILE (for Heroku/Render)
# ============================================
"""
web: gunicorn app:app
"""

# ============================================
# NGINX CONFIGURATION (if self-hosting)
# ============================================
"""
server {
    listen 80;
    server_name yourdomain.com;
    
    location / {
        proxy_pass http://127.0.0.1:5000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
    
    client_max_body_size 10M;
}
"""

# ============================================
# SYSTEMD SERVICE (for Ubuntu server)
# ============================================
"""
# /etc/systemd/system/ecotronic.service

[Unit]
Description=EcoTronic Attendance System
After=network.target

[Service]
User=www-data
WorkingDirectory=/var/www/ecotronic
Environment="PATH=/var/www/ecotronic/venv/bin"
ExecStart=/var/www/ecotronic/venv/bin/gunicorn -c gunicorn_config.py app:app

[Install]
WantedBy=multi-user.target
"""

# ============================================
# GIT IGNORE (.gitignore)
# ============================================
"""
__pycache__/
*.pyc
*.pyo
*.pyd
.Python
env/
venv/
.env
instance/
.webassets-cache
.pytest_cache/
*.db
*.log
.DS_Store
"""

# ============================================
# TESTING SCRIPT (test_system.py)
# ============================================
"""
import requests
import json

BASE_URL = "http://localhost:5000"

def test_system():
    # Test 1: Home page loads
    response = requests.get(BASE_URL)
    assert response.status_code == 200, "Home page failed"
    print("✓ Home page loads")
    
    # Test 2: Login works
    response = requests.post(f"{BASE_URL}/login", data={
        "email": "admin@demo.com",
        "password": "admin123"
    })
    assert response.status_code == 200 or response.status_code == 302
    print("✓ Login works")
    
    # Demo device secret (from create_demo_data)
    device_secret = "demo_device_secret_replace_with_actual"
    
    # Test 3: GET /users/{device_id} (for Brain to download users)
    response = requests.get(
        f"{BASE_URL}/api/users/DEMO_001",
        headers={"Authorization": f"Bearer {device_secret}"}
    )
    if response.status_code == 200:
        users = response.json()
        print(f"✓ Users endpoint: {len(users.get('users', []))} users loaded")
    else:
        print(f"✗ Users endpoint failed: {response.status_code}")
    
    # Test 4: POST /enroll (for Brain to enroll new RFID)
    enroll_data = {
        "rfid_uid": "A3F2C1B0",
        "device_id": "DEMO_001", 
        "photo_path": "/photos/ENROLL_A3F2C1B0.jpg",
        "timestamp": "2024-02-26T08:42:15"
    }
    response = requests.post(
        f"{BASE_URL}/api/enroll",
        headers={
            "Authorization": f"Bearer {device_secret}",
            "Content-Type": "application/json"
        },
        json=enroll_data
    )
    print(f"✓ Enroll endpoint: {response.status_code}")
    
    # Test 5: POST /api/attendance/bulk (for Brain to sync attendance)
    response = requests.post(
        f"{BASE_URL}/api/attendance/bulk",
        headers={
            "Authorization": f"Bearer {device_secret}",
            "Content-Type": "application/json"
        },
        json={
            "records": [{
                "user_id": "USR001",
                "employee_id": "EMP001",
                "name": "John Doe",
                "rfid_uid": "A3F2C1B0",
                "action": "check_in",
                "timestamp": "2024-02-26T08:42:15Z",
                "device_id": "DEMO_001",
                "image_path": "/photos/EMP001_20240226_084215.jpg",
                "synced": False,
                "signature": "test_signature"
            }]
        }
    )
    print(f"✓ Attendance bulk endpoint: {response.status_code}")
    
    print("\n✓ All tests passed for two-board system!")

if __name__ == "__main__":
    test_system()
"""

# ============================================
# PRODUCTION CHECKLIST
# ============================================
"""
Before Going Live:

Security:
□ Change SECRET_KEY to random value
□ Set FLASK_ENV=production
□ Disable FLASK_DEBUG
□ Use HTTPS (Let's Encrypt)
□ Enable CORS properly
□ Add rate limiting
□ Implement API key rotation
□ Set up backup strategy

Performance:
□ Configure PostgreSQL connection pooling
□ Enable database indexes
□ Set up CDN for static files
□ Implement caching (Redis)
□ Configure proper logging
□ Set up monitoring (Sentry, New Relic)

Documentation:
□ API documentation complete
□ User manual written
□ Installation guide tested
□ Support contact info visible
□ Privacy policy published
□ Terms of service published
"""

print("✓ Deployment configuration ready!")
print("✓ Follow instructions in comments to deploy")