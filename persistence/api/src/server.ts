/**
 * @file server.ts
 * @brief HTTP entry point for `@centroid-gai/api`: a small hand-rolled router over `node:http`.
 *
 * Routes baseline artifacts and persistent neural conversations without a framework.
 * Domain modules own persistence, the native bridge and bounded worker execution;
 * this entry point handles HTTP dispatch and coordinates startup and shutdown.
 * See docs/api-contract.md and docs/chat-service.md for the endpoint contracts.
 */
import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { artifactRoute } from './artifact-routes.ts';
import { loadCurrentModelArtifact as loadModelArtifact } from './composed-models.ts';
import type { CompositionRecipe } from '../../shared/artifacts.ts';
import { ChatService } from './chat/service.ts';
import { PostgresChatStore } from './chat/postgres-store.ts';
import { ProcessChatWorkers } from './chat/workers.ts';
import { chatRoute } from './chat/routes.ts';
import { MemoryService } from './chat/memory.ts';
import { ResearchService } from './chat/research.ts';
import { configuredResearch } from './chat/search-provider.ts';

import {
    closeModelRepository,
    deleteModelArtifact,
    listModelArtifacts,
    saveModelArtifact,
} from "./model-repository.ts";
import {
    generateNativeModel,
    inspectNativeModel,
    type NativeModelConfig,
    nativePersistenceSchema,
    trainNativeModel,
} from "./native.ts";

const port = Number(process.env.PORT ?? "3000");
const maxRequestBytes = Number(process.env.MAX_REQUEST_BYTES ?? String(64 * 1024 * 1024));
const chatStore = new PostgresChatStore();
const chatOwner = process.env.CGAI_CHAT_OWNER_ID ?? 'local-owner';
const memory = new MemoryService(chatStore, chatOwner);
const researchConfiguration = configuredResearch();
const research = new ResearchService(memory, researchConfiguration.provider,
    process.env.CGAI_REPOSITORY_SNAPSHOT, researchConfiguration.fetchPage);
const chat = new ChatService(chatStore, new ProcessChatWorkers(), chatOwner, research);
await chat.recover();
let retentionTask: Promise<void> | undefined;
const retentionTimer = setInterval(() => {
    if (retentionTask) return;
    retentionTask = chat.prune().catch((error: unknown) => console.error('Chat retention failed', error))
        .finally(() => { retentionTask = undefined; });
}, 3600000);
retentionTimer.unref();

/** Serializes JSON while preserving C/SQL uint64 counters as decimal strings. */
function jsonValue(value: unknown): string {
    return JSON.stringify(value, (_key, item: unknown) =>
        typeof item === "bigint" ? item.toString() : item,
    );
}

/** Writes one JSON response with the API's stable media type and UTF-8 charset. */
function sendJson(response: ServerResponse, status: number, value: unknown): void {
    const body = jsonValue(value);
    response.writeHead(status, {
        "content-type": "application/json; charset=utf-8",
    });
    response.end(body);
}

/**
 * Reads the complete request body while enforcing the configured byte limit.
 *
 * @param request Incoming Node HTTP request whose stream is consumed exactly once.
 * @returns A newly allocated contiguous Buffer containing the request bytes.
 * @throws RangeError when the body exceeds `MAX_REQUEST_BYTES`; stream errors propagate.
 */
async function readBody(request: IncomingMessage): Promise<Buffer> {
    // Step 1: Accumulate chunks and track bytes independently; stream chunks are not guaranteed
    // to be Buffer instances and the limit must be checked before retaining another chunk.
    const chunks: Buffer[] = [];
    let length = 0;
    for await (const chunk of request) {
        const buffer = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
        length += buffer.byteLength;
        if (length > maxRequestBytes) {
            throw new RangeError(`request body exceeds ${maxRequestBytes} bytes`);
        }
        chunks.push(buffer);
    }
    // Step 2: Join the retained chunks once so callers can parse JSON or pass exact artifact bytes
    // to the native validator without changing their offsets or content.
    return Buffer.concat(chunks, length);
}

