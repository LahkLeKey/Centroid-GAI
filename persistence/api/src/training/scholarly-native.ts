/** Process boundary: all learning, scoring and replay execute in the compiled C11 core. */
import { createHash } from 'node:crypto';
import { execFile } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import { promisify } from 'node:util';

export interface NativeScholarlyMetrics {
    readonly tokens: number;
    readonly unknownTokens: number;
    readonly crossEntropy: number;
    readonly accuracy: number;
}
export interface NativeScholarlyCandidate {
    readonly version: 1;
    readonly before: { readonly training: NativeScholarlyMetrics; readonly development: NativeScholarlyMetrics };
    readonly after: { readonly training: NativeScholarlyMetrics; readonly development: NativeScholarlyMetrics };
    readonly epochs: number;
    readonly steps: number;
}
export interface ScholarlyNative {
    fingerprint(): Promise<string>;
    init(train: string, checkpoint: string, signal: AbortSignal): Promise<void>;
    candidate(parent: string, train: string, development: string, output: string,
        epochs: number, rate: number, signal: AbortSignal): Promise<NativeScholarlyCandidate>;
    score(checkpoint: string, text: string, signal: AbortSignal): Promise<NativeScholarlyMetrics>;
    replay(checkpoint: string, train: string, output: string, rate: number, signal: AbortSignal): Promise<void>;
    export(checkpoint: string, output: string, signal: AbortSignal): Promise<void>;
}
const execute = promisify(execFile);
const object = (value: unknown): value is Record<string, unknown> => !!value && typeof value === 'object' && !Array.isArray(value);

export function nativeScholarlyMetrics(value: unknown): NativeScholarlyMetrics {
    if (!object(value) || !Number.isSafeInteger(value.tokens) || (value.tokens as number) < 2 ||
        !Number.isSafeInteger(value.unknownTokens) || (value.unknownTokens as number) < 0 ||
        (value.unknownTokens as number) >= (value.tokens as number) ||
        typeof value.crossEntropy !== 'number' || !Number.isFinite(value.crossEntropy) || value.crossEntropy < 0 ||
        typeof value.accuracy !== 'number' || !Number.isFinite(value.accuracy) || value.accuracy < 0 || value.accuracy > 1)
        throw new Error('invalid native scholarly scoring result');
    return { tokens: value.tokens as number, unknownTokens: value.unknownTokens as number,
        crossEntropy: value.crossEntropy, accuracy: value.accuracy };
}
export function nativeScholarlyCandidate(value: unknown): NativeScholarlyCandidate {
    if (!object(value) || value.version !== 1 || !object(value.before) || !object(value.after) ||
        !Number.isSafeInteger(value.epochs) || (value.epochs as number) < 1 ||
        !Number.isSafeInteger(value.steps) || (value.steps as number) < 2)
        throw new Error('invalid native scholarly candidate result');
    return { version: 1, before: { training: nativeScholarlyMetrics(value.before.training),
        development: nativeScholarlyMetrics(value.before.development) },
        after: { training: nativeScholarlyMetrics(value.after.training), development: nativeScholarlyMetrics(value.after.development) },
        epochs: value.epochs as number, steps: value.steps as number };
}

/** No shell interpolation. Native subprocesses have explicit time/output/cancellation limits. */
export class NativeScholarlyCli implements ScholarlyNative {
    private readonly executable: string;
    private readonly timeoutMs: number;
    constructor(executable: string, timeoutMs = 180000) {
        this.executable = executable; this.timeoutMs = timeoutMs;
    }
    async fingerprint(): Promise<string> {
        return createHash('sha256').update(await readFile(this.executable)).digest('hex');
    }
    private async run(args: readonly string[], signal: AbortSignal): Promise<string> {
        signal.throwIfAborted();
        const result = await execute(this.executable, [...args], {
            timeout: this.timeoutMs, maxBuffer: 1024 * 1024, encoding: 'utf8', windowsHide: true, signal,
        });
        return result.stdout;
    }
    async init(train: string, checkpoint: string, signal: AbortSignal): Promise<void> {
        await this.run(['neural-init', train, checkpoint], signal);
    }
    async candidate(parent: string, train: string, development: string, output: string,
        epochs: number, rate: number, signal: AbortSignal): Promise<NativeScholarlyCandidate> {
        return nativeScholarlyCandidate(JSON.parse(await this.run(['neural-candidate', parent, train, development, output,
            String(epochs), String(rate)], signal)));
    }
    async score(checkpoint: string, text: string, signal: AbortSignal): Promise<NativeScholarlyMetrics> {
        return nativeScholarlyMetrics(JSON.parse(await this.run(['neural-score', checkpoint, text], signal)));
    }
    async replay(checkpoint: string, train: string, output: string, rate: number, signal: AbortSignal): Promise<void> {
        await this.run(['neural-replay', checkpoint, train, output, String(rate)], signal);
    }
    async export(checkpoint: string, output: string, signal: AbortSignal): Promise<void> {
        await this.run(['neural-export', checkpoint, output], signal);
    }
}
