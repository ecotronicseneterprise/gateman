# HETZNER VPS RECOVERY & REBUILD GUIDE
**Owner:** Clifford Nwanna (Ecotronics Enterprise)
**Server:** ubuntu-4gb-nbg1-4 (CAX11, Nuremberg, Ubuntu 24.04 LTS aarch64)
**Last rebuild:** 3 June 2026
**Default IP:** 46.225.186.103 (note: changes on every full rebuild)

This document covers EVERYTHING needed to rebuild this VPS from scratch, or recover from compromise. Read end-to-end before starting. Updated based on real lessons from the 3 June 2026 rebuild.

---

## 0. WHY THIS DOCUMENT EXISTS

The previous server was compromised (suspected: weak/exposed root password + permissive SSH config + CRON_SECRET still at dev default). The rebuild took ~4 hours of pain instead of ~30 minutes because of preventable mistakes:

1. SSH key wasn't injected at rebuild time → locked out
2. Hetzner web console silently mangled pasted commands (corrupted `@`, `_`, `|`, `>`, `#` characters)
3. Password reset issued multiple times — only the LAST one is valid
4. `PermitRootLogin prohibit-password` blocks password auth for root, which kept being misinterpreted as "wrong password"
5. Compromised secrets (.env values) were re-pasted into chat — these MUST be rotated

This guide eliminates all of those.

---

## 1. ARCHITECTURE OVERVIEW

```
┌─────────────────────────────────────────────────────────┐
│  Hetzner CAX11 (Ubuntu 24.04, ARM64, 4GB RAM, 40GB)     │
│  IP: 46.225.186.103                                     │
│                                                          │
│  Public access (UFW allows 22, 80, 443 only)            │
│       │                                                  │
│       ▼                                                  │
│  ┌──────────────────────────────────────────────┐       │
│  │ Caddy (ports 80/443, auto TLS via LE)        │       │
│  │  - upjobs.co            → localhost:3000     │       │
│  │  - liveportfolio.site   → localhost:3001     │       │
│  │  - 46.225.186.103       → /var/www/gateman   │       │
│  │  - 46.225.186.103.sslip.io → localhost:8001  │       │
│  └──────────────────────────────────────────────┘       │
│                                                          │
│  PM2 (runs as deploy user, auto-restarts on boot)       │
│   - upjobs (next.js, port 3000)                         │
│   - liveportfolio (next.js, port 3001)                  │
│                                                          │
│  Docker (jarvis stack)                                  │
│   - jarvis-backend (FastAPI, 127.0.0.1:8001)            │
│   - jarvis-postgres (127.0.0.1:5433)                    │
│   - jarvis-redis (127.0.0.1:6380)                       │
│                                                          │
│  Static (served directly by Caddy)                      │
│   - /var/www/gateman/dashboard                          │
└─────────────────────────────────────────────────────────┘
```

---

## 2. FOLDER STRUCTURE (CANONICAL)

```
/home/deploy/
├── .ssh/
│   ├── authorized_keys        # Clifford's PC public key (Ed25519)
│   ├── config                 # SSH config — github.com uses github_deploy
│   ├── github_deploy          # Private deploy key (server → GitHub)
│   └── github_deploy.pub      # Public deploy key (added to GitHub)
└── apps/
    ├── cv360/                 # UpJobs.co source (Next.js, runs port 3000)
    ├── liveportfolio/         # liveportfolio.site source (Next.js, port 3001)
    ├── gateman/               # Gateman backend + dashboard build source
    └── Jarvis/                # Personal AI assistant (Docker stack)

/var/www/
├── gateman/dashboard/         # Built static gateman dashboard (served by Caddy)
├── jarvis/                    # Static jarvis assets
└── html/                      # Default

/etc/caddy/Caddyfile           # Reverse proxy config
/var/log/caddy/                # Caddy logs (upjobs-access.log, liveportfolio-access.log)
```

---

## 3. PRE-REBUILD CHECKLIST (do BEFORE clicking rebuild)

- [ ] **Local SSH key exists**: `Get-Content ~/.ssh/id_ed25519.pub` (PowerShell). If file missing, generate with `ssh-keygen -t ed25519 -C "clifford@hetzner"` and accept defaults.
- [ ] **Add local key to Hetzner Console → Security → SSH Keys** (so it can be injected at rebuild).
- [ ] **Add local key to GitHub** at https://github.com/settings/keys (used as fallback to curl into server).
- [ ] **All env files backed up locally** (UpJobs `.env`, liveportfolio `.env.local`, gateman config, jarvis docker-compose env, CRON_SECRET).
- [ ] **All codebases pushed to GitHub** (`git status` shows clean in each repo).
- [ ] **Database backups exported** (Supabase: download SQL dump from dashboard; jarvis-postgres: `docker exec jarvis-postgres-1 pg_dumpall -U postgres > backup.sql`).
- [ ] **DNS records noted** for upjobs.co and liveportfolio.site (A records to point to NEW IP after rebuild).
- [ ] **All production secrets rotated** if rebuild is due to suspected compromise (Paystack, OpenAI, Supabase service role, Resend, CRON_SECRET, etc.).

---

## 4. THE REBUILD ITSELF