/** Parses one request body as JSON without weakening the caller's expected type contract. */
async function readJson<T>(request: IncomingMessage): Promise<T> {
    return JSON.parse((await readBody(request)).toString("utf8")) as T;
}

/** JSON body accepted by the model training endpoint. */
interface TrainRequest {
    readonly text: string;
    readonly config?: NativeModelConfig;
}

/** JSON body accepted by the model generation endpoint. */
interface GenerateRequest {
    readonly prompt: string;
    readonly maxTokens?: number;
    readonly temperature?: number;
    readonly seed?: string;
}

const versionedPrefix = ["api", "v1"];

function apiSegments(pathname: string): string[] {
    const segments = pathname.split("/").filter(Boolean).map(decodeURIComponent);
    return segments[0] === versionedPrefix[0] && segments[1] === versionedPrefix[1]
        ? segments.slice(2)
        : segments;
}

/** Maps a database row to the metadata shape exposed by catalog and inspection endpoints. */
function modelMetadata(model: {
    readonly id: string;
    readonly name: string;
    readonly formatVersion: number;
    readonly libraryVersion: string;
    readonly dimensions: number;
    readonly centroidCount: number;
    readonly contextWindow: number;
    readonly vocabularySize: bigint;
    readonly examplesSeen: bigint;
    readonly checksumSha256: string;
    readonly createdAt: string;
    readonly updatedAt: string;
    readonly compositionJson: string | null;
}) {
    const recipe = model.compositionJson ? JSON.parse(model.compositionJson) as CompositionRecipe : null;
    return {
        id: model.id,
        name: model.name,
        formatVersion: model.formatVersion,
        libraryVersion: model.libraryVersion,
        dimensions: model.dimensions,
        centroidCount: model.centroidCount,
        contextWindow: model.contextWindow,
        vocabularySize: model.vocabularySize,
        examplesSeen: model.examplesSeen,
        checksumSha256: model.checksumSha256,
        createdAt: model.createdAt,
        updatedAt: model.updatedAt,
        composition: recipe ? { autoRebuild: recipe.autoRebuild === true, sourceCount: recipe.sources.length } : null,
    };
}

/**
 * Dispatches one HTTP request to the versioned or compatibility API routes.
 *
 * The router deliberately keeps validation, native work, and persistence in explicit phases:
 * route selection happens first, request data is validated before native calls, native output is
 * inspected before persistence, and only then is the response published. A thrown error is left
 * for the outer handler to convert into the API's JSON error response.
 *
 * @param request Incoming request with a borrowed method, URL, headers, and body stream.
 * @param response Mutable response used exactly once by the selected route.
 * @returns A promise that settles after the response has been written.
 * @throws RangeError for oversized bodies and Error subclasses for malformed input/native/DB
 * failures; the outer server handler assigns their HTTP status.
 */
