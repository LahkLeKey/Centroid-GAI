/** Deterministic extractive checks. No lexical-overlap or model-judged semantic claims. */
import { createHash } from 'node:crypto';
import type { ChatDialogueMessage, ChatExample, ChatReply } from '../../../shared/chat.ts';
import type { ChatQualityRates, ChatTrainingQualityReport, ChatTrainingValidation, ChatValidationCase, ChatValidationCaseResult } from '../../../shared/chat-quality.ts';
import { evidenceUnits, normalizeEvidence } from './evidence.ts';

export const normalizeAnswer = normalizeEvidence;
export const normalizeDialogue = (messages: readonly ChatDialogueMessage[]) =>
    JSON.stringify(messages.map(message => [message.role, normalizeEvidence(message.content)]));
const hash = (value: unknown) => createHash('sha256').update(JSON.stringify(value)).digest('hex');
const boundedString = (value: unknown, maximum = 16384): value is string => typeof value === 'string' &&
    value.trim().length > 0 && !value.includes('\0') && Buffer.byteLength(value) <= maximum;
const object = (value: unknown): value is Record<string, unknown> => !!value && typeof value === 'object' && !Array.isArray(value);

function dialogue(value: unknown, allowEvidence: boolean): asserts value is readonly ChatDialogueMessage[] {
    if (!Array.isArray(value) || !value.length || value.length > 64) throw new Error('validation dialogue must contain 1..64 messages');
    let next = 'user';
    for (const message of value) {
        if (!object(message) || !boundedString(message.content)) throw new Error('invalid validation message content');
        if (allowEvidence && next === 'user' && message.role === 'evidence') continue;
        if (message.role !== next) throw new Error('validation messages must alternate user and assistant; evidence belongs in evidence records');
        next = next === 'user' ? 'assistant' : 'user';
    }
    if (value.at(-1)?.role !== 'user') throw new Error('validation dialogue must end with the current user question');
}

function lineage(value: Record<string, unknown>): { families: string[]; sources: string[] } {
    const families: string[] = [], sources: string[] = [];
    for (const key of ['family', 'familyId', 'sourceId'] as const) {
        if (value[key] !== undefined) {
            if (!boundedString(value[key], 200)) throw new Error(`invalid ${key} lineage`);
            (key === 'sourceId' ? sources : families).push(value[key]);
        }
    }
    if (value.sources !== undefined) {
        if (!Array.isArray(value.sources) || value.sources.length > 64 || value.sources.some(source => !boundedString(source, 200)))
            throw new Error('invalid source lineage');
        sources.push(...value.sources as string[]);
    }
    if (Array.isArray(value.evidence)) for (const source of value.evidence) {
        if (object(source) && boundedString(source.id, 200)) sources.push(source.id);
    }
    return { families, sources };
}