### 4.1 In Hetzner Console
1. Server page → **Rebuild** tab (NOT Rescue)
2. Image: **Ubuntu 24.04**
3. Confirm with server name
4. ⚠️ **Hetzner's CAX11 rebuild dialog does NOT show an SSH key checkbox**. The key must be added afterwards via web console (see 4.3).

### 4.2 Note the new IP
After rebuild, the IP may change. Confirm it on the server overview page.

### 4.3 First login (get into the server)
- Click **Reset Root Password** → note the password shown (e.g. `iwfLbvTvixRh`)
- Click the **`>_` Console** button (top right of server page) — opens the browser-based KVM/serial console
- Login: `root` / [the password from reset]
- ⚠️ **Every time you click Reset Root Password, the previous one becomes invalid.** Only the most recently shown password works.
- ⚠️ **Click GUI-Mode at the bottom right** of the web console for better paste behaviour, then close & reopen the console.

---

## 5. WEB CONSOLE GOTCHAS (CRITICAL — read carefully)

The Hetzner browser console silently corrupts certain characters during paste/fast typing. Symptoms seen on 3 June 2026:

| Intended char | Gets typed as | Result |
|---|---|---|
| `@` | `2` | Email/keys broken |
| `_` (underscore) | `-` (hyphen) | `sshd_config` → `sshd-config` (file not found) |
| `\|` (pipe) | `\` or `1` | Commands fail with "No such file" |
| `>` (redirect) | nothing | `echo foo > file` → just runs echo |
| `#` | nothing | Wouldn't paste at start of nano lines |
| Long lines | random duplicated `a`s, missing chars | `authorized_keys` ended up containing `clifford2hetzner` instead of `clifford@hetzner` |

### Rules for the web console:
1. **Toggle GUI-Mode on first** (bottom right of console).
2. **Type slowly, one char at a time**, when special chars are involved.
3. **Avoid pipes, redirects, and complex one-liners.** Break them up.
4. **Prefer `nano` for any edit** — open file, edit visually, save.
5. **Prefer `curl -o` over `echo … >> file`** — downloads come in clean.
6. **Verify everything after writing** with `cat <file>` and `wc -c <file>`.
7. **Once you have SSH working from PowerShell, never use the web console again** unless you've locked yourself out.

---

## 6. SECURING THE SERVER (do this IMMEDIATELY after first root login)

All commands run from the web console as root.

### 6.1 Get your local SSH key onto the server (the GitHub trick)
```bash
mkdir -p /root/.ssh && chmod 700 /root/.ssh
curl -o /root/.ssh/authorized_keys https://github.com/cliffordnwanna.keys
chmod 600 /root/.ssh/authorized_keys
cat /root/.ssh/authorized_keys
```
The `cat` should print your ed25519 key cleanly. (This is why we add the local key to GitHub before rebuild — it bypasses every console paste bug.)

### 6.2 Temporarily allow root password login (so we can SSH in and stop using the broken web console)
Edit `/etc/ssh/sshd_config` with nano:
```bash
nano /etc/ssh/sshd_config
```
- Find `#PermitRootLogin prohibit-password` → change to `PermitRootLogin yes` (remove the `#`)
- Save: Ctrl+O, Enter, Ctrl+X
- Restart SSH: `systemctl restart ssh`

### 6.3 Set a known root password (because the Hetzner-reset one is weird to type)
```bash
passwd
# Type a memorable strong password twice
```

### 6.4 Test SSH from PowerShell (LOCAL PC)
```powershell
ssh-keygen -R 46.225.186.103   # clear old host fingerprint
ssh root@46.225.186.103
```
Type `yes` to confirm fingerprint. If your key works → no password prompt. If not → enter the password from 6.3.

### 6.5 Create the deploy user (run on server as root, now via SSH)
```bash
adduser deploy
# Set a strong password (different from root)
# Press Enter through the GECOS prompts (Full Name, Phone, etc.) or fill in
usermod -aG sudo deploy

mkdir -p /home/deploy/.ssh
curl -o /home/deploy/.ssh/authorized_keys https://github.com/cliffordnwanna.keys
chown -R deploy:deploy /home/deploy/.ssh
chmod 700 /home/deploy/.ssh
chmod 600 /home/deploy/.ssh/authorized_keys
```

### 6.6 Test deploy login from a NEW PowerShell window
```powershell
ssh -i ~/.ssh/id_ed25519 deploy@46.225.186.103
```
Should log in immediately with no password.

### 6.7 Lock down SSH (only after 6.6 succeeds — keep the root session open as safety!)
As `deploy`:
```bash
sudo nano /etc/ssh/sshd_config
```
Set / confirm:
```
PermitRootLogin no
PasswordAuthentication no
PubkeyAuthentication yes
```
Then:
```bash
sudo systemctl restart ssh
```
Verify from a third PowerShell window:
```powershell
ssh -i ~/.ssh/id_ed25519 deploy@46.225.186.103   # should work
ssh root@46.225.186.103                           # should reject
```

### 6.8 Firewall + fail2ban
```bash
sudo apt update
sudo apt install -y ufw fail2ban
sudo ufw allow OpenSSH
sudo ufw allow 80
sudo ufw allow 443
sudo ufw --force enable
sudo systemctl enable --now fail2ban
sudo ufw status
sudo systemctl status fail2ban --no-pager
```