async function route(request: IncomingMessage, response: ServerResponse): Promise<void> {
    // Step 1: Normalize the request method, URL, and optional `/api/v1` prefix before deciding
    // which domain owns the request.
    const method = request.method ?? "GET";
    const url = new URL(request.url ?? "/", "http://localhost");
    const segments = apiSegments(url.pathname);
    if (await chatRoute(chat, request, response, segments, { readJson, sendJson })) return;

    if (method === "GET" && (url.pathname === "/health" || url.pathname === "/api/v1/health")) {
        sendJson(response, 200, {
            status: "ok",
            native: true,
            database: "postgresql",
        });
        return;
    }
    // Step 2: Serve system discovery endpoints without touching the model table.
    if (
        method === "GET" &&
        (url.pathname === "/native/schema" || url.pathname === "/api/v1/native/schema")
    ) {
        sendJson(response, 200, nativePersistenceSchema);
        return;
    }
    if (segments.length === 1 && segments[0] === "models" && method === "GET") {
        sendJson(response, 200, (await listModelArtifacts()).map(modelMetadata));
        return;
    }
    // Step 3: Reject paths outside the model domain and require a non-empty encoded model name.
    if (segments[0] !== "models") {
        sendJson(response, 404, { error: "route not found" });
        return;
    }
    if (!segments[1]) {
        sendJson(response, 404, { error: "model name is required" });
        return;
    }

    const name = segments[1];
    if (await artifactRoute(request, response, url, segments, sendJson, readJson)) return;
    if (segments.length === 2 && method === "PUT") {
        // Step 4: Validate uploaded bytes in C, then persist C-derived metadata with the payload.
        const payload = await readBody(request);
        const metadata = inspectNativeModel(payload);
        const saved = await saveModelArtifact({ name, payload, metadata });
        sendJson(response, 201, { id: saved.id, name: saved.name, metadata });
        return;
    }
    if (segments.length === 2 && method === "GET") {
        const model = await loadModelArtifact(name);
        if (!model) {
            sendJson(response, 404, { error: "model not found" });
            return;
        }
        response.writeHead(200, {
            "content-type": "application/vnd.centroid-gai.model",
            "content-length": String(model.payload.byteLength),
            etag: `"sha256:${model.checksumSha256}"`,
        });
        response.end(model.payload);
        return;
    }
    if (segments.length === 3 && segments[2] === "metadata" && method === "GET") {
        const model = await loadModelArtifact(name);
        if (!model) {
            sendJson(response, 404, { error: "model not found" });
            return;
        }
        sendJson(response, 200, modelMetadata(model));
        return;
    }
    if (segments.length === 2 && method === "DELETE") {
        const deleted = await deleteModelArtifact(name);
        sendJson(response, deleted ? 200 : 404, { deleted });
        return;
    }
    if (segments.length === 3 && segments[2] === "train" && method === "POST") {
        // Step 5: Validate JSON, train in C, inspect the result, and publish only after the upsert.
        const input = await readJson<TrainRequest>(request);
        if (typeof input.text !== "string" || input.text.length === 0) {
            throw new TypeError("training text must be a non-empty string");
        }
        const payload = trainNativeModel(input.text, input.config);
        const metadata = inspectNativeModel(payload);
        const saved = await saveModelArtifact({ name, payload, metadata });
        sendJson(response, 201, { id: saved.id, name: saved.name, metadata });
        return;
    }
    if (segments.length === 3 && segments[2] === "generate" && method === "POST") {
        // Step 6: Load the persisted artifact before asking C to generate from it.
        const input = await readJson<GenerateRequest>(request);
        if (typeof input.prompt !== "string") {
            throw new TypeError("prompt must be a string");
        }
        const model = await loadModelArtifact(name);
        if (!model) {
            sendJson(response, 404, { error: "model not found" });
            return;
        }
        const continuation = generateNativeModel(
            model.payload,
            input.prompt,
            input.maxTokens,
            input.temperature,
            input.seed === undefined ? 0n : BigInt(input.seed),
        );
        sendJson(response, 200, { name, prompt: input.prompt, continuation });
        return;
    }

    sendJson(response, 404, { error: "route not found" });
}

/** Converts route failures into the documented JSON error shape. */
const server = createServer((request, response) => {
    route(request, response).catch((error: unknown) => {
        const message = error instanceof Error ? error.message : "unknown error";
        sendJson(response, error instanceof RangeError ? 413 : 400, {
            error: message,
        });
    });
});

server.listen(port, "0.0.0.0", () => {
    console.log(`Centroid-GAI API listening on port ${port}`);
});

/** Stop admission, settle HTTP/native work, then release the shared database pool. */
async function stopServer(): Promise<void> {
    const drained = new Promise<void>((resolve, reject) => {
        server.close((error) => error ? reject(error) : resolve());
    });
    clearInterval(retentionTimer);
    await Promise.all([drained, chat.close(), retentionTask]);
    await closeModelRepository();
}

let shutdown: Promise<void> | undefined;
function requestShutdown(): void {
    shutdown ??= stopServer().catch((error: unknown) => {
        console.error('API shutdown failed', error);
        process.exitCode = 1;
    });
}
process.once("SIGINT", requestShutdown);
process.once("SIGTERM", requestShutdown);
