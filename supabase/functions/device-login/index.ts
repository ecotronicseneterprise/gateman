import { getSupabaseAdmin, authenticateDevice, auditLog, checkRateLimit } from '../_shared/auth.ts';
import { handleCors, jsonResponse, errorResponse } from '../_shared/cors.ts';

/**
 * Edge Function: device-login
 *
 * Called by ESP32 firmware on boot (after provisioning) to validate credentials
 * and get org context. Lightweight auth check — no data returned beyond IDs.
 *
 * Auth: device_uid + device_secret in JSON body. Service role key internally.
 *
 * POST /functions/v1/device-login
 * Body: { device_uid: string, device_secret: string }
 *
 * 200: { status: "ok", device_id, organization_id }
 * 401: Invalid credentials or device revoked
 * 500: Internal error
 */
Deno.serve(async (req: Request) => {
  const cors = handleCors(req);
  if (cors) return cors;

  try {
    const { device_uid, device_secret } = await req.json();

    if (!device_uid || !device_secret) {
      return errorResponse('device_uid and device_secret required', 400);
    }

    const supabase = getSupabaseAdmin();

    // Check brute-force rate limit (5 failed attempts per 5 minutes per device_uid)
    const rateLimited = await checkRateLimit(supabase, {
      organization_id: null,
      actor_id: device_uid,
      action: 'device.login_failed',
      maxCount: 5,
      windowMinutes: 5,
    });
    if (rateLimited) {
      console.warn(`[device-login] rate limited | device_uid=${device_uid}`);
      return errorResponse('Too many failed login attempts. Try again later.', 429);
    }

    const device = await authenticateDevice(supabase, device_uid, device_secret);

    if (!device) {
      console.warn(`[device-login] auth failed | device_uid=${device_uid}`);

      // Audit log the failed attempt
      auditLog(supabase, {
        organization_id: null,
        actor_type: 'device',
        actor_id: device_uid,
        action: 'device.login_failed',
        metadata: { device_uid },
      });

      return errorResponse('Invalid device credentials', 401);
    }

    console.log(`[device-login] success | org=${device.organization_id} device_id=${device.id}`);

    auditLog(supabase, {
      organization_id: device.organization_id,
      actor_type: 'device',
      actor_id: device.id,
      action: 'device.login',
      resource_type: 'device',
      resource_id: device.id,
    });

    return jsonResponse({
      status: 'ok',
      device_id: device.id,
      organization_id: device.organization_id,
    });
  } catch (err) {
    console.error('device-login error:', err);
    return errorResponse('Internal server error', 500);
  }
});