/** Validate an untrusted API suite before training starts; callers translate errors to HTTP 400. */
export function validateTrainingValidation(examples: readonly ChatExample[], value: unknown): ChatTrainingValidation {
    if (!object(value) || value.version !== 1 || !Array.isArray(value.cases) || value.cases.length < 2 || value.cases.length > 64)
        throw new Error('training requires validation version 1 with 2..64 held-out cases');
    if (Buffer.byteLength(JSON.stringify(value)) > 1024 * 1024) throw new Error('validation suite exceeds 1 MiB');
    if (!Array.isArray(examples) || !examples.length) throw new Error('training examples are required');
    const trainingIds = new Set<string>(), trainingDialogues = new Set<string>(), trainingExamples = new Set<string>(),
        trainingFamilies = new Set<string>(), trainingSources = new Set<string>();
    for (const example of examples) {
        if (!object(example) || !boundedString(example.answer)) throw new Error('invalid training example');
        dialogue(example.messages, true);
        if (example.id !== undefined) {
            if (!boundedString(example.id, 200) || trainingIds.has(example.id)) throw new Error('duplicate or invalid training example ID');
            trainingIds.add(example.id);
        }
        const identity = normalizeDialogue(example.messages.filter(message => message.role !== 'evidence'));
        const completeIdentity = normalizeDialogue(example.messages);
        if (trainingExamples.has(completeIdentity)) throw new Error('duplicate normalized training dialogue');
        trainingExamples.add(completeIdentity);
        trainingDialogues.add(identity);
        const metadata = lineage(example);
        metadata.families.forEach(family => trainingFamilies.add(family));
        metadata.sources.forEach(source => trainingSources.add(source));
    }
    const ids = new Set<string>(), dialogues = new Set<string>(), answerTargets = new Set<string>(), abstentionTargets = new Set<string>();
    let answerable = 0, unanswerable = 0;
    for (const candidate of value.cases) {
        if (!object(candidate) || !boundedString(candidate.id, 200) || ids.has(candidate.id) || trainingIds.has(candidate.id))
            throw new Error('validation case ID is missing, duplicated, or leaks from training');
        ids.add(candidate.id);
        dialogue(candidate.messages, false);
        const identity = normalizeDialogue(candidate.messages);
        if (dialogues.has(identity) || trainingDialogues.has(identity)) throw new Error('validation normalized dialogue leaks or is duplicated');
        dialogues.add(identity);
        const metadata = lineage(candidate);
        if (metadata.families.some(family => trainingFamilies.has(family))) throw new Error('validation question family leaks from training');
        if (metadata.sources.some(source => trainingSources.has(source))) throw new Error('validation source leaks from training');
        if (!['answer', 'abstain'].includes(candidate.expected as string)) throw new Error('validation expected must be answer or abstain');
        if (!Array.isArray(candidate.acceptedAnswers) || !candidate.acceptedAnswers.length || candidate.acceptedAnswers.length > 8 ||
            candidate.acceptedAnswers.some(answer => !boundedString(answer))) throw new Error('validation acceptedAnswers must contain 1..8 bounded answers');
        const targets = candidate.acceptedAnswers.map(answer => normalizeAnswer(answer as string));
        if (new Set(targets).size !== targets.length) throw new Error('duplicate normalized accepted answer');
        const evidence = candidate.evidence ?? [];
        if (!Array.isArray(evidence) || evidence.length > 16) throw new Error('invalid bounded validation evidence');
        const sourceIds = new Set<string>();
        for (const source of evidence) {
            if (!object(source) || !boundedString(source.id, 200) || !boundedString(source.excerpt) || sourceIds.has(source.id))
                throw new Error('invalid or duplicate validation evidence');
            sourceIds.add(source.id);
        }
        if (candidate.expected === 'answer') {
            answerable++;
            const units = new Set(evidence.flatMap(source => evidenceUnits(source.excerpt as string).map(unit => normalizeAnswer(unit.text))));
            if (targets.some(target => !units.has(target))) throw new Error('answerable validation targets must match complete evidence units');
            targets.forEach(target => answerTargets.add(target));
        } else {
            unanswerable++;
            targets.forEach(target => abstentionTargets.add(target));
        }
    }
    if (!answerable || !unanswerable) throw new Error('validation requires answerable and unanswerable coverage');
    if ([...answerTargets].some(target => abstentionTargets.has(target))) throw new Error('answer and abstention targets must be distinct');
    // Return an owned suite: callers cannot mutate admitted cases while training is queued.
    return structuredClone(value) as unknown as ChatTrainingValidation;
}

/** Current evidence follows completed history and precedes the current question. */
export function validationMessages(record: ChatValidationCase): ChatDialogueMessage[] {
    return [...record.messages.slice(0, -1), ...(record.evidence ?? []).map(source => ({ role: 'evidence' as const, content: source.excerpt })), record.messages.at(-1)!];
}

