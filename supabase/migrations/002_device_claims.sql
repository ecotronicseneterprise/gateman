-- ============================================================
-- DEVICE CLAIMS TABLE - For Auto-Discovery Provisioning
-- ============================================================

CREATE TABLE IF NOT EXISTS device_claims (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  device_mac TEXT NOT NULL,
  organization_id UUID NOT NULL REFERENCES organizations(id) ON DELETE CASCADE,
  provision_token TEXT NOT NULL,
  wifi_ssid TEXT NOT NULL,
  wifi_password TEXT NOT NULL,
  claimed_by UUID REFERENCES auth.users(id) ON DELETE SET NULL,
  claimed BOOLEAN DEFAULT FALSE,
  expires_at TIMESTAMPTZ NOT NULL,
  created_at TIMESTAMPTZ DEFAULT now(),
  updated_at TIMESTAMPTZ DEFAULT now()
);

-- Indexes
CREATE INDEX idx_device_claims_mac ON device_claims(device_mac) WHERE NOT claimed;
CREATE INDEX idx_device_claims_expires ON device_claims(expires_at) WHERE NOT claimed;

-- RLS Policies
ALTER TABLE device_claims ENABLE ROW LEVEL SECURITY;

-- Service role can do anything (for Edge Functions)
CREATE POLICY "Service role full access"
  ON device_claims
  FOR ALL
  TO service_role
  USING (true)
  WITH CHECK (true);

-- Cleanup function for expired claims
CREATE OR REPLACE FUNCTION cleanup_expired_claims()
RETURNS void
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
BEGIN
  DELETE FROM device_claims
  WHERE expires_at < now() AND NOT claimed;
END;
$$;

-- Optional: Schedule cleanup (requires pg_cron extension)
-- SELECT cron.schedule('cleanup-expired-claims', '*/5 * * * *', 'SELECT cleanup_expired_claims()');
