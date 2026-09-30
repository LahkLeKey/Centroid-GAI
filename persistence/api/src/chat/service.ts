import { createHash, randomUUID } from 'node:crypto';
import type { ChatMessage, ChatReply, ChatSendRequest, ChatSendResponse, ChatTrainRequest, ChatTrainingJob, Conversation } from '../../../shared/chat.ts';
import type { ChatTrainResult } from '../chat-native.ts';
import type { ChatStore } from './store.ts';
import { ChatWorkerError, type ChatWorkers } from './workers.ts';
import type { ResearchService } from './research.ts';

export class ChatServiceError extends Error {
    readonly status: number;
    constructor(message: string, status = 400) { super(message); this.status = status; }
}
export function chatText(value: unknown, label: string, maximum = 65536): string {
    if (typeof value !== 'string' || !value.trim() || value.includes('\0') || Buffer.byteLength(value) > maximum)
        throw new ChatServiceError(`${label} must be nonempty text of at most ${maximum} bytes`);
    return value;
}
function chatSeed(value: unknown, label: string): void {
    if (value !== undefined && (typeof value !== 'string' || !/^\d{1,20}$/.test(value) || BigInt(value) > 18446744073709551615n))
        throw new ChatServiceError(`${label} must be an unsigned 64-bit decimal string`);
}
/** Validate JSON types at admission; the native model remains authoritative for shape limits. */
function trainingSettings(value: unknown, label: string, numericFields: readonly string[]): void {
    if (value === undefined) return;
    if (!value || typeof value !== 'object' || Array.isArray(value)) throw new ChatServiceError(`${label} must be an object`);
    const settings = value as Record<string, unknown>;
    for (const field of numericFields) {
        const number = settings[field];
        if (number !== undefined && (typeof number !== 'number' || !Number.isFinite(number)))
            throw new ChatServiceError(`${label}.${field} must be a finite number`);
    }
    chatSeed(settings.seed, `${label}.seed`);
}
const now = () => new Date().toISOString();
const fingerprint = (value: unknown) => createHash('sha256').update(JSON.stringify(value)).digest('hex');
interface Active { requestId: string; fingerprint: string; controller: AbortController; promise: Promise<ChatSendResponse> }