---

## 7. INSTALL CORE TOOLS

```bash
sudo apt install -y curl git build-essential python3-pip python3-venv

# Node.js 20 LTS
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs

# PM2 globally
sudo npm install -g pm2
pm2 startup
# Copy/paste the sudo env PATH=… command it prints, run it

# Caddy
sudo apt install -y debian-keyring debian-archive-keyring apt-transport-https
curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/gpg.key | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt | sudo tee /etc/apt/sources.list.d/caddy-stable.list
sudo apt update && sudo apt install -y caddy

# Docker (for Jarvis)
sudo apt install -y docker.io docker-compose-v2
sudo usermod -aG docker deploy
# Log out and back in for group to take effect

# Verify
node -v && npm -v && pm2 -v && caddy version && python3 --version && docker --version
```

---

## 8. GIT ACCESS — SSH DEPLOY KEY (no tokens, no leaks)

Tokens in commands leak to bash history and chat logs. SSH deploy key is cleaner.

```bash
ssh-keygen -t ed25519 -C "deploy@hetzner-server" -f ~/.ssh/github_deploy -N ""

cat >> ~/.ssh/config << 'EOF'
Host github.com
  HostName github.com
  User git
  IdentityFile ~/.ssh/github_deploy
EOF
chmod 600 ~/.ssh/config

cat ~/.ssh/github_deploy.pub
```
Copy the printed line → https://github.com/settings/keys → New SSH key → paste → save.

Test:
```bash
ssh -T git@github.com
# Should say: Hi cliffordnwanna! You've successfully authenticated...
```

---

## 9. CLONE & DEPLOY APPS

```bash
mkdir -p ~/apps && cd ~/apps

git clone git@github.com:ecotronicseneterprise/cv360.git
git clone git@github.com:ecotronicseneterprise/liveportfolio.git
git clone git@github.com:ecotronicseneterprise/gateman.git
git clone git@github.com:cliffordnwanna/Jarvis.git
```

For each app, restore the `.env` (or `.env.local`) file from your local backup. NEVER commit env files. After restoring, rotate any secret that's older than ~90 days as a hygiene rule.

### 9.1 UpJobs.co (cv360 → next.js)
```bash
cd ~/apps/cv360
# Restore .env from local backup (transfer via scp or paste into nano)
npm install
npm run build
pm2 start ecosystem.config.js --env production
pm2 save
```
Update `.env`:
- `CRON_SECRET` → generate new with `openssl rand -hex 32` (do NOT reuse the dev default)
- All other secrets from your local backup

### 9.2 Liveportfolio.site
```bash
cd ~/apps/liveportfolio
# Restore .env.local from local backup (NEW rotated secrets)
npm install
npm run build
# If using ecosystem.config.js:
pm2 start ecosystem.config.js --env production
# Or directly:
pm2 start npm --name liveportfolio -- start -- --port 3001
pm2 save
```

### 9.3 Gateman (static dashboard)
```bash
cd ~/apps/gateman
# Follow gateman's deployment guide (usually: npm run build, then copy build output)
sudo mkdir -p /var/www/gateman/dashboard
sudo cp -r dist/* /var/www/gateman/dashboard/  # adjust path per gateman's build output
sudo chown -R deploy:deploy /var/www/gateman
```

### 9.4 Jarvis (Docker)
```bash
cd ~/apps/Jarvis
# Restore docker-compose env files
docker compose up -d
docker ps   # confirm jarvis-backend, jarvis-postgres, jarvis-redis are healthy
```

---

## 10. CADDY CONFIG (CANONICAL Caddyfile)

`/etc/caddy/Caddyfile` — restore exactly this (from the last working version on 3 June 2026):

```caddy
# Gateman Dashboard (served by IP — no domain yet)
46.225.186.103 {
    root * /var/www/gateman/dashboard
    file_server
    try_files {path} /index.html
}

upjobs.co, www.upjobs.co {
    reverse_proxy localhost:3000 {
        transport http {
            read_timeout 90s
            write_timeout 90s
            dial_timeout 10s
        }
        header_up Host {host}
        header_up X-Real-IP {remote_host}
        header_up X-Forwarded-For {remote_host}
        header_up X-Forwarded-Proto {scheme}
    }
    request_body { max_size 10MB }
    encode gzip zstd
    header {
        -Server
        X-Content-Type-Options "nosniff"
        X-Frame-Options "SAMEORIGIN"
        Referrer-Policy "strict-origin-when-cross-origin"
        X-XSS-Protection "1; mode=block"
        Cache-Control "public, max-age=31536000, immutable" {
            path /_next/static/*
        }
    }
    log {
        output file /var/log/caddy/upjobs-access.log
        format json
    }
}

# Jarvis (served by sslip.io subdomain for free TLS without DNS)
46.225.186.103.sslip.io {
    reverse_proxy localhost:8001
}

liveportfolio.site, www.liveportfolio.site {
    reverse_proxy localhost:3001 {
        transport http {
            read_timeout 90s
            write_timeout 90s
            dial_timeout 10s
        }
        header_up Host {host}
        header_up X-Real-IP {remote_host}
        header_up X-Forwarded-For {remote_host}
        header_up X-Forwarded-Proto {scheme}
    }
    request_body { max_size 5MB }
    encode gzip zstd
    header {
        -Server
        X-Content-Type-Options "nosniff"
        X-Frame-Options "SAMEORIGIN"
        Referrer-Policy "strict-origin-when-cross-origin"
    }
    log {
        output file /var/log/caddy/liveportfolio-access.log
        format json
    }
}
```

