
# **SSH Deployment Guide - Gateman Dashboard**

---

## **Server Details:**
- **IP:** `46.225.186.103`
- **User:** `deploy` (or `root`)
- **Web Root:** `/var/www/ecotronics/` (or `/var/www/html/`)

---

## **Deployment Steps:**

### **1. Deploy Dashboard via SCP:**

```bash
# From your local machine (Windows PowerShell or Git Bash)
cd "c:\Ecotronics Enterprise\gateman"

# Upload dashboard files
scp -r dashboard/* deploy@46.225.186.103:/var/www/ecotronics/

# Or if using root:
scp -r dashboard/* root@46.225.186.103:/var/www/html/
```

**Enter password when prompted.**

---

### **2. Alternative: Using SFTP (FileZilla/WinSCP):**

**FileZilla/WinSCP Settings:**
- **Host:** `46.225.186.103`
- **Protocol:** SFTP
- **Port:** 22
- **Username:** `deploy` (or `root`)
- **Password:** [your password]

**Upload:**
- Local: [c:\Ecotronics Enterprise\gateman\dashboard\](cci:9://file:///Ecotronics%20Enterprise/gateman/dashboard:0:0-0:0)
- Remote: `/var/www/ecotronics/` (or `/var/www/html/`)

---

### **3. Verify Deployment:**

**SSH into server:**
```bash
ssh deploy@46.225.186.103
```

**Check files:**
```bash
ls -la /var/www/ecotronics/
# Should see: index.html, public/
```

**Check web server is running:**
```bash
# For Nginx:
sudo systemctl status nginx

# For Apache:
sudo systemctl status apache2

# For Node.js (if using http-server):
pm2 list
```

---

### **4. Test Dashboard:**

**Open browser:**
```
http://46.225.186.103
```

**Should see:**
- Gateman login page
- Logo displaying correctly (blue/cyan gradient gate icon)
- Sign In / Sign Up tabs

---

## **If Web Server Not Running:**

### **Option A: Using Nginx (Recommended):**

```bash
# SSH into server
ssh deploy@46.225.186.103

# Install Nginx (if not installed)
sudo apt update
sudo apt install nginx -y

# Configure Nginx
sudo nano /etc/nginx/sites-available/default

# Add this configuration:
server {
    listen 80;
    server_name 46.225.186.103;
    root /var/www/ecotronics;
    index index.html;
    
    location / {
        try_files $uri $uri/ /index.html;
    }
}

# Save and exit (Ctrl+X, Y, Enter)

# Restart Nginx
sudo systemctl restart nginx
```

---

### **Option B: Using http-server (Node.js):**

```bash
# SSH into server
ssh deploy@46.225.186.103

# Install Node.js (if not installed)
curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
sudo apt install -y nodejs

# Install http-server globally
sudo npm install -g http-server

# Run http-server
cd /var/www/ecotronics
http-server -p 80

# Or use PM2 to keep it running:
sudo npm install -g pm2
pm2 start "http-server -p 80" --name gateman-dashboard
pm2 save
pm2 startup
```

---

## **Quick Deploy Command (One-liner):**

```bash
cd "c:\Ecotronics Enterprise\gateman" && scp -r dashboard/* deploy@46.225.186.103:/var/www/ecotronics/
```

---

**Deploy the dashboard and share the URL to verify!** 🚀\