import type { ChatSource } from './chat.ts';

/** Evidence routing is a recorded decision, not an uncalibrated factual confidence score. */
export interface ResearchTrace {
    readonly status: 'searched' | 'memory' | 'disabled' | 'unavailable' | 'failed' | 'none';
    readonly reason: string;
    readonly provider: string | null;
    readonly queries: number;
    readonly fetched: number;
    readonly elapsedMs: number;
    readonly mode: 'sources' | 'memory' | 'neural' | 'clarification' | 'abstained' | 'preference';
}

/** Every saved source remains attributed to its fetched passage and owner. */
export interface MemoryRecord {
    readonly id: string;
    readonly kind: 'source' | 'preference';
    readonly ownerId: string;
    readonly content: string;
    readonly sources: readonly ChatSource[];
    readonly state: 'supported' | 'user-stated' | 'stale' | 'disputed';
    readonly queryKey: string | null;
    readonly conversationId: string | null;
    readonly createdAt: string;
    readonly verifiedAt: string;
    readonly expiresAt: string | null;
    readonly applicability?: string;
    readonly supersedes?: string;
}

export interface MemoryDocument {
    readonly ownerId: string;
    readonly revision: number;
    readonly enabled: boolean;
    readonly retentionDays: number;
    readonly records: readonly MemoryRecord[];
    readonly researchQuota?: { readonly day: string; readonly used: number };
}

export interface ResearchAnswer {
    readonly content: string;
    readonly sources: readonly ChatSource[];
    readonly research: ResearchTrace;
    readonly memoryIds: readonly string[];
}
