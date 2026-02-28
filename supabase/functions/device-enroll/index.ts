import { serve } from "https://deno.land/std@0.168.0/http/server.ts";
import { getSupabaseAdmin, authenticateDevice, auditLog } from "../_shared/auth.ts";
import { handleCors, jsonResponse, errorResponse } from "../_shared/cors.ts";

serve(async (req: Request) => {
  if (req.method === "OPTIONS") return handleCors();

  try {
    const body = await req.json();
    const { device_uid, device_secret, credential_value, credential_type, photo_base64 } = body;

    if (!device_uid || !device_secret || !credential_value) {
      return errorResponse("missing fields: device_uid, device_secret, credential_value", 400);
    }

    const supabase = getSupabaseAdmin();

    // Authenticate device
    const device = await authenticateDevice(supabase, device_uid, device_secret);
    if (!device) {
      console.warn(`[device-enroll] AUTH_FAIL uid=${device_uid}`);
      return errorResponse("unauthorized", 401);
    }

    const orgId = device.organization_id;
    const credType = credential_type || "rfid";

    console.log(`[device-enroll] org=${orgId} device=${device.id} credential=${credential_value}`);

    // Check if this credential is already in enrollment_queue (pending) or user_credentials
    const { data: existingCred } = await supabase
      .from("user_credentials")
      .select("id")
      .eq("organization_id", orgId)
      .eq("type", credType)
      .eq("value", credential_value)
      .maybeSingle();

    if (existingCred) {
      console.log(`[device-enroll] credential already assigned, skipping`);
      return jsonResponse({ status: "already_assigned", credential_value });
    }

    // Check if already pending in enrollment queue
    const { data: existingEnroll } = await supabase
      .from("enrollment_queue")
      .select("id")
      .eq("organization_id", orgId)
      .eq("credential_value", credential_value)
      .eq("status", "pending")
      .maybeSingle();

    if (existingEnroll) {
      console.log(`[device-enroll] credential already pending enrollment`);
      return jsonResponse({ status: "already_pending", enrollment_id: existingEnroll.id });
    }

    // Insert into enrollment queue
    const insertPayload: Record<string, unknown> = {
      organization_id: orgId,
      device_id: device.id,
      credential_type: credType,
      credential_value,
      status: "pending",
    };

    const { data: enroll, error: insertErr } = await supabase
      .from("enrollment_queue")
      .insert(insertPayload)
      .select("id")
      .single();

    if (insertErr) {
      console.error(`[device-enroll] INSERT_FAIL`, insertErr);
      return errorResponse("insert_failed", 500);
    }

    // Optional photo upload
    if (photo_base64 && enroll) {
      try {
        const raw = atob(photo_base64);
        const buf = new Uint8Array(raw.length);
        for (let i = 0; i < raw.length; i++) buf[i] = raw.charCodeAt(i);

        const path = `${orgId}/${device.id}/enrollments/${enroll.id}.jpg`;
        const { error: uploadErr } = await supabase.storage
          .from("attendance-photos")
          .upload(path, buf, { contentType: "image/jpeg", upsert: false });

        if (!uploadErr) {
          await supabase
            .from("enrollment_queue")
            .update({ photo_url: path })
            .eq("id", enroll.id);
        } else {
          console.warn(`[device-enroll] photo upload failed`, uploadErr);
        }
      } catch (photoErr) {
        console.warn(`[device-enroll] photo processing error`, photoErr);
      }
    }

    // Audit log
    auditLog(supabase, {
      organization_id: orgId,
      actor_type: "device",
      actor_id: device.id,
      action: "enrollment.submitted",
      resource_type: "enrollment_queue",
      resource_id: enroll.id,
      metadata: { credential_type: credType, credential_value },
    });

    console.log(`[device-enroll] SUCCESS enrollment_id=${enroll.id}`);
    return jsonResponse({ status: "ok", enrollment_id: enroll.id });

  } catch (err) {
    console.error(`[device-enroll] UNHANDLED`, err);
    return errorResponse("server_error", 500);
  }
});
