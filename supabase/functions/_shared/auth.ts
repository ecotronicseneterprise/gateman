import { createClient, SupabaseClient } from 'https://esm.sh/@supabase/supabase-js@2';

// SHA-256 hash function (matches device-provision)
async function hashSecret(secret: string): Promise<string> {
  const encoder = new TextEncoder();
  const data = encoder.encode(secret);
  const hashBuffer = await crypto.subtle.digest('SHA-256', data);
  const hashArray = Array.from(new Uint8Array(hashBuffer));
  return hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
}

export function getSupabaseAdmin(): SupabaseClient {
  return createClient(
    Deno.env.get('SUPABASE_URL')!,
    Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
  );
}

export interface DeviceRecord {
  id: string;
  organization_id: string;
  device_uid: string;
  device_secret: string;
  name: string;
  status: string;
  last_seen: string | null;
}

/**
 * Authenticate a device by device_uid + plaintext device_secret.
 * Uses service role key (bypasses RLS).
 * Returns the device record if valid, null otherwise.
 */
export async function authenticateDevice(
  supabase: SupabaseClient,
  deviceUid: string,
  deviceSecret: string
): Promise<DeviceRecord | null> {
  if (!deviceUid || !deviceSecret) return null;

  const { data: device, error } = await supabase
    .from('devices')
    .select('id, organization_id, device_uid, device_secret, name, status, last_seen')
    .eq('device_uid', deviceUid)
    .eq('status', 'active')
    .single();

  if (error || !device) return null;

  // Hash the provided secret and compare with stored hash
  const hashedSecret = await hashSecret(deviceSecret);
  const secretValid = hashedSecret === device.device_secret;
  if (!secretValid) return null;

  // Update last_seen (fire-and-forget)
  supabase
    .from('devices')
    .update({ last_seen: new Date().toISOString() })
    .eq('id', device.id)
    .then(() => {});

  return device as DeviceRecord;
}

/**
 * Check if organization subscription is active or trial.
 */
export async function checkSubscriptionActive(
  supabase: SupabaseClient,
  organizationId: string
): Promise<boolean> {
  const { data } = await supabase
    .from('subscriptions')
    .select('status, trial_ends_at')
    .eq('organization_id', organizationId)
    .in('status', ['active', 'trial'])
    .single();

  if (!data) return false;

  if (data.status === 'trial' && data.trial_ends_at) {
    return new Date(data.trial_ends_at) > new Date();
  }

  return true;
}

/**
 * Insert an audit log entry. Fire-and-forget — never blocks the caller.
 */
export function auditLog(
  supabase: SupabaseClient,
  params: {
    organization_id: string | null;
    actor_type: 'user' | 'device' | 'system';
    actor_id?: string;
    action: string;
    resource_type?: string;
    resource_id?: string;
    metadata?: Record<string, unknown>;
    ip_address?: string;
  }
): void {
  supabase
    .from('audit_logs')
    .insert({
      organization_id: params.organization_id,
      actor_type: params.actor_type,
      actor_id: params.actor_id || null,
      action: params.action,
      resource_type: params.resource_type || null,
      resource_id: params.resource_id || null,
      metadata: params.metadata || {},
      ip_address: params.ip_address || null,
    })
    .then(({ error }) => {
      if (error) console.error('audit_log insert failed:', error.message);
    });
}

/**
 * Simple rate limiter using audit_logs count within a time window.
 * Returns true if the action count exceeds maxCount in windowMinutes.
 */
export async function checkRateLimit(
  supabase: SupabaseClient,
  params: {
    organization_id: string | null;
    actor_id: string;
    action: string;
    maxCount: number;
    windowMinutes: number;
  }
): Promise<boolean> {
  const windowStart = new Date(Date.now() - params.windowMinutes * 60 * 1000).toISOString();

  let query = supabase
    .from('audit_logs')
    .select('*', { count: 'exact', head: true })
    .eq('action', params.action)
    .eq('actor_id', params.actor_id)
    .gte('created_at', windowStart);

  if (params.organization_id) {
    query = query.eq('organization_id', params.organization_id);
  } else {
    query = query.is('organization_id', null);
  }

  const { count } = await query;
  return (count ?? 0) >= params.maxCount;
}
