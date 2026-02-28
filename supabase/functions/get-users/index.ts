import { getSupabaseAdmin, authenticateDevice, checkSubscriptionActive } from '../_shared/auth.ts';
import { handleCors, jsonResponse, errorResponse } from '../_shared/cors.ts';

/**
 * Edge Function: get-users
 *
 * Called by ESP32 firmware to download the employee list + RFID credentials
 * for the device's organization. Used to populate the local user cache on device.
 *
 * Auth: device_uid + device_secret in JSON body. Service role key internally.
 *
 * POST /functions/v1/get-users
 * Body: { device_uid: string, device_secret: string }
 *
 * 200: {
 *   users: [{ user_id, name, employee_id, department, rfid_uid }],
 *   device_id: string
 * }
 *
 * Response shape matches the old /api/users/:deviceId for firmware compatibility.
 *
 * 401: Invalid device credentials
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

    // 1. Authenticate device
    const device = await authenticateDevice(supabase, device_uid, device_secret);
    if (!device) {
      console.warn(`[get-users] auth failed | device_uid=${device_uid}`);
      return errorResponse('Invalid device credentials', 401);
    }

    console.log(`[get-users] request | org=${device.organization_id} device=${device.id}`);

    // 2. Check subscription is active
    const subActive = await checkSubscriptionActive(supabase, device.organization_id);
    if (!subActive) {
      console.warn(`[get-users] subscription inactive | org=${device.organization_id}`);
      return errorResponse('Subscription inactive', 403);
    }

    // 3. Fetch active users for this organization
    const { data: users, error: usersErr } = await supabase
      .from('users')
      .select('id, name, employee_id, department')
      .eq('organization_id', device.organization_id)
      .eq('active', true);

    if (usersErr) {
      console.error('Users fetch error:', usersErr);
      return errorResponse('Failed to fetch users', 500);
    }

    // 3. Fetch RFID credentials for these users
    const userIds = (users || []).map((u: { id: string }) => u.id);

    let credentials: { user_id: string; value: string }[] = [];
    if (userIds.length > 0) {
      const { data: creds, error: credsErr } = await supabase
        .from('user_credentials')
        .select('user_id, value')
        .eq('organization_id', device.organization_id)
        .eq('type', 'rfid')
        .in('user_id', userIds);

      if (credsErr) {
        console.error('Credentials fetch error:', credsErr);
        return errorResponse('Failed to fetch credentials', 500);
      }
      credentials = creds || [];
    }

    // 4. Build credential lookup map
    const rfidMap = new Map<string, string>();
    for (const c of credentials) {
      rfidMap.set(c.user_id, c.value);
    }

    // 5. Build response matching old firmware-expected shape:
    //    { users: [{ user_id, name, employee_id, department, rfid_uid }], device_id }
    const result = (users || [])
      .filter((u: { id: string }) => rfidMap.has(u.id))
      .map((u: { id: string; name: string; employee_id: string; department: string | null }) => ({
        user_id: u.id,
        name: u.name,
        employee_id: u.employee_id,
        department: u.department,
        rfid_uid: rfidMap.get(u.id),
      }));

    console.log(`[get-users] success | org=${device.organization_id} users=${result.length}`);

    return jsonResponse({
      users: result,
      device_id: device.id,
    });
  } catch (err) {
    console.error('get-users error:', err);
    return errorResponse('Internal server error', 500);
  }
});
