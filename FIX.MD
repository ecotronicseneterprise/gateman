[
  {
    "table_name": "attendance_logs",
    "column_name": "id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": "gen_random_uuid()"
  },
  {
    "table_name": "attendance_logs",
    "column_name": "organization_id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "attendance_logs",
    "column_name": "device_id",
    "data_type": "uuid",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "attendance_logs",
    "column_name": "user_id",
    "data_type": "uuid",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "attendance_logs",
    "column_name": "credential_value",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "attendance_logs",
    "column_name": "action",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "attendance_logs",
    "column_name": "device_event_id",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "attendance_logs",
    "column_name": "timestamp",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "attendance_logs",
    "column_name": "photo_url",
    "data_type": "text",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "attendance_logs",
    "column_name": "synced_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "audit_logs",
    "column_name": "id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": "gen_random_uuid()"
  },
  {
    "table_name": "audit_logs",
    "column_name": "organization_id",
    "data_type": "uuid",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "audit_logs",
    "column_name": "actor_type",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "audit_logs",
    "column_name": "actor_id",
    "data_type": "text",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "audit_logs",
    "column_name": "action",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "audit_logs",
    "column_name": "resource_type",
    "data_type": "text",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "audit_logs",
    "column_name": "resource_id",
    "data_type": "text",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "audit_logs",
    "column_name": "metadata",
    "data_type": "jsonb",
    "is_nullable": "YES",
    "column_default": "'{}'::jsonb"
  },
  {
    "table_name": "audit_logs",
    "column_name": "ip_address",
    "data_type": "text",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "audit_logs",
    "column_name": "created_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "device_claims",
    "column_name": "id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": "gen_random_uuid()"
  },
  {
    "table_name": "device_claims",
    "column_name": "device_mac",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "device_claims",
    "column_name": "organization_id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "device_claims",
    "column_name": "provision_token",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "device_claims",
    "column_name": "wifi_ssid",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "device_claims",
    "column_name": "wifi_password",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "device_claims",
    "column_name": "claimed_by",
    "data_type": "uuid",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "device_claims",
    "column_name": "claimed",
    "data_type": "boolean",
    "is_nullable": "YES",
    "column_default": "false"
  },
  {
    "table_name": "device_claims",
    "column_name": "expires_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "device_claims",
    "column_name": "created_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "YES",
    "column_default": "now()"
  },
  {
    "table_name": "device_claims",
    "column_name": "updated_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "YES",
    "column_default": "now()"
  },
  {
    "table_name": "devices",
    "column_name": "id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": "gen_random_uuid()"
  },
  {
    "table_name": "devices",
    "column_name": "organization_id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "devices",
    "column_name": "device_uid",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "devices",
    "column_name": "device_secret",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "devices",
    "column_name": "name",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": "'New Device'::text"
  },
  {
    "table_name": "devices",
    "column_name": "location",
    "data_type": "text",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "devices",
    "column_name": "status",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": "'active'::text"
  },
  {
    "table_name": "devices",
    "column_name": "firmware_version",
    "data_type": "text",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "devices",
    "column_name": "last_seen",
    "data_type": "timestamp with time zone",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "devices",
    "column_name": "created_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "devices",
    "column_name": "updated_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "enrollment_queue",
    "column_name": "id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": "gen_random_uuid()"
  },
  {
    "table_name": "enrollment_queue",
    "column_name": "organization_id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "enrollment_queue",
    "column_name": "device_id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "enrollment_queue",
    "column_name": "credential_type",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": "'rfid'::text"
  },
  {
    "table_name": "enrollment_queue",
    "column_name": "credential_value",
    "data_type": "text",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "enrollment_queue",
    "column_name": "photo_url",
    "data_type": "text",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "enrollment_queue",
    "column_name": "status",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": "'pending'::text"
  },
  {
    "table_name": "enrollment_queue",
    "column_name": "assigned_to",
    "data_type": "uuid",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "enrollment_queue",
    "column_name": "created_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "enrollment_queue",
    "column_name": "resolved_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "org_members",
    "column_name": "id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": "gen_random_uuid()"
  },
  {
    "table_name": "org_members",
    "column_name": "organization_id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "org_members",
    "column_name": "user_id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "org_members",
    "column_name": "role",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": "'viewer'::text"
  },
  {
    "table_name": "org_members",
    "column_name": "created_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "organizations",
    "column_name": "id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": "gen_random_uuid()"
  },
  {
    "table_name": "organizations",
    "column_name": "name",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "organizations",
    "column_name": "slug",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "organizations",
    "column_name": "settings",
    "data_type": "jsonb",
    "is_nullable": "YES",
    "column_default": "'{}'::jsonb"
  },
  {
    "table_name": "organizations",
    "column_name": "created_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "organizations",
    "column_name": "updated_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "payment_references",
    "column_name": "id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": "gen_random_uuid()"
  },
  {
    "table_name": "payment_references",
    "column_name": "reference",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "payment_references",
    "column_name": "organization_id",
    "data_type": "uuid",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "payment_references",
    "column_name": "event_type",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "payment_references",
    "column_name": "created_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "provision_tokens",
    "column_name": "id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": "gen_random_uuid()"
  },
  {
    "table_name": "provision_tokens",
    "column_name": "organization_id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "provision_tokens",
    "column_name": "token",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "provision_tokens",
    "column_name": "device_name",
    "data_type": "text",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "provision_tokens",
    "column_name": "expires_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "(now() + '00:10:00'::interval)"
  },
  {
    "table_name": "provision_tokens",
    "column_name": "used_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "provision_tokens",
    "column_name": "used_by_device_id",
    "data_type": "uuid",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "provision_tokens",
    "column_name": "created_by",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "provision_tokens",
    "column_name": "created_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "subscriptions",
    "column_name": "id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": "gen_random_uuid()"
  },
  {
    "table_name": "subscriptions",
    "column_name": "organization_id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "subscriptions",
    "column_name": "plan_type",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "subscriptions",
    "column_name": "status",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": "'active'::text"
  },
  {
    "table_name": "subscriptions",
    "column_name": "device_limit",
    "data_type": "integer",
    "is_nullable": "NO",
    "column_default": "1"
  },
  {
    "table_name": "subscriptions",
    "column_name": "user_limit",
    "data_type": "integer",
    "is_nullable": "NO",
    "column_default": "50"
  },
  {
    "table_name": "subscriptions",
    "column_name": "retention_days",
    "data_type": "integer",
    "is_nullable": "NO",
    "column_default": "180"
  },
  {
    "table_name": "subscriptions",
    "column_name": "trial_ends_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "subscriptions",
    "column_name": "current_period_start",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "subscriptions",
    "column_name": "current_period_end",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "(now() + '30 days'::interval)"
  },
  {
    "table_name": "subscriptions",
    "column_name": "paystack_reference",
    "data_type": "text",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "subscriptions",
    "column_name": "created_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "subscriptions",
    "column_name": "updated_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "user_credentials",
    "column_name": "id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": "gen_random_uuid()"
  },
  {
    "table_name": "user_credentials",
    "column_name": "user_id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "user_credentials",
    "column_name": "organization_id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "user_credentials",
    "column_name": "type",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "user_credentials",
    "column_name": "value",
    "data_type": "text",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "user_credentials",
    "column_name": "created_at",
    "data_type": "timestamp with time zone",
    "is_nullable": "NO",
    "column_default": "now()"
  },
  {
    "table_name": "users",
    "column_name": "id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": "gen_random_uuid()"
  },
  {
    "table_name": "users",
    "column_name": "instance_id",
    "data_type": "uuid",
    "is_nullable": "YES",
    "column_default": null
  },
  {
    "table_name": "users",
    "column_name": "organization_id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  },
  {
    "table_name": "users",
    "column_name": "id",
    "data_type": "uuid",
    "is_nullable": "NO",
    "column_default": null
  }
]


``bash
# SSH into server
ssh deploy@46.225.186.103
cd /var/www/gateman
npx supabase functions deploy device-provision --project-ref ueobebsgheecclwcbigy --no-verify-jwt

npx supabase functions deploy get-users --project-ref ueobebsgheecclwcbigy --no-verify-jwt


above is the table schema and some codes i often use

### Status
- Firmware is resolving RFID UIDs to real employees (`Cherry`, `Chukwuma Nwanna`) and alternating check‑in/check‑out cycles correctly.
- Each tap triggers CAM capture (`📷`), device sync, and Supabase date updates—showing both photo ingestion and server time alignment are healthy.
- Supabase functions with `--no-verify-jwt` are evidently live; `check-enrollment` and `device-enroll` are adding credentials, and `get-users` is now returning the populated list.

### Recommended regression checks
1. **Dashboard parity**
   - Confirm the dashboard Attendance or Audit views reflect the four taps above (timestamps and in/out states).
2. **Offline queue**
   - Simulate a WiFi outage, tap a card, then reconnect to ensure queued events flush without duplicates.
3. **Enrollment retry**
   - Run one more dashboard-driven enrollment to ensure the LED enters enroll mode and a fresh card UID reaches `user_credentials`.
4. **Photo retrieval**
   - Open the dashboard photo viewer for a newly synced log to verify signed URLs resolve within 5 minutes.
5. **Multi-device guard (if applicable)**
   - If another device exists under the same org, make sure it only receives its own enroll commands and cannot see others’ logs.

### Risks / considerations
- **Edge Function drift**: re‑deploying without `--no-verify-jwt` in future updates would break device auth; document the flag in deploy scripts.
- **Empty credentials**: If someone deletes rows from `user_credentials`, `get-users` reverts to zero results; add a monitoring alert if desired.

### Next actions
1. Perform the regression checks above (especially dashboard verification and offline queue test).
2. Capture screenshots/logs for the full end-to-end run as evidence for stakeholders.
3. Update the deployment checklist to include the `--no-verify-jwt` requirement for all device functions.

### Open Questions
1. Do you want automated alerts (e.g., Supabase function log triggers) when a device reports `0 loaded` users after a sync?
2. Should we add a manual enrollment fallback (serial command) for field technicians when the dashboard isn’t reachable?