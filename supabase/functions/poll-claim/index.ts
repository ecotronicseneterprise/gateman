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
    const { device_mac } = await req.json();

    if (!device_mac) {
      return errorResponse('Missing device_mac', 400);
    }

    // Check if device has been claimed
    const { data: claim, error: claimError } = await supabase
      .from('device_claims')
      .select('*')
      .eq('device_mac', device_mac)
      .eq('claimed', false)
      .gt('expires_at', new Date().toISOString())
      .order('created_at', { ascending: false })
      .limit(1)
      .single();

    if (claimError || !claim) {
      return jsonResponse({ claimed: false });
    }

    // Mark as claimed
    await supabase
      .from('device_claims')
      .update({ claimed: true, updated_at: new Date().toISOString() })
      .eq('id', claim.id);

    console.log(`[poll-claim] Device ${device_mac} retrieved claim info`);

    return jsonResponse({
      claimed: true,
      provision_token: claim.provision_token,
      wifi_ssid: claim.wifi_ssid,
      wifi_password: claim.wifi_password,
    });

  } catch (err) {
    console.error('[poll-claim] Error:', err);
    return errorResponse(err.message || 'Internal server error', 500);
  }
});
