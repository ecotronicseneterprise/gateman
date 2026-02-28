import { getSupabaseAdmin, auditLog } from '../_shared/auth.ts';
import { handleCors, jsonResponse, errorResponse } from '../_shared/cors.ts';
import { hash } from 'https://deno.land/x/bcrypt@v0.4.1/mod.ts';

/**
 * Edge Function: device-provision
 *
 * Called by ESP32 firmware during first boot.
 * Validates a single-use provisioning token, creates a device record
 * with a bcrypt-hashed secret, and returns the plaintext secret ONCE.
 *
 * Auth: None (token-based). Uses service role key internally.
 *
 * POST /functions/v1/device-provision
 * Body: { device_uid: string, provisioning_token: string }
 *
 * 200: { device_secret, device_id, supabase_url }
 * 401: Invalid or expired token
 * 403: Device limit reached
 * 409: Device already provisioned
 * 500: Internal error
 */
Deno.serve(async (req: Request) => {
  const cors = handleCors(req);
  if (cors) return cors;

  try {
    const { device_uid, provisioning_token } = await req.json();

    if (!device_uid || !provisioning_token) {
      return errorResponse('device_uid and provisioning_token required', 400);
    }

    const supabase = getSupabaseAdmin();

    // 1. Validate provisioning token (single-use, not expired)
    const { data: token, error: tokenErr } = await supabase
      .from('provision_tokens')
      .select('*')
      .eq('token', provisioning_token)
      .is('used_at', null)
      .gt('expires_at', new Date().toISOString())
      .single();

    if (tokenErr || !token) {
      console.warn(`[device-provision] invalid token attempt | device_uid=${device_uid}`);
      return errorResponse('Invalid or expired provisioning token', 401);
    }

    console.log(`[device-provision] valid token | org=${token.organization_id} device_uid=${device_uid}`);

    // 2. Check device not already provisioned
    const { data: existing } = await supabase
      .from('devices')
      .select('id')
      .eq('device_uid', device_uid)
      .single();

    if (existing) {
      console.warn(`[device-provision] duplicate device_uid=${device_uid}`);
      return errorResponse('Device already provisioned', 409);
    }

    // 3. Check device limit for org's subscription plan
    const { count: deviceCount } = await supabase
      .from('devices')
      .select('*', { count: 'exact', head: true })
      .eq('organization_id', token.organization_id)
      .in('status', ['active', 'inactive']);

    const { data: sub } = await supabase
      .from('subscriptions')
      .select('device_limit, status, trial_ends_at')
      .eq('organization_id', token.organization_id)
      .in('status', ['active', 'trial'])
      .single();

    if (!sub) {
      return errorResponse('No active subscription for this organization', 403);
    }

    if (sub.status === 'trial' && sub.trial_ends_at && new Date(sub.trial_ends_at) < new Date()) {
      return errorResponse('Trial expired', 403);
    }

    if (deviceCount !== null && deviceCount >= sub.device_limit) {
      return errorResponse(
        `Device limit reached (${sub.device_limit}). Upgrade your plan to add more devices.`,
        403
      );
    }

    // 4. Generate device secret — plaintext returned once, bcrypt hash stored
    const rawSecret = crypto.randomUUID() + '-' + crypto.randomUUID();
    const hashedSecret = await hash(rawSecret, 10);

    // 5. Create device record
    const { data: device, error: insertErr } = await supabase
      .from('devices')
      .insert({
        organization_id: token.organization_id,
        device_uid: device_uid,
        device_secret: hashedSecret,
        name: token.device_name || 'New Device',
        status: 'active',
        last_seen: new Date().toISOString(),
      })
      .select('id')
      .single();

    if (insertErr || !device) {
      console.error('Device insert failed:', insertErr);
      return errorResponse('Provisioning failed', 500);
    }

    // 6. Mark token as used — conditional update prevents race condition
    const { data: markResult, error: markErr } = await supabase
      .from('provision_tokens')
      .update({
        used_at: new Date().toISOString(),
        used_by_device_id: device.id,
      })
      .eq('id', token.id)
      .is('used_at', null)
      .select('id');

    if (markErr || !markResult || markResult.length === 0) {
      console.warn(`[device-provision] token race condition — already used | token_id=${token.id}`);
      return errorResponse('Token already used', 409);
    }

    console.log(`[device-provision] success | org=${token.organization_id} device_id=${device.id} device_uid=${device_uid}`);

    auditLog(supabase, {
      organization_id: token.organization_id,
      actor_type: 'device',
      actor_id: device_uid,
      action: 'device.provisioned',
      resource_type: 'device',
      resource_id: device.id,
      metadata: { device_name: token.device_name, token_id: token.id },
    });

    // 7. Return plaintext secret to device (ONLY TIME this is sent)
    return jsonResponse({
      device_secret: rawSecret,
      device_id: device.id,
      supabase_url: Deno.env.get('SUPABASE_URL'),
    });
  } catch (err) {
    console.error('device-provision error:', err);
    return errorResponse('Internal server error', 500);
  }
});