Then:
```bash
sudo mkdir -p /var/log/caddy
sudo systemctl restart caddy
sudo systemctl status caddy --no-pager
```

DNS A records (at your registrar):
- `upjobs.co` → server IP
- `www.upjobs.co` → server IP
- `liveportfolio.site` → server IP
- `www.liveportfolio.site` → server IP

Caddy will auto-issue Let's Encrypt certs on first request once DNS resolves.

---

## 11. THE SSH COMMAND YOU'LL ACTUALLY USE EVERY DAY

```powershell
ssh -i ~/.ssh/id_ed25519 deploy@46.225.186.103
```

### Save it as a shortcut

Add this to `~/.ssh/config` on your **PC** (create the file if missing):
```
Host vps
  HostName 46.225.186.103
  User deploy
  IdentityFile ~/.ssh/id_ed25519
```

Now you can just type:
```powershell
ssh vps
```
to connect. Replace `46.225.186.103` if the IP changes.

For VS Code Remote-SSH:
1. Install the "Remote - SSH" extension
2. Press `Ctrl+Shift+P` → "Remote-SSH: Connect to Host..." → choose `vps`
3. VS Code opens a window on the server. Open `/home/deploy/apps/<project>` to start editing live.

---

## 12. SECRET ROTATION CHECKLIST (do every 90 days OR after any suspected leak)

- [ ] **Paystack**: dashboard.paystack.com → Settings → Developers → regenerate secret key. Update `PAYSTACK_SECRET_KEY` in liveportfolio `.env.local` + upjobs `.env`.
- [ ] **OpenAI**: platform.openai.com/api-keys → revoke old, create new. Update `OPENAI_API_KEY` everywhere.
- [ ] **Supabase**: project Settings → API → "Reset" service role JWT. Update `SUPABASE_SERVICE_ROLE_KEY` everywhere it's used (liveportfolio, upjobs, any other consumer). Anon key can stay.
- [ ] **Resend**: resend.com/api-keys → revoke + new. Update `RESEND_API_KEY`.
- [ ] **CRON_SECRET**: `openssl rand -hex 32` → update both `.env` and any crontab/cron job that calls the API.
- [ ] **Server passwords**: `sudo passwd deploy` and (if root login ever re-enabled) `sudo passwd root`.
- [ ] **GitHub deploy key**: regenerate `~/.ssh/github_deploy` and re-add to github.com → settings → keys; remove the old one.
- [ ] **Hetzner SSH key**: same — generate new local key, add to Hetzner Console → Security → SSH Keys, replace old one.

After rotation, restart the affected apps:
```bash
pm2 restart upjobs
pm2 restart liveportfolio
docker compose -f ~/apps/Jarvis/docker-compose.yml restart
```

---

## 13. BACKUPS (set this up — don't skip it again)

Quick daily backup of env files + Caddy config + crontab to your home directory + a remote:

```bash
mkdir -p ~/backups
cat > ~/backups/daily_backup.sh << 'EOF'
#!/usr/bin/env bash
set -e
DATE=$(date +%Y-%m-%d)
DEST=~/backups/$DATE
mkdir -p $DEST
cp /etc/caddy/Caddyfile $DEST/
cp ~/apps/cv360/.env $DEST/upjobs.env 2>/dev/null || true
cp ~/apps/liveportfolio/.env.local $DEST/liveportfolio.env.local 2>/dev/null || true
crontab -l > $DEST/crontab.txt 2>/dev/null || true
# Keep only last 14 days
find ~/backups -maxdepth 1 -type d -mtime +14 -exec rm -rf {} +
EOF
chmod +x ~/backups/daily_backup.sh
(crontab -l 2>/dev/null; echo "0 3 * * * /home/deploy/backups/daily_backup.sh") | crontab -
```

Also enable Hetzner's automated server backups (Hetzner Console → server → Backups → Enable; ~20% surcharge but worth it).

---

## 14. APP-SPECIFIC DEPLOYMENT PROMPT (use this in each repo)

Copy the section below into each project's README or a `DEPLOYMENT.md`. When you ask an AI assistant to help redeploy, paste this VPS guide + the prompt:

> You have access to my Hetzner VPS deployment guide (above). The VPS uses Ubuntu 24.04 ARM64, Caddy reverse proxy, PM2 for Node apps, Docker for stateful services. The deploy user is `deploy`, all apps live under `/home/deploy/apps/<repo-name>`, all env files are restored from local backup (never committed). Caddy config lives at `/etc/caddy/Caddyfile`.
>
> Before suggesting any deployment commands:
> 1. Read this project's package.json / Dockerfile / build scripts to identify the exact build + run commands and required ports.
> 2. Confirm the project's directory on the server (likely `/home/deploy/apps/<this-repo-name>`).
> 3. Confirm whether the project is currently registered with PM2 (`pm2 list`) or Docker (`docker ps`), to decide between fresh-deploy and rolling-update.
> 4. Identify all environment variables expected, cross-checked against `.env.example` or `process.env.*` references in source. Do NOT invent variables.
> 5. Confirm the corresponding entry in `/etc/caddy/Caddyfile` exists and points to the right port. If not, propose the exact Caddy block to add.
> 6. Output a step-by-step plan with: pre-checks, build steps, restart steps, post-checks (curl/health-check). Do NOT batch-execute destructive commands without confirmation.
>
> The most common failure mode is directory mismatch: the repo name on GitHub differs from the local folder name. Always `pwd` and `ls` before assuming a path. Second most common: env var name drift between local dev and production.

