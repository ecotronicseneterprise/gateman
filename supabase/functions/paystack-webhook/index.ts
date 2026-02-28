import { getSupabaseAdmin, auditLog } from '../_shared/auth.ts';
import { handleCors, jsonResponse, errorResponse } from '../_shared/cors.ts';

/**
 * Edge Function: paystack-webhook
 *
 * Receives Paystack webhook events and updates subscription status.
 * Follows the same pattern as CV360's paystack_webhook.py but adapted for Deno.
 *
 * Handles:
 *   - charge.success → activate/renew subscription
 *   - subscription.disable → mark subscription cancelled
 *   - invoice.payment_failed → mark subscription past_due
 *
 * Auth: HMAC-SHA512 signature verification using PAYSTACK_SECRET_KEY.
 *
 * POST /functions/v1/paystack-webhook
 * Headers: x-paystack-signature: <hmac_sha512_hex>
 * Body: Paystack event JSON
 *
 * ENV VARS (set in Supabase Dashboard > Edge Functions > Secrets):
 *   - PAYSTACK_SECRET_KEY (from Paystack Dashboard > Settings > API Keys)
 *
 * Paystack metadata expected on checkout initialization:
 *   metadata: { organization_id, plan_type, billing: "monthly"|"yearly" }
 */

// Plan config matching schema
const PLAN_CONFIG: Record<string, { device_limit: number; user_limit: number; retention_days: number }> = {
  starter:    { device_limit: 1,   user_limit: 50,    retention_days: 180 },
  growth:     { device_limit: 5,   user_limit: 500,   retention_days: 365 },
  enterprise: { device_limit: 999, user_limit: 99999, retention_days: 730 },
};

Deno.serve(async (req: Request) => {
  const cors = handleCors(req);
  if (cors) return cors;

  try {
    // 1. Read raw body for signature verification
    const rawBody = await req.text();
    const signature = req.headers.get('x-paystack-signature');

    if (!signature) {
      console.warn('[paystack-webhook] missing x-paystack-signature header');
      return errorResponse('Missing signature', 400);
    }

    // 2. Verify HMAC-SHA512 signature
    const secret = Deno.env.get('PAYSTACK_SECRET_KEY');
    if (!secret) {
      console.error('[paystack-webhook] PAYSTACK_SECRET_KEY not configured');
      return errorResponse('Webhook not configured', 500);
    }

    const encoder = new TextEncoder();
    const key = await crypto.subtle.importKey(
      'raw',
      encoder.encode(secret),
      { name: 'HMAC', hash: 'SHA-512' },
      false,
      ['sign']
    );
    const sig = await crypto.subtle.sign('HMAC', key, encoder.encode(rawBody));
    const expectedSignature = Array.from(new Uint8Array(sig))
      .map(b => b.toString(16).padStart(2, '0'))
      .join('');

    if (expectedSignature !== signature) {
      console.warn('[paystack-webhook] invalid signature');
      return errorResponse('Invalid signature', 401);
    }

    // 3. Parse event
    const payload = JSON.parse(rawBody);
    const event = payload.event as string;
    const data = payload.data || {};

    console.log(`[paystack-webhook] event=${event} reference=${data.reference || 'n/a'}`);

    const supabase = getSupabaseAdmin();

    // 4. Route by event type
    if (event === 'charge.success') {
      await handleChargeSuccess(supabase, data);
    } else if (event === 'subscription.disable' || event === 'subscription.not_renew') {
      await handleSubscriptionCancel(supabase, data);
    } else if (event === 'invoice.payment_failed') {
      await handlePaymentFailed(supabase, data);
    } else {
      console.log(`[paystack-webhook] ignoring event: ${event}`);
    }

    return jsonResponse({ success: true });
  } catch (err) {
    console.error('[paystack-webhook] error:', err);
    return errorResponse('Internal server error', 500);
  }
});

/**
 * Handle successful payment — activate or renew subscription.
 * Re-verifies transaction with Paystack API for security.
 */
