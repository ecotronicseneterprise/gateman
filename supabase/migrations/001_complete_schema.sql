-- ============================================================
-- ECOTRONICS GATEMAN — COMPLETE PRODUCTION SCHEMA
-- Run in Supabase SQL Editor (single execution)
-- Includes: all tables, indexes, RLS, triggers, RPCs, go-live fixes
-- ============================================================

-- 1. ORGANIZATIONS
CREATE TABLE organizations (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  name TEXT NOT NULL,
  slug TEXT NOT NULL UNIQUE,
  settings JSONB DEFAULT '{}',
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 2. ORG MEMBERS (links Supabase Auth users to organizations)
CREATE TABLE org_members (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  user_id UUID NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
  role TEXT NOT NULL DEFAULT 'viewer' CHECK (role IN ('owner', 'admin', 'viewer')),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(organization_id, user_id)
);

CREATE INDEX idx_org_members_user ON org_members(user_id);
CREATE INDEX idx_org_members_org ON org_members(organization_id);

-- 3. SUBSCRIPTIONS
CREATE TABLE subscriptions (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  plan_type TEXT NOT NULL CHECK (plan_type IN ('starter', 'growth', 'enterprise')),
  status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'past_due', 'cancelled', 'trial')),
  device_limit INTEGER NOT NULL DEFAULT 1,
  user_limit INTEGER NOT NULL DEFAULT 50,
  retention_days INTEGER NOT NULL DEFAULT 180,
  trial_ends_at TIMESTAMPTZ,
  current_period_start TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  current_period_end TIMESTAMPTZ NOT NULL DEFAULT (NOW() + INTERVAL '30 days'),
  paystack_reference TEXT,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE UNIQUE INDEX idx_sub_org_active ON subscriptions(organization_id) WHERE status IN ('active', 'trial');

-- 4. DEVICES
CREATE TABLE devices (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  device_uid TEXT NOT NULL UNIQUE,
  device_secret TEXT NOT NULL,
  name TEXT NOT NULL DEFAULT 'New Device',
  location TEXT,
  status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'inactive', 'revoked')),
  firmware_version TEXT,
  last_seen TIMESTAMPTZ,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_devices_org ON devices(organization_id);
CREATE INDEX idx_devices_uid ON devices(device_uid);

-- 5. PROVISION TOKENS
CREATE TABLE provision_tokens (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  token TEXT NOT NULL UNIQUE,
  device_name TEXT,
  expires_at TIMESTAMPTZ NOT NULL DEFAULT (NOW() + INTERVAL '10 minutes'),
  used_at TIMESTAMPTZ,
  used_by_device_id UUID REFERENCES devices(id),
  created_by UUID NOT NULL REFERENCES auth.users(id),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_provision_token ON provision_tokens(token) WHERE used_at IS NULL;

-- 6. USERS (employees/staff within an organization)
CREATE TABLE users (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  employee_id TEXT NOT NULL,
  name TEXT NOT NULL,
  department TEXT,
  email TEXT,
  active BOOLEAN NOT NULL DEFAULT TRUE,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(organization_id, employee_id)
);

CREATE INDEX idx_users_org ON users(organization_id);

-- 7. USER CREDENTIALS (RFID, PIN, biometric — org-scoped)
CREATE TABLE user_credentials (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  type TEXT NOT NULL CHECK (type IN ('rfid', 'pin', 'fingerprint', 'face')),
  value TEXT NOT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(organization_id, type, value)
);

CREATE INDEX idx_credentials_user ON user_credentials(user_id);
CREATE INDEX idx_credentials_org ON user_credentials(organization_id);
CREATE INDEX idx_credentials_lookup ON user_credentials(organization_id, type, value);

-- 8. ATTENDANCE LOGS
-- FIX: ON DELETE SET NULL for device_id and user_id to preserve history
CREATE TABLE attendance_logs (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  device_id UUID REFERENCES devices(id) ON DELETE SET NULL,
  user_id UUID REFERENCES users(id) ON DELETE SET NULL,
  credential_value TEXT NOT NULL,
  action TEXT NOT NULL CHECK (action IN ('check_in', 'check_out')),
  device_event_id TEXT NOT NULL,
  timestamp TIMESTAMPTZ NOT NULL,
  photo_url TEXT,
  synced_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(device_id, device_event_id)
);

CREATE INDEX idx_logs_org ON attendance_logs(organization_id);
CREATE INDEX idx_logs_org_ts ON attendance_logs(organization_id, timestamp DESC);
CREATE INDEX idx_logs_device ON attendance_logs(device_id);
CREATE INDEX idx_logs_user ON attendance_logs(user_id);
CREATE INDEX idx_logs_event ON attendance_logs(device_id, device_event_id);

-- 9. ENROLLMENT QUEUE
CREATE TABLE enrollment_queue (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  device_id UUID NOT NULL REFERENCES devices(id),
  credential_type TEXT NOT NULL DEFAULT 'rfid',
  credential_value TEXT NOT NULL,
  photo_url TEXT,
  status TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'assigned', 'rejected')),
  assigned_to UUID REFERENCES users(id),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  resolved_at TIMESTAMPTZ
);