---

## 15. EMERGENCY RECOVERY — "I'm locked out"

1. **Hetzner Console → server → Reset Root Password** (get a fresh password)
2. **Click `>_` Console** → login as root
3. **Re-add your SSH key** using the GitHub trick:
   ```bash
   mkdir -p /root/.ssh && chmod 700 /root/.ssh
   curl -o /root/.ssh/authorized_keys https://github.com/cliffordnwanna.keys
   chmod 600 /root/.ssh/authorized_keys
   ```
4. **Re-enable root login temporarily**:
   ```bash
   nano /etc/ssh/sshd_config
   # Set: PermitRootLogin yes
   systemctl restart ssh
   ```
5. SSH in from PowerShell, fix whatever, then re-harden (`PermitRootLogin no`).

If the server is completely fucked: **Rebuild** (section 4). All your code is on GitHub, all env values are in your local backups, all data is in Supabase / Docker volumes (which survive a server-level rebuild only if you used Hetzner Volumes — otherwise restore from your backups).

---

## 16. QUICK REFERENCE — DAILY COMMANDS

| What | Command |
|---|---|
| SSH in | `ssh vps` (after step 11 config) |
| List PM2 apps | `pm2 list` |
| Restart an app | `pm2 restart <name>` |
| View app logs | `pm2 logs <name> --lines 100` |
| List Docker | `docker ps` |
| Restart Jarvis | `docker compose -f ~/apps/Jarvis/docker-compose.yml restart` |
| Reload Caddy after Caddyfile edit | `sudo systemctl reload caddy` |
| Check Caddy logs | `sudo tail -f /var/log/caddy/upjobs-access.log` |
| Check firewall | `sudo ufw status` |
| Check failed login attempts | `sudo fail2ban-client status sshd` |
| Server disk usage | `df -h` |
| Server memory | `free -h` |
| Top processes | `htop` (install: `sudo apt install htop`) |

---

## 17. WHAT NOT TO DO

- ❌ **Don't paste secrets into AI chat tools** — they may be logged. Rotate any secret that's ever been pasted.
- ❌ **Don't commit `.env` files**. Add `.env*` to `.gitignore` in every repo.
- ❌ **Don't run apps as root.** Always `deploy`. PM2 was configured for `deploy`.
- ❌ **Don't disable fail2ban or UFW**, even temporarily. The internet attacks every public IP every minute.
- ❌ **Don't reuse passwords across services.**
- ❌ **Don't trust the Hetzner web console for complex commands.** Always exit to SSH as soon as you can.
- ❌ **Don't click Reset Root Password multiple times in a row** without using the new one — only the latest is valid.
- ❌ **Don't use `PermitRootLogin prohibit-password` and then try to login as root with a password.** That setting actively blocks it (only keys allowed). This caused hours of confusion on 3 June 2026.

---

## 18. HARDENING BEYOND THE BASICS (DEFENSE IN DEPTH)

Sections 6–7 covered the must-haves. The previous compromise happened despite the server being a fresh install, which means the basics aren't enough. Add the layers below in order. Each is independent — if one fails, the next still protects you.

### 18.1 Automatic security updates (close known CVEs without you remembering)
```bash
sudo apt install -y unattended-upgrades
sudo dpkg-reconfigure -plow unattended-upgrades   # answer Yes
```
Verify it's enabled:
```bash
cat /etc/apt/apt.conf.d/20auto-upgrades
# Should show: APT::Periodic::Unattended-Upgrade "1";
```
Edit `/etc/apt/apt.conf.d/50unattended-upgrades` and uncomment:
```
"${distro_id}:${distro_codename}-security";
Unattended-Upgrade::Automatic-Reboot "true";
Unattended-Upgrade::Automatic-Reboot-Time "04:00";
```
Now CVEs get patched at 4am automatically, with a reboot only if the kernel updated.

### 18.2 Real SSH hardening (not just "key-only")
Edit `/etc/ssh/sshd_config` and set / confirm:
```
Port 2202                                    # any non-22 port; reduces brute-force volume ~99%
PermitRootLogin no
PasswordAuthentication no
PubkeyAuthentication yes
KbdInteractiveAuthentication no
ChallengeResponseAuthentication no
PermitEmptyPasswords no
MaxAuthTries 3
MaxSessions 4
LoginGraceTime 20
ClientAliveInterval 300
ClientAliveCountMax 2
AllowUsers deploy                            # only deploy can SSH at all
AllowAgentForwarding no
AllowTcpForwarding no                         # set to "yes" if you need tunnels
X11Forwarding no
PrintMotd no
UseDNS no
```
After saving, **before** restarting SSH:
1. Open the new port in UFW: `sudo ufw allow 2202/tcp` then `sudo ufw delete allow OpenSSH`
2. Update your local `~/.ssh/config`: add `Port 2202` to the `Host vps` block
3. **Keep an existing SSH session open** as safety, then restart in a different window:
   ```bash
   sudo systemctl restart ssh
   ```
