/** Exact extraction diagnostics, not a semantic factuality or entailment verifier. */
import type { ChatReply } from '../../../shared/chat.ts';
import type { DialogueEvidence, FactualDialogueRecord } from '../chat/dataset.ts';
import { evidenceUnits, normalizeEvidence } from '../chat/evidence.ts';

export const ABSTENTION = 'I cannot answer from the supplied evidence.';
export const EVIDENCE_ABSTENTION = 'I do not have supporting evidence for that claim.';
export const CLARIFICATION = 'Please name the setting or source you want me to check.';
export const factualityScoringVersion = 'exact-evidence-and-actions-v3';
export const normalizeExactAnswer = normalizeEvidence;
export type FactualAction = 'answer' | 'abstain' | 'clarify';
const abstentionPhrases = new Set([ABSTENTION, EVIDENCE_ABSTENTION].map(normalizeExactAnswer));
const clarificationPhrases = new Set([CLARIFICATION].map(normalizeExactAnswer));

/** Closed phrase recognition only. Other nonempty text is an answer attempt, not proof of correctness. */
export function classifyObservedAction(content: string): FactualAction | 'empty' {
    const normalized = normalizeExactAnswer(content);
    if (!normalized) return 'empty';
    if (abstentionPhrases.has(normalized)) return 'abstain';
    if (clarificationPhrases.has(normalized)) return 'clarify';
    return 'answer';
}
export function expectedDialogueAction(record: FactualDialogueRecord): FactualAction {
    return record.expected === 'answer' ? 'answer' : record.category === 'clarification' ? 'clarify' : 'abstain';
}
export interface FactualObservation {
    content: string;
    citations?: readonly DialogueEvidence[];
    finishReason: string;
    unknownTokens?: number;
    droppedMessages?: number;
    droppedEvidence?: number;
    evidenceTokens?: number;
    promptTokens?: number;
    error?: string;
}

/** A complete accepted answer is required: overlap cannot hide negation or extra invented claims. */
export function scoreDialogue(record: FactualDialogueRecord, reply: FactualObservation) {
    const hasError = reply.error !== undefined || reply.finishReason === 'error';
    const answer = normalizeExactAnswer(reply.content);
    const exactMatch = !hasError && record.acceptedAnswers.some(expected => normalizeExactAnswer(expected) === answer);
    const expectedAction = expectedDialogueAction(record);
    const observedAction = hasError ? 'error' : classifyObservedAction(reply.content);
    const abstained = observedAction === 'abstain';
    const clarified = observedAction === 'clarify';
    const supports = (excerpt: string) => evidenceUnits(excerpt).some(unit => normalizeExactAnswer(unit.text) === answer);
    const exactSourceSupport = !!answer && record.evidence.some(source => supports(source.excerpt));
    const validCitations = hasError ? [] : (reply.citations ?? []).filter(citation => record.evidence.some(source =>
        source.id === citation.id && source.startLine === citation.startLine && source.endLine === citation.endLine && source.excerpt === citation.excerpt));
    const supportedCitation = validCitations.some(source => supports(source.excerpt));
    const relevant = (excerpt: string) => record.expected === 'answer' && evidenceUnits(excerpt).some(unit =>
        record.acceptedAnswers.some(expected => normalizeExactAnswer(expected) === normalizeExactAnswer(unit.text)));
    return { id: record.id, category: record.category, expected: record.expected, expectedAction, observedAction, exactMatch,
        actionCorrect: !hasError && expectedAction === observedAction,
        correctAnswer: record.expected === 'answer' && exactMatch, abstained, clarified,
        // Retained for compatibility: this measures exact gold wording, including clarification targets.
        correctAbstention: record.expected === 'abstain' && exactMatch,
        exactAbstentionWording: record.expected === 'abstain' && exactMatch,
        safeNonAnswer: !hasError && record.expected === 'abstain' && (abstained || clarified),
        exactSourceSupport: !hasError && !abstained && !clarified && exactSourceSupport,
        validCitations: validCitations.length, citations: reply.citations?.length ?? 0,
        citationSupportsCompleteAnswer: !hasError && !abstained && !clarified && supportedCitation,
        goldEvidenceSelected: validCitations.some(source => relevant(source.excerpt)),
        irrelevantEvidenceSelected: validCitations.some(source => !relevant(source.excerpt)),
        requiredClaimTextPresent: !hasError && record.requiredClaims.every(claim => answer.includes(normalizeExactAnswer(claim))),
        naturalTermination: !hasError && reply.finishReason === 'eos', error: hasError ? reply.error?.trim() || 'reply finished with error' : null,
        unknownTokens: reply.unknownTokens ?? null, droppedMessages: reply.droppedMessages ?? null,
        droppedEvidence: reply.droppedEvidence ?? null, evidenceTokens: reply.evidenceTokens ?? null,
        promptTokens: reply.promptTokens ?? null };
}
export type DialogueScore = ReturnType<typeof scoreDialogue>;
const rate = (numerator: number, denominator: number) => ({ numerator, denominator, rate: denominator ? numerator / denominator : null });
function summarizeScores(scores: readonly DialogueScore[]) {
    const answers = scores.filter(score => score.expected === 'answer');
    const abstentions = scores.filter(score => score.expected === 'abstain');
    const abstentionActions = scores.filter(score => score.expectedAction === 'abstain');
    const clarificationActions = scores.filter(score => score.expectedAction === 'clarify');
    const emitted = scores.filter(score => !score.abstained && !score.clarified && !score.error);
    const measured = scores.filter(score => score.unknownTokens !== null);
    return { cases: scores.length, errors: scores.filter(score => score.error).length,
        exactAnswers: rate(scores.filter(score => score.exactMatch).length, scores.length),
        correctAnswerRate: rate(answers.filter(score => score.correctAnswer).length, answers.length),
        correctAbstentionRate: rate(abstentions.filter(score => score.correctAbstention).length, abstentions.length),
        exactAbstentionWordingRate: rate(abstentions.filter(score => score.exactAbstentionWording).length, abstentions.length),
        actionAccuracy: rate(scores.filter(score => score.actionCorrect).length, scores.length),
        abstentionActionRecall: rate(abstentionActions.filter(score => score.actionCorrect).length, abstentionActions.length),
        clarificationActionRecall: rate(clarificationActions.filter(score => score.actionCorrect).length, clarificationActions.length),
        safeNonAnswerRate: rate(abstentions.filter(score => score.safeNonAnswer).length, abstentions.length),
        unnecessaryAbstentionRate: rate(answers.filter(score => score.abstained).length, answers.length),
        unnecessaryNonAnswerRate: rate(answers.filter(score => score.abstained || score.clarified).length, answers.length),
        exactSourceSupportRate: rate(emitted.filter(score => score.exactSourceSupport).length, emitted.length),
        citationValidity: rate(scores.reduce((total, score) => total + score.validCitations, 0), scores.reduce((total, score) => total + score.citations, 0)),
        citedCompleteAnswerRate: rate(answers.filter(score => score.citationSupportsCompleteAnswer).length, answers.length),
        answerEvidenceCoverage: rate(answers.filter(score => score.goldEvidenceSelected).length, answers.length),
        irrelevantEvidenceSelectionRate: rate(scores.filter(score => score.irrelevantEvidenceSelected).length, scores.length),
        naturalTermination: rate(scores.filter(score => score.naturalTermination).length, scores.length),
        usage: { measuredCases: measured.length, unknownTokens: measured.reduce((total, score) => total + score.unknownTokens!, 0),
            casesWithUnknownTokens: measured.filter(score => score.unknownTokens! > 0).length,
            droppedMessages: measured.reduce((total, score) => total + (score.droppedMessages ?? 0), 0),
            droppedEvidence: measured.every(score => score.droppedEvidence !== null) && measured.length ? measured.reduce((total, score) => total + score.droppedEvidence!, 0) : null,
            evidenceTokens: measured.every(score => score.evidenceTokens !== null) && measured.length ? measured.reduce((total, score) => total + score.evidenceTokens!, 0) : null } };
}

