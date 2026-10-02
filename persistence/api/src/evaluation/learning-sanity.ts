/** Closed-vocabulary memorization experiments separate fitting failures from unseen words. */
import { createHash } from 'node:crypto';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { cpus, platform, arch } from 'node:os';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import type { ChatDialogueMessage, ChatExample, ChatReply, ChatModelConfig } from '../../../shared/chat.ts';
import { cLocaleTokens } from './codebase-data.ts';

export interface SanityCase extends ChatExample {
    id: string;
    category: 'direct' | 'evidence' | 'follow-up' | 'correction' | 'abstention' | 'clarification';
}
export interface SanityFixture {
    version: 1;
    purpose: 'closed-vocabulary-memorization-sanity';
    description: string;
    cases: SanityCase[];
    controls: { id: string; kind: 'swapped' | 'removed'; sourceCaseId: string; targetCaseId: string }[];
}
export type SanityBackend = Pick<typeof import('../chat-native.ts'), 'trainChatModel' | 'replyChatModel'>;
const hash = (value: string | Buffer) => createHash('sha256').update(value).digest('hex');
const digest = (value: unknown) => hash(JSON.stringify(value));
const root = new URL('../../../..', import.meta.url);
const fixturePath = new URL('data/chat/learning-sanity-v1.json', root);
const text = (value: unknown): value is string => typeof value === 'string' && value.trim().length > 0 && !value.includes('\0') && Buffer.byteLength(value) <= 512;

/** Loading this fixture performs no native training or addon import. */
export async function loadSanityFixture(): Promise<SanityFixture> {
    return validateSanityFixture(JSON.parse(await readFile(fixturePath, 'utf8')));
}

export function validateSanityFixture(value: unknown): SanityFixture {
    const fixture = value as SanityFixture;
    if (!fixture || fixture.version !== 1 || fixture.purpose !== 'closed-vocabulary-memorization-sanity' || !text(fixture.description) ||
        !Array.isArray(fixture.cases) || fixture.cases.length !== 13 || !Array.isArray(fixture.controls))
        throw new Error('sanity fixture version one requires exactly 13 authored cases and controls');
    const ids = new Set<string>(), dialogues = new Set<string>();
    for (const record of fixture.cases) {
        if (!record || !text(record.id) || ids.has(record.id) || !text(record.answer) || !['direct', 'evidence', 'follow-up', 'correction', 'abstention', 'clarification'].includes(record.category) ||
            !Array.isArray(record.messages) || !record.messages.length || record.messages.length > 8) throw new Error('invalid sanity case');
        ids.add(record.id);
        let role = 'user';
        for (const message of record.messages) {
            if (!message || !text(message.content)) throw new Error('invalid sanity message');
            if (message.role === 'evidence' && role === 'user') continue;
            if (message.role !== role) throw new Error('invalid sanity dialogue order');
            role = role === 'user' ? 'assistant' : 'user';
        }
        if (record.messages.at(-1)!.role !== 'user') throw new Error('sanity current user question required');
        const identity = digest(record.messages.map(message => [message.role, cLocaleTokens(message.content)]));
        if (dialogues.has(identity)) throw new Error('duplicate normalized sanity dialogue');
        dialogues.add(identity);
    }
    const controls = buildSanityControls(fixture);
    if (!controls.some(control => control.kind === 'swapped') || !controls.some(control => control.kind === 'removed')) throw new Error('both evidence controls required');
    return structuredClone(fixture);
}

/** Controls transform evidence only and must equal an explicitly authored case with an unambiguous label. */
export function buildSanityControls(fixture: SanityFixture) {
    const cases = new Map(fixture.cases.map(record => [record.id, record]));
    const ids = new Set<string>();
    return fixture.controls.map(control => {
        if (!control || !text(control.id) || ids.has(control.id) || !['swapped', 'removed'].includes(control.kind)) throw new Error('invalid sanity control');
        ids.add(control.id);
        const source = cases.get(control.sourceCaseId), target = cases.get(control.targetCaseId);
        if (!source || !target || !source.messages.some(message => message.role === 'evidence')) throw new Error('sanity control cases required');
        const bare = source.messages.filter(message => message.role !== 'evidence');
        const replacement = control.kind === 'swapped' ? target.messages.filter(message => message.role === 'evidence') : [];
        const messages: ChatDialogueMessage[] = [...bare.slice(0, -1), ...replacement, bare.at(-1)!];
        if ((control.kind === 'swapped' && !replacement.length) || digest(messages) !== digest(target.messages) || source.answer === target.answer)
            throw new Error('sanity control must change evidence only and reference a different authored answer');
        return { ...control, messages, answer: target.answer };
    });
}