4. Test new port from a NEW window: `ssh -p 2202 deploy@<IP>` — only close the safety session if it works.

The non-default port alone cuts brute-force log noise by ~99% because bots scan port 22.

### 18.3 Tighter fail2ban config
Default fail2ban only protects SSH and bans for 10 minutes. Make it meaner:
```bash
sudo nano /etc/fail2ban/jail.local
```
Paste:
```ini
[DEFAULT]
bantime  = 24h
findtime = 10m
maxretry = 3
backend  = systemd
banaction = ufw
destemail = nwannachumaclifford@gmail.com
sender = fail2ban@upjobs.co
action = %(action_mwl)s

[sshd]
enabled = true
port    = 2202

[caddy-status]
enabled = true
filter  = caddy-status
logpath = /var/log/caddy/*-access.log
maxretry = 20
findtime = 1m
bantime = 1h
```
Create the filter:
```bash
sudo nano /etc/fail2ban/filter.d/caddy-status.conf
```
```ini
[Definition]
failregex = "status":4(0[01345689]|29),"resp_headers"
ignoreregex =
```
This bans IPs that get 20+ 4xx errors in 1 minute (scrapers, login bruteforcers). Then:
```bash
sudo systemctl restart fail2ban
sudo fail2ban-client status
```

### 18.4 Hetzner Cloud Firewall (defense in depth at the network edge)
UFW runs on the server. The Hetzner Cloud Firewall runs **before** packets reach the server. Use both.

1. Hetzner Console → **Firewalls** → **Create Firewall**
2. Name: `vps-edge`
3. Inbound rules:
   - TCP `2202` (your new SSH port) — source: your home IP only if static, else `0.0.0.0/0`
   - TCP `80` — source `0.0.0.0/0`
   - TCP `443` — source `0.0.0.0/0`
4. Outbound: leave default (allow all)
5. Apply to server `ubuntu-4gb-nbg1-4`

Now even if UFW fails, Hetzner blocks the traffic.

