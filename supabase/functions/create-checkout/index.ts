import { serve } from "https://deno.land/std@0.168.0/http/server.ts";
import { createClient } from "https://esm.sh/@supabase/supabase-js@2";
import { getSupabaseAdmin, auditLog } from "../_shared/auth.ts";
import { handleCors, jsonResponse, errorResponse } from "../_shared/cors.ts";

serve(async (req: Request) => {
  if (req.method === "OPTIONS") return handleCors();

  try {
    // Authenticate user via JWT
    const authHeader = req.headers.get("Authorization");
    if (!authHeader) return errorResponse("missing authorization", 401);

    const supabaseUser = createClient(
      Deno.env.get("SUPABASE_URL")!,
      Deno.env.get("SUPABASE_ANON_KEY")!,
      { global: { headers: { Authorization: authHeader } } }
    );

    const { data: { user }, error: authErr } = await supabaseUser.auth.getUser();
    if (authErr || !user) return errorResponse("unauthorized", 401);

    const body = await req.json();
    const { organization_id, plan_type, billing, callback_url } = body;

    if (!organization_id || !plan_type) {
      return errorResponse("missing: organization_id, plan_type", 400);
    }

    const supabase = getSupabaseAdmin();

    // Verify user is owner/admin of this org
    const { data: membership } = await supabase
      .from("org_members")
      .select("role")
      .eq("user_id", user.id)
      .eq("organization_id", organization_id)
      .single();

    if (!membership || !["owner", "admin"].includes(membership.role)) {
      return errorResponse("forbidden — must be owner or admin", 403);
    }

    // Plan pricing (kobo = NGN * 100)
    const planPricing: Record<string, Record<string, number>> = {
      starter:    { monthly: 3900_00, yearly: 39000_00 },
      growth:     { monthly: 12900_00, yearly: 129000_00 },
      enterprise: { monthly: 49900_00, yearly: 499000_00 },
    };

    const billingCycle = billing || "monthly";
    const amount = planPricing[plan_type]?.[billingCycle];
    if (!amount) {
      return errorResponse(`invalid plan_type or billing: ${plan_type}/${billingCycle}`, 400);
    }

    const PAYSTACK_SECRET = Deno.env.get("PAYSTACK_SECRET_KEY");
    if (!PAYSTACK_SECRET) {
      console.error("[create-checkout] PAYSTACK_SECRET_KEY not set");
      return errorResponse("payment configuration error", 500);
    }

    // Initialize Paystack transaction
    const paystackPayload = {
      email: user.email,
      amount,
      currency: "NGN",
      metadata: {
        organization_id,
        plan_type,
        billing: billingCycle,
        user_id: user.id,
      },
      callback_url: callback_url || Deno.env.get("FRONTEND_URL") || "",
    };

    console.log(`[create-checkout] Initializing Paystack: org=${organization_id} plan=${plan_type} amount=${amount}`);

    const paystackResp = await fetch("https://api.paystack.co/transaction/initialize", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${PAYSTACK_SECRET}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify(paystackPayload),
    });

    const paystackData = await paystackResp.json();

    if (!paystackResp.ok || !paystackData.status) {
      console.error("[create-checkout] Paystack error:", paystackData);
      return errorResponse("paystack initialization failed", 502);
    }

    // Audit log
    auditLog(supabase, {
      organization_id,
      actor_type: "user",
      actor_id: user.id,
      action: "checkout.initiated",
      resource_type: "subscription",
      metadata: { plan_type, billing: billingCycle, reference: paystackData.data.reference },
    });

    console.log(`[create-checkout] SUCCESS ref=${paystackData.data.reference}`);

    return jsonResponse({
      checkout_url: paystackData.data.authorization_url,
      reference: paystackData.data.reference,
      access_code: paystackData.data.access_code,
    });

  } catch (err) {
    console.error("[create-checkout] UNHANDLED", err);
    return errorResponse("server_error", 500);
  }
});
