import { serve } from "https://deno.land/std@0.168.0/http/server.ts";
import { getSupabaseAdmin, authenticateDevice } from "../_shared/auth.ts";
import { handleCors, jsonResponse, errorResponse } from "../_shared/cors.ts";

/**
 * check-enrollment — Device polls this to see if admin has requested a card enrollment.
 *
 * Body: { device_uid, device_secret }
 * Returns: { enroll: true, enrollment_id } if a 'waiting' record exists, or { enroll: false }.
 */
serve(async (req: Request) => {
  const corsResp = handleCors(req);
  if (corsResp) return corsResp;

  try {
    const body = await req.json();
    const { device_uid, device_secret } = body;

    if (!device_uid || !device_secret) {
      return errorResponse("missing device_uid or device_secret", 400);
    }

    const supabase = getSupabaseAdmin();

    // Authenticate device
    const device = await authenticateDevice(supabase, device_uid, device_secret);
    if (!device) {
      return errorResponse("unauthorized", 401);
    }

    // Check for any 'waiting' enrollment for this device
    const { data: waiting } = await supabase
      .from("enrollment_queue")
      .select("id, assigned_to, credential_type")
      .eq("organization_id", device.organization_id)
      .eq("device_id", device.id)
      .eq("status", "waiting")
      .order("created_at", { ascending: false })
      .limit(1)
      .maybeSingle();

    if (!waiting) {
      return jsonResponse({ enroll: false });
    }

    return jsonResponse({
      enroll: true,
      enrollment_id: waiting.id,
      assigned_to: waiting.assigned_to,
      credential_type: waiting.credential_type || "rfid",
    });

  } catch (err) {
    console.error("[check-enrollment] UNHANDLED", err);
    return errorResponse("server_error", 500);
  }
});