export function scoreTrainingValidationCase(record: ChatValidationCase, reply: ChatReply, abstentionAnswers: readonly string[]): ChatValidationCaseResult {
    if (!reply || typeof reply.content !== 'string' || reply.content.includes('\0') || Buffer.byteLength(reply.content) > 65536 ||
        !['eos', 'length', 'repetition'].includes(reply.finishReason))
        throw new Error('invalid validation reply');
    for (const key of ['promptTokens', 'unknownTokens', 'droppedMessages', 'generatedTokens'] as const)
        if (!Number.isSafeInteger(reply[key]) || reply[key] < 0) throw new Error(`invalid validation reply ${key}`);
    const droppedEvidence = reply.droppedEvidence ?? 0;
    if (!Number.isSafeInteger(droppedEvidence) || droppedEvidence < 0 || reply.unknownTokens > reply.promptTokens)
        throw new Error('invalid validation evidence or unknown-token accounting');
    if (record.evidence?.length && (!Number.isSafeInteger(reply.evidenceTokens) || reply.evidenceTokens! <= 0 ||
        reply.evidenceTokens! > reply.promptTokens || !Number.isSafeInteger(reply.droppedEvidence)))
        throw new Error('validation evidence retention requires explicit valid native evidence counters');
    const normalized = normalizeAnswer(reply.content);
    const correct = record.acceptedAnswers.some(answer => normalizeAnswer(answer) === normalized);
    const supported = record.expected === 'answer' ? (record.evidence ?? []).some(source =>
        evidenceUnits(source.excerpt).some(unit => normalizeAnswer(unit.text) === normalized)) : null;
    return { id: record.id, expected: record.expected, content: reply.content, correct, supported,
        abstained: abstentionAnswers.some(answer => normalizeAnswer(answer) === normalized), finishReason: reply.finishReason,
        promptTokens: reply.promptTokens, unknownTokens: reply.unknownTokens, droppedMessages: reply.droppedMessages, droppedEvidence };
}

export const trainingQualityThresholds: ChatQualityRates = Object.freeze({ exactAnswerAccuracy: 1, answerableAccuracy: 1,
    supportRate: 1, abstentionRecall: 1, falseAbstentionRate: 0, unknownTokenRate: 0, droppedCaseRate: 0, droppedEvidenceCaseRate: 0, eosRate: 1 });

export function summarizeTrainingValidation(cases: readonly ChatValidationCaseResult[], suiteSha256: string): ChatTrainingQualityReport {
    const answers = cases.filter(record => record.expected === 'answer'), abstentions = cases.filter(record => record.expected === 'abstain');
    const rate = (count: number, total: number) => total ? count / total : 0;
    const metrics: ChatQualityRates = {
        exactAnswerAccuracy: rate(cases.filter(record => record.correct).length, cases.length),
        answerableAccuracy: rate(answers.filter(record => record.correct).length, answers.length),
        supportRate: rate(answers.filter(record => record.supported).length, answers.length),
        abstentionRecall: rate(abstentions.filter(record => record.correct && record.abstained).length, abstentions.length),
        falseAbstentionRate: rate(answers.filter(record => record.abstained).length, answers.length),
        unknownTokenRate: rate(cases.reduce((total, record) => total + record.unknownTokens, 0), cases.reduce((total, record) => total + record.promptTokens, 0)),
        droppedCaseRate: rate(cases.filter(record => record.droppedMessages > 0).length, cases.length),
        droppedEvidenceCaseRate: rate(cases.filter(record => record.droppedEvidence > 0).length, cases.length),
        eosRate: rate(cases.filter(record => record.finishReason === 'eos').length, cases.length),
    };
    const failures: string[] = [];
    if (!answers.length || !abstentions.length || cases.length > 64) failures.push('answerable and unanswerable coverage is required within 64 cases');
    for (const key of Object.keys(trainingQualityThresholds) as (keyof ChatQualityRates)[]) {
        const target = trainingQualityThresholds[key];
        if (target === 1 ? metrics[key] < target : metrics[key] > target) failures.push(`${key}=${metrics[key]} does not meet ${target}`);
    }
    return { version: 1, policy: 'extractive-development-v1', scope: 'bounded-development-engineering-gate', passed: failures.length === 0,
        suiteSha256, caseCount: cases.length, thresholds: trainingQualityThresholds, metrics, failures, cases };
}

export function evaluateTrainingValidation(payload: Buffer, validation: ChatTrainingValidation,
    reply: (payload: Buffer, messages: readonly ChatDialogueMessage[], options: { maxTokens: number; temperature: number; seed: string }) => ChatReply): ChatTrainingQualityReport {
    const abstentions = validation.cases.filter(record => record.expected === 'abstain').flatMap(record => record.acceptedAnswers);
    const scores = validation.cases.map(record => scoreTrainingValidationCase(record,
        reply(payload, validationMessages(record), { maxTokens: 128, temperature: 0, seed: '42' }), abstentions));
    return summarizeTrainingValidation(scores, hash(validation));
}
