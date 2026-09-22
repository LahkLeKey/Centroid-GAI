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

const require = createRequire(import.meta.url);
const binding = require("../build/Release/centroid_gai_native.node") as NativeBinding;

if (binding.abiVersion() !== 2) {
    throw new Error(`unsupported Centroid-GAI native ABI ${binding.abiVersion()}`);
}

export const nativePersistenceSchema: unknown = JSON.parse(binding.persistenceSchema());

/** Trains in native C and returns a database-ready binary artifact. */
export function trainNativeModel(text: string, config?: NativeModelConfig): Buffer {
    return config === undefined ? binding.trainModel(text) : binding.trainModel(text, config);
}

/** Validates a native artifact and extracts authoritative C metadata. */
export function inspectNativeModel(payload: Uint8Array): ModelMetadata {
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
