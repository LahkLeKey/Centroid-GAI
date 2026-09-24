/**
 * @file native.ts
 * @brief Thin, typed wrapper over the compiled `centroid_gai_native` Node-API addon.
 *
 * The addon (built from `native/*.c` plus the shared C sources under `src/abi*.c`) is the only
 * bridge between this package and the C library; every export here just narrows and forwards
 * calls into it. Keeping this file small means `model-repository.ts` and `server.ts` never touch
 * `node:module`/`require` or raw `Buffer` reinterpretation themselves.
 */
import {createRequire} from "node:module";

/** Configuration accepted by the native C model constructor. */
export interface NativeModelConfig {
    readonly dimensions?: number;
    readonly centroidCount?: number;
    readonly contextWindow?: number;
    readonly seed?: bigint;
}

/** Metadata returned by the native ABI and persisted through Prisma. */
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

/** Trains in native C and returns a database-ready binary artifact. */
export function trainNativeModel(text: string, config?: NativeModelConfig): Buffer {
    return config === undefined ? binding.trainModel(text) : binding.trainModel(text, config);
}

/** Validates a native artifact and extracts authoritative C metadata. */
export function inspectNativeModel(payload: Uint8Array): ModelMetadata {
    // `payload` may already be a `Buffer` (from an HTTP body) or a plain `Uint8Array` (from a
    // Prisma `Bytes` column); wrap it without copying so the addon sees the exact same bytes
    // C validated on write, then let C re-derive metadata rather than trusting caller-supplied
    // values.
    const native = binding.inspectModel(
        Buffer.from(payload.buffer, payload.byteOffset, payload.byteLength),
    );
    return {...native, libraryVersion : binding.libraryVersion()};
}

/** Runs generation in native C directly against a database artifact. */
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
