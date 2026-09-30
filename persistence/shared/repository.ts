import type { ChatSource } from './chat.ts';
import type { ResearchTrace } from './research.ts';

/** Identity of verified, immutable repository source material. */
export interface RepositorySnapshotIdentity {
    readonly repository: string;
    readonly commit: string;
    readonly manifestSha256: string;
}
export interface RepositoryKnowledgeInfo {
    readonly ready: boolean;
    readonly error?: string;
    readonly snapshot?: RepositorySnapshotIdentity;
    readonly snapshots: readonly RepositorySnapshotIdentity[];
    readonly documentCount: number;
    readonly passageCount: number;
    readonly paths: readonly string[];
}
export interface RepositoryState {
    readonly snapshot: RepositorySnapshotIdentity;
    readonly topic?: string;
    readonly taskId?: string;
    readonly topicCandidates?: readonly string[];
    /** An explicitly selected path in the pinned snapshot. */
    readonly focusPath?: string;
    /** Reviewed task IDs awaiting an explicit choice. */
    readonly taskCandidates?: readonly string[];
    readonly pendingReference?: 'file' | 'task' | 'topic';
    readonly userReportedProgress?: string;
    /** Completed software research context, scoped to the pinned snapshot. */
    readonly investigation?: RepositoryResearchRequest;
}
export interface RepositoryResearchRequest {
    readonly kind: 'impact' | 'symbol';
    readonly target: string;
}
export interface RepositoryInvestigation extends RepositoryResearchRequest {
    readonly snapshot: RepositorySnapshotIdentity;
    readonly findings: readonly {
        readonly kind: 'target' | 'dependency' | 'dependent' | 'symbol-occurrence';
        /** File containing the cited evidence. */
        readonly path: string;
        readonly relatedPath?: string;
        readonly sourceId: string;
    }[];
    readonly relatedTasks: readonly {
        readonly taskId: string;
        readonly title: string;
        readonly checks: readonly { readonly command: string; readonly cwd: string }[];
        readonly sourceId: string;
    }[];
    readonly limitations: readonly string[];
    readonly truncated: boolean;
}
export interface RepositoryAction {
    readonly taskId: string;
    readonly title: string;
    readonly rationale: string;
    readonly paths: readonly string[];
    readonly prerequisites: readonly string[];
    readonly checks: readonly { readonly command: string; readonly cwd: string }[];
    readonly completionCondition: string;
    readonly verification: 'proposed';
}
export interface RepositoryAnswer {
    readonly content: string;
    readonly sources: readonly ChatSource[];
    readonly research: ResearchTrace;
    readonly memoryIds: readonly string[];
    readonly repositoryState: RepositoryState;
    readonly action?: RepositoryAction;
    readonly verification?: RepositoryVerificationReport;
    readonly investigation?: RepositoryInvestigation;
    readonly finishReason: 'sources' | 'clarification' | 'abstained';
}
/** Operator-captured check results, bound to one immutable snapshot; not an attestation. */
export interface RepositoryVerificationReport {
    readonly version: 1;
    readonly provenance: 'operator-captured';
    readonly snapshot: RepositorySnapshotIdentity;
    readonly taskId: string;
    readonly startedAt: string;
    readonly finishedAt: string;
    readonly status: 'passed' | 'failed';
    readonly checks: readonly {
        readonly command: string;
        readonly cwd: string;
        readonly exitCode: number;
        readonly stdout: string;
        readonly stderr: string;
        readonly durationMs: number;
        readonly outputTruncated: boolean;
    }[];
}
export interface RepositorySwitchRequest {
    readonly revision: number;
    readonly snapshot: RepositorySnapshotIdentity;
}
export interface RepositoryTransition {
    readonly from: RepositorySnapshotIdentity;
    readonly to: RepositorySnapshotIdentity;
    readonly revision: number;
    readonly createdAt: string;
}