/** One API process owns execution; durable compare-and-swap protects transcript writes. */
export class ChatService {
    private readonly active = new Map<string, Active>();
    private readonly jobs = new Set<Promise<void>>();
    private readonly operations = new Set<Promise<unknown>>();
    private trainingAdmissions = 0;
    private closed = false;
    readonly store: ChatStore;
    private readonly workers: ChatWorkers;
    readonly ownerId: string;
    readonly research: ResearchService | undefined;
    constructor(store: ChatStore, workers: ChatWorkers, ownerId: string, research?: ResearchService) {
        this.store = store; this.workers = workers; this.ownerId = ownerId; this.research = research;
        chatText(ownerId, 'configured owner', 256);
    }
    private assertOpen(): void {
        if (this.closed) throw new ChatServiceError('service shutting down', 503);
    }
    /** Track admission as well as execution so shutdown cannot close storage underneath it. */
    private operation<T>(run: () => Promise<T>): Promise<T> {
        if (this.closed) return Promise.reject(new ChatServiceError('service shutting down', 503));
        const operation = Promise.resolve().then(() => { this.assertOpen(); return run(); });
        this.operations.add(operation);
        void operation.then(() => this.operations.delete(operation), () => this.operations.delete(operation));
        return operation;
    }
    /** Run before listening. Interrupted work becomes explicit failure, never completed context. */
    async recover(): Promise<void> {
        await this.research?.memory.prune();
        for (const conversation of await this.store.listConversations()) {
            if (conversation.ownerId !== this.ownerId || this.active.has(conversation.id)) continue;
            if (!conversation.messages.some((message) => message.status === 'pending')) continue;
            const messages = conversation.messages.map((message): ChatMessage => message.status === 'pending'
                ? { ...message, status: 'error', finishReason: 'error', error: 'service restarted during request' } : message);
            await this.commit(conversation, messages);
        }
        for (const job of await this.store.listJobs()) {
            if (job.status === 'running' || job.status === 'queued')
                await this.store.saveJob({ ...job, status: 'error', error: 'service restarted during training', updatedAt: now() });
        }
        await this.prune();
    }
    /** Retention removes whole inactive transcripts and their solely derived memories. */
    async prune(): Promise<void> {
        const memory = this.research?.memory;
        if (!memory) return;
        const settings = await memory.read();
        const cutoff = Date.now() - settings.retentionDays * 86400000;
        for (const conversation of await this.store.listConversations()) {
            if (conversation.ownerId !== this.ownerId || this.active.has(conversation.id) ||
                conversation.messages.some((message) => message.status === 'pending') || Date.parse(conversation.updatedAt) >= cutoff) continue;
            if (await this.store.deleteConversation(conversation.id, conversation.revision)) await memory.forget(undefined, conversation.id);
        }
        await memory.prune();
    }
    async conversation(id: string): Promise<Conversation> {
        const value = await this.store.getConversation(id);
        if (!value || value.ownerId !== this.ownerId) throw new ChatServiceError('conversation not found', 404);
        return value;
    }
    create(modelName?: string, title = 'Conversation'): Promise<Conversation> {
        return this.operation(() => this.createConversation(modelName, title));
    }
    private async createConversation(modelName: string | undefined, title: string): Promise<Conversation> {
        if (modelName !== undefined) chatText(modelName, 'modelName', 200);
        chatText(title, 'title', 200);
        const model = modelName === undefined ? null : await this.store.findModel(modelName);
        this.assertOpen();
        if (modelName !== undefined && (!model || model.metadata.engineKind !== 'neural-centroid-chat'))
            throw new ChatServiceError('chat model not found', 404);
        const timestamp = now();
        const conversation: Conversation = { id: randomUUID(), ownerId: this.ownerId, title, modelName: modelName ?? null,
            modelChecksum: model?.checksumSha256 ?? null, protocolVersion: model?.metadata.protocolVersion ?? 1,
            revision: 0, createdAt: timestamp, updatedAt: timestamp, messages: [] };
        await this.store.createConversation(conversation);
        return conversation;
    }
    private validateSend(input: ChatSendRequest): string {
        if (!input || typeof input !== 'object') throw new ChatServiceError('message request required');
        chatText(input.requestId, 'requestId', 200);
        chatText(input.content, 'content');
        if (!Number.isSafeInteger(input.revision) || input.revision < 0) throw new ChatServiceError('invalid revision');
        if (input.maxTokens !== undefined && (!Number.isInteger(input.maxTokens) || input.maxTokens < 0 || input.maxTokens > 4096))
            throw new ChatServiceError('maxTokens must be 0..4096');
        if (input.temperature !== undefined && (!Number.isFinite(input.temperature) || input.temperature < 0 || input.temperature > 100))
            throw new ChatServiceError('temperature must be 0..100');
        chatSeed(input.seed, 'seed');
        if (input.answerMode !== undefined && !['sources', 'neural'].includes(input.answerMode)) throw new ChatServiceError('invalid answerMode');
        for (const option of ['autoSearch', 'rememberSources'] as const)
            if (input[option] !== undefined && typeof input[option] !== 'boolean') throw new ChatServiceError(`${option} must be a boolean`);
        if (input.publicQuery !== undefined) chatText(input.publicQuery, 'publicQuery', 512);
        if (input.applicability !== undefined) chatText(input.applicability, 'applicability', 512);
        return fingerprint([input.content, input.maxTokens ?? 128, input.temperature ?? 0, input.seed ?? '0', input.autoSearch ?? true,
            input.rememberSources ?? false, input.publicQuery ?? '', input.applicability ?? '', input.answerMode ?? 'sources']);
    }
    private async commit(previous: Conversation, messages: readonly ChatMessage[]): Promise<Conversation> {
        const next = { ...previous, messages, revision: previous.revision + 1, updatedAt: now() };
        if (!await this.store.saveConversation(next, previous.revision)) throw new ChatServiceError('stale conversation revision', 409);
        return next;
    }
    send(id: string, input: ChatSendRequest): Promise<ChatSendResponse> {
        return this.operation(() => this.sendMessage(id, input));
    }
    private async sendMessage(id: string, input: ChatSendRequest): Promise<ChatSendResponse> {
        const requestFingerprint = this.validateSend(input);
        const conversation = await this.conversation(id);
        this.assertOpen();
        const running = this.active.get(id);
        if (running?.requestId === input.requestId) {
            if (running.fingerprint !== requestFingerprint) throw new ChatServiceError('requestId already used for different input', 409);
            return running.promise;
        }
        const prior = conversation.messages.find((message) => message.requestId === input.requestId);
        if (prior) {
            if (prior.requestFingerprint !== requestFingerprint) throw new ChatServiceError('requestId already used for different input', 409);
            const active = this.active.get(id);
            return active?.requestId === input.requestId ? active.promise : { conversation, requestId: input.requestId };
        }
        if (!conversation.modelChecksum && (input.answerMode === 'neural' || !this.research))
            throw new ChatServiceError(input.answerMode === 'neural'
                ? 'neural answers require a conversation created with a trained modelName'
                : 'research is unavailable for this conversation', input.answerMode === 'neural' ? 400 : 503);
        if (conversation.revision !== input.revision) throw new ChatServiceError('stale conversation revision', 409);
        if (this.active.has(id) || conversation.messages.some((message) => message.status === 'pending'))
            throw new ChatServiceError('conversation already has an active request', 409);
        if (conversation.messages.length >= 1000 || Buffer.byteLength(JSON.stringify(conversation)) > 8 * 1024 * 1024)
            throw new ChatServiceError('conversation retention limit reached', 413);
        const controller = new AbortController();
        const operation = this.execute(conversation, input, requestFingerprint, controller.signal);
        this.active.set(id, { requestId: input.requestId, fingerprint: requestFingerprint, controller, promise: operation });
        try { return await operation; }
        finally { this.active.delete(id); }
    }
    private async execute(conversation: Conversation, input: ChatSendRequest, requestFingerprint: string, signal: AbortSignal): Promise<ChatSendResponse> {
        const base = { requestId: input.requestId, requestFingerprint, createdAt: now() };
        const user: ChatMessage = { ...base, id: randomUUID(), sequence: conversation.messages.length,
            role: 'user', content: input.content, status: 'pending' };
        const assistant: ChatMessage = { ...base, id: randomUUID(), sequence: user.sequence + 1,
            role: 'assistant', content: '', status: 'pending' };
        let pending: Conversation;
        try { pending = await this.commit(conversation, [...conversation.messages, user, assistant]); }
        catch (error) {
            // A delayed snapshot can arrive after an identical request has already completed.
            // Resolve that retry from the authoritative transcript instead of generating twice.
            if (error instanceof ChatServiceError && error.status === 409) {
                const current = await this.conversation(conversation.id);
                const previous = current.messages.find((message) => message.requestId === input.requestId);
                if (previous?.requestFingerprint === requestFingerprint) return { conversation: current, requestId: input.requestId };
                if (previous) throw new ChatServiceError('requestId already used for different input', 409);
            }
            throw error;
        }
        let completed: ChatMessage;
        try {
            signal.throwIfAborted();
            if (input.answerMode !== 'neural' && this.research) {
                const answer = await this.research.answer(input, conversation.id, signal);
                signal.throwIfAborted();
                completed = { ...assistant, ...answer, status: 'complete', finishReason: answer.research.mode === 'clarification' ? 'clarification' :
                    answer.research.mode === 'abstained' ? 'abstained' : 'sources',
                    usage: { generatedTokens: 0, promptTokens: 0, droppedMessages: 0, unknownTokens: 0 } };
            } else {
                if (!conversation.modelChecksum) throw new Error('conversation has no pinned neural model');
                const artifact = await this.store.getArtifact(conversation.modelChecksum);
                signal.throwIfAborted();
                if (!artifact || fingerprintBytes(artifact.payload) !== conversation.modelChecksum)
                    throw new Error('missing or corrupt pinned chat artifact');
                const history = conversation.messages.filter((message) => message.status === 'complete');
                const reply = await this.workers.run<ChatReply>({ kind: 'reply', payload: artifact.payload,
                    messages: [...history, { role: 'user', content: input.content }], options: input }, signal);
                signal.throwIfAborted();
                completed = { ...assistant, status: 'complete', content: reply.content, finishReason: reply.finishReason,
                    usage: { generatedTokens: reply.generatedTokens, promptTokens: reply.promptTokens,
                        droppedMessages: reply.droppedMessages, unknownTokens: reply.unknownTokens } };
            }
        } catch (error) {
            const cancelled = signal.aborted;
            completed = { ...assistant, status: cancelled ? 'cancelled' : 'error', finishReason: cancelled ? 'cancelled' : 'error',
                error: error instanceof Error ? error.message : 'chat request failed' };
        }
        const result = await this.commit(pending, [...conversation.messages,
            { ...user, status: completed.status }, completed]);
        return { conversation: result, requestId: input.requestId };
    }
    async cancel(id: string, requestId: string): Promise<Conversation> {
        await this.conversation(id);
        const active = this.active.get(id);
        if (!active || active.requestId !== requestId) throw new ChatServiceError('active request not found', 404);
        active.controller.abort();
        return (await active.promise).conversation;
    }
    async remove(id: string, revision: number, forgetMemory = false): Promise<void> {
        const conversation = await this.conversation(id);
        if (this.active.has(id) || conversation.messages.some((message) => message.status === 'pending'))
            throw new ChatServiceError('cancel active request before deleting', 409);
        if (conversation.revision !== revision || !await this.store.deleteConversation(id, revision))
            throw new ChatServiceError('stale conversation revision', 409);
        if (forgetMemory) await this.research?.memory.forget(undefined, id);
    }
    train(input: ChatTrainRequest): Promise<ChatTrainingJob> {
        return this.operation(() => this.queueTraining(input));
    }
    private async queueTraining(input: ChatTrainRequest): Promise<ChatTrainingJob> {
        if (this.jobs.size + this.trainingAdmissions >= 16) throw new ChatServiceError('training queue is full', 429);
        chatText(input?.name, 'name', 200);
        trainingSettings(input.config, 'config', ['embeddingDimensions', 'hiddenDimensions', 'centroidCount', 'promptWindow', 'responseWindow', 'routingTemperature']);
        trainingSettings(input.training, 'training', ['epochs', 'learningRate']);
        if (!Array.isArray(input.examples) || !input.examples.length || input.examples.length > 10000 ||
            Buffer.byteLength(JSON.stringify(input)) > 16 * 1024 * 1024) throw new ChatServiceError('invalid bounded training dataset');
        const ids = new Set<string>(), dialogues = new Set<string>();
        for (const example of input.examples) {
            if (!example || !Array.isArray(example.messages) || typeof example.answer !== 'string') throw new ChatServiceError('invalid training example');
            if (example.id && ids.has(example.id)) throw new ChatServiceError('duplicate training example ID');
            if (example.id) ids.add(example.id);
            const identity = fingerprint(example.messages);
            if (dialogues.has(identity)) throw new ChatServiceError('duplicate training dialogue');
            dialogues.add(identity);
        }
        const job: ChatTrainingJob = { id: randomUUID(), modelName: input.name, status: 'queued', createdAt: now(), updatedAt: now() };
        ++this.trainingAdmissions;
        try {
            await this.store.saveJob(job);
            if (this.closed) {
                await this.store.saveJob({ ...job, status: 'error', error: 'service shutting down', updatedAt: now() });
                this.assertOpen();
            }
            const operation = this.runTraining(job, input);
            this.jobs.add(operation);
            void operation.finally(() => this.jobs.delete(operation)).catch(() => {});
            return job;
        } finally { --this.trainingAdmissions; }
    }
    private async runTraining(job: ChatTrainingJob, input: ChatTrainRequest): Promise<void> {
        try {
            await this.store.saveJob({ ...job, status: 'running', updatedAt: now() });
            this.assertOpen();
            const result = await this.workers.run<ChatTrainResult>({ kind: 'train', examples: input.examples,
                ...(input.config === undefined ? {} : { config: input.config }), ...(input.training === undefined ? {} : { training: input.training }) });
            this.assertOpen();
            const payload = Buffer.from(result.payload);
            const model = await this.store.publishModel(input.name, { payload, checksumSha256: fingerprintBytes(payload),
                metadata: result.metadata, createdAt: now(), provenance: { ...input.provenance,
                    datasetSha256: fingerprint(input.examples), protocolVersion: 1, tokenizerVersion: 1,
                    training: input.training ?? {}, trainingMetrics: result.after } });
            await this.store.saveJob({ ...job, status: 'complete', model, updatedAt: now(), metrics: { before: result.before, after: result.after } });
        } catch (error) {
            await this.store.saveJob({ ...job, status: 'error', updatedAt: now(), error: error instanceof Error ? error.message : 'training failed' });
        }
    }
    async close(): Promise<void> {
        this.closed = true;
        for (const active of this.active.values()) active.controller.abort();
        await this.workers.close();
        while (this.operations.size || this.jobs.size)
            await Promise.allSettled([...this.operations, ...this.jobs]);
    }
}
export function fingerprintBytes(payload: Buffer): string { return createHash('sha256').update(payload).digest('hex'); }
export function chatErrorStatus(error: unknown): number {
    return error instanceof ChatServiceError || error instanceof ChatWorkerError ? error.status : 500;
}
