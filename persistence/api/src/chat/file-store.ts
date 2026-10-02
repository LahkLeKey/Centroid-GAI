import { randomUUID } from 'node:crypto';
import { mkdir, open, readFile, rename, unlink } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import type { ChatModelSummary, ChatTrainingJob, Conversation } from '../../../shared/chat.ts';
import type { ChatStore, StoredChatArtifact } from './store.ts';
import type { MemoryDocument } from '../../../shared/research.ts';

interface Document {
    version: 1;
    models: Record<string, ChatModelSummary>;
    artifacts: Record<string, Omit<StoredChatArtifact, 'payload'> & { payload: string }>;
    conversations: Record<string, Conversation>;
    jobs: Record<string, ChatTrainingJob>;
    memory?: Record<string, MemoryDocument>;
}

/** A single-owner local store. The lock forbids two API processes sharing one file. */
export class FileChatStore implements ChatStore {
    private document: Document;
    private queue: Promise<unknown> = Promise.resolve();
    private closed = false;
    private readonly path: string;
    private readonly lock: string;
    private constructor(path: string, lock: string, document: Document) {
        this.path = path;
        this.lock = lock;
        this.document = document;
    }

    static async open(path: string): Promise<FileChatStore> {
        const absolute = resolve(path);
        await mkdir(dirname(absolute), { recursive: true });
        const lock = JSON.stringify({ pid: process.pid, nonce: randomUUID() });
        const lockPath = `${absolute}.lock`;
        let handle;
        try { handle = await open(lockPath, 'wx', 0o600); }
        catch (error) {
            if ((error as NodeJS.ErrnoException).code !== 'EEXIST') throw error;
            const previous = JSON.parse(await readFile(lockPath, 'utf8')) as { pid: number };
            if (!Number.isInteger(previous.pid) || previous.pid <= 0) throw new Error('invalid chat store lock');
            try { process.kill(previous.pid, 0); throw new Error('chat store is already open in another process'); }
            catch (cause) {
                if ((cause as NodeJS.ErrnoException).code !== 'ESRCH') throw cause;
            }
            await unlink(lockPath);
            handle = await open(lockPath, 'wx', 0o600);
        }
        await handle.writeFile(lock);
        await handle.close();
        try {
            let document: Document = { version: 1, models: {}, artifacts: {}, conversations: {}, jobs: {} };
            try { document = JSON.parse(await readFile(absolute, 'utf8')) as Document; }
            catch (error) { if ((error as NodeJS.ErrnoException).code !== 'ENOENT') throw error; }
            if (document.version !== 1 || !document.models || !document.artifacts || !document.conversations || !document.jobs) {
                throw new Error('unsupported or corrupt chat store');
            }
            return new FileChatStore(absolute, lock, document);
        } catch (error) {
            await unlink(lockPath);
            throw error;
        }
    }

    private async mutate<T>(change: (document: Document) => T): Promise<T> {
        if (this.closed) throw new Error('chat store is closed');
        const operation = this.queue.then(async () => {
            const next = structuredClone(this.document);
            const value = change(next);
            const temporary = `${this.path}.${randomUUID()}.tmp`;
            const handle = await open(temporary, 'wx', 0o600);
            try {
                await handle.writeFile(JSON.stringify(next));
                await handle.sync();
            } finally { await handle.close(); }
            try { await rename(temporary, this.path); }
            catch (error) { await unlink(temporary).catch(() => {}); throw error; }
            this.document = next;
            return value;
        });
        this.queue = operation.catch(() => {});
        return operation;
    }

    async listModels(): Promise<ChatModelSummary[]> { return structuredClone(Object.values(this.document.models)); }
    async getMemory(ownerId: string): Promise<MemoryDocument | null> {
        const records = this.document.memory ?? {};
        return Object.hasOwn(records, ownerId) ? structuredClone(records[ownerId]!) : null;
    }
    async saveMemory(value: MemoryDocument, expectedRevision: number): Promise<boolean> {
        return this.mutate((document) => {
            const records = document.memory ??= {};
            const current = Object.hasOwn(records, value.ownerId) ? records[value.ownerId]!.revision : 0;
            if (current !== expectedRevision) return false;
            if (value.revision !== expectedRevision + 1) throw new Error('memory revision must advance by one');
            Object.defineProperty(records, value.ownerId, { value: structuredClone(value), enumerable: true, writable: true, configurable: true });
            return true;
        });
    }
    async findModel(name: string): Promise<ChatModelSummary | null> {
        return Object.hasOwn(this.document.models, name) ? structuredClone(this.document.models[name]!) : null;
    }
    async getArtifact(checksum: string): Promise<StoredChatArtifact | null> {
        const value = this.document.artifacts[checksum];
        return value ? { ...structuredClone(value), payload: Buffer.from(value.payload, 'base64') } : null;
    }
    async saveArtifact(artifact: StoredChatArtifact): Promise<void> {
        await this.mutate(document => {
            document.artifacts[artifact.checksumSha256] ??= { ...artifact, payload: artifact.payload.toString('base64') };
        });
    }
    async publishModel(name: string, artifact: StoredChatArtifact): Promise<ChatModelSummary> {
        return this.mutate((document) => {
            document.artifacts[artifact.checksumSha256] ??= { ...artifact, payload: artifact.payload.toString('base64') };
            const { payload: _payload, ...metadata } = document.artifacts[artifact.checksumSha256]!;
            const summary = { name, ...metadata };
            Object.defineProperty(document.models, name, { value: summary, enumerable: true, writable: true, configurable: true });
            return structuredClone(summary);
        });
    }
    async createConversation(conversation: Conversation): Promise<void> {
        await this.mutate((document) => {
            if (document.conversations[conversation.id]) throw new Error('conversation already exists');
            document.conversations[conversation.id] = structuredClone(conversation);
        });
    }
    async getConversation(id: string): Promise<Conversation | null> {
        return Object.hasOwn(this.document.conversations, id) ? structuredClone(this.document.conversations[id]!) : null;
    }
    async listConversations(): Promise<Conversation[]> { return structuredClone(Object.values(this.document.conversations)); }
    async saveConversation(conversation: Conversation, expectedRevision: number): Promise<boolean> {
        return this.mutate((document) => {
            if (document.conversations[conversation.id]?.revision !== expectedRevision) return false;
            if (conversation.revision !== expectedRevision + 1) throw new Error('revision must advance by one');
            document.conversations[conversation.id] = structuredClone(conversation);
            return true;
        });
    }
    async deleteConversation(id: string, expectedRevision: number): Promise<boolean> {
        return this.mutate((document) => {
            if (document.conversations[id]?.revision !== expectedRevision) return false;
            delete document.conversations[id];
            return true;
        });
    }
    async saveJob(job: ChatTrainingJob): Promise<void> {
        await this.mutate((document) => { document.jobs[job.id] = structuredClone(job); });
    }
    async getJob(id: string): Promise<ChatTrainingJob | null> {
        return Object.hasOwn(this.document.jobs, id) ? structuredClone(this.document.jobs[id]!) : null;
    }
    async listJobs(): Promise<ChatTrainingJob[]> { return structuredClone(Object.values(this.document.jobs)); }
    async close(): Promise<void> {
        this.closed = true;
        await this.queue;
        if (await readFile(`${this.path}.lock`, 'utf8') === this.lock) await unlink(`${this.path}.lock`);
    }
}
