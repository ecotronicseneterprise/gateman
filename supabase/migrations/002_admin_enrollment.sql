-- 002: Admin-driven enrollment flow
-- Adds 'waiting' status so admin can initiate enrollment from dashboard
-- Device polls for waiting enrollments, user taps card, auto-assigned

-- 1. Expand status CHECK to include 'waiting'
ALTER TABLE enrollment_queue DROP CONSTRAINT IF EXISTS enrollment_queue_status_check;
ALTER TABLE enrollment_queue ADD CONSTRAINT enrollment_queue_status_check
  CHECK (status IN ('pending', 'assigned', 'rejected', 'waiting'));

-- 2. Make credential_value nullable (not known yet when admin initiates)
ALTER TABLE enrollment_queue ALTER COLUMN credential_value DROP NOT NULL;

-- 3. Add INSERT policy so authenticated admins/owners can create waiting enrollments
CREATE POLICY "enrollment_insert" ON enrollment_queue FOR INSERT WITH CHECK (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

-- 4. Index for device polling (waiting enrollments by org)
CREATE INDEX idx_enrollment_waiting ON enrollment_queue(organization_id, device_id)
  WHERE status = 'waiting';
