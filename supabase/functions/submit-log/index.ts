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
 * 200: { status: "ok", inserted: false, discarded: "stale_timestamp" }
 *      (event older than 7 days — accepted so the device drops it from its
 *       offline queue, but not stored; audited as attendance.rejected)
 * 400: Missing required fields
 * 401: Invalid device credentials
 * 403: Subscription inactive (audited as attendance.rejected)
 * 422: Timestamp in future (>5 min) — device should retry
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
      console.warn(`[submit-log] 400 missing fields | uid=${device_uid ? 'ok' : 'MISSING'} secret=${device_secret ? 'ok' : 'MISSING'} event_id=${device_event_id ?? 'MISSING'} cred=${credential_value ?? 'MISSING'} time=${event_time ?? 'MISSING'} action=${action ?? 'MISSING'}`);
      return errorResponse(
        'Required: device_uid, device_secret, device_event_id, credential_value, event_time, action',
        400
      );
    }

    if (action !== 'check_in' && action !== 'check_out') {
      console.warn(`[submit-log] 400 bad action | action=${action} event=${device_event_id}`);
      return errorResponse('action must be "check_in" or "check_out"', 400);
    }

    // Validate timestamp format; range checks happen after auth so rejections can be audited
    // Unparseable timestamps (e.g. the firmware's gmtime 32→64-bit cast bug produces
    // year-2818659 dates) can never become valid — handled after auth as a discard so
    // the device drops them from its queue instead of retrying a 400 forever.
    const eventTs = new Date(event_time);
    const invalidTime = isNaN(eventTs.getTime());
    const now = Date.now();
    const sevenDaysMs = 7 * 24 * 60 * 60 * 1000;
    const fiveMinMs = 5 * 60 * 1000;
    if (!invalidTime && eventTs.getTime() - now > fiveMinMs) {
      // Future-stamped events become valid once the clock catches up — 422 so the device retries
      console.warn(`[submit-log] 422 future event_time | time=${event_time} event=${device_event_id}`);
      return errorResponse('event_time is too far in the future', 422);
    }

    const supabase = getSupabaseAdmin();

    // 1. Authenticate device
    const device = await authenticateDevice(supabase, device_uid, device_secret);
    if (!device) {
      return errorResponse('Invalid device credentials', 401);
    }

    // Stale or unparseable events can never become valid, and a 4xx would leave them
    // retrying in the device's offline queue forever. Accept-and-discard (200,
    // inserted:false) so the firmware treats them as duplicates and drops them from
    // its queue; audit the discard.
    if (invalidTime) {
      console.warn(`[submit-log] invalid event_time discarded | device=${device.id} event=${device_event_id} time=${event_time}`);
      auditLog(supabase, {
        organization_id: device.organization_id,
        actor_type: 'device',
        actor_id: device.id,
        action: 'attendance.rejected',
        resource_type: 'attendance_log',
        metadata: { reason: 'invalid_timestamp', device_event_id, event_time, event_action: action, credential_value },
      });
      return jsonResponse({ status: 'ok', inserted: false, log_id: null, discarded: 'invalid_timestamp' });
    }
    if (now - eventTs.getTime() > sevenDaysMs) {
      console.warn(`[submit-log] stale event discarded | device=${device.id} event=${device_event_id} event_time=${event_time}`);
      auditLog(supabase, {
        organization_id: device.organization_id,
        actor_type: 'device',
        actor_id: device.id,
        action: 'attendance.rejected',
        resource_type: 'attendance_log',
        metadata:{ reason: 'stale_timestamp', device_event_id, event_time, event_action: action, credential_value },
      });
      return jsonResponse({ status: 'ok', inserted: false, log_id: null, discarded: 'stale_timestamp' });
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
      auditLog(supabase, {
        organization_id: device.organization_id,
        actor_type: 'device',
        actor_id: device.id,
        action: 'attendance.rejected',
        resource_type: 'attendance_log',
        metadata:{ reason: 'subscription_inactive', device_event_id, event_time, event_action: action },
      });
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

    // Unknown card — the log is still recorded (user_id null), but audit it so
    // admins can see unrecognised cards in the dashboard and enroll them
    if (!userId) {
      console.warn(`[submit-log] unknown credential | device=${device.id} card=${credential_value}`);
      auditLog(supabase, {
        organization_id: device.organization_id,
        actor_type: 'device',
        actor_id: device.id,
        action: 'attendance.unknown_credential',
        resource_type: 'user_credentials',
        metadata: { credential_value, device_event_id, event_time, event_action: action },
      });
    }

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
      .select('id');

    // If ignoreDuplicates: true and it was a duplicate, data will be empty array
    const wasDuplicate = !insertErr && (!inserted || inserted.length === 0);
    const logId = inserted && inserted.length > 0 ? inserted[0].id : null;

    if (insertErr) {
      console.error('Attendance insert error:', insertErr);
      console.error('Insert error details:', JSON.stringify(insertErr, null, 2));
      return errorResponse(`Failed to insert attendance log: ${insertErr.message || 'Unknown error'}`, 500);
    }

    // 6. Upload photo if present and log was newly inserted (non-blocking)
    if (photo_base64 && logId) {
      // Fire and forget - don't block response
      (async () => {
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
            console.log(`[submit-log] photo uploaded | log_id=${logId}`);
          } else {
            console.error('Photo upload failed:', uploadErr.message);
          }
        } catch (photoErr) {
          console.error('Photo processing error:', photoErr);
        }
      })();
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
