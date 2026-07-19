@echo off
REM Deploy all Gateman Edge Functions to Supabase
REM Run from gateman/ directory

echo ========================================
echo Deploying Gateman Edge Functions
echo ========================================

echo.
REM Device functions MUST be deployed with --no-verify-jwt: devices authenticate
REM with device_uid+device_secret in the body, not Supabase JWTs. Deploying them
REM without the flag breaks all device auth.

echo [1/10] Deploying device-provision...
npx supabase functions deploy device-provision --project-ref ueobebsgheecclwcbigy --no-verify-jwt

echo.
echo [2/10] Deploying device-login...
npx supabase functions deploy device-login --project-ref ueobebsgheecclwcbigy --no-verify-jwt

echo.
echo [3/10] Deploying submit-log...
npx supabase functions deploy submit-log --project-ref ueobebsgheecclwcbigy --no-verify-jwt

echo.
echo [4/10] Deploying get-users...
npx supabase functions deploy get-users --project-ref ueobebsgheecclwcbigy --no-verify-jwt

echo.
echo [5/10] Deploying create-provision-token...
npx supabase functions deploy create-provision-token --project-ref ueobebsgheecclwcbigy

echo.
echo [6/10] Deploying device-enroll...
npx supabase functions deploy device-enroll --project-ref ueobebsgheecclwcbigy --no-verify-jwt

echo.
echo [7/10] Deploying create-checkout...
npx supabase functions deploy create-checkout --project-ref ueobebsgheecclwcbigy

echo.
echo [8/10] Deploying paystack-webhook...
npx supabase functions deploy paystack-webhook --project-ref ueobebsgheecclwcbigy

echo.
echo [9/10] Deploying start-enrollment...
npx supabase functions deploy start-enrollment --project-ref ueobebsgheecclwcbigy

echo.
echo [10/10] Deploying check-enrollment...
npx supabase functions deploy check-enrollment --project-ref ueobebsgheecclwcbigy --no-verify-jwt

echo.
echo ========================================
echo Deployment Complete!
echo ========================================
pause