/** Exact free-running text and EOS are independent requirements; loss never substitutes for either. */
export function scoreSanityReply(expected: string, reply: ChatReply, requiresEvidence = false) {
    const counters = [reply.promptTokens, reply.generatedTokens, reply.unknownTokens, reply.droppedMessages, reply.evidenceTokens, reply.droppedEvidence];
    const validAccounting = counters.every(count => Number.isSafeInteger(count) && count! >= 0) && reply.promptTokens > 0 &&
        (!expected.length || reply.generatedTokens > 0) && reply.unknownTokens <= reply.promptTokens && reply.evidenceTokens! <= reply.promptTokens;
    const exact = reply.content === expected, eos = reply.finishReason === 'eos';
    const vocabularyClean = validAccounting && reply.unknownTokens === 0;
    const contextComplete = validAccounting && reply.droppedMessages === 0 && reply.droppedEvidence === 0 && (!requiresEvidence || reply.evidenceTokens! > 0);
    return { exact, eos, validAccounting, vocabularyClean, contextComplete, passed: exact && eos && vocabularyClean && contextComplete };
}

function summarize(results: readonly { score: ReturnType<typeof scoreSanityReply> }[]) {
    return { cases: results.length, exactAnswers: results.filter(result => result.score.exact).length,
        eosAnswers: results.filter(result => result.score.eos).length, vocabularyClean: results.filter(result => result.score.vocabularyClean).length,
        contextComplete: results.filter(result => result.score.contextComplete).length, passedCases: results.filter(result => result.score.passed).length,
        passed: results.length > 0 && results.every(result => result.score.passed) };
}

function specifications(epochs?: number) {
    if (epochs !== undefined && (!Number.isSafeInteger(epochs) || epochs < 1 || epochs > 2000)) throw new Error('sanity epochs must be an integer in 1..2000');
    const configs = epochs === undefined ? [
        { id: 'initial-12-small', caseCount: 12, centroids: 16, epochs: 400 },
        { id: 'initial-12-expanded', caseCount: 12, centroids: 32, epochs: 600 },
        { id: 'final-13-small', caseCount: 13, centroids: 16, epochs: 400 },
        { id: 'final-13-expanded', caseCount: 13, centroids: 32, epochs: 600 },
    ] : [{ id: 'explicit-epochs', caseCount: 13, centroids: 32, epochs }];
    return configs.map(({ centroids, ...specification }) => ({ ...specification,
        config: { embeddingDimensions: 4, hiddenDimensions: 8, centroidCount: centroids, promptWindow: 24,
            responseWindow: 4, evidenceWindow: 12, seed: '42' } satisfies ChatModelConfig,
        training: { epochs: specification.epochs, learningRate: 0.003, seed: '42' } }));
}

