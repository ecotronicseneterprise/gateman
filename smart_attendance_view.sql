-- ============================================================
-- SMART ATTENDANCE VIEW - First In, Last Out Per Day
-- Run in Supabase SQL Editor
-- ============================================================

-- Create a database function to get smart attendance (first-in, last-out per day)
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
  WITH daily_logs AS (
    SELECT 
      al.user_id,
      u.employee_id,
      u.name,
      u.department,
      DATE(al.timestamp) as log_date,
      al.timestamp,
      al.action,
      d.name as device_name,
      al.photo_url
    FROM attendance_logs al
    LEFT JOIN users u ON al.user_id = u.id
    LEFT JOIN devices d ON al.device_id = d.id
    WHERE al.organization_id = org_id
      AND (from_date IS NULL OR al.timestamp >= from_date)
      AND (to_date IS NULL OR al.timestamp <= to_date)
  ),
  first_in AS (
    SELECT DISTINCT ON (user_id, log_date)
      user_id,
      employee_id,
      name,
      department,
      log_date,
      timestamp as check_in_time,
      device_name as check_in_device,
      photo_url as check_in_photo
    FROM daily_logs
    WHERE action = 'check_in'
    ORDER BY user_id, log_date, timestamp ASC
  ),
  last_out AS (
    SELECT DISTINCT ON (user_id, log_date)
      user_id,
      log_date,
      timestamp as check_out_time,
      device_name as check_out_device,
      photo_url as check_out_photo
    FROM daily_logs
    WHERE action = 'check_out'
    ORDER BY user_id, log_date, timestamp DESC
  )
  SELECT 
    fi.user_id,
    fi.employee_id,
    fi.name,
    fi.department,
    fi.log_date as date,
    fi.check_in_time,
    lo.check_out_time,
    fi.check_in_device,
    lo.check_out_device,
    fi.check_in_photo,
    lo.check_out_photo,
    CASE 
      WHEN lo.check_out_time IS NOT NULL THEN
        EXTRACT(EPOCH FROM (lo.check_out_time - fi.check_in_time)) / 3600
      ELSE NULL
    END as hours_worked
  FROM first_in fi
  LEFT JOIN last_out lo ON fi.user_id = lo.user_id AND fi.log_date = lo.log_date
  ORDER BY fi.log_date DESC, fi.check_in_time DESC;
END;
$$;

-- Grant execute permission to authenticated users
GRANT EXECUTE ON FUNCTION get_smart_attendance(UUID, TIMESTAMPTZ, TIMESTAMPTZ) TO authenticated;

-- Test the function
-- SELECT * FROM get_smart_attendance('your-org-id'::UUID);
