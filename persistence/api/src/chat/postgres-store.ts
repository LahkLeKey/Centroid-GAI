import { randomUUID } from 'node:crypto';
import { db } from '@centroid-gai/db';
import type { ChatModelMetadata, ChatModelSummary, ChatTrainingJob, Conversation } from '../../../shared/chat.ts';
import type { ChatStore, StoredChatArtifact } from './store.ts';
import type { MemoryDocument } from '../../../shared/research.ts';

/** PostgreSQL is authoritative for identities, transcript revisions, and immutable model bytes. */
export class PostgresChatStore implements ChatStore {
    async getMemory(ownerId: string): Promise<MemoryDocument | null> {
        const row = await db.orm.public.ChatMemoryState.where({ ownerId }).first();
        return row ? JSON.parse(row.document) as MemoryDocument : null;
    }
    async saveMemory(document: MemoryDocument, expectedRevision: number): Promise<boolean> {
        if (document.revision !== expectedRevision + 1) throw new Error('memory revision must advance by one');
        if (expectedRevision === 0) {
            try {
                await db.orm.public.ChatMemoryState.create({ ownerId: document.ownerId, revision: document.revision, document: JSON.stringify(document) });
                return true;
            } catch (error) {
                if (await this.getMemory(document.ownerId)) return false;
                throw error;
            }
        }
        return await db.orm.public.ChatMemoryState.where({ ownerId: document.ownerId, revision: expectedRevision })
            .update({ revision: document.revision, document: JSON.stringify(document), updatedAt: new Date().toISOString() }) !== null;
    }
    async getArtifact(checksumSha256: string): Promise<StoredChatArtifact | null> {
        const row = await db.orm.public.NeuralChatArtifact.where({ checksumSha256 }).first();
        return row ? {
            checksumSha256, payload: Buffer.from(row.payload),
            metadata: JSON.parse(row.metadataJson) as ChatModelMetadata,
            provenance: row.provenanceJson ? JSON.parse(row.provenanceJson) as Record<string, unknown> : null,
            createdAt: row.createdAt,
        } : null;
    }
    async findModel(name: string): Promise<ChatModelSummary | null> {
        const head = await db.orm.public.NeuralChatModel.where({ name }).first();
        if (!head) return null;
        const artifact = await this.getArtifact(head.checksumSha256);
        if (!artifact) throw new Error('chat model references missing artifact');
        const { payload: _payload, ...summary } = artifact;
        return { name, ...summary };
    }
    async listModels(): Promise<ChatModelSummary[]> {
        const heads = await db.orm.public.NeuralChatModel.all();
        const models = await Promise.all(heads.map((row) => this.findModel(row.name)));
        return models.filter((row): row is ChatModelSummary => row !== null);
    }
    async saveArtifact(artifact: StoredChatArtifact): Promise<void> {
        // An artifact insert precedes publication. A crash may leave an unreferenced immutable
        // artifact, but can never leave a named model pointing at incomplete bytes.
        await db.orm.public.NeuralChatArtifact.upsert({
            conflictOn: { checksumSha256: artifact.checksumSha256 },
            create: {
                id: randomUUID(), checksumSha256: artifact.checksumSha256,
                payload: artifact.payload, metadataJson: JSON.stringify(artifact.metadata),
                provenanceJson: artifact.provenance === null ? null : JSON.stringify(artifact.provenance),
                createdAt: artifact.createdAt,
            },
            update: { checksumSha256: artifact.checksumSha256 },
        });
    }
    async publishModel(name: string, artifact: StoredChatArtifact): Promise<ChatModelSummary> {
        await this.saveArtifact(artifact);
        await db.orm.public.NeuralChatModel.upsert({
            conflictOn: { name },
            create: { name, checksumSha256: artifact.checksumSha256 },
            update: { checksumSha256: artifact.checksumSha256, updatedAt: new Date().toISOString() },
        });
        // A concurrent publication may already have replaced this name. The completed job
        // must identify its own immutable result rather than another job's latest model.
        const stored = await this.getArtifact(artifact.checksumSha256);
        if (!stored) throw new Error('published chat artifact is missing');
        const { payload: _payload, ...summary } = stored;
        return { name, ...summary };
    }
    async createConversation(conversation: Conversation): Promise<void> {
        await db.orm.public.ChatConversation.create({
            id: conversation.id, revision: conversation.revision,
            document: JSON.stringify(conversation), updatedAt: conversation.updatedAt,
        });
    }
    async getConversation(id: string): Promise<Conversation | null> {
        const row = await db.orm.public.ChatConversation.where({ id }).first();
        return row ? JSON.parse(row.document) as Conversation : null;
    }
    async listConversations(): Promise<Conversation[]> {
        return (await db.orm.public.ChatConversation.all()).map((row) => JSON.parse(row.document) as Conversation);
    }
    async saveConversation(conversation: Conversation, expectedRevision: number): Promise<boolean> {
        if (conversation.revision !== expectedRevision + 1) throw new Error('revision must advance by one');
        const row = await db.orm.public.ChatConversation.where({ id: conversation.id, revision: expectedRevision }).update({
            revision: conversation.revision, document: JSON.stringify(conversation), updatedAt: conversation.updatedAt,
        });
        return row !== null;
    }
    async deleteConversation(id: string, expectedRevision: number): Promise<boolean> {
        return (await db.orm.public.ChatConversation.where({ id, revision: expectedRevision }).delete()) !== null;
    }
    async saveJob(job: ChatTrainingJob): Promise<void> {
        await db.orm.public.ChatTrainingJob.upsert({
            conflictOn: { id: job.id }, create: { id: job.id, document: JSON.stringify(job), updatedAt: job.updatedAt },
            update: { document: JSON.stringify(job), updatedAt: job.updatedAt },
        });
    }
    async getJob(id: string): Promise<ChatTrainingJob | null> {
        const row = await db.orm.public.ChatTrainingJob.where({ id }).first();
        return row ? JSON.parse(row.document) as ChatTrainingJob : null;
    }
    async listJobs(): Promise<ChatTrainingJob[]> {
        return (await db.orm.public.ChatTrainingJob.all()).map((row) => JSON.parse(row.document) as ChatTrainingJob);
    }
}