### 18.5 Cloudflare in front of your domains (free WAF, DDoS, bot protection)
For `upjobs.co` and `liveportfolio.site`:
1. Add domain to Cloudflare (free plan)
2. Update registrar nameservers to Cloudflare's
3. A records: point to your server IP, **proxied (orange cloud ON)**
4. Cloudflare → **Security → WAF** → enable managed rules
5. Cloudflare → **Security → Bots** → enable "Bot Fight Mode"
6. Cloudflare → **SSL/TLS** → set to "Full (strict)" (Caddy already serves real certs)
7. Cloudflare → **Rules → Page Rules** → for `/api/*` set "Cache Level: Bypass" (Next.js API routes shouldn't be cached)

Your server IP is now hidden behind Cloudflare. Attackers can't directly hit it on 80/443.

Bonus: tighten UFW to only accept 80/443 from Cloudflare IPs:
```bash
sudo ufw delete allow 80
sudo ufw delete allow 443
for ip in $(curl -s https://www.cloudflare.com/ips-v4); do
  sudo ufw allow from $ip to any port 80 proto tcp
  sudo ufw allow from $ip to any port 443 proto tcp
done
```

### 18.6 Kernel & network hardening (sysctl)
Create `/etc/sysctl.d/99-hardening.conf`:
```
# Network
net.ipv4.conf.all.rp_filter = 1
net.ipv4.conf.default.rp_filter = 1
net.ipv4.icmp_echo_ignore_broadcasts = 1
net.ipv4.conf.all.accept_redirects = 0
net.ipv4.conf.default.accept_redirects = 0
net.ipv4.conf.all.send_redirects = 0
net.ipv4.conf.all.accept_source_route = 0
net.ipv4.tcp_syncookies = 1
net.ipv4.tcp_max_syn_backlog = 2048
net.ipv4.conf.all.log_martians = 1

# IPv6
net.ipv6.conf.all.accept_redirects = 0
net.ipv6.conf.default.accept_redirects = 0
net.ipv6.conf.all.accept_source_route = 0

# Kernel
kernel.randomize_va_space = 2
kernel.kptr_restrict = 2
kernel.dmesg_restrict = 1
fs.protected_hardlinks = 1
fs.protected_symlinks = 1
fs.suid_dumpable = 0
```
Apply:
```bash
sudo sysctl --system
```

### 18.7 File integrity monitoring (AIDE)
Detects if a binary or config file is silently modified (rootkit, backdoor):
```bash
sudo apt install -y aide
sudo aideinit                                  # builds the initial database (~15 mins)
sudo mv /var/lib/aide/aide.db.new /var/lib/aide/aide.db
# Daily check at 5am
echo "0 5 * * * root /usr/bin/aide --check | mail -s 'AIDE report' nwannachumaclifford@gmail.com" | sudo tee /etc/cron.d/aide
```
If you ever see alerts about `/usr/bin/*` or `/etc/ssh/*` changing without you doing it — that's a compromise signal.

### 18.8 Rootkit & malware scans
```bash
sudo apt install -y rkhunter chkrootkit
sudo rkhunter --update
sudo rkhunter --propupd                        # accept current state as baseline
echo "0 6 * * * root /usr/bin/rkhunter --cronjob --update --quiet" | sudo tee /etc/cron.d/rkhunter
echo "0 6 * * * root /usr/sbin/chkrootkit -q | mail -s 'chkrootkit' nwannachumaclifford@gmail.com" | sudo tee /etc/cron.d/chkrootkit
```

### 18.9 Audit logging (auditd)
Records every privileged action to disk — invaluable for post-incident forensics:
```bash
sudo apt install -y auditd
sudo systemctl enable --now auditd
```
Add rules in `/etc/audit/rules.d/hardening.rules`:
```
-w /etc/passwd -p wa -k passwd_changes
-w /etc/shadow -p wa -k shadow_changes
-w /etc/ssh/sshd_config -p wa -k ssh_config_changes
-w /etc/sudoers -p wa -k sudoers_changes
-w /var/log/auth.log -p wa -k auth_log
-w /home/deploy/.ssh -p wa -k deploy_ssh
-a always,exit -F arch=b64 -S execve -F euid=0 -k root_commands
```
Then:
```bash
sudo augenrules --load
sudo systemctl restart auditd
```
Query with `sudo ausearch -k ssh_config_changes`.

### 18.10 Sudo hygiene
```bash
sudo visudo
```
At the bottom, **remove** any line like `deploy ALL=(ALL) NOPASSWD: ALL` if it exists. Always require password for sudo. Add:
```
Defaults    timestamp_timeout=5
Defaults    passwd_tries=3
Defaults    logfile="/var/log/sudo.log"
Defaults    log_input,log_output
```
This logs every sudo command + its output to `/var/log/sudo.log`.

### 18.11 Lock down PM2 / Node / Docker exposure
- **PM2 / Node**: confirm Node ports bind to `127.0.0.1`, NOT `0.0.0.0`:
  ```bash
  netstat -tlnp 2>/dev/null | grep -E "node|pm2"
  ```
  Set `HOST=127.0.0.1` in Next.js ecosystem.config.js. Caddy proxies from localhost; the public never hits Node directly.
- **Docker**: jarvis ports should bind to `127.0.0.1:8001`, `127.0.0.1:5433`, `127.0.0.1:6380` (✅ they do, per your `docker ps`). NEVER use `-p 8001:8000` without an IP — that binds to all interfaces.
- **Docker daemon**: confirm `/etc/docker/daemon.json` doesn't expose a TCP socket.

### 18.12 App-level hardening (Next.js / Supabase)
- **Supabase RLS**: every table MUST have Row Level Security enabled. Anon key + no RLS = full DB read by anyone with the key.
- **Service role key**: ONLY used server-side. Never ship in client bundles. Search: `grep -r "SUPABASE_SERVICE_ROLE_KEY" src/ app/ pages/` — should only appear in API routes / server components.
- **CRON_SECRET / webhook secrets**: validate with constant-time compare (`crypto.timingSafeEqual`), not `===`. Regenerate any default-valued secret immediately.
- **Rate limit auth routes** in Caddy:
  ```caddy
  @auth path /api/auth/*
  rate_limit @auth {
      zone auth_zone {
          key {remote_ip}
          events 10
          window 1m
      }
  }
  ```
- **Add HSTS + CSP** to your Caddy `header` blocks:
  - `Strict-Transport-Security "max-age=63072000; includeSubDomains; preload"`
  - `Content-Security-Policy` (start in report-only mode)
- **Next.js**: anything prefixed `NEXT_PUBLIC_` ships to the browser. Audit each one.

### 18.13 Secret storage on the server
- `.env` files: `chmod 600`, owned by `deploy:deploy`. Verify:
  ```bash
  find ~/apps -name ".env*" -exec ls -la {} \;
  ```
- Never `echo $SECRET` in commands — it lands in `~/.bash_history`. Use `read -s SECRET`.
- Clear sensitive history: `history -c && history -w` after any risky session.

### 18.14 Encrypted off-server backups
Local backups don't help if the server is wiped. Push encrypted backups elsewhere:
```bash
sudo apt install -y restic
# Use Hetzner Storage Box, Backblaze B2, or S3
restic init --repo b2:bucketname:/vps-backups
cat > ~/backups/offsite.sh << 'EOF'
#!/usr/bin/env bash
export RESTIC_PASSWORD_FILE=/home/deploy/.restic-password
export B2_ACCOUNT_ID=xxx
export B2_ACCOUNT_KEY=xxx
restic -r b2:bucketname:/vps-backups backup /home/deploy/backups /etc/caddy /home/deploy/apps/cv360/.env /home/deploy/apps/liveportfolio/.env.local
restic -r b2:bucketname:/vps-backups forget --keep-daily 14 --keep-weekly 8 --prune
EOF
chmod +x ~/backups/offsite.sh
chmod 600 /home/deploy/.restic-password
```
Restic encrypts client-side. Even if the bucket is breached the data is useless.

Also enable **Hetzner automatic snapshots** (Console → server → Backups → Enable, ~20% surcharge).

### 18.15 Monitoring & alerts (know something's wrong fast)
- **Uptime monitoring**: [UptimeRobot](https://uptimerobot.com) free — pings your domains every 5 min, emails/SMS if down.
- **Server health**: [Netdata](https://www.netdata.cloud) — install per their docs, restrict access to your IP.
- **Daily auth log summary**:
  ```bash
  sudo apt install -y logwatch
  echo "/usr/sbin/logwatch --output mail --mailto nwannachumaclifford@gmail.com --detail high" | sudo tee /etc/cron.daily/00logwatch
  ```
- **Fail2ban status anytime**:
  ```bash
  sudo fail2ban-client status sshd
  sudo fail2ban-client status caddy-status
  ```

### 18.16 GitHub & supply chain
- **Branch protection** on `main` for all production repos (settings → branches): require PRs + status checks.
- **Secret scanning + push protection** (settings → code security & analysis). GitHub blocks pushes containing detected secrets.
- **Dependabot security updates** — auto-PRs for vulnerable npm/pip packages.
- **Sign commits** with SSH/GPG (verified badge prevents impersonation).
- **Audit dependencies** monthly: `npm audit && npm audit fix`.
- **Pin GitHub Actions** to commit SHAs (not `@v3`) for workflows that have secrets.
- **Rotate GitHub deploy key** (`~/.ssh/github_deploy`) every 6 months.

### 18.17 If you suspect another compromise (incident response)
1. **Cut public access first** — Hetzner Cloud Firewall: allow only your IP. Buys time without destroying evidence.
2. **Do NOT reboot.** Live RAM contains forensic evidence.
3. Save logs while the server is still up:
   ```bash
   sudo cp -r /var/log /home/deploy/incident-$(date +%F)/
   sudo last -F > /home/deploy/incident-$(date +%F)/logins.txt
   sudo netstat -tnpa > /home/deploy/incident-$(date +%F)/connections.txt
   sudo ps auxf > /home/deploy/incident-$(date +%F)/processes.txt
   sudo crontab -l > /home/deploy/incident-$(date +%F)/crontab-root.txt
   crontab -l > /home/deploy/incident-$(date +%F)/crontab-deploy.txt
   sudo ls -la /tmp /var/tmp /dev/shm > /home/deploy/incident-$(date +%F)/tmp.txt
   ```
4. `scp` those off the server.
5. **Rotate every secret** (Section 12).
6. **Rebuild from scratch** (Section 4). Never "clean" a compromised server — you can't trust it.
7. Restore code from GitHub, data from backups created BEFORE the compromise window.

### 18.18 Quarterly security review (calendar reminder)
- [ ] `apt list --upgradable` — anything outstanding?
- [ ] `sudo lynis audit system` — score should be >80
- [ ] Review fail2ban ban list — any patterns?
- [ ] `sudo last -F | head -50` — any logins you don't recognise?
- [ ] `sudo grep -i accepted /var/log/auth.log* | tail` — same
- [ ] Rotate secrets per Section 12 if 90+ days since last rotation
- [ ] Confirm Cloudflare WAF rules still tuned for current traffic
- [ ] Test restore: pull a recent restic snapshot, verify integrity
- [ ] Check Hetzner billing — unexpected egress or compute spike = possible miner

---

## 19. ATTACK SURFACE SUMMARY (what's now closed)

| Layer | Attack vector | Mitigation |
|---|---|---|
| **Network edge** | DDoS, IP exposure | Cloudflare proxy (§18.5), Hetzner Cloud Firewall (§18.4) |
| **Transport** | Port 22 brute force | Non-standard SSH port (§18.2), UFW (§6.8) |
| **SSH auth** | Password guess, key theft | Key-only, no root, AllowUsers deploy (§6.7, §18.2) |
| **App brute force** | Login spam, scraping | fail2ban + Caddy filter (§18.3), Caddy rate-limit (§18.12) |
| **Unpatched CVE** | Kernel/OpenSSH bugs | Unattended-upgrades (§18.1) |
| **Privilege escalation** | sudo abuse, kernel exploit | sysctl hardening (§18.6), sudo logging (§18.10), no NOPASSWD |
| **Persistence/rootkit** | Backdoored binary | AIDE (§18.7), rkhunter/chkrootkit (§18.8), auditd (§18.9) |
| **App logic** | RLS bypass, IDOR, weak secrets | Supabase RLS, secret scanning, rotation (§18.12) |
| **Secret leak** | Token/key in git or logs | Push protection, scanning, history hygiene (§18.13, §18.16) |
| **Lost laptop** | Stolen local SSH key | Passphrase on key, rotate quarterly (§12) |
| **Data loss** | Server destroyed | Encrypted off-site backups (§18.14), Hetzner snapshots |
| **Lateral movement** | App → server | Apps bind 127.0.0.1 only (§18.11) |
| **Detection lag** | Compromise undetected for weeks | Netdata, logwatch, fail2ban alerts (§18.15), AIDE (§18.7) |

Each row is independent — multiple layers must fail at once for a real breach. That's "defense in depth."

---

**END OF DOCUMENT** — keep updated after every meaningful infra change. Commit to a private repo (e.g. `cliffordnwanna/infra-notes`) so it survives a laptop loss.
