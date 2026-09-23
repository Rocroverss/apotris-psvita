import { Application, Router, Status } from 'https://deno.land/x/oak@v12.6.1/mod.ts';
// Using built-in Web Crypto API instead of std library to avoid WASM compilation issues
import { BlobServiceClient, BlobUploadCommonResponse, ContainerClient } from 'npm:@azure/storage-blob@12';

const app = new Application();
const router = new Router();

// --- Azure Blob Storage Configuration ---
export const AZURE_STORAGE_CONNECTION_STRING = Deno.env.get(
    'AZURE_STORAGE_CONNECTION_STRING',
);

if (!AZURE_STORAGE_CONNECTION_STRING) {
    console.warn(
        'WARNING: AZURE_STORAGE_CONNECTION_STRING environment variable is not set. File upload endpoint will not function correctly.',
    );
}

function getBlobContainerClient(containerName: string): ContainerClient | null {
    if (!AZURE_STORAGE_CONNECTION_STRING) {
        console.error(
            'Azure Storage connection string is not configured. Cannot get blob container client.',
        );
        return null;
    }
    const blobServiceClient = BlobServiceClient.fromConnectionString(
        AZURE_STORAGE_CONNECTION_STRING,
    );
    return blobServiceClient.getContainerClient(containerName);
}

// --- General Endpoints ---
router.get('/v1/latest-version', (ctx) => {
    ctx.response.body = { latestVersion: 'v4.2.0' };
});

router.get('/v1/generate-device-id', (ctx) => {
    const characters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789';
    let result = '';
    const charactersLength = characters.length;
    for (let i = 0; i < 5; i++) {
        result += characters.charAt(
            Math.floor(Math.random() * charactersLength),
        );
    }

    ctx.response.body = {
        deviceId: result,
    };
});

// --- Saves Endpoint: UploadSave ---
router.post('/v1/upload-save', async (ctx) => {
    const userId = ctx.request.url.searchParams.get('userId');
    if (!userId) {
        ctx.response.status = Status.BadRequest;
        ctx.response.body = { error: "Query parameter 'userId' is required." };
        return;
    }

    if (!AZURE_STORAGE_CONNECTION_STRING) {
        ctx.response.status = Status.InternalServerError;
        ctx.response.body = {
            error: 'Azure Storage is not configured on the server.',
        };
        return;
    }

    try {
        const body = ctx.request.body({ type: 'form-data' });
        const formDataReader = await body.value;
        const formData = await formDataReader.read({
            maxFileSize: 10 * 1024 * 1024, /* 10MB limit */
        });

        const fileEntry = formData.files && 'save' in formData.files ? formData.files.save : null;

        if (!fileEntry || Array.isArray(fileEntry)) {
            ctx.response.status = Status.BadRequest;
            ctx.response.body = {
                error: "Please pass a single file in the request under the 'save' field.",
            };
            return;
        }

        const file = fileEntry as any; // Type assertion for Oak FormFile
        if (!file.content) {
            ctx.response.status = Status.BadRequest;
            ctx.response.body = {
                error: 'File content is missing.',
            };
            return;
        }

        const originalFileName = file.filename || 'unknown_file';
        // content is Uint8Array as per Oak's FormFile type
        const fileContent = file.content as Uint8Array;
        const fileSize = fileContent.byteLength;
        const contentType = file.contentType || 'application/octet-stream';

        console.log(
            `Received file: ${originalFileName}, Size: ${fileSize} bytes, ContentType: ${contentType} for userId: ${userId}`,
        );

        const blobContainerClient = getBlobContainerClient('saves');
        if (!blobContainerClient) {
            ctx.response.status = Status.InternalServerError;
            ctx.response.body = {
                error: 'Could not connect to Azure Blob Storage.',
            };
            return;
        }
        const blobName = `${userId}/Apotris.sav`;
        const blockBlobClient = blobContainerClient.getBlockBlobClient(blobName);

        console.log(`Attempting to upload to Azure Blob Storage: ${blobName}`);
        const uploadBlobResponse: BlobUploadCommonResponse = await blockBlobClient.upload(fileContent, fileSize, {
            blobHTTPHeaders: { blobContentType: contentType },
        });

        const blobUri = blockBlobClient.url;
        console.log(
            `File uploaded successfully to ${blobUri}. Request ID: ${uploadBlobResponse.requestId}`,
        );

        ctx.response.status = Status.OK;
        ctx.response.body = {
            message: `File ${originalFileName} received and uploaded successfully.`,
            uri: blobUri,
        };
    } catch (ex) {
        const error = ex instanceof Error ? ex : new Error(String(ex));
        console.error(
            `Error processing file upload for userId: ${userId}: ${error.message}`,
        );
        if (error.stack) {
            console.error(error.stack);
        }
        ctx.response.status = Status.InternalServerError;
        ctx.response.body = { error: `Error processing file: ${error.message}` };
    }
});

export { app, router };
