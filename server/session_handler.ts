import { Context, Router, Status } from 'https://deno.land/x/oak@v12.6.1/mod.ts';
import { createClient } from 'jsr:@supabase/supabase-js@2';

export const SUPABASE_URL = Deno.env.get('SUPABASE_URL');
export const SUPABASE_SERVICE_KEY = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY');

const corsHeaders = {
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type, x-supabase-auth',
    'Access-Control-Allow-Methods': 'POST, OPTIONS',
};

function applyCorsHeaders(ctx: Context) {
    for (const [key, value] of Object.entries(corsHeaders)) {
        ctx.response.headers.set(key, value);
    }
}

const PENDING_LOGIN_TIMEOUT_MS = 5 * 60 * 1000;

async function handlePollSession(ctx: Context) {
    if (ctx.request.method !== 'POST') {
        ctx.response.status = 405;
        ctx.response.body = { error: 'Method Not Allowed' };
        ctx.response.headers.set('Content-Type', 'application/json');
        return;
    }
    try {
        const body = await ctx.request.body({ type: 'json' }).value;
        const { device_id } = body;

        if (!device_id || typeof device_id !== 'string') {
            ctx.response.status = 400;
            ctx.response.body = { error: 'Missing or invalid device_id' };
            ctx.response.headers.set('Content-Type', 'application/json');
            return;
        }
        if (!SUPABASE_URL || !SUPABASE_SERVICE_KEY) {
            ctx.response.status = 500;
            ctx.response.body = { error: 'Supabase configuration missing' };
            ctx.response.headers.set('Content-Type', 'application/json');
            return;
        }
        const supabase = createClient(SUPABASE_URL, SUPABASE_SERVICE_KEY);
        // 1. Fetch the pending login
        const { data: pendingLogin, error: fetchError } = await supabase
            .schema('qr-auth')
            .from('logins')
            .select(
                'device_id, refresh_token, access_token, expires_at, created_at, claimed_at',
            )
            .eq('device_id', device_id)
            .single(); // Expecting only one row
        if (fetchError || !pendingLogin) {
            if (fetchError && fetchError.code === 'PGRST116') {
                ctx.response.status = 202;
                ctx.response.body = {
                    status: 'pending',
                    message: 'No login attempt found for this device ID or it has expired.',
                };
                ctx.response.headers.set('Content-Type', 'application/json');
                return;
            }
            console.error('Error fetching pending login:', fetchError);
            ctx.response.status = 500;
            ctx.response.body = {
                error: 'Failed to fetch login status',
                details: fetchError?.message,
            };
            ctx.response.headers.set('Content-Type', 'application/json');
            return;
        }

        if (pendingLogin.claimed_at) {
            ctx.response.status = 409;
            ctx.response.body = {
                status: 'claimed_already',
                message: 'This login has already been claimed.',
            };
            ctx.response.headers.set('Content-Type', 'application/json');
            return;
        }

        const createdAtTime = new Date(pendingLogin.created_at).getTime();
        if (Date.now() > createdAtTime + PENDING_LOGIN_TIMEOUT_MS) {
            await supabase
                .schema('qr-auth')
                .from('logins')
                .delete()
                .eq('device_id', device_id);
            ctx.response.status = 410;
            ctx.response.body = {
                status: 'expired',
                message: 'Login attempt expired.',
            };
            ctx.response.headers.set('Content-Type', 'application/json');
            return;
        }
        const { error: updateError } = await supabase
            .schema('qr-auth')
            .from('logins')
            .update({
                claimed_at: new Date().toISOString(),
            })
            .eq('device_id', device_id);
        if (updateError) {
            console.error('Error marking login as claimed:', updateError);
            ctx.response.status = 500;
            ctx.response.body = {
                error: 'Failed to finalize login',
                details: updateError.message,
            };
            ctx.response.headers.set('Content-Type', 'application/json');
            return;
        }
        ctx.response.status = 200;
        ctx.response.body = {
            status: 'success',
            access_token: pendingLogin.access_token,
            refresh_token: pendingLogin.refresh_token,
            expires_at: pendingLogin.expires_at,
        };
        ctx.response.headers.set('Content-Type', 'application/json');
        return;
    } catch (e) {
        const error = e instanceof Error ? e : new Error(String(e));
        console.error('Error in poll-device-auth function:', error);
        ctx.response.status = 500;
        ctx.response.body = {
            error: 'Internal Server Error',
            details: error.message,
        };
        ctx.response.headers.set('Content-Type', 'application/json');
    }
}

