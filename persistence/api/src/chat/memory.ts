import { randomUUID } from 'node:crypto';
import type { ChatSource } from '../../../shared/chat.ts';
import type { MemoryDocument, MemoryRecord } from '../../../shared/research.ts';
import type { ChatStore } from './store.ts';
import { ChatServiceError, chatText } from './service.ts';

/** Conservative cache policy, not learned confidence or a guarantee that the source is current. */
export function sourceFreshness(query: string): { policy: 'volatile-v1' | 'daily-v1'; ttlMs: number } {
    return /\b(current|latest|today|now|price|prices|weather|stock|exchange rate|president|ceo|release|releases)\b/i.test(query)
        ? { policy: 'volatile-v1', ttlMs: 300000 } : { policy: 'daily-v1', ttlMs: 86400000 };
}

/** Owner-scoped authoritative records. No live weights or public cache are mutated. */
export class MemoryService {
    private readonly store: Pick<ChatStore, 'getMemory' | 'saveMemory'>;
    readonly ownerId: string;
    private readonly clock: () => number;
    constructor(store: Pick<ChatStore, 'getMemory' | 'saveMemory'>, ownerId: string, clock = Date.now) {
        chatText(ownerId, 'memory owner', 512);
        this.store = store; this.ownerId = ownerId; this.clock = clock;
    }
    async read(): Promise<MemoryDocument> {
        const document = await this.store.getMemory(this.ownerId) ?? { ownerId: this.ownerId, revision: 0,
            enabled: true, retentionDays: 30, records: [] };
        if (document.ownerId !== this.ownerId) throw new ChatServiceError('memory owner mismatch', 500);
        const now = this.clock();
        return { ...document, records: document.records.filter((record) => record.ownerId === this.ownerId &&
            Date.parse(record.createdAt) + document.retentionDays * 86400000 > now).map((record) =>
            record.kind === 'source' && ['supported', 'sourced'].includes(record.state) &&
                (!record.expiresAt || !(Date.parse(record.expiresAt) > now))
                ? { ...record, state: 'stale' } : record) };
    }
    private async update(change: (document: MemoryDocument) => MemoryDocument, signal?: AbortSignal): Promise<MemoryDocument> {
        for (let attempt = 0; attempt < 5; attempt++) {
            signal?.throwIfAborted();
            const previous = await this.read();
            signal?.throwIfAborted();
            const next = { ...change(previous), revision: previous.revision + 1 };
            if (next.records.length > 1000) throw new ChatServiceError('memory record limit reached', 413);
            if (await this.store.saveMemory(next, previous.revision)) return next;
        }
        throw new ChatServiceError('concurrent memory update', 409);
    }
    async settings(enabled: boolean, retentionDays: number): Promise<MemoryDocument> {
        if (typeof enabled !== 'boolean' || !Number.isInteger(retentionDays) || retentionDays < 1 || retentionDays > 365)
            throw new ChatServiceError('memory settings require enabled and retentionDays 1..365');
        return this.update((value) => ({ ...value, enabled, retentionDays,
            records: value.records.filter((record) => Date.parse(record.createdAt) + retentionDays * 86400000 > this.clock()) }));
    }
    async remember(content: string, conversationId: string | null, supersedes?: string): Promise<MemoryRecord> {
        chatText(content, 'memory content', 4096);
        const previous = supersedes ? (await this.read()).records.find((entry) => entry.id === supersedes) : undefined;
        const timestamp = new Date(this.clock()).toISOString();
        const record: MemoryRecord = { id: randomUUID(), ownerId: this.ownerId, kind: 'preference', content,
            sources: [], state: 'user-stated', queryKey: null, conversationId: conversationId ?? previous?.conversationId ?? null, createdAt: timestamp,
            verifiedAt: timestamp, expiresAt: null, ...(supersedes ? { supersedes } : {}) };
        await this.update((value) => {
            if (!value.enabled) throw new ChatServiceError('memory is disabled', 409);
            if (supersedes && !value.records.some((entry) => entry.id === supersedes)) throw new ChatServiceError('memory not found', 404);
            return { ...value, records: [...value.records.filter((entry) => entry.id !== supersedes), record] };
        });
        return record;
    }
    async sources(queryKey: string, applicability: string, sources: readonly ChatSource[], conversationId: string,
        signal?: AbortSignal, freshnessQuery = queryKey): Promise<MemoryRecord[]> {
        const timestamp = new Date(this.clock()).toISOString();
        const freshness = sourceFreshness(freshnessQuery);
        const seen = new Set<string>();
        const records = sources.filter((source) => {
            if (!source.contentHash || !/^[a-f0-9]{64}$/i.test(source.contentHash) || seen.has(source.contentHash) ||
                !source.fetchedAt || !Number.isFinite(Date.parse(source.fetchedAt)) ||
                Date.parse(source.fetchedAt) > this.clock() || Date.parse(source.fetchedAt) + freshness.ttlMs <= this.clock() ||
                !source.excerpt || !(source.url || source.commit)) return false;
            seen.add(source.contentHash);
            return true;
        })
            .map((source): MemoryRecord => ({ id: randomUUID(), ownerId: this.ownerId, kind: 'source', content: source.excerpt,
                sources: [source], state: 'sourced', queryKey, applicability, conversationId, createdAt: timestamp,
                verifiedAt: timestamp, freshnessPolicy: freshness.policy,
                expiresAt: new Date(Date.parse(source.fetchedAt!) + freshness.ttlMs).toISOString() }));
        await this.update((value) => {
            if (!value.enabled) throw new ChatServiceError('memory is disabled', 409);
            const hashes = new Set(records.map((record) => record.sources[0]!.contentHash));
            const identities = new Set(records.flatMap(record => record.sources.map(source => source.url ??
                `${source.path}@${source.commit ?? ''}`)));
            const retained = value.records.filter((record) => !(record.queryKey === queryKey && record.applicability === applicability &&
                record.sources.some((source) => hashes.has(source.contentHash) || identities.has(source.url ??
                    `${source.path}@${source.commit ?? ''}`))));
            return { ...value, records: [...retained, ...records] };
        }, signal);
        return records;
    }
    async retrieve(queryKey: string, applicability: string): Promise<MemoryRecord[]> {
        const document = await this.read();
        if (!document.enabled) return [];
        const ttl = sourceFreshness(queryKey).ttlMs;
        return document.records.filter((record) => record.kind === 'source' && ['supported', 'sourced'].includes(record.state) &&
            record.queryKey === queryKey && record.applicability === applicability &&
            record.sources.length > 0 && record.sources.every(source => source.fetchedAt &&
                Date.parse(source.fetchedAt) <= this.clock() && Date.parse(source.fetchedAt) + ttl > this.clock()));
    }
    async reserveQuery(day: string, limit: number, signal?: AbortSignal): Promise<boolean> {
        if (!/^\d{4}-\d{2}-\d{2}$/.test(day) || !Number.isSafeInteger(limit) || limit < 0)
            throw new ChatServiceError('invalid research quota');
        try {
            await this.update((value) => {
                const used = value.researchQuota?.day === day ? value.researchQuota.used : 0;
                if (used >= limit) throw new ChatServiceError('research quota exhausted', 429);
                return { ...value, researchQuota: { day, used: used + 1 } };
            }, signal);
            return true;
        } catch (error) {
            if (error instanceof ChatServiceError && error.status === 429) return false;
            throw error;
        }
    }
    async forget(id?: string, conversationId?: string): Promise<void> {
        await this.update((value) => ({ ...value, records: value.records.filter((record) =>
            id ? record.id !== id : conversationId ? record.conversationId !== conversationId : false) }));
    }
    async dispute(id: string): Promise<void> {
        await this.update((value) => {
            if (!value.records.some((record) => record.id === id)) throw new ChatServiceError('memory not found', 404);
            return { ...value, records: value.records.map((record) => record.id === id ? { ...record, state: 'disputed' } : record) };
        });
    }
    /** Purges expired retention data from authoritative storage, not just retrieval results. */
    async prune(): Promise<void> { await this.update((value) => value); }
}
