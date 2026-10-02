import type { ChatModelMetadata, ChatModelSummary, ChatTrainingJob, Conversation } from '../../../shared/chat.ts';
import type { MemoryDocument } from '../../../shared/research.ts';

export interface StoredChatArtifact {
    readonly checksumSha256: string;
    readonly payload: Buffer;
    readonly metadata: ChatModelMetadata;
    readonly provenance: Readonly<Record<string, unknown>> | null;
    readonly createdAt: string;
}

/** Persistence operations: compare-and-swap commits the entire transcript with its revision. */
export interface ChatStore {
    getMemory(ownerId: string): Promise<MemoryDocument | null>;
    saveMemory(document: MemoryDocument, expectedRevision: number): Promise<boolean>;
    listModels(): Promise<ChatModelSummary[]>;
    findModel(name: string): Promise<ChatModelSummary | null>;
    getArtifact(checksum: string): Promise<StoredChatArtifact | null>;
    /** Persist an immutable candidate without changing any named model head. */
    saveArtifact(artifact: StoredChatArtifact): Promise<void>;
    publishModel(name: string, artifact: StoredChatArtifact): Promise<ChatModelSummary>;
    createConversation(conversation: Conversation): Promise<void>;
    getConversation(id: string): Promise<Conversation | null>;
    listConversations(): Promise<Conversation[]>;
    saveConversation(conversation: Conversation, expectedRevision: number): Promise<boolean>;
    deleteConversation(id: string, expectedRevision: number): Promise<boolean>;
    saveJob(job: ChatTrainingJob): Promise<void>;
    getJob(id: string): Promise<ChatTrainingJob | null>;
    listJobs(): Promise<ChatTrainingJob[]>;
}