async function handleChargeSuccess(
  supabase: ReturnType<typeof getSupabaseAdmin>,
  data: Record<string, unknown>
) {
  if (data.status !== 'success') {
    console.log(`[paystack-webhook] charge status not success: ${data.status}`);
    return;
  }

  const reference = data.reference as string;
  if (!reference) {
    console.warn('[paystack-webhook] missing transaction reference');
    return;
  }

  // Idempotency check — skip if reference already processed
  const { data: existingRef } = await supabase
    .from('payment_references')
    .select('id')
    .eq('reference', reference)
    .maybeSingle();

  if (existingRef) {
    console.log(`[paystack-webhook] duplicate reference=${reference}, skipping`);
    return;
  }

  // Re-verify with Paystack API
  const secret = Deno.env.get('PAYSTACK_SECRET_KEY')!;
  const verifyResp = await fetch(`https://api.paystack.co/transaction/verify/${reference}`, {
    headers: { 'Authorization': `Bearer ${secret}` },
  });

  if (!verifyResp.ok) {
    console.error(`[paystack-webhook] verify failed: ${verifyResp.status}`);
    return;
  }

  const verifyJson = await verifyResp.json();
  const verifyData = verifyJson.data;

  if (verifyData?.status !== 'success') {
    console.warn(`[paystack-webhook] verify status: ${verifyData?.status}`);
    return;
  }

  // Extract metadata
  const metadata = verifyData.metadata || {};
  const organizationId = metadata.organization_id as string;
  const planType = (metadata.plan_type as string) || 'starter';
  const billing = (metadata.billing as string) || 'monthly';

  if (!organizationId) {
    console.error('[paystack-webhook] missing organization_id in metadata');
    return;
  }

  const config = PLAN_CONFIG[planType] || PLAN_CONFIG.starter;
  const periodDays = billing === 'yearly' ? 365 : 30;
  const now = new Date();
  const periodEnd = new Date(now.getTime() + periodDays * 24 * 60 * 60 * 1000);

  // Find existing active/trial subscription for this org
  const { data: existing } = await supabase
    .from('subscriptions')
    .select('id')
    .eq('organization_id', organizationId)
    .in('status', ['active', 'trial'])
    .single();

  const subPayload = {
    organization_id: organizationId,
    plan_type: planType,
    status: 'active' as const,
    device_limit: config.device_limit,
    user_limit: config.user_limit,
    retention_days: config.retention_days,
    trial_ends_at: null,
    current_period_start: now.toISOString(),
    current_period_end: periodEnd.toISOString(),
    paystack_reference: reference,
  };

  let error;
  if (existing) {
    // Update existing subscription
    ({ error } = await supabase
      .from('subscriptions')
      .update(subPayload)
      .eq('id', existing.id));
  } else {
    // Insert new subscription (e.g. reactivation after cancellation)
    ({ error } = await supabase
      .from('subscriptions')
      .insert(subPayload));
  }

  if (error) {
    console.error('[paystack-webhook] subscription update error:', error);
    return;
  }

  // Record reference to prevent replay
  await supabase
    .from('payment_references')
    .insert({ reference, organization_id: organizationId, event_type: 'charge.success' })
    .then(({ error: refErr }) => {
      if (refErr) console.warn('[paystack-webhook] payment_references insert failed:', refErr);
    });

  console.log(`[paystack-webhook] subscription activated | org=${organizationId} plan=${planType} billing=${billing} ref=${reference}`);

  auditLog(supabase, {
    organization_id: organizationId,
    actor_type: 'system',
    action: 'subscription.activated',
    resource_type: 'subscription',
    metadata: { plan_type: planType, billing, reference, amount: verifyData.amount },
  });
}

/**
 * Handle subscription cancellation.
 */
async function handleSubscriptionCancel(
  supabase: ReturnType<typeof getSupabaseAdmin>,
  data: Record<string, unknown>
) {
  const metadata = (data.customer as Record<string, unknown>)?.metadata as Record<string, unknown> || {};
  const organizationId = metadata.organization_id as string;

  if (!organizationId) {
    console.warn('[paystack-webhook] cancel: missing organization_id');
    return;
  }

  const { error } = await supabase
    .from('subscriptions')
    .update({ status: 'cancelled' })
    .eq('organization_id', organizationId)
    .in('status', ['active', 'trial']);

  if (error) {
    console.error('[paystack-webhook] cancel update error:', error);
    return;
  }

  console.log(`[paystack-webhook] subscription cancelled | org=${organizationId}`);

  auditLog(supabase, {
    organization_id: organizationId,
    actor_type: 'system',
    action: 'subscription.cancelled',
    resource_type: 'subscription',
  });
}

/**
 * Handle failed payment — mark subscription as past_due.
 */
async function handlePaymentFailed(
  supabase: ReturnType<typeof getSupabaseAdmin>,
  data: Record<string, unknown>
) {
  const metadata = (data as Record<string, unknown>).metadata as Record<string, unknown> || {};
  const organizationId = metadata.organization_id as string;

  if (!organizationId) {
    console.warn('[paystack-webhook] payment_failed: missing organization_id');
    return;
  }

  const { error } = await supabase
    .from('subscriptions')
    .update({ status: 'past_due' })
    .eq('organization_id', organizationId)
    .eq('status', 'active');

  if (error) {
    console.error('[paystack-webhook] past_due update error:', error);
    return;
  }

  console.log(`[paystack-webhook] subscription past_due | org=${organizationId}`);

  auditLog(supabase, {
    organization_id: organizationId,
    actor_type: 'system',
    action: 'subscription.past_due',
    resource_type: 'subscription',
  });
}