export function summarizeDialogueScores(scores: readonly DialogueScore[]) {
    const categories = [...new Set(scores.map(score => score.category))].sort();
    return { ...summarizeScores(scores), perCategory: Object.fromEntries(categories.map(category =>
        [category, summarizeScores(scores.filter(score => score.category === category))])) };
}

/** Positive controls score known targets; action/rubric contradictions are warnings, never silently rewritten. */
export function evaluateScorerControls(records: readonly FactualDialogueRecord[]) {
    const failures: string[] = [];
    const rubricActionConflicts: { id: string; acceptedAnswerIndex: number; expectedAction: FactualAction; observedAction: ReturnType<typeof classifyObservedAction> }[] = [];
    let cases = 0;
    for (const record of records) for (const [index, answer] of record.acceptedAnswers.entries()) {
        ++cases;
        const citations = record.expected === 'answer' ? record.evidence.filter(source => evidenceUnits(source.excerpt).some(unit =>
            normalizeExactAnswer(unit.text) === normalizeExactAnswer(answer))) : [];
        const score = scoreDialogue(record, { content: answer, citations, finishReason: 'oracle' });
        const label = record.id + '[' + index + ']';
        if (!score.exactMatch) failures.push(label + ': accepted answer did not receive exact-match credit');
        if (record.expected === 'answer' && (!score.correctAnswer || !score.exactSourceSupport || !score.citationSupportsCompleteAnswer ||
            !score.goldEvidenceSelected || !score.requiredClaimTextPresent || !citations.length || score.validCitations !== citations.length))
            failures.push(label + ': gold answer did not receive complete-unit support, claims, and valid-citation credit');
        if (record.expected === 'abstain' && !score.exactAbstentionWording) failures.push(label + ': accepted non-answer wording was rejected');
        const observedAction = classifyObservedAction(answer);
        if (score.expectedAction !== observedAction) rubricActionConflicts.push({ id: record.id, acceptedAnswerIndex: index,
            expectedAction: score.expectedAction, observedAction });
    }
    if (!cases) failures.push('no accepted-answer controls supplied');
    return { scoringVersion: factualityScoringVersion, passed: failures.length === 0, records: records.length, cases, failures, rubricActionConflicts };
}

export { sourceOnlyBaseline } from './source-baseline.ts';

export function nativeObservation(reply: ChatReply): FactualObservation { return { ...reply }; }

/** Rank on development only; callers must not supply final-test scores here. */
export function selectDevelopmentCandidate<T extends { id: string; development: readonly DialogueScore[]; crossEntropy: number }>(candidates: readonly T[]): T {
    if (!candidates.length) throw new Error('no successful development candidates');
    return [...candidates].sort((left, right) =>
        right.development.filter(score => score.exactMatch).length - left.development.filter(score => score.exactMatch).length ||
        left.crossEntropy - right.crossEntropy || left.id.localeCompare(right.id))[0]!;
}
