/**
 * @file native.ts
 * @brief Thin, typed wrapper over the compiled `centroid_gai_native` Node-API addon.
 *
 * The addon (built from `native/*.c` plus the shared C sources under `src/abi*.c`) is the only
 * bridge between this package and the C library; every export here just narrows and forwards
 * calls into it. Keeping this file small means `model-repository.ts` and `server.ts` never touch
 * `node:module`/`require` or raw `Buffer` reinterpretation themselves.
 */
import { createRequire } from "node:module";
import type { ContentsBySection, ContentsSection, PatternMatches } from '../../shared/artifacts.ts';

/** Configuration accepted by the native C model constructor; omitted fields use C defaults. */
export interface NativeModelConfig {
    readonly dimensions?: number;
    readonly centroidCount?: number;
    readonly contextWindow?: number;
    readonly seed?: bigint;
}

/**
 * Metadata returned by the native ABI and persisted through Prisma.
 *
 * Numeric counts are bigint values at the JavaScript boundary because the C ABI exposes uint64
 * counters without lossy conversion. The HTTP serializer later renders those values as decimal
 * strings, while the database contract preserves them as PostgreSQL bigint values.
 */
export interface ModelMetadata {
    readonly formatVersion: number;
    readonly libraryVersion: string;
    readonly dimensions: number;
    readonly centroidCount: number;
    readonly contextWindow: number;
    readonly vocabularySize: bigint;
    readonly examplesSeen: bigint;
}

interface NativeBinding {
    matchPatterns(payload: Buffer, text: string, limit: number): string;
    inspectContents(payload: Buffer, section: number, offset: number, limit: number, centroid: number): string;
    mergeModels(payloads: Buffer[], targetCentroids: number): Buffer;
    abiVersion(): number;
    libraryVersion(): string;
    persistenceSchema(): string;
    trainModel(text: string, config?: NativeModelConfig): Buffer;
    inspectModel(payload: Buffer): Omit<ModelMetadata, "libraryVersion">;
    generateModel(
        payload: Buffer,
        prompt: string,
        maxTokens: number,
        temperature: number,
        seed: bigint,
    ): string;
}

// Step 1: Load the compiled `.node` addon once at module load. `createRequire` is needed because
// this package is ESM (`"type": "module"`) and native addons are still loaded through CommonJS
// `require`.
const require = createRequire(import.meta.url);
const binding = require("../build/Release/centroid_gai_native.node") as NativeBinding;

// Step 2: Pin the expected ABI version so a mismatched native rebuild fails loudly at startup
// instead of producing subtly wrong bytes at request time.
if (binding.abiVersion() !== 2) {
    throw new Error(`unsupported Centroid-GAI native ABI ${binding.abiVersion()}`);
}

/** JSON Schema for the persisted artifact, exported by C and re-served at `/native/schema`. */
export const nativePersistenceSchema: unknown = JSON.parse(binding.persistenceSchema());

/**
 * Trains in native C and returns a database-ready binary artifact.
 *
 * @param text Borrowed non-empty training corpus text; C reads it before this call returns.
 * @param config Optional borrowed configuration. Omitted fields retain the native defaults.
 * @returns A newly allocated Node Buffer containing one complete `.cgai` artifact.
 * @throws A native binding error when the corpus or configuration is rejected.
 */
export function trainNativeModel(text: string, config?: NativeModelConfig): Buffer {
    return config === undefined ? binding.trainModel(text) : binding.trainModel(text, config);
}

const contentsSections = { summary: 0, vocabulary: 1, centroids: 2, centroid: 3, highlights: 4 } as const;
export function inspectNativeContents<S extends ContentsSection>(payload: Uint8Array, section: S, offset = 0, limit = 25, centroid = 0): ContentsBySection[S] {
    return JSON.parse(binding.inspectContents(Buffer.from(payload.buffer, payload.byteOffset, payload.byteLength), contentsSections[section], offset, limit, centroid)) as ContentsBySection[S];
}

export function mergeNativeModels(payloads: Uint8Array[], targetCentroids = 0): Buffer {
    return binding.mergeModels(payloads.map((payload) => Buffer.from(payload.buffer, payload.byteOffset, payload.byteLength)), targetCentroids);
}

export function matchNativePatterns(payload: Uint8Array, text: string, limit = 5): PatternMatches {
    if (typeof text !== 'string' || !text.trim() || text.includes('\0') || Buffer.byteLength(text, 'utf8') > 16384)
        throw new TypeError('Matching text must contain 1 to 16384 UTF-8 bytes and no NUL characters');
    return JSON.parse(binding.matchPatterns(Buffer.from(payload.buffer, payload.byteOffset, payload.byteLength), text, limit)) as PatternMatches;
}

/**
 * Validates a native artifact and extracts authoritative C metadata.
 *
 * @param payload Borrowed artifact bytes. The view may be a Buffer or a subrange of another
 * Uint8Array; its byte offset and byte length are preserved when crossing into C.
 * @returns Metadata decoded from the artifact header and the loaded native library version.
 * @throws A native binding error when magic, version, bounds, or payload structure is invalid.
 */
export function inspectNativeModel(payload: Uint8Array): ModelMetadata {
    // `payload` may already be a `Buffer` (from an HTTP body) or a plain `Uint8Array` (from a
    // Prisma `Bytes` column); wrap it without copying so the addon sees the exact same bytes
    // C validated on write, then let C re-derive metadata rather than trusting caller-supplied
    // values.
    const native = binding.inspectModel(
        Buffer.from(payload.buffer, payload.byteOffset, payload.byteLength),
    );
    return { ...native, libraryVersion: binding.libraryVersion() };
}

/**
 * Runs generation in native C directly against a database artifact.
 *
 * @param payload Borrowed, previously validated `.cgai` bytes.
 * @param prompt Borrowed prompt text passed to the native generation workspace.
 * @param maxTokens Maximum number of generated tokens; defaults to 40.
 * @param temperature Sampling temperature; zero selects deterministic greedy generation.
 * @param seed Deterministic sampling seed; zero is the default seed.
 * @returns Generated UTF-8 text owned by the returned JavaScript string.
 * @throws A native binding error when the artifact or generation arguments are invalid.
 */
export function generateNativeModel(
    payload: Uint8Array,
    prompt: string,
    maxTokens = 40,
    temperature = 0.8,
    seed = 0n,
): string {
    return binding.generateModel(
        Buffer.from(payload.buffer, payload.byteOffset, payload.byteLength),
        prompt,
        maxTokens,
        temperature,
        seed,
    );
}
