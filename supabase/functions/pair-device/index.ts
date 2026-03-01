import { serve } from "https://deno.land/std@0.168.0/http/server.ts";
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.39.3";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
};

serve(async (req) => {
  if (req.method === "OPTIONS") {
    return new Response(null, { headers: corsHeaders });
  }

  try {
    const supabaseUrl = Deno.env.get("SUPABASE_URL")!;
    const supabaseServiceKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;
    const supabaseAnonKey = Deno.env.get("SUPABASE_ANON_KEY")!;
    
    // Create anon client for auth validation
    const supabaseAnon = createClient(supabaseUrl, supabaseAnonKey, {
      global: { headers: { Authorization: req.headers.get("Authorization")! } }
    });
    
    // Create service role client for database operations
    const supabase = createClient(supabaseUrl, supabaseServiceKey);

    // Get user session from JWT
    const { data: { user }, error: authError } = await supabaseAnon.auth.getUser();

    if (authError || !user) {
      console.error("[pair-device] Auth error:", authError);
      return new Response(
        JSON.stringify({ error: "Unauthorized - please sign in again" }),
        { status: 401, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    // Get request body
    const { pairing_code, device_name } = await req.json();

    if (!pairing_code || !device_name) {
      return new Response(
        JSON.stringify({ error: "Missing pairing_code or device_name" }),
        { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    // Validate pairing code format (12 hex characters)
    if (!/^[0-9A-F]{12}$/i.test(pairing_code)) {
      return new Response(
        JSON.stringify({ error: "Invalid pairing code format. Expected 12 hex characters." }),
        { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    // Convert pairing code back to MAC address format
    const mac = pairing_code.match(/.{1,2}/g)!.join(":").toUpperCase();

    console.log(`[pair-device] User ${user.id} attempting to pair device with MAC ${mac}`);

    // Get user's organization
    const { data: membership, error: memberError } = await supabase
      .from("org_members")
      .select("organization_id, role, organizations(subscription_status, plan_type)")
      .eq("user_id", user.id)
      .single();

    if (memberError || !membership) {
      return new Response(
        JSON.stringify({ error: "User not associated with any organization" }),
        { status: 403, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    const orgId = membership.organization_id;
    const subscription = membership.organizations as any;

    // Check subscription status
    if (subscription.subscription_status !== "trial" && subscription.subscription_status !== "active") {
      return new Response(
        JSON.stringify({ error: "Subscription required. Please activate your subscription." }),
        { status: 403, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    // Check device limit based on plan
    const { count: deviceCount } = await supabase
      .from("devices")
      .select("*", { count: "exact", head: true })
      .eq("organization_id", orgId);

    const limits: Record<string, number> = {
      starter: 3,
      professional: 10,
      enterprise: 999,
    };

    const limit = limits[subscription.plan_type] || 3;

    if ((deviceCount || 0) >= limit) {
      return new Response(
        JSON.stringify({ 
          error: `Device limit reached. Your ${subscription.plan_type} plan allows ${limit} devices.` 
        }),
        { status: 403, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    // Check if device already exists
    const { data: existingDevice } = await supabase
      .from("devices")
      .select("id, name, organization_id")
      .eq("device_uid", mac)
      .single();

    if (existingDevice) {
      if (existingDevice.organization_id === orgId) {
        return new Response(
          JSON.stringify({ 
            error: `Device already registered as "${existingDevice.name}"` 
          }),
          { status: 409, headers: { ...corsHeaders, "Content-Type": "application/json" } }
        );
      } else {
        return new Response(
          JSON.stringify({ error: "Device already registered to another organization" }),
          { status: 409, headers: { ...corsHeaders, "Content-Type": "application/json" } }
        );
      }
    }

    // Generate device secret (bcrypt-compatible hash)
    const secret = crypto.randomUUID().replace(/-/g, "");

    // Create device record
    const { data: device, error: deviceError } = await supabase
      .from("devices")
      .insert({
        organization_id: orgId,
        device_uid: mac,
        device_secret: secret,
        name: device_name,
        status: "active",
      })
      .select()
      .single();

    if (deviceError) {
      console.error("[pair-device] Device creation failed:", deviceError);
      return new Response(
        JSON.stringify({ error: "Failed to create device record" }),
        { status: 500, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    console.log(`[pair-device] Device ${device.id} paired successfully to org ${orgId}`);

    return new Response(
      JSON.stringify({
        success: true,
        device_id: device.id,
        device_name: device.name,
        message: "Device paired successfully! It will provision automatically.",
      }),
      { status: 200, headers: { ...corsHeaders, "Content-Type": "application/json" } }
    );

  } catch (error) {
    console.error("[pair-device] Error:", error);
    return new Response(
      JSON.stringify({ error: "Internal server error" }),
      { status: 500, headers: { ...corsHeaders, "Content-Type": "application/json" } }
    );
  }
});
