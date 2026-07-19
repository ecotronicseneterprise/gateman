-- Enable "nuclear" employee delete from the dashboard: removes the employee row,
-- which cascades to their credentials (user_credentials.user_id is ON DELETE CASCADE)
-- and anonymizes attendance history (attendance_logs.user_id is ON DELETE SET NULL).
--
-- Two blockers fixed here:
-- 1. users has SELECT/INSERT/UPDATE policies but no DELETE policy — client deletes
--    silently remove zero rows.
-- 2. enrollment_queue.assigned_to references users(id) with no ON DELETE action,
--    so deleting any employee who ever went through enrollment fails with an FK
--    violation. Rebuilt as ON DELETE SET NULL — the constraint name MUST stay
--    "enrollment_queue_assigned_to_fkey" because the dashboard embeds it in
--    PostgREST resource hints (users!enrollment_queue_assigned_to_fkey).

CREATE POLICY "users_delete" ON users FOR DELETE USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);

ALTER TABLE enrollment_queue DROP CONSTRAINT enrollment_queue_assigned_to_fkey;
ALTER TABLE enrollment_queue ADD CONSTRAINT enrollment_queue_assigned_to_fkey
  FOREIGN KEY (assigned_to) REFERENCES users(id) ON DELETE SET NULL;