CREATE INDEX idx_enrollment_org ON enrollment_queue(organization_id) WHERE status = 'pending';

-- 10. AUDIT LOGS (immutable — no UPDATE/DELETE policies)
CREATE TABLE audit_logs (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  organization_id UUID REFERENCES organizations(id) ON DELETE CASCADE,
  actor_type TEXT NOT NULL CHECK (actor_type IN ('user', 'device', 'system')),
  actor_id TEXT,
  action TEXT NOT NULL,
  resource_type TEXT,
  resource_id TEXT,
  metadata JSONB DEFAULT '{}',
  ip_address TEXT,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_audit_org ON audit_logs(organization_id, created_at DESC);
CREATE INDEX idx_audit_action ON audit_logs(action);

-- 11. PAYMENT REFERENCES (Paystack webhook idempotency)
CREATE TABLE payment_references (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  reference TEXT UNIQUE NOT NULL,
  organization_id UUID REFERENCES organizations(id) ON DELETE CASCADE,
  event_type TEXT NOT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ============================================================
-- ROW LEVEL SECURITY POLICIES
-- ============================================================

ALTER TABLE organizations ENABLE ROW LEVEL SECURITY;
ALTER TABLE org_members ENABLE ROW LEVEL SECURITY;
ALTER TABLE subscriptions ENABLE ROW LEVEL SECURITY;
ALTER TABLE devices ENABLE ROW LEVEL SECURITY;
ALTER TABLE provision_tokens ENABLE ROW LEVEL SECURITY;
ALTER TABLE users ENABLE ROW LEVEL SECURITY;
ALTER TABLE user_credentials ENABLE ROW LEVEL SECURITY;
ALTER TABLE attendance_logs ENABLE ROW LEVEL SECURITY;
ALTER TABLE enrollment_queue ENABLE ROW LEVEL SECURITY;
ALTER TABLE audit_logs ENABLE ROW LEVEL SECURITY;
ALTER TABLE payment_references ENABLE ROW LEVEL SECURITY;

-- Organizations: members can view their orgs
CREATE POLICY "org_select" ON organizations FOR SELECT USING (
  id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Organizations: any authenticated user can create (signup flow)
CREATE POLICY "org_insert" ON organizations FOR INSERT WITH CHECK (
  auth.uid() IS NOT NULL
);

-- Org Members: see members of your orgs
CREATE POLICY "members_select" ON org_members FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Org Members: user can insert themselves as owner (signup flow)
CREATE POLICY "members_self_insert" ON org_members FOR INSERT WITH CHECK (
  user_id = auth.uid() AND role = 'owner'
);

-- Org Members: owners/admins can add other members
CREATE POLICY "members_insert" ON org_members FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Subscriptions: members can view their org's subscription
CREATE POLICY "sub_select" ON subscriptions FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Devices: members can view their org's devices
CREATE POLICY "devices_select" ON devices FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Devices: owners/admins can insert devices
CREATE POLICY "devices_insert" ON devices FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Provision Tokens: owners/admins can view and create
CREATE POLICY "tokens_select" ON provision_tokens FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

CREATE POLICY "tokens_insert" ON provision_tokens FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Users (employees): members can view their org's users
CREATE POLICY "users_select" ON users FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Users: owners/admins can insert
CREATE POLICY "users_insert" ON users FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Users: owners/admins can update
CREATE POLICY "users_update" ON users FOR UPDATE USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- User Credentials: members can view their org's credentials
CREATE POLICY "credentials_select" ON user_credentials FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- User Credentials: owners/admins can insert
CREATE POLICY "credentials_insert" ON user_credentials FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Attendance Logs: members can view their org's logs
CREATE POLICY "logs_select" ON attendance_logs FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Enrollment Queue: members can view their org's enrollments
CREATE POLICY "enrollment_select" ON enrollment_queue FOR SELECT USING (
  organization_id IN (SELECT organization_id FROM org_members WHERE user_id = auth.uid())
);

-- Enrollment Queue: owners/admins can update (assign/reject)
CREATE POLICY "enrollment_update" ON enrollment_queue FOR UPDATE USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Audit Logs: owners/admins can view their org's audit trail (immutable — no UPDATE/DELETE)
CREATE POLICY "audit_select" ON audit_logs FOR SELECT USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- Payment References: no client access (service role only)
-- No SELECT/INSERT policies — only Edge Functions with service role access this table

-- ============================================================
-- TRIGGERS & FUNCTIONS
-- ============================================================

-- Auto-create trial subscription on org creation
CREATE OR REPLACE FUNCTION handle_new_organization()
RETURNS TRIGGER AS $$
BEGIN
  INSERT INTO subscriptions (organization_id, plan_type, status, device_limit, user_limit, retention_days, trial_ends_at)
  VALUES (NEW.id, 'starter', 'trial', 1, 50, 180, NOW() + INTERVAL '14 days');
  RETURN NEW;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

CREATE TRIGGER on_org_created
  AFTER INSERT ON organizations
  FOR EACH ROW EXECUTE FUNCTION handle_new_organization();

-- Auto-update updated_at timestamp
CREATE OR REPLACE FUNCTION update_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = NOW();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_organizations_updated BEFORE UPDATE ON organizations FOR EACH ROW EXECUTE FUNCTION update_updated_at();
CREATE TRIGGER trg_devices_updated BEFORE UPDATE ON devices FOR EACH ROW EXECUTE FUNCTION update_updated_at();
CREATE TRIGGER trg_users_updated BEFORE UPDATE ON users FOR EACH ROW EXECUTE FUNCTION update_updated_at();
CREATE TRIGGER trg_subscriptions_updated BEFORE UPDATE ON subscriptions FOR EACH ROW EXECUTE FUNCTION update_updated_at();

-- ============================================================
-- DATABASE FUNCTIONS (RPCs for Dashboard)
-- FIX: All RPCs now enforce auth.uid() org membership check
-- ============================================================

-- Dashboard: hourly stats (FIXED — now validates caller membership)
CREATE OR REPLACE FUNCTION get_hourly_stats(org_id UUID)
RETURNS TABLE(hour TEXT, action TEXT, count BIGINT) AS $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM public.org_members WHERE user_id = auth.uid() AND organization_id = org_id
  ) THEN
    RAISE EXCEPTION 'access_denied';
  END IF;

  RETURN QUERY
  SELECT to_char(al.timestamp, 'HH24') as hour, al.action, COUNT(*)
  FROM public.attendance_logs al
  WHERE al.organization_id = org_id AND al.timestamp::date = CURRENT_DATE
  GROUP BY to_char(al.timestamp, 'HH24'), al.action
  ORDER BY to_char(al.timestamp, 'HH24');
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

-- Dashboard: weekly stats (FIXED)
CREATE OR REPLACE FUNCTION get_weekly_stats(org_id UUID)
RETURNS TABLE(date DATE, unique_staff BIGINT, total_taps BIGINT) AS $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM public.org_members WHERE user_id = auth.uid() AND organization_id = org_id
  ) THEN
    RAISE EXCEPTION 'access_denied';
  END IF;

  RETURN QUERY
  SELECT al.timestamp::date as date,
         COUNT(DISTINCT al.user_id) as unique_staff,
         COUNT(*) as total_taps
  FROM public.attendance_logs al
  WHERE al.organization_id = org_id AND al.timestamp >= CURRENT_DATE - INTERVAL '7 days'
  GROUP BY al.timestamp::date
  ORDER BY al.timestamp::date;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

-- Dashboard: department presence (FIXED)
CREATE OR REPLACE FUNCTION get_department_presence(org_id UUID)
RETURNS TABLE(department TEXT, present BIGINT) AS $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM public.org_members WHERE user_id = auth.uid() AND organization_id = org_id
  ) THEN
    RAISE EXCEPTION 'access_denied';
  END IF;

  RETURN QUERY
  SELECT u.department, COUNT(DISTINCT al.user_id) as present
  FROM public.attendance_logs al
  JOIN public.users u ON al.user_id = u.id
  WHERE al.organization_id = org_id
    AND al.timestamp::date = CURRENT_DATE
    AND al.action = 'check_in'
  GROUP BY u.department;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

