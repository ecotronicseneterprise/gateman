import { serve } from "https://deno.land/std@0.168.0/http/server.ts";
import { getSupabaseAdmin, auditLog } from "../_shared/auth.ts";
import { handleCors, jsonResponse, errorResponse } from "../_shared/cors.ts";

/**
 * start-enrollment — Admin initiates card enrollment for an employee.
 *
 * Body: { user_id, device_id, organization_id }
 * Auth: Supabase JWT (admin/owner)
 *
 * Creates a 'waiting' record in enrollment_queue.
 * The device polls check-enrollment and enters enroll mode when it sees this.
 */
serve(async (req: Request) => {
  const corsResp = handleCors(req);
  if (corsResp) return corsResp;

  try {
    const supabase = getSupabaseAdmin();
    const body = await req.json();
    const { user_id, device_id, organization_id, caller_user_id } = body;

    if (!user_id || !device_id || !organization_id || !caller_user_id) {
      return errorResponse("missing fields: user_id, device_id, organization_id, caller_user_id", 400);
    }

    // Verify caller is admin/owner of this org
    const { data: membership } = await supabase
      .from("org_members")
      .select("role")
      .eq("user_id", caller_user_id)
      .eq("organization_id", organization_id)
      .in("role", ["owner", "admin"])
      .maybeSingle();

    if (!membership) return errorResponse("forbidden", 403);

    // Verify device belongs to this org
    const { data: device } = await supabase
      .from("devices")
      .select("id, name")
      .eq("id", device_id)
      .eq("organization_id", organization_id)
      .eq("status", "active")
      .maybeSingle();

    if (!device) return errorResponse("device not found or inactive", 404);

    // Cancel any existing 'waiting' enrollments for this org+device (only one at a time)
    await supabase
      .from("enrollment_queue")
      .update({ status: "rejected", resolved_at: new Date().toISOString() })
      .eq("organization_id", organization_id)
      .eq("device_id", device_id)
      .eq("status", "waiting");

    // Insert waiting enrollment
    const { data: enroll, error: insertErr } = await supabase
      .from("enrollment_queue")
      .insert({
        organization_id,
        device_id,
        credential_type: "rfid",
        credential_value: null,
        status: "waiting",
        assigned_to: user_id,
      })
      .select("id")
      .single();

    if (insertErr) {
      console.error("[start-enrollment] INSERT_FAIL", insertErr);
      return errorResponse("insert_failed: " + insertErr.message, 500);
    }

    // Audit
    auditLog(supabase, {
      organization_id,
      actor_type: "user",
      actor_id: caller_user_id,
      action: "enrollment.initiated",
      resource_type: "enrollment_queue",
      resource_id: enroll.id,
      metadata: { target_user_id: user_id, device_id },
    });

    console.log(`[start-enrollment] OK enrollment_id=${enroll.id} user=${user_id} device=${device_id}`);
    return jsonResponse({ status: "waiting", enrollment_id: enroll.id });

  } catch (err) {
    console.error("[start-enrollment] UNHANDLED", err);
    return errorResponse("server_error", 500);
  }
});