async function handleStoreDeviceTokenInternal(ctx: Context) {
    if (ctx.request.method !== 'POST') {
        ctx.response.status = Status.MethodNotAllowed;
        ctx.response.body = { error: 'Method Not Allowed' };
        ctx.response.headers.set('Content-Type', 'application/json');
        return;
    }

    if (!SUPABASE_URL || !SUPABASE_SERVICE_KEY) {
        console.error(
            'Supabase URL or Service Key is not configured on the server.',
        );
        ctx.response.status = Status.InternalServerError;
        ctx.response.body = {
            error: 'Supabase configuration missing on server.',
        };
        ctx.response.headers.set('Content-Type', 'application/json');
        return;
    }

    try {
        const body = await ctx.request.body({ type: 'json' }).value;
        const { device_id, session } = body;

        if (!device_id || typeof device_id !== 'string') {
            ctx.response.status = Status.BadRequest;
            ctx.response.body = { error: 'Missing or invalid device_id' };
            ctx.response.headers.set('Content-Type', 'application/json');
            return;
        }

        if (
            !session ||
            !session.refresh_token ||
            !session.access_token ||
            !session.user ||
            !session.user.id
        ) {
            ctx.response.status = Status.BadRequest;
            ctx.response.body = { error: 'Missing or invalid session data' };
            ctx.response.headers.set('Content-Type', 'application/json');
            return;
        }

        const supabase = createClient(SUPABASE_URL, SUPABASE_SERVICE_KEY);

        // Upsert into the 'logins' table within the 'qr-auth' schema
        const { data, error: dbError } = await supabase
            .schema('qr-auth')
            .from('logins')
            .upsert({
                device_id: device_id,
                refresh_token: session.refresh_token,
                access_token: session.access_token,
                expires_at: session.expires_at ? new Date(session.expires_at * 1000).toISOString() : null,
                // user_id: session.user.id,
            })
            .select();

        if (dbError) {
            console.error(
                'Supabase DB Error storing device token:',
                JSON.stringify(dbError, null, 2),
            );
            ctx.response.status = Status.InternalServerError; // Or a more specific Supabase error mapping
            ctx.response.body = {
                error: 'Failed to store token',
                details: dbError.message || 'No specific message',
                code: dbError.code || 'Unknown code',
                hint: dbError.hint || 'No hint',
            };
            ctx.response.headers.set('Content-Type', 'application/json');
            return;
        }

        console.log(
            'Device token stored/updated for device_id:',
            device_id,
            'Data:',
            data,
        );
        ctx.response.status = Status.OK;
        ctx.response.body = { message: 'Token stored successfully' };
        ctx.response.headers.set('Content-Type', 'application/json');
    } catch (e) {
        const error = e instanceof Error ? e : new Error(String(e));
        console.error('Error in store-device-token handler:', error);
        ctx.response.status = Status.InternalServerError;
        ctx.response.body = {
            error: 'Internal Server Error',
            details: error.message,
        };
        ctx.response.headers.set('Content-Type', 'application/json');
    }
}

// Unified handler for POST and OPTIONS
export async function handleStoreDeviceToken(ctx: Context) {
    applyCorsHeaders(ctx);

    if (ctx.request.method === 'OPTIONS') {
        ctx.response.status = Status.OK;
        ctx.response.body = 'ok';
        return;
    }

    await handleStoreDeviceTokenInternal(ctx);
}

const supabaseRouter = new Router();
supabaseRouter.post('/v1/store-device-token', handleStoreDeviceToken);
supabaseRouter.options('/v1/store-device-token', handleStoreDeviceToken);
supabaseRouter.post('/v1/poll-device-token', handlePollSession);

export { supabaseRouter };
