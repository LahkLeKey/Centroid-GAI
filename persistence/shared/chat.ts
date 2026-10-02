/** Versioned HTTP contracts for the experimental centroid neural chatbot. */
export interface ChatModelMetadata {
    readonly engineKind: 'neural-centroid-chat' | 'web-research';
    readonly formatVersion: number;
    readonly protocolVersion: number;
    readonly tokenizerVersion: number;
    readonly config: ChatModelConfig;
    readonly vocabularySize: number;
    readonly parameterCount: number;
}

export interface ChatModelConfig {
    readonly embeddingDimensions?: number;
    readonly hiddenDimensions?: number;
    readonly centroidCount?: number;
    readonly promptWindow?: number;
    readonly responseWindow?: number;
    /** Maximum encoded evidence slots in protocol-two models; zero selects half the prompt. */
    readonly evidenceWindow?: number;
    readonly routingTemperature?: number;
    readonly seed?: string;
}
export interface ChatTrainingOptions {
    readonly epochs?: number;
    readonly learningRate?: number;
    readonly seed?: string;
}
export interface ChatDialogueMessage { readonly role: 'user' | 'assistant' | 'evidence'; readonly content: string }
export interface ChatExample { readonly id?: string; readonly messages: readonly ChatDialogueMessage[]; readonly answer: string }
export interface ChatMetrics {
    readonly tokens: number;
    readonly unknownTokens: number;
    readonly crossEntropy: number;
    readonly perplexity: number;
    readonly accuracy: number;
}
export interface ChatReply extends ChatUsage {
    readonly content: string;
    readonly finishReason: 'eos' | 'length' | 'repetition';
}
export interface ChatModelSummary {
    readonly name: string;
    readonly checksumSha256: string;
    readonly metadata: ChatModelMetadata;
    readonly provenance: Readonly<Record<string, unknown>> | null;
    readonly createdAt: string;
}
export interface ChatSource {
    readonly id: string;
    readonly path: string;
    readonly excerpt: string;
    readonly url?: string;
    readonly startLine?: number;
    readonly endLine?: number;
    readonly commit?: string;
    readonly title?: string;
    readonly fetchedAt?: string;
    readonly publishedAt?: string;
    readonly contentHash?: string;
    readonly blob?: string;
    readonly documentSha256?: string;
    readonly passageSha256?: string;
    readonly coordinateSystem?: 'snapshot-normalized-lines';
    readonly manifestSha256?: string;
}
export interface ChatMessage extends ChatDialogueMessage {
    readonly requestFingerprint?: string;
    readonly id: string;
    readonly sequence: number;
    readonly requestId: string;
    readonly createdAt: string;
    readonly status: 'complete' | 'pending' | 'error' | 'cancelled';
    readonly finishReason?: 'eos' | 'length' | 'repetition' | 'cancelled' | 'error' | 'sources' | 'abstained' | 'clarification';
    readonly error?: string;
    readonly sources?: readonly ChatSource[];
    readonly usage?: ChatUsage;
    readonly research?: import('./research.ts').ResearchTrace;
    readonly memoryIds?: readonly string[];
    readonly repositoryState?: import('./repository.ts').RepositoryState;
    readonly action?: import('./repository.ts').RepositoryAction;
    readonly verification?: import('./repository.ts').RepositoryVerificationReport;
    readonly investigation?: import('./repository.ts').RepositoryInvestigation;
    readonly grounding?: import('./grounding.ts').ChatGrounding;
}
export interface ChatUsage {
    readonly generatedTokens: number;
    readonly promptTokens: number;
    readonly droppedMessages: number;
    readonly unknownTokens: number;
    /** Absent on legacy replies. Includes evidence role/boundary controls. */
    readonly evidenceTokens?: number;
    readonly droppedEvidence?: number;
}
export interface ConversationSummary {
    readonly ownerId: string;
    readonly id: string;
    readonly title: string;
    /** Null until a conversation is explicitly created with a trained neural model. */
    readonly modelName: string | null;
    readonly modelChecksum: string | null;
    readonly protocolVersion: number;
    readonly revision: number;
    readonly createdAt: string;
    readonly updatedAt: string;
    /** Missing on older transcripts means public/research scope. */
    readonly scope?: 'public' | 'repository';
    readonly repositorySnapshot?: import('./repository.ts').RepositorySnapshotIdentity;
    readonly repositoryState?: import('./repository.ts').RepositoryState;
    readonly repositoryTransitions?: readonly import('./repository.ts').RepositoryTransition[];
}
export interface Conversation extends ConversationSummary { readonly messages: readonly ChatMessage[] }
export interface ChatCreateRequest {
    readonly modelName?: string;
    readonly title?: string;
    readonly scope?: 'public' | 'repository';
    readonly snapshot?: import('./repository.ts').RepositorySnapshotIdentity;
}
export interface ChatSendRequest {
    readonly requestId: string;
    readonly revision: number;
    readonly content: string;
    readonly maxTokens?: number;
    readonly temperature?: number;
    readonly seed?: string;
    /** Search public sources when local evidence is missing; defaults to true. */
    readonly autoSearch?: boolean;
    readonly rememberSources?: boolean;
    /** Optional public query override; otherwise only the current message is searched. */
    readonly publicQuery?: string;
    readonly applicability?: string;
    readonly answerMode?: 'sources' | 'neural';
    /** Inspect pinned repository dependencies or exact symbol occurrences. Repository scope only. */
    readonly repositoryResearch?: import('./repository.ts').RepositoryResearchRequest;
}
/** POST messages returns the complete authoritative session, including error/cancelled replies. */
export interface ChatSendResponse { readonly conversation: Conversation; readonly requestId: string }
export interface ChatTrainRequest {
    readonly name: string;
    readonly examples: readonly ChatExample[];
    /** Independent development cases required before a trained candidate can be published. */
    readonly validation: import('./chat-quality.ts').ChatTrainingValidation;
    readonly config?: ChatModelConfig;
    readonly training?: ChatTrainingOptions;
    readonly provenance?: Readonly<Record<string, unknown>>;
}
export interface ChatTrainingJob {
    readonly id: string;
    readonly modelName: string;
    readonly status: 'queued' | 'running' | 'complete' | 'rejected' | 'error';
    readonly createdAt: string;
    readonly updatedAt: string;
    readonly model?: ChatModelSummary;
    readonly metrics?: Readonly<Record<string, unknown>>;
    readonly error?: string;
    readonly candidateChecksum?: string;
    readonly quality?: import('./chat-quality.ts').ChatQualityReport;
}