-- Dashboard: summary stats (FIXED)
CREATE OR REPLACE FUNCTION get_dashboard_stats(org_id UUID)
RETURNS JSON AS $$
DECLARE
  result JSON;
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM public.org_members WHERE user_id = auth.uid() AND organization_id = org_id
  ) THEN
    RAISE EXCEPTION 'access_denied';
  END IF;

  SELECT json_build_object(
    'total_employees', (SELECT COUNT(*) FROM public.users WHERE organization_id = org_id AND active = TRUE),
    'today_records', (SELECT COUNT(*) FROM public.attendance_logs WHERE organization_id = org_id AND timestamp::date = CURRENT_DATE),
    'checked_in', (
      SELECT COUNT(DISTINCT user_id) FROM public.attendance_logs
      WHERE organization_id = org_id AND timestamp::date = CURRENT_DATE AND action = 'check_in'
      AND user_id NOT IN (
        SELECT COALESCE(user_id, '00000000-0000-0000-0000-000000000000') FROM public.attendance_logs
        WHERE organization_id = org_id AND timestamp::date = CURRENT_DATE AND action = 'check_out'
        AND user_id IS NOT NULL
      )
    ),
    'devices', (SELECT COUNT(*) FROM public.devices WHERE organization_id = org_id AND status = 'active')
  ) INTO result;
  RETURN result;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

-- ============================================================
-- STORAGE BUCKET (manual step in Supabase Dashboard)
-- ============================================================
-- Name: attendance-photos
-- Public: OFF
-- File size limit: 50KB (QQVGA JPEG)
-- Allowed MIME types: image/jpeg
