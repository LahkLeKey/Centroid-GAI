import type { ChatDialogueMessage } from './chat.ts';

/** A bounded development check, not a claim of general factual reliability. */
export interface ChatValidationCase {
    readonly id: string;
    readonly messages: readonly ChatDialogueMessage[];
    readonly expected: 'answer' | 'abstain';
    readonly acceptedAnswers: readonly string[];
    readonly evidence?: readonly { readonly id: string; readonly excerpt: string }[];
    readonly familyId?: string;
    readonly sourceId?: string;
    /** Dataset lineage aliases retained when importing reviewed dialogue records. */
    readonly family?: string;
    readonly sources?: readonly string[];
}
export interface ChatTrainingValidation {
    readonly version: 1;
    readonly cases: readonly ChatValidationCase[];
}
export interface ChatValidationCaseResult {
    readonly id: string;
    readonly expected: 'answer' | 'abstain';
    readonly content: string;
    readonly correct: boolean;
    readonly supported: boolean | null;
    readonly abstained: boolean;
    readonly finishReason: string;
    readonly promptTokens: number;
    readonly unknownTokens: number;
    readonly droppedMessages: number;
    readonly droppedEvidence: number;
}
export interface ChatQualityRates {
    readonly exactAnswerAccuracy: number;
    readonly answerableAccuracy: number;
    readonly supportRate: number;
    readonly abstentionRecall: number;
    readonly falseAbstentionRate: number;
    readonly unknownTokenRate: number;
    readonly droppedCaseRate: number;
    readonly droppedEvidenceCaseRate: number;
    readonly eosRate: number;
}
export interface ChatTrainingQualityReport {
    readonly version: 1;
    readonly policy: 'extractive-development-v1';
    readonly scope: 'bounded-development-engineering-gate';
    readonly passed: boolean;
    readonly suiteSha256: string;
    readonly caseCount: number;
    readonly thresholds: ChatQualityRates;
    readonly metrics: ChatQualityRates;
    readonly failures: readonly string[];
    readonly cases: readonly ChatValidationCaseResult[];
}
export type ChatQualityReport = ChatTrainingQualityReport;
