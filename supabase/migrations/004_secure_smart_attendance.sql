-- 004: Close cross-tenant data leak in get_smart_attendance
--
-- get_smart_attendance was SECURITY DEFINER and GRANTed to `authenticated`,
-- but — unlike every other dashboard RPC (get_hourly_stats, get_weekly_stats,
-- get_department_presence, get_dashboard_stats) — it never checked that the
-- calling user belongs to the org_id they passed in. Any authenticated user
-- could call it with an arbitrary org_id and read another tenant's full
-- attendance history, including photo paths.
--
-- This replaces it with the same org_members membership guard used by the
-- other RPCs, and keeps the FULL OUTER JOIN body from FIX_ATTENDANCE_SQL.sql
-- (the more complete of the two prior versions — correctly handles days with
-- only a check-in or only a check-out).

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
  IF NOT EXISTS (
    SELECT 1 FROM public.org_members WHERE user_id = auth.uid() AND organization_id = org_id
  ) THEN
    RAISE EXCEPTION 'access_denied';
  END IF;

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

GRANT EXECUTE ON FUNCTION get_smart_attendance(UUID, TIMESTAMPTZ, TIMESTAMPTZ) TO authenticated;
