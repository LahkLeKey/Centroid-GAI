import {createServer, type IncomingMessage, type ServerResponse} from "node:http";

import {
    closeModelRepository,
    deleteModelArtifact,
    listModelArtifacts,
    loadModelArtifact,
    saveModelArtifact,
} from "./model-repository.ts";
import {
    generateNativeModel,
    inspectNativeModel,
    type NativeModelConfig,
    nativePersistenceSchema,
    trainNativeModel,
} from "./native.ts";

const port = Number(process.env["PORT"] ?? "3000");
const maxRequestBytes = Number(process.env["MAX_REQUEST_BYTES"] ?? String(64 * 1024 * 1024));

function jsonValue(value: unknown): string {
    return JSON.stringify(
        value,
        (_key, item: unknown) => typeof item === "bigint" ? item.toString() : item,
    );
}

function sendJson(response: ServerResponse, status: number, value: unknown): void {
    const body = jsonValue(value);
    response.writeHead(status, {"content-type" : "application/json; charset=utf-8"});
    response.end(body);
}

async function readBody(request: IncomingMessage): Promise<Buffer> {
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
    return Buffer.concat(chunks, length);
}

async function readJson<T>(request: IncomingMessage): Promise<T> {
    return JSON.parse((await readBody(request)).toString("utf8")) as T;
}

interface TrainRequest {
    readonly text: string;
    readonly config?: NativeModelConfig;
}

interface GenerateRequest {
    readonly prompt: string;
    readonly maxTokens?: number;
    readonly temperature?: number;
    readonly seed?: string;
}

const versionedPrefix = [ "api", "v1" ];

function apiSegments(pathname: string): string[] {
    const segments = pathname.split("/").filter(Boolean).map(decodeURIComponent);
    return segments[0] === versionedPrefix[0] && segments[1] === versionedPrefix[1]
               ? segments.slice(2)
               : segments;
}

function modelMetadata(model: {
    readonly id: string; readonly name : string; readonly formatVersion : number; readonly libraryVersion : string; readonly dimensions : number; readonly centroidCount : number; readonly contextWindow : number; readonly vocabularySize : bigint; readonly examplesSeen : bigint; readonly checksumSha256 : string; readonly createdAt : string; readonly updatedAt :
                                                                                                                                                                                                                                                                                                                                                                  string;
}) {
    return {
        id : model.id,
        name : model.name,
        formatVersion : model.formatVersion,
        libraryVersion : model.libraryVersion,
        dimensions : model.dimensions,
        centroidCount : model.centroidCount,
        contextWindow : model.contextWindow,
        vocabularySize : model.vocabularySize,
        examplesSeen : model.examplesSeen,
        checksumSha256 : model.checksumSha256,
        createdAt : model.createdAt,
        updatedAt : model.updatedAt,
    };
}

async function route(request: IncomingMessage, response: ServerResponse): Promise<void> {
    const method = request.method ?? "GET";
    const url = new URL(request.url ?? "/", "http://localhost");
    const segments = apiSegments(url.pathname);

    if (method === "GET" && (url.pathname === "/health" || url.pathname === "/api/v1/health")) {
        sendJson(response, 200, {status : "ok", native : true, database : "postgresql"});
        return;
    }
    if (method === "GET" &&
        (url.pathname === "/native/schema" || url.pathname === "/api/v1/native/schema")) {
        sendJson(response, 200, nativePersistenceSchema);
        return;
    }
    if (segments.length === 1 && segments[0] === "models" && method === "GET") {
        sendJson(response, 200, (await listModelArtifacts()).map(modelMetadata));
        return;
    }
    if (segments[0] !== "models") {
        sendJson(response, 404, {error : "route not found"});
        return;
    }
    if (!segments[1]) {
        sendJson(response, 404, {error : "model name is required"});
        return;
    }

    const name = segments[1];
    if (segments.length === 2 && method === "PUT") {
        const payload = await readBody(request);
        const metadata = inspectNativeModel(payload);
        const saved = await saveModelArtifact({name, payload, metadata});
        sendJson(response, 201, {id : saved.id, name : saved.name, metadata});
        return;
    }
    if (segments.length === 2 && method === "GET") {
        const model = await loadModelArtifact(name);
        if (!model) {
            sendJson(response, 404, {error : "model not found"});
            return;
        }
        response.writeHead(200, {
            "content-type" : "application/vnd.centroid-gai.model",
            "content-length" : String(model.payload.byteLength),
            etag : `"sha256:${model.checksumSha256}"`,
        });
        response.end(model.payload);
        return;
    }
    if (segments.length === 3 && segments[2] === "metadata" && method === "GET") {
        const model = await loadModelArtifact(name);
        if (!model) {
            sendJson(response, 404, {error : "model not found"});
            return;
        }
        sendJson(response, 200, modelMetadata(model));
        return;
    }
    if (segments.length === 2 && method === "DELETE") {
        const deleted = await deleteModelArtifact(name);
        sendJson(response, deleted ? 200 : 404, {deleted});
        return;
    }
    if (segments.length === 3 && segments[2] === "train" && method === "POST") {
        const input = await readJson<TrainRequest>(request);
        if (typeof input.text !== "string" || input.text.length === 0) {
            throw new TypeError("training text must be a non-empty string");
        }
        const payload = trainNativeModel(input.text, input.config);
        const metadata = inspectNativeModel(payload);
        const saved = await saveModelArtifact({name, payload, metadata});
        sendJson(response, 201, {id : saved.id, name : saved.name, metadata});
        return;
    }
    if (segments.length === 3 && segments[2] === "generate" && method === "POST") {
        const input = await readJson<GenerateRequest>(request);
        if (typeof input.prompt !== "string") {
            throw new TypeError("prompt must be a string");
        }
        const model = await loadModelArtifact(name);
        if (!model) {
            sendJson(response, 404, {error : "model not found"});
            return;
        }
        const continuation = generateNativeModel(
            model.payload,
            input.prompt,
            input.maxTokens,
            input.temperature,
            input.seed === undefined ? 0n : BigInt(input.seed),
        );
        sendJson(response, 200, {name, prompt : input.prompt, continuation});
        return;
    }

    sendJson(response, 404, {error : "route not found"});
}

const server = createServer((request, response) => {
    route(request, response).catch((error: unknown) => {
        const message = error instanceof Error ? error.message : "unknown error";
        sendJson(response, error instanceof RangeError ? 413 : 400, {error : message});
    });
});

server.listen(port, "0.0.0.0",
              () => { console.log(`Centroid-GAI API listening on port ${port}`); });

async function shutdown(): Promise<void> {
    server.close();
    await closeModelRepository();
}

process.once("SIGINT", () => void shutdown());
process.once("SIGTERM", () => void shutdown());
