-- ============================================================
-- STORAGE POLICIES FOR attendance-photos BUCKET
-- Run in Supabase SQL Editor
-- ============================================================

-- Policy 1: Service role can INSERT (upload) photos
CREATE POLICY "Service role can upload photos"
ON storage.objects
FOR INSERT
TO service_role
WITH CHECK (bucket_id = 'attendance-photos');

-- Policy 2: Service role can UPDATE photos
CREATE POLICY "Service role can update photos"
ON storage.objects
FOR UPDATE
TO service_role
USING (bucket_id = 'attendance-photos')
WITH CHECK (bucket_id = 'attendance-photos');

-- Policy 3: Authenticated users can SELECT (view) photos
CREATE POLICY "Authenticated users can view photos"
ON storage.objects
FOR SELECT
TO authenticated
USING (bucket_id = 'attendance-photos');

-- Policy 4: Service role can SELECT (needed for signed URLs)
CREATE POLICY "Service role can view photos"
ON storage.objects
FOR SELECT
TO service_role
USING (bucket_id = 'attendance-photos');

-- Verify policies created
SELECT 
  schemaname,
  tablename,
  policyname,
  permissive,
  roles,
  cmd
FROM pg_policies 
WHERE tablename = 'objects' 
  AND policyname LIKE '%photos%'
ORDER BY policyname;
