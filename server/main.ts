import { app as generalApp, AZURE_STORAGE_CONNECTION_STRING, router as generalRouter } from './general_routes.ts';
import { getOnlinePlayerCounts, handleWebSocket } from './signaling.ts';
import { SUPABASE_SERVICE_KEY, SUPABASE_URL, supabaseRouter } from './session_handler.ts';

const app = generalApp;
const router = generalRouter;

// --- Server Configuration ---
const HOST_NAME = Deno.env.get('HOST_NAME') || '0.0.0.0';
const PORT = parseInt(
    Deno.env.get('SERVER_PORT') || Deno.env.get('PORT') || '8088',
    10,
);

// --- Multiplayer Endpoints ---
router.get('/v1/online-players', (ctx) => {
    ctx.response.body = getOnlinePlayerCounts();
});

router.get('/ws/:clientID/:roomID', (ctx) => {
    return handleWebSocket(ctx as any); // Type assertion for RouterContext
});

// --- Application Setup ---
app.use(router.routes());
app.use(supabaseRouter.routes());
app.use(router.allowedMethods());
app.use(supabaseRouter.allowedMethods());

// --- Global Error Handling for Oak ---
app.addEventListener('error', (evt) => {
    console.error('Oak Application Error:', evt.error);
});

// --- Server Start ---
const startupMessages = [
    `Server configured to run on http://${HOST_NAME === '0.0.0.0' ? 'localhost' : HOST_NAME}:${PORT}`,
];

if (!AZURE_STORAGE_CONNECTION_STRING) {
    startupMessages.push(
        "Reminder: AZURE_STORAGE_CONNECTION_STRING is not set. '/v1/upload-save' will not work as expected.",
    );
}

if (!SUPABASE_URL || !SUPABASE_SERVICE_KEY) {
    startupMessages.push(
        "WARNING: SUPABASE_URL or SUPABASE_SERVICE_ROLE_KEY environment variables are not set. '/v1/store-device-token' endpoint will not function correctly.",
    );
}

console.log(startupMessages.join('\n'));

console.log(
    `Server listening on http://${HOST_NAME === '0.0.0.0' ? 'localhost' : HOST_NAME}:${PORT}`,
);
console.log('Available REST endpoints (from general_routes.ts):');
console.log(
    `  GET  http://${HOST_NAME === '0.0.0.0' ? 'localhost' : HOST_NAME}:${PORT}/v1/latest-version`,
);
console.log(
    `  GET  http://${HOST_NAME === '0.0.0.0' ? 'localhost' : HOST_NAME}:${PORT}/v1/generate-device-id`,
);
console.log(
    `  POST http://${
        HOST_NAME === '0.0.0.0' ? 'localhost' : HOST_NAME
    }:${PORT}/v1/upload-save (requires 'userId' query param and 'save' file in form-data)`,
);
console.log('Available Supabase related endpoints (from supabase_handler.ts):');
console.log(
    `  POST http://${
        HOST_NAME === '0.0.0.0' ? 'localhost' : HOST_NAME
    }:${PORT}/v1/store-device-token (Handles device token storage)`,
);
console.log(
    `  OPTIONS http://${
        HOST_NAME === '0.0.0.0' ? 'localhost' : HOST_NAME
    }:${PORT}/v1/store-device-token (For CORS preflight)`,
);
console.log(
    `  POST http://${
        HOST_NAME === '0.0.0.0' ? 'localhost' : HOST_NAME
    }:${PORT}/v1/poll-device-token (Checks if device has been logged in)`,
);
console.log('Available multiplayer endpoints:');
console.log(
    `  GET  http://${HOST_NAME === '0.0.0.0' ? 'localhost' : HOST_NAME}:${PORT}/v1/online-players`,
);
console.log('Available WebSocket endpoint:');
console.log(
    `  GET  ws://${
        HOST_NAME === '0.0.0.0' ? 'localhost' : HOST_NAME
    }:${PORT}/ws/:clientID/:roomID (e.g., /ws/player123/myroom or /ws/player456/match)`,
);

await app.listen({ port: PORT, hostname: HOST_NAME });

// --- Graceful Shutdown ---
const shutdown = () => {
    console.log('Shutting down server...');
    Deno.exit(0);
};

Deno.addSignalListener('SIGINT', shutdown);
Deno.addSignalListener('SIGTERM', shutdown);
