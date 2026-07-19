import { serve } from "https://deno.land/std@0.168.0/http/server.ts";
import { createClient } from "https://esm.sh/@supabase/supabase-js@2";
import { getSupabaseAdmin, auditLog, checkRateLimit } from "../_shared/auth.ts";
import { handleCors, jsonResponse, errorResponse } from "../_shared/cors.ts";

/**
 * start-enrollment — Admin initiates card enrollment for an employee.
 *
 * Body: { user_id, device_id, organization_id }
 * Auth: Supabase JWT (admin/owner) — verified via Authorization header.
 *
 * Creates a 'waiting' record in enrollment_queue.
 * The device polls check-enrollment and enters enroll mode when it sees this.
 */
serve(async (req: Request) => {
  const corsResp = handleCors(req);
  if (corsResp) return corsResp;

  try {
    // Authenticate caller via JWT (mirrors create-checkout / create-provision-token)
    const authHeader = req.headers.get("Authorization");
    if (!authHeader) return errorResponse("missing authorization", 401);

    const supabaseUser = createClient(
      Deno.env.get("SUPABASE_URL")!,
      Deno.env.get("SUPABASE_ANON_KEY")!,
      { global: { headers: { Authorization: authHeader } } }
    );

    const { data: { user }, error: authErr } = await supabaseUser.auth.getUser();
    if (authErr || !user) return errorResponse("unauthorized", 401);

    const supabase = getSupabaseAdmin();
    const body = await req.json();
    const { user_id, device_id, organization_id } = body;

    if (!user_id || !device_id || !organization_id) {
      return errorResponse("missing fields: user_id, device_id, organization_id", 400);
    }

    // Verify caller is admin/owner of this org — caller identity now comes from
    // the verified JWT (user.id), not a client-supplied caller_user_id field.
    const { data: membership } = await supabase
      .from("org_members")
      .select("role")
      .eq("user_id", user.id)
      .eq("organization_id", organization_id)
      .in("role", ["owner", "admin"])
      .maybeSingle();

    if (!membership) return errorResponse("forbidden", 403);

    // Rate limit: max 20 enrollment starts per org per 5 minutes — generous
    // enough for onboarding a batch of employees, tight enough to block abuse.
    const rateLimited = await checkRateLimit(supabase, {
      organization_id,
      actor_id: user.id,
      action: "enrollment.initiated",
      maxCount: 20,
      windowMinutes: 5,
    });
    if (rateLimited) {
      return errorResponse("Too many enrollment requests. Try again in a few minutes.", 429);
    }

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
      actor_id: user.id,
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
