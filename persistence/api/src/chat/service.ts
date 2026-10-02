import { createHash, randomUUID } from 'node:crypto';
import type { ChatCreateRequest, ChatMessage, ChatReply, ChatSendRequest, ChatSendResponse, ChatTrainRequest, ChatTrainingJob, Conversation } from '../../../shared/chat.ts';
import type { ChatQualityReport } from '../../../shared/chat-quality.ts';
import type { ResearchAnswer } from '../../../shared/research.ts';
import type { ChatTrainResult } from '../chat-native.ts';
import type { ChatStore } from './store.ts';
import { ChatWorkerError, type ChatWorkers } from './workers.ts';
import type { ResearchService } from './research.ts';
import type { RepositoryService } from './repository.ts';
import { validRepositoryResearch } from './repository-investigation.ts';
import { checkGroundedReply, groundingFallback, selectEvidence } from './grounding.ts';
import { validateTrainingValidation } from './training-quality.ts';
import type { RepositoryKnowledgeInfo, RepositorySnapshotIdentity, RepositorySwitchRequest } from '../../../shared/repository.ts';

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
    readonly repository: RepositoryService | undefined;
    constructor(store: ChatStore, workers: ChatWorkers, ownerId: string, research?: ResearchService, repository?: RepositoryService) {
        this.store = store; this.workers = workers; this.ownerId = ownerId; this.research = research; this.repository = repository;
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
    repositoryKnowledge(): RepositoryKnowledgeInfo {
        return this.repository?.info() ?? { ready: false, error: 'repository snapshot not configured', snapshots: [], documentCount: 0, passageCount: 0, paths: [] };
    }
    private repositoryIdentity(requested?: RepositorySnapshotIdentity): RepositorySnapshotIdentity {
        if (requested !== undefined && (!requested || typeof requested !== 'object' || Array.isArray(requested) ||
            typeof requested.repository !== 'string' || !requested.repository || requested.repository.length > 256 ||
            typeof requested.commit !== 'string' || !/^(?:[a-f0-9]{40}|[a-f0-9]{64})$/.test(requested.commit) ||
            typeof requested.manifestSha256 !== 'string' || !/^[a-f0-9]{64}$/.test(requested.manifestSha256)))
            throw new ChatServiceError('invalid repository snapshot identity');
        const knowledge = this.repositoryKnowledge();
        if (!knowledge.ready || !this.repository) throw new ChatServiceError(knowledge.error ?? 'repository knowledge unavailable', 503);
        if (requested && !knowledge.snapshots.some((snapshot) => snapshot.repository === requested.repository &&
            snapshot.commit === requested.commit && snapshot.manifestSha256 === requested.manifestSha256))
            throw new ChatServiceError('repository snapshot is not loaded', 404);
        return this.repository.identity(requested);
    }
    create(modelName?: string, title = 'Conversation', options: Pick<ChatCreateRequest, 'scope' | 'snapshot'> = {}): Promise<Conversation> {
        return this.operation(() => this.createConversation(modelName, title, options));
    }
    private async createConversation(modelName: string | undefined, title: string, options: Pick<ChatCreateRequest, 'scope' | 'snapshot'>): Promise<Conversation> {
        if (options.scope !== undefined && !['public', 'repository'].includes(options.scope)) throw new ChatServiceError('invalid conversation scope');
        const scope = options.scope ?? 'public';
        if (scope !== 'repository' && options.snapshot !== undefined) throw new ChatServiceError('snapshot requires repository scope');
        if (scope === 'repository' && modelName !== undefined) throw new ChatServiceError('repository scope uses source evidence, not a neural modelName');
        const snapshot = scope === 'repository' ? this.repositoryIdentity(options.snapshot) : undefined;
        if (modelName !== undefined) chatText(modelName, 'modelName', 200);
        chatText(title, 'title', 200);
        const model = modelName === undefined ? null : await this.store.findModel(modelName);
        this.assertOpen();
        if (modelName !== undefined && (!model || model.metadata.engineKind !== 'neural-centroid-chat'))
            throw new ChatServiceError('chat model not found', 404);
        const timestamp = now();
        const conversation: Conversation = { id: randomUUID(), ownerId: this.ownerId, title, modelName: modelName ?? null,
            modelChecksum: model?.checksumSha256 ?? null, protocolVersion: model?.metadata.protocolVersion ?? 1,
            revision: 0, createdAt: timestamp, updatedAt: timestamp, messages: [], scope,
            ...(snapshot ? { repositorySnapshot: snapshot, repositoryState: { snapshot } } : {}) };
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
        if (input.repositoryResearch !== undefined && !validRepositoryResearch(input.repositoryResearch))
            throw new ChatServiceError('repositoryResearch requires kind impact or symbol and a valid source path or identifier target');
        const identity: unknown[] = [input.content, input.maxTokens ?? 128, input.temperature ?? 0, input.seed ?? '0', input.autoSearch ?? true,
            input.rememberSources ?? false, input.publicQuery ?? '', input.applicability ?? '', input.answerMode ?? 'sources'];
        // Preserve fingerprints of old requests without this option.
        if (input.repositoryResearch) identity.push([input.repositoryResearch.kind, input.repositoryResearch.target]);
        return fingerprint(identity);
    }
    private async commit(previous: Conversation, messages: readonly ChatMessage[],
        patch: Pick<Conversation, 'repositorySnapshot' | 'repositoryState' | 'repositoryTransitions'> = {}): Promise<Conversation> {
        const next = { ...previous, ...patch, messages, revision: previous.revision + 1, updatedAt: now() };
        if (!await this.store.saveConversation(next, previous.revision)) throw new ChatServiceError('stale conversation revision', 409);
        return next;
    }
    switchRepository(id: string, input: RepositorySwitchRequest): Promise<Conversation> {
        return this.operation(async () => {
            if (!input || !Number.isSafeInteger(input.revision) || input.revision < 0 || !input.snapshot)
                throw new ChatServiceError('revision and snapshot are required');
            const conversation = await this.conversation(id);
            this.assertOpen();
            if (conversation.scope !== 'repository' || !conversation.repositorySnapshot)
                throw new ChatServiceError('conversation is not repository scoped');
            if (conversation.revision !== input.revision) throw new ChatServiceError('stale conversation revision', 409);
            if (this.active.has(id) || conversation.messages.some((message) => message.status === 'pending'))
                throw new ChatServiceError('cancel active request before switching repository snapshot', 409);
            const snapshot = this.repositoryIdentity(input.snapshot);
            if (snapshot.manifestSha256 === conversation.repositorySnapshot.manifestSha256) return conversation;
            const transitions = conversation.repositoryTransitions ?? [];
            if (transitions.length >= 100) throw new ChatServiceError('repository transition limit reached', 413);
            return this.commit(conversation, conversation.messages, { repositorySnapshot: snapshot,
                repositoryState: { snapshot }, repositoryTransitions: [...transitions, { from: conversation.repositorySnapshot,
                    to: snapshot, revision: conversation.revision + 1, createdAt: now() }] });
        });
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
        if (conversation.scope === 'repository') {
            if (!conversation.repositorySnapshot) throw new ChatServiceError('repository conversation has no pinned snapshot', 503);
            this.repositoryIdentity(conversation.repositorySnapshot);
            if (input.answerMode === 'neural' || input.publicQuery !== undefined)
                throw new ChatServiceError('repository scope accepts source answers and never public queries');
        }
        if (conversation.scope !== 'repository' && input.repositoryResearch !== undefined)
            throw new ChatServiceError('repositoryResearch requires repository scope');
        if (conversation.scope !== 'repository' && !conversation.modelChecksum && (input.answerMode === 'neural' || !this.research))
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
            if (conversation.scope === 'repository' && this.repository) {
                const answer = await this.repository.answer(input, conversation, signal);
                signal.throwIfAborted();
                completed = { ...assistant, ...answer, status: 'complete',
                    usage: { generatedTokens: 0, promptTokens: 0, droppedMessages: 0, unknownTokens: 0 } };
            } else if (input.answerMode !== 'neural' && this.research) {
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
                completed = await this.groundedReply(assistant, conversation, input, artifact, signal);
            }
        } catch (error) {
            const cancelled = signal.aborted;
            completed = { ...assistant, status: cancelled ? 'cancelled' : 'error', finishReason: cancelled ? 'cancelled' : 'error',
                error: error instanceof Error ? error.message : 'chat request failed' };
        }
        const result = await this.commit(pending, [...conversation.messages,
            { ...user, status: completed.status }, completed],
            completed.status === 'complete' && completed.repositoryState ? { repositoryState: completed.repositoryState } : {});
        return { conversation: result, requestId: input.requestId };
    }
    /** Factual neural output is limited to verifiable complete source quotations. */
    private async groundedReply(assistant: ChatMessage, conversation: Conversation, input: ChatSendRequest,
        artifact: import('./store.ts').StoredChatArtifact, signal: AbortSignal): Promise<ChatMessage> {
        const answer: ResearchAnswer = this.research ? await this.research.answer(input, conversation.id, signal) : {
            content: 'No evidence service is configured. I cannot support a factual answer.', sources: [], memoryIds: [],
            research: { status: 'unavailable', mode: 'abstained', reason: 'evidence service unavailable',
                provider: null, queries: 0, fetched: 0, elapsedMs: 0 },
        };
        signal.throwIfAborted();
        const selection = selectEvidence(input.content, answer.sources, artifact.metadata);
        const fallback: ChatMessage = { ...assistant, ...answer, status: 'complete',
            finishReason: answer.research.mode === 'clarification' ? 'clarification' : answer.sources.length ? 'sources' : 'abstained',
            grounding: groundingFallback(selection.reason, selection, !answer.sources.length),
            usage: { generatedTokens: 0, promptTokens: 0, droppedMessages: 0, unknownTokens: 0, evidenceTokens: 0, droppedEvidence: 0 } };
        if (!selection.entries.length) return fallback;
        const history = conversation.messages.filter(message => message.status === 'complete')
            .map(({ role, content }) => ({ role, content }));
        const reply = await this.workers.run<ChatReply>({ kind: 'reply', payload: artifact.payload,
            messages: [...history, ...selection.messages, { role: 'user', content: input.content }], options: input }, signal);
        signal.throwIfAborted();
        const checked = checkGroundedReply(reply, selection);
        const usage = { generatedTokens: reply.generatedTokens, promptTokens: reply.promptTokens,
            droppedMessages: reply.droppedMessages, unknownTokens: reply.unknownTokens,
            ...(reply.evidenceTokens === undefined ? {} : { evidenceTokens: reply.evidenceTokens }),
            ...(reply.droppedEvidence === undefined ? {} : { droppedEvidence: reply.droppedEvidence }) };
        if (checked.content === undefined || checked.sources === undefined)
            return { ...fallback, grounding: checked.grounding, usage };
        return { ...fallback, content: checked.content, sources: checked.sources, grounding: checked.grounding,
            finishReason: reply.finishReason, usage, research: { ...answer.research, mode: 'neural' } };
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
        trainingSettings(input.config, 'config', ['embeddingDimensions', 'hiddenDimensions', 'centroidCount', 'promptWindow', 'responseWindow', 'evidenceWindow', 'routingTemperature']);
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
        try { input = { ...structuredClone(input), validation: validateTrainingValidation(input.examples, input.validation) }; }
        catch (error) { throw new ChatServiceError(error instanceof Error ? error.message : 'invalid development validation'); }
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
        let candidateChecksum: string | undefined;
        let measured: Pick<ChatTrainingJob, 'quality' | 'metrics'> = {};
        let published: ChatTrainingJob['model'];
        try {
            await this.store.saveJob({ ...job, status: 'running', updatedAt: now() });
            this.assertOpen();
            const result = await this.workers.run<ChatTrainResult>({ kind: 'train', examples: input.examples,
                ...(input.config === undefined ? {} : { config: input.config }), ...(input.training === undefined ? {} : { training: input.training }) });
            this.assertOpen();
            const payload = Buffer.from(result.payload);
            candidateChecksum = fingerprintBytes(payload);
            const artifact = { payload, checksumSha256: fingerprintBytes(payload),
                metadata: result.metadata, createdAt: now(), provenance: { user: input.provenance ?? null, createdByJobId: job.id,
                    datasetSha256: fingerprint(input.examples), protocolVersion: result.metadata.protocolVersion,
                    tokenizerVersion: result.metadata.tokenizerVersion,
                    training: input.training ?? {}, trainingMetrics: result.after } };
            await this.store.saveArtifact(artifact);
            this.assertOpen();
            const quality = await this.workers.run<ChatQualityReport>({ kind: 'validate', payload, validation: input.validation });
            this.assertOpen();
            measured = { quality, metrics: { before: result.before, after: result.after } };
            if (!quality.passed) {
                await this.store.saveJob({ ...job, ...measured, candidateChecksum, updatedAt: now(), status: 'rejected',
                    error: 'candidate failed the development quality gate; model head was not changed' });
                return;
            }
            published = await this.store.publishModel(input.name, artifact);
            await this.store.saveJob({ ...job, ...measured, candidateChecksum, updatedAt: now(), status: 'complete', model: published });
        } catch (error) {
            const detail = error instanceof Error ? error.message : 'training failed';
            await this.store.saveJob({ ...job, ...measured, ...(candidateChecksum ? { candidateChecksum } : {}),
                ...(published ? { model: published } : {}), status: 'error', updatedAt: now(),
                error: published ? `Validated model was published, but recording the completed job failed: ${detail}` : detail });
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
