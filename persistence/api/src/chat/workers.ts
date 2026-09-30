import { fork } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import type { ChatDialogueMessage, ChatExample, ChatModelConfig, ChatSendRequest, ChatTrainingOptions } from '../../../shared/chat.ts';

export type ChatWorkerTask =
    | { kind: 'inspect'; payload: Buffer }
    | { kind: 'reply'; payload: Buffer; messages: readonly ChatDialogueMessage[]; options: Pick<ChatSendRequest, 'maxTokens' | 'temperature' | 'seed'> }
    | { kind: 'train'; examples: readonly ChatExample[]; config?: ChatModelConfig; training?: ChatTrainingOptions };
export interface ChatWorkers {
    run<T>(task: ChatWorkerTask, signal?: AbortSignal): Promise<T>;
    close(): void | Promise<void>;
}
export class ChatWorkerError extends Error {
    readonly status: number;
    constructor(message: string, status = 400) { super(message); this.status = status; }
}
interface Pending {
    readonly task: ChatWorkerTask;
    readonly signal: AbortSignal | undefined;
    readonly resolve: (value: unknown) => void;
    readonly reject: (error: Error) => void;
}

/** Separate training and inference lanes keep long training calls from occupying reply slots. */
export class ProcessChatWorkers implements ChatWorkers {
    private readonly queued: Pending[] = [];
    private readonly active = new Map<Pending, () => void>();
    private readonly exits = new Set<Promise<void>>();
    private closed = false;
    private readonly workerPath: string;
    private readonly timeoutMs: number;
    private readonly trainTimeoutMs: number;
    constructor(options: { workerPath?: string; timeoutMs?: number; trainTimeoutMs?: number } = {}) {
        this.workerPath = options.workerPath ?? fileURLToPath(new URL('./worker.ts', import.meta.url));
        this.timeoutMs = options.timeoutMs ?? 30_000;
        this.trainTimeoutMs = options.trainTimeoutMs ?? 600_000;
    }
    run<T>(task: ChatWorkerTask, signal?: AbortSignal): Promise<T> {
        if (this.closed) return Promise.reject(new ChatWorkerError('chat workers are shutting down', 503));
        if (signal?.aborted) return Promise.reject(new ChatWorkerError('request cancelled', 409));
        if (this.queued.length >= 16) return Promise.reject(new ChatWorkerError('chat worker queue is full', 429));
        return new Promise<T>((resolve, reject) => {
            const pending: Pending = { task, signal, resolve: (value) => resolve(value as T), reject };
            const resolvePending = pending.resolve;
            const rejectPending = pending.reject;
            const abortQueued = () => {
                const index = this.queued.indexOf(pending);
                if (index >= 0) {
                    this.queued.splice(index, 1);
                    signal?.removeEventListener('abort', abortQueued);
                    reject(new ChatWorkerError('request cancelled', 409));
                }
            };
            signal?.addEventListener('abort', abortQueued, { once: true });
            // Remove the queue abort listener when either execution path settles.
            const wrapped: Pending = { ...pending, resolve: (value) => { signal?.removeEventListener('abort', abortQueued); resolvePending(value); }, reject: (error) => { signal?.removeEventListener('abort', abortQueued); rejectPending(error); } };
            // Keep the same identity used by abortQueued.
            Object.assign(pending, wrapped);
            this.queued.push(pending);
            this.drain();
        });
    }
    private drain(): void {
        if (this.closed) return;
        const training = [...this.active.keys()].filter((item) => item.task.kind === 'train').length;
        const inference = this.active.size - training;
        const index = this.queued.findIndex((item) => item.task.kind === 'train' ? training < 1 : inference < 2);
        if (index < 0) return;
        const pending = this.queued.splice(index, 1)[0]!;
        let child: ReturnType<typeof fork>;
        try { child = fork(this.workerPath, [], { serialization: 'advanced', stdio: ['ignore', 'ignore', 'pipe', 'ipc'], execArgv: ['--max-old-space-size=512'] }); }
        catch (error) {
            pending.reject(error instanceof Error ? error : new ChatWorkerError('could not start native chat worker', 500));
            this.drain();
            return;
        }
        let result: { error: Error | null; value?: unknown } | undefined;
        let stderr = '';
        child.stderr?.on('data', (chunk: Buffer) => { stderr = (stderr + chunk.toString()).slice(-2048); });
        const finish = (error: Error | null, value?: unknown) => {
            if (result) return;
            result = { error, value };
            clearTimeout(timer);
            pending.signal?.removeEventListener('abort', cancel);
            // Keep the lane occupied until the OS confirms termination. A timed-out native
            // operation must not keep consuming resources while its replacement starts.
            child.kill('SIGKILL');
        };
        const cancel = () => finish(new ChatWorkerError('request cancelled', 409));
        const timer = setTimeout(() => finish(new ChatWorkerError('native chat operation timed out', 504)), pending.task.kind === 'train' ? this.trainTimeoutMs : this.timeoutMs);
        this.active.set(pending, cancel);
        pending.signal?.addEventListener('abort', cancel, { once: true });
        child.once('error', (error) => finish(error));
        child.once('exit', (code) => finish(new ChatWorkerError(`native chat worker exited (${code ?? 'signal'})${stderr ? `: ${stderr}` : ''}`, 500)));
        child.once('message', (message: unknown) => {
            if (!message || typeof message !== 'object' || !('ok' in message) || typeof message.ok !== 'boolean') {
                finish(new ChatWorkerError('invalid native chat worker response', 500));
                return;
            }
            const response = message as { ok: boolean; value?: unknown; error?: unknown };
            finish(response.ok ? null : new ChatWorkerError(typeof response.error === 'string' ? response.error : 'native operation failed'), response.value);
        });
        const exited = new Promise<void>((resolve) => child.once('close', (code) => {
            if (!result) finish(new ChatWorkerError(`native chat worker closed (${code ?? 'signal'})`, 500));
            this.active.delete(pending);
            if (result!.error) pending.reject(result!.error); else pending.resolve(result!.value);
            resolve();
            this.drain();
        }));
        this.exits.add(exited);
        void exited.then(() => this.exits.delete(exited));
        try { child.send(pending.task, (error) => { if (error) finish(error); }); }
        catch (error) { finish(error instanceof Error ? error : new ChatWorkerError('native chat worker IPC failed', 500)); }
        if (pending.signal?.aborted) cancel();
        this.drain();
    }
    async close(): Promise<void> {
        this.closed = true;
        for (const pending of this.queued.splice(0)) pending.reject(new ChatWorkerError('chat workers are shutting down', 503));
        for (const cancel of [...this.active.values()]) cancel();
        await Promise.all(this.exits);
    }
}
