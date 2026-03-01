-- Complete fix for get_smart_attendance function
-- Run this in Supabase SQL Editor

DROP FUNCTION IF EXISTS get_smart_attendance(UUID, TIMESTAMPTZ, TIMESTAMPTZ);

CREATE OR REPLACE FUNCTION get_smart_attendance(
  org_id UUID,
  from_date TIMESTAMPTZ DEFAULT NULL,
  to_date TIMESTAMPTZ DEFAULT NULL
)
RETURNS TABLE (
  user_id UUID,
  employee_id TEXT,
  name TEXT,
  department TEXT,
  date DATE,
  check_in_time TIMESTAMPTZ,
  check_out_time TIMESTAMPTZ,
  check_in_device TEXT,
  check_out_device TEXT,
  check_in_photo TEXT,
  check_out_photo TEXT,
  hours_worked NUMERIC
) 
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
BEGIN
  RETURN QUERY
  SELECT 
    COALESCE(ci.user_id, co.user_id) as user_id,
    COALESCE(ci.employee_id, co.employee_id) as employee_id,
    COALESCE(ci.name, co.name) as name,
    COALESCE(ci.department, co.department) as department,
    COALESCE(ci.log_date, co.log_date) as date,
    ci.check_in_time,
    co.check_out_time,
    ci.check_in_device,
    co.check_out_device,
    ci.check_in_photo,
    co.check_out_photo,
    CASE 
      WHEN ci.check_in_time IS NOT NULL AND co.check_out_time IS NOT NULL THEN
        EXTRACT(EPOCH FROM (co.check_out_time - ci.check_in_time)) / 3600
      ELSE NULL
    END as hours_worked
  FROM (
    -- First check-in per user per day
    SELECT DISTINCT ON (al.user_id, DATE(al.timestamp))
      al.user_id,
      u.employee_id,
      u.name,
      u.department,
      DATE(al.timestamp) as log_date,
      al.timestamp as check_in_time,
      d.name as check_in_device,
      al.photo_url as check_in_photo
    FROM attendance_logs al
    LEFT JOIN users u ON al.user_id = u.id
    LEFT JOIN devices d ON al.device_id = d.id
    WHERE al.organization_id = org_id
      AND al.action = 'check_in'
      AND (from_date IS NULL OR al.timestamp >= from_date)
      AND (to_date IS NULL OR al.timestamp <= to_date)
    ORDER BY al.user_id, DATE(al.timestamp), al.timestamp ASC
  ) ci
  FULL OUTER JOIN (
    -- Last check-out per user per day
    SELECT DISTINCT ON (al.user_id, DATE(al.timestamp))
      al.user_id,
      u.employee_id,
      u.name,
      u.department,
      DATE(al.timestamp) as log_date,
      al.timestamp as check_out_time,
      d.name as check_out_device,
      al.photo_url as check_out_photo
    FROM attendance_logs al
    LEFT JOIN users u ON al.user_id = u.id
    LEFT JOIN devices d ON al.device_id = d.id
    WHERE al.organization_id = org_id
      AND al.action = 'check_out'
      AND (from_date IS NULL OR al.timestamp >= from_date)
      AND (to_date IS NULL OR al.timestamp <= to_date)
    ORDER BY al.user_id, DATE(al.timestamp), al.timestamp DESC
  ) co ON ci.user_id = co.user_id AND ci.log_date = co.log_date
  ORDER BY COALESCE(ci.log_date, co.log_date) DESC, COALESCE(ci.check_in_time, co.check_out_time) DESC;
END;
$$;
