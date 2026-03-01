import { createClient } from 'https://esm.sh/@supabase/supabase-js@2';
import { corsHeaders, jsonResponse, errorResponse } from '../_shared/cors.ts';

const SUPABASE_URL = Deno.env.get('SUPABASE_URL')!;
const SUPABASE_SERVICE_ROLE_KEY = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!;

Deno.serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders });
  }

  try {
    const supabase = createClient(SUPABASE_URL, SUPABASE_SERVICE_ROLE_KEY);

    // Get authenticated user
    const authHeader = req.headers.get('Authorization');
    if (!authHeader) {
      return errorResponse('Missing authorization header', 401);
    }

    const token = authHeader.replace('Bearer ', '');
    const { data: { user }, error: userError } = await supabase.auth.getUser(token);
    
    if (userError || !user) {
      return errorResponse('Invalid session', 401);
    }

    const { device_mac, device_name, wifi_ssid, wifi_password } = await req.json();

    if (!device_mac || !wifi_ssid || !wifi_password) {
      return errorResponse('Missing required fields', 400);
    }

    // Get user's organization
    const { data: membership } = await supabase
      .from('org_members')
      .select('organization_id, role')
      .eq('user_id', user.id)
      .single();

    if (!membership) {
      return errorResponse('User not part of any organization', 403);
    }

    const organizationId = membership.organization_id;

    // Check subscription status
    const { data: subscription } = await supabase
      .from('subscriptions')
      .select('status, plan_type, device_limit')
      .eq('organization_id', organizationId)
      .single();

    if (!subscription || subscription.status !== 'active') {
      return errorResponse('No active subscription', 403);
    }

    // Check device limit
    const { count: deviceCount } = await supabase
      .from('devices')
      .select('id', { count: 'exact', head: true })
      .eq('organization_id', organizationId);

    if (deviceCount && deviceCount >= subscription.device_limit) {
      return errorResponse(`Device limit reached (${subscription.device_limit})`, 403);
    }

    // Generate provisioning token
    const provisionToken = crypto.randomUUID().replace(/-/g, '');

    // Create provision token record
    const { data: tokenRecord, error: tokenError } = await supabase
      .from('provision_tokens')
      .insert({
        organization_id: organizationId,
        token: provisionToken,
        device_name: device_name || 'New Device',
        created_by: user.id,
        expires_at: new Date(Date.now() + 10 * 60 * 1000).toISOString(), // 10 minutes
      })
      .select('id')
      .single();

    if (tokenError) {
      console.error('Token creation error:', tokenError);
      return errorResponse('Failed to create provision token', 500);
    }

    // Store claim info for device to poll
    const { error: claimError } = await supabase
      .from('device_claims')
      .insert({
        device_mac,
        organization_id: organizationId,
        provision_token: provisionToken,
        wifi_ssid,
        wifi_password,
        claimed_by: user.id,
        expires_at: new Date(Date.now() + 10 * 60 * 1000).toISOString(),
      });

    if (claimError) {
      console.error('Claim creation error:', claimError);
      return errorResponse('Failed to claim device', 500);
    }

    console.log(`[claim-device] Device ${device_mac} claimed by org ${organizationId}`);

    return jsonResponse({
      success: true,
      message: 'Device claimed successfully. It will provision automatically.',
      device_mac,
    });

  } catch (err) {
    console.error('[claim-device] Error:', err);
    return errorResponse(err.message || 'Internal server error', 500);
  }
});