/** Run bounded deterministic experiments only on the authored training cases; no held-out set is selected on. */
export async function runLearningSanity(options: { epochs?: number; loadBackend?: () => Promise<SanityBackend> } = {}) {
    const requested = specifications(options.epochs);
    const fixture = await loadSanityFixture();
    const controls = buildSanityControls(fixture);
    let native: SanityBackend | undefined, backendError: string | null = null;
    try { native = await (options.loadBackend?.() ?? import('../chat-native.ts')); }
    catch (error) { backendError = error instanceof Error ? error.message : String(error); }
    const started = performance.now();
    const attempts = requested.map(specification => {
        const trainingCases = fixture.cases.slice(0, specification.caseCount);
        const attemptStart = performance.now();
        const lexical = new Set(trainingCases.flatMap(record => [record.answer, ...record.messages.map(message => message.content)]).flatMap(cLocaleTokens));
        const identity = { ...specification, trainingCaseIds: trainingCases.map(record => record.id), trainingSha256: digest(trainingCases),
            lexicalVocabularySha256: digest([...lexical].sort()), lexicalVocabularySize: lexical.size };
        try {
        if (!native) throw new Error(backendError ?? 'native backend unavailable');
        const trained = native.trainChatModel(trainingCases, specification.config, specification.training);
        const trainingMs = performance.now() - attemptStart;
        const answer = (messages: readonly ChatDialogueMessage[]) => native!.replyChatModel(trained.payload, messages, { maxTokens: 16, temperature: 0, seed: '42' });
        const results = trainingCases.map(record => {
            const reply = answer(record.messages);
            return { id: record.id, category: record.category, expected: record.answer, reply,
                score: scoreSanityReply(record.answer, reply, record.messages.some(message => message.role === 'evidence')) };
        });
        const controlResults = controls.map(control => {
            const reply = answer(control.messages);
            const original = results.find(result => result.id === control.sourceCaseId);
            return { ...control, labelAuthoredInThisTrainingAttempt: trainingCases.some(record => record.id === control.targetCaseId), reply,
                changedFromOriginal: original !== undefined && reply.content !== original.reply.content,
                score: scoreSanityReply(control.answer, reply, control.messages.some(message => message.role === 'evidence')) };
        });
        return { ...identity, status: 'complete' as const, error: null, metadata: trained.metadata,
            artifactSha256: hash(trained.payload), before: trained.before, after: trained.after, trainingMs,
            results, summary: summarize(results), controls: { results: controlResults,
                authoredSummary: summarize(controlResults.filter(control => control.labelAuthoredInThisTrainingAttempt)),
                authoredChangedAnswers: controlResults.filter(control => control.labelAuthoredInThisTrainingAttempt && control.changedFromOriginal).length,
                interpretation: 'evidence transformations map to authored labels; omitted-case controls are exploratory and never gate this attempt' } };
        } catch (error) {
            return { ...identity, status: 'failed' as const, error: error instanceof Error ? error.message : String(error), metadata: null,
                artifactSha256: null, before: null, after: null, trainingMs: performance.now() - attemptStart,
                results: [], summary: summarize([]), controls: { results: [], authoredSummary: summarize([]), authoredChangedAnswers: 0,
                    interpretation: 'attempt failed before complete measured results were available' } };
        }
    });
    const selected = attempts.at(-1)!;
    const implementationPaths = ['src/core/tokenizer.c', 'src/chat/chat_model.c', 'src/chat/chat_prompt.c', 'src/chat/chat_codec.c', 'src/chat/chat_evaluation.c',
        'src/neural/neural_model.c', 'src/neural/neural_math.c', 'src/neural/neural_vocabulary.c', 'src/neural/neural_training.c', 'src/neural/neural_generation.c',
        'src/internal/chat_internal.h', 'src/internal/neural_internal.h', 'include/centroid_gai_chat.h',
        'persistence/api/src/chat-native.ts', 'persistence/api/src/evaluation/learning-sanity.ts'];
    const implementationSha256 = Object.fromEntries(await Promise.all(implementationPaths.map(async path => [path, hash(await readFile(new URL(path, root)))])));
    return { version: 1, experiment: fixture.purpose, fixtureSha256: digest(fixture), fixture, implementationSha256,
        hardware: { platform: platform(), architecture: arch(), cpu: cpus()[0]?.model ?? 'unknown', node: process.version },
        protocol: { seed: '42', generation: { maxTokens: 16, temperature: 0 },
            selection: 'final declared 13-case attempt; all exploratory calibration runs are retained',
            productionConfigChanged: false, heldOutGeneralizationMeasured: false },
        backendError, attempts, selectedAttempt: selected.id,
        passed: selected.summary.passed && selected.controls.authoredSummary.passed && selected.trainingCaseIds.length === fixture.cases.length,
        summary: selected.summary, evidenceControls: { ...selected.controls.authoredSummary, changedAnswers: selected.controls.authoredChangedAnswers }, elapsedMs: performance.now() - started,
        limitations: ['Every scored main example is a training example; success establishes memorization capacity only.',
            'Evidence swaps and removals test authored toy patterns, not unseen factual reasoning or repository performance.',
            'Vocabulary includes only each attempt’s training messages and answers; there is no held-out or source-document vocabulary expansion.',
            'The initial 12-case calibration omits the no-evidence signal label; its removal controls are exploratory, not pass requirements.',
            'Low teacher-forced loss and high token accuracy can coexist with wrong free-running answers; both are reported separately.',
            'The tiny architecture and 600-epoch setting are isolated sanity settings and do not change production training defaults.'] };
}

async function main() {
    const { values } = parseArgs({ allowNegative: true, options: { output: { type: 'string' }, epochs: { type: 'string' }, 'require-pass': { type: 'boolean', default: true } } });
    const report = await runLearningSanity(values.epochs === undefined ? {} : { epochs: Number(values.epochs) });
    const output = values.output ? resolve(values.output) : fileURLToPath(new URL('build/evaluation/learning-sanity/report.json', root));
    await mkdir(dirname(output), { recursive: true });
    await writeFile(output, JSON.stringify(report, null, 2) + '\n');
    console.log(JSON.stringify({ output, passed: report.passed, summary: report.summary, evidenceControls: report.evidenceControls,
        attempts: report.attempts.map(attempt => ({ id: attempt.id, summary: attempt.summary, after: attempt.after, trainingMs: attempt.trainingMs })) }));
    if (values['require-pass'] && !report.passed) process.exitCode = 1;
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) await main();
