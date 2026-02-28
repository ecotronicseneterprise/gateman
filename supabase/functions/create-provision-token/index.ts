import { createClient } from 'https://esm.sh/@supabase/supabase-js@2';
import { handleCors, jsonResponse, errorResponse } from '../_shared/cors.ts';
import { auditLog, checkRateLimit } from '../_shared/auth.ts';

/**
 * Edge Function: create-provision-token
 *
 * Called by dashboard (authenticated admin/owner) to generate a single-use
 * provisioning token for a new device. Returns token + QR payload.
 *
 * Auth: Supabase Auth JWT (anon key + session). Validates org membership + role.
 *
 * POST /functions/v1/create-provision-token
 * Headers: Authorization: Bearer <supabase_session_token>
 * Body: { device_name?: string, organization_id: string }
 *
 * 200: { token, expires_at, qr_payload, provision_url }
 * 401: Not authenticated
 * 403: Not owner/admin of this org, or device limit reached
 * 500: Internal error
 */
Deno.serve(async (req: Request) => {
  const cors = handleCors(req);
  if (cors) return cors;

  try {
    // Create anon client with Authorization header from request
    // Supabase gateway already validated the JWT - we just need to pass it through
    const authHeader = req.headers.get('Authorization') || '';
    const supabaseClient = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_ANON_KEY')!,
      { global: { headers: { Authorization: authHeader } } }
    );

    // Get current user - this uses the JWT that was already validated by the gateway
    const { data: { user }, error: userErr } = await supabaseClient.auth.getUser();
    
    if (userErr || !user) {
      console.error('[create-provision-token] auth failed:', userErr?.message);
      return errorResponse('Unauthorized - please sign in', 401);
    }

    const userId = user.id;
    console.log('[create-provision-token] authenticated user:', userId);

    // Use service role for privileged operations
    const supabase = createClient(
      Deno.env.get('SUPABASE_URL')!,
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
    );

    const { device_name, organization_id } = await req.json();
    if (!organization_id) {
      return errorResponse('organization_id required', 400);
    }

    // 1. Verify user is owner or admin of the org
    const { data: membership } = await supabase
      .from('org_members')
      .select('role')
      .eq('organization_id', organization_id)
      .eq('user_id', userId)
      .in('role', ['owner', 'admin'])
      .single();

    if (!membership) {
      console.warn(`[create-provision-token] unauthorized | user=${userId} org=${organization_id}`);
      return errorResponse('Not authorized: must be org owner or admin', 403);
    }

    // Rate limit: max 10 tokens per org per hour
    const rateLimited = await checkRateLimit(supabase, {
      organization_id: organization_id,
      actor_id: userId,
      action: 'provision_token.created',
      maxCount: 10,
      windowMinutes: 60,
    });
    if (rateLimited) {
      console.warn(`[create-provision-token] rate limited | org=${organization_id} user=${userId}`);
      return errorResponse('Rate limit exceeded. Max 10 provisioning tokens per hour.', 429);
    }

    // 2. Check device limit
    const { count: deviceCount } = await supabase
      .from('devices')
      .select('*', { count: 'exact', head: true })
      .eq('organization_id', organization_id)
      .in('status', ['active', 'inactive']);

    const { data: sub } = await supabase
      .from('subscriptions')
      .select('device_limit, status, trial_ends_at')
      .eq('organization_id', organization_id)
      .in('status', ['active', 'trial'])
      .single();

    if (!sub) {
      return errorResponse('No active subscription', 403);
    }

    if (sub.status === 'trial' && sub.trial_ends_at && new Date(sub.trial_ends_at) < new Date()) {
      return errorResponse('Trial expired. Please upgrade.', 403);
    }

    if (deviceCount !== null && deviceCount >= sub.device_limit) {
      return errorResponse(
        `Device limit reached (${sub.device_limit}). Upgrade to add more devices.`,
        403
      );
    }

    // 3. Generate single-use token (32-char hex)
    const tokenBytes = new Uint8Array(16);
    crypto.getRandomValues(tokenBytes);
    const token = Array.from(tokenBytes).map(b => b.toString(16).padStart(2, '0')).join('');

    const expiresAt = new Date(Date.now() + 10 * 60 * 1000).toISOString(); // 10 minutes

    // 4. Store token
    const { error: insertErr } = await supabase
      .from('provision_tokens')
      .insert({
        organization_id: organization_id,
        token: token,
        device_name: device_name || null,
        expires_at: expiresAt,
        created_by: userId,
      });

    if (insertErr) {
      console.error('Token insert error:', insertErr);
      return errorResponse('Failed to create provision token', 500);
    }

    // 5. Build QR payload
    const supabaseUrl = Deno.env.get('SUPABASE_URL')!;
    const qrPayload = JSON.stringify({
      token: token,
      url: supabaseUrl,
    });

    const provisionUrl = `${supabaseUrl}/functions/v1/device-provision`;

    console.log(`[create-provision-token] success | org=${organization_id} user=${userId} token_prefix=${token.substring(0, 8)}...`);

    auditLog(supabase, {
      organization_id: organization_id,
      actor_type: 'user',
      actor_id: userId,
      action: 'provision_token.created',
      resource_type: 'provision_token',
      metadata: { device_name: device_name, expires_at: expiresAt },
    });

    return jsonResponse({
      token: token,
      expires_at: expiresAt,
      qr_payload: qrPayload,
      provision_url: provisionUrl,
    });
  } catch (err) {
    console.error('create-provision-token error:', err);
    return errorResponse('Internal server error', 500);
  }
});
