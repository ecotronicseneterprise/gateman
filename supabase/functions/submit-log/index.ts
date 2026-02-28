import { getSupabaseAdmin, authenticateDevice, checkSubscriptionActive, auditLog, checkRateLimit } from '../_shared/auth.ts';
import { handleCors, jsonResponse, errorResponse } from '../_shared/cors.ts';
import { decode as base64Decode } from 'https://deno.land/std@0.208.0/encoding/base64.ts';

/**
 * Edge Function: submit-log
 *
 * Called by ESP32 firmware for each attendance event (per-record sync).
 * Authenticates device, resolves RFID → user, inserts attendance log
 * with idempotent device_event_id, and optionally uploads photo.
 *
 * Auth: device_uid + device_secret in JSON body. Service role key internally.
 *
 * POST /functions/v1/submit-log
 * Body: {
 *   device_uid: string,
 *   device_secret: string,
 *   device_event_id: string,       // "{device_uid}-{timestamp}-{counter}"
 *   credential_value: string,       // RFID UID
 *   event_time: string,             // ISO 8601
 *   action: "check_in" | "check_out",
 *   photo_base64?: string,          // optional JPEG base64
 *   photo_mime?: string             // optional, defaults to "image/jpeg"
 * }
 *
 * 200: { status: "ok", inserted: true, log_id: "..." }
 * 200: { status: "ok", inserted: false } (duplicate — idempotent)
 * 400: Missing required fields
 * 401: Invalid device credentials
 * 403: Subscription inactive
 * 422: Timestamp too old (>7 days) or in future (>5 min)
 * 500: Internal error
 */
Deno.serve(async (req: Request) => {
  const cors = handleCors(req);
  if (cors) return cors;

  try {
    const body = await req.json();
    const {
      device_uid,
      device_secret,
      device_event_id,
      credential_value,
      event_time,
      action,
      photo_base64,
      photo_mime,
    } = body;

    // Validate required fields
    if (!device_uid || !device_secret || !device_event_id || !credential_value || !event_time || !action) {
      return errorResponse(
        'Required: device_uid, device_secret, device_event_id, credential_value, event_time, action',
        400
      );
    }

    if (action !== 'check_in' && action !== 'check_out') {
      return errorResponse('action must be "check_in" or "check_out"', 400);
    }

    // Validate timestamp — reject if older than 7 days or more than 5 min in future
    const eventTs = new Date(event_time);
    if (isNaN(eventTs.getTime())) {
      return errorResponse('Invalid event_time format', 400);
    }
    const now = Date.now();
    const sevenDaysMs = 7 * 24 * 60 * 60 * 1000;
    const fiveMinMs = 5 * 60 * 1000;
    if (now - eventTs.getTime() > sevenDaysMs) {
      return errorResponse('event_time is older than 7 days', 422);
    }
    if (eventTs.getTime() - now > fiveMinMs) {
      return errorResponse('event_time is too far in the future', 422);
    }

    const supabase = getSupabaseAdmin();

    // 1. Authenticate device
    const device = await authenticateDevice(supabase, device_uid, device_secret);
    if (!device) {
      return errorResponse('Invalid device credentials', 401);
    }

    console.log(`[submit-log] auth ok | org=${device.organization_id} device=${device.id} event=${device_event_id}`);

    // 2. Rate limit: max 60 submissions per device per minute
    const rateLimited = await checkRateLimit(supabase, {
      organization_id: device.organization_id,
      actor_id: device.id,
      action: 'attendance.submitted',
      maxCount: 60,
      windowMinutes: 1,
    });
    if (rateLimited) {
      console.warn(`[submit-log] rate limited | device=${device.id}`);
      return errorResponse('Rate limit exceeded. Max 60 submissions per minute.', 429);
    }

    // 3. Check subscription is active
    const subActive = await checkSubscriptionActive(supabase, device.organization_id);
    if (!subActive) {
      console.warn(`[submit-log] subscription inactive | org=${device.organization_id}`);
      return errorResponse('Subscription inactive or expired', 403);
    }

    // 4. Resolve credential → user_id (RFID lookup within org)
    const { data: credential } = await supabase
      .from('user_credentials')
      .select('user_id')
      .eq('organization_id', device.organization_id)
      .eq('type', 'rfid')
      .eq('value', credential_value)
      .single();

    const userId = credential?.user_id || null;

    // 5. Insert attendance log with idempotent device_event_id
    //    ON CONFLICT (device_id, device_event_id) DO NOTHING
    const { data: inserted, error: insertErr } = await supabase
      .from('attendance_logs')
      .upsert(
        {
          organization_id: device.organization_id,
          device_id: device.id,
          user_id: userId,
          credential_value: credential_value,
          action: action,
          device_event_id: device_event_id,
          timestamp: event_time,
          synced_at: new Date().toISOString(),
        },
        {
          onConflict: 'device_id,device_event_id',
          ignoreDuplicates: true,
        }
      )
      .select('id')
      .single();

    // If ignoreDuplicates: true and it was a duplicate, inserted will be null
    const wasDuplicate = !inserted && !insertErr;
    const logId = inserted?.id;

    // 6. Upload photo if present and log was newly inserted
    if (photo_base64 && logId) {
      try {
        const photoBytes = base64Decode(photo_base64);
        const photoPath = `${device.organization_id}/${device.id}/${logId}.jpg`;

        const { error: uploadErr } = await supabase.storage
          .from('attendance-photos')
          .upload(photoPath, photoBytes, {
            contentType: photo_mime || 'image/jpeg',
            upsert: false,
          });

        if (!uploadErr) {
          await supabase
            .from('attendance_logs')
            .update({ photo_url: photoPath })
            .eq('id', logId);
        } else {
          console.error('Photo upload failed:', uploadErr.message);
          // Non-fatal — log was inserted, photo can be retried
        }
      } catch (photoErr) {
        console.error('Photo processing error:', photoErr);
        // Non-fatal — do not fail the entire request for photo issues
      }
    }

    if (insertErr) {
      console.error('Attendance insert error:', insertErr);
      return errorResponse('Failed to insert attendance log', 500);
    }

    if (!wasDuplicate && logId) {
      console.log(`[submit-log] inserted | org=${device.organization_id} log_id=${logId} user=${userId || 'unknown'} action=${action}`);
      auditLog(supabase, {
        organization_id: device.organization_id,
        actor_type: 'device',
        actor_id: device.id,
        action: 'attendance.submitted',
        resource_type: 'attendance_log',
        resource_id: logId,
        metadata: { credential_value, event_action: action, device_event_id, user_id: userId },
      });
    } else {
      console.log(`[submit-log] duplicate skipped | device=${device.id} event=${device_event_id}`);
    }

    return jsonResponse({
      status: 'ok',
      inserted: !wasDuplicate,
      log_id: logId || null,
    });
  } catch (err) {
    console.error('submit-log error:', err);
    return errorResponse('Internal server error', 500);
  }
});
