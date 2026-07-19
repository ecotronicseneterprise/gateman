-- Allow org owners/admins to delete user credentials (unassign RFID cards).
-- Without this policy the dashboard's "Remove Card" action silently deletes
-- zero rows: RLS has SELECT and INSERT policies for user_credentials (001)
-- but no DELETE policy. Same role gate as credentials_insert.
CREATE POLICY "credentials_delete" ON user_credentials FOR DELETE USING (
  organization_id IN (
    SELECT organization_id FROM org_members
    WHERE user_id = auth.uid() AND role IN ('owner', 'admin')
  )
);
