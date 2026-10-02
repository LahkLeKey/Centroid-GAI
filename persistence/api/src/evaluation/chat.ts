/** Reproducible provided-evidence extraction diagnostic, not a general factuality benchmark. */
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { parseArgs } from 'node:util';
import { cpus, platform, arch } from 'node:os';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { validateFactualDataset, type FactualDialogueRecord } from '../chat/dataset.ts';
import { evaluateScorerControls, factualityScoringVersion, nativeObservation, scoreDialogue, selectDevelopmentCandidate, sourceOnlyBaseline, summarizeDialogueScores } from './factuality.ts';
import type { ChatModelConfig, ChatModelMetadata, ChatTrainingOptions } from '../../../shared/chat.ts';
import { evaluateTrainingValidation, validateTrainingValidation } from '../chat/training-quality.ts';
import { buildTrainingRequest, verifyDatasetRelease } from '../training/workflow.ts';

const { values } = parseArgs({ options: { dataset: { type: 'string' }, release: { type: 'string' }, output: { type: 'string' },
    candidates: { type: 'string' }, 'require-quality': { type: 'boolean' }, 'development-only': { type: 'boolean' } } });
if (values.dataset !== undefined && values.release !== undefined) throw new Error('--dataset and --release are mutually exclusive');
if (values.release !== undefined && !values.release.trim()) throw new Error('--release requires a directory');
const candidateCount = Number(values.candidates ?? (values.release === undefined ? '2' : '1'));
if (![1, 2].includes(candidateCount)) throw new Error('--candidates must be 1 or 2');
const qualityRequired = values['require-quality'] ?? false;
const developmentOnly = values['development-only'] ?? false;
const generationSettings = { maxTokens: 128, temperature: 0, seed: '42' } as const;
const sha256 = (value: string | Buffer) => createHash('sha256').update(value).digest('hex');
const root = new URL('../../../..', import.meta.url);
const releaseDirectory = values.release === undefined ? undefined : resolve(values.release);
const releaseManifest = releaseDirectory ? await verifyDatasetRelease(releaseDirectory) : undefined;
const datasetPath = releaseDirectory ? resolve(releaseDirectory, 'dataset.json')
    : values.dataset ? resolve(values.dataset) : new URL('data/chat/factual-dialogues-v2.json', root);
const datasetBytes = await readFile(datasetPath);
if (releaseManifest && sha256(datasetBytes) !== releaseManifest.files['dataset.json']) throw new Error('release dataset changed after verification');
const { dataset, hashes } = validateFactualDataset(JSON.parse(datasetBytes.toString('utf8')));
const output = resolve(values.output ?? fileURLToPath(new URL('build/evaluation/chat', root)));
await mkdir(output, { recursive: true });
const failures: { stage: string; error: string }[] = [];
const errorText = (error: unknown) => error instanceof Error ? error.message : String(error);
const startedAt = performance.now();
const baseline = (records: readonly FactualDialogueRecord[]) => records.map(record => {
    const started = performance.now();
    const reply = sourceOnlyBaseline(record.messages, record.evidence);
    return { id: record.id, reply, score: scoreDialogue(record, reply), elapsedMs: performance.now() - started };
});
// Iteration mode does not generate or score final-test answers, including baseline/control outputs.
const finalTestRecords = developmentOnly ? [] : dataset.test;
const sourceBaseline = { development: baseline(dataset.development), test: baseline(finalTestRecords) };
const scorerControls = { training: evaluateScorerControls(dataset.train), development: evaluateScorerControls(dataset.development),
    test: developmentOnly ? null : evaluateScorerControls(finalTestRecords) };
if ([scorerControls.training, scorerControls.development, scorerControls.test].some(control => control && !control.passed))
    failures.push({ stage: 'scorer-positive-controls', error: 'accepted gold output failed strict scoring controls' });
const baseConfig: ChatModelConfig = { embeddingDimensions: 8, hiddenDimensions: 16, centroidCount: 16, promptWindow: 128, responseWindow: 8, evidenceWindow: 64, seed: '42' };
// Independent bounded restarts avoid implying that optimizer state can currently resume.
const specifications: { id: string; config: ChatModelConfig; training: ChatTrainingOptions }[] = releaseManifest ? [
    { id: 'release-configured', config: releaseManifest.identity.options.config, training: releaseManifest.identity.options.training },
    { id: 'release-double-learning-rate', config: releaseManifest.identity.options.config,
        training: { ...releaseManifest.identity.options.training, learningRate: Math.min(1, (releaseManifest.identity.options.training.learningRate ?? 0.01) * 2) } },
] : [
    { id: 'adam-005', config: baseConfig, training: { epochs: 20, learningRate: 0.005 } },
    { id: 'adam-010', config: baseConfig, training: { epochs: 20, learningRate: 0.01 } },
];
type Native = typeof import('../chat-native.ts');
let native: Native | undefined;
try { native = await import('../chat-native.ts'); }
catch (error) { failures.push({ stage: 'load-native-addon', error: errorText(error) }); }
type Candidate = { id: string; config: ChatModelConfig; training: ChatTrainingOptions; payload: Buffer;
    development: ReturnType<typeof scoreDialogue>[]; developmentResults: ReturnType<typeof runNative>;
    crossEntropy: number; trainingMs: number; before: unknown; after: unknown; metadata: ChatModelMetadata; developmentMetrics: unknown };
function runNative(payload: Buffer, records: readonly FactualDialogueRecord[]) {
    return records.map(record => {
        const started = performance.now();
        let reply;
        try { reply = nativeObservation(native!.replyChatModel(payload, record.messages, generationSettings)); }
        catch (error) { reply = { content: '', finishReason: 'error', error: errorText(error) }; }
        return { id: record.id, expected: record.expected, acceptedAnswers: record.acceptedAnswers,
            requiredClaims: record.requiredClaims, evidence: record.evidence, reply, score: scoreDialogue(record, reply),
            elapsedMs: performance.now() - started };
    });
}
const candidates: Candidate[] = [];
if (native) for (const specification of specifications.slice(0, candidateCount)) {
    try {
        const started = performance.now();
        const trained = native.trainChatModel(dataset.train, specification.config, specification.training);
        const trainingMs = performance.now() - started;
        const developmentMetrics = native.evaluateChatModel(trained.payload, dataset.development);
        const developmentResults = runNative(trained.payload, dataset.development);
        if (!Number.isFinite(developmentMetrics.crossEntropy) || developmentResults.some(result => result.score.error))
            throw new Error('candidate failed development evaluation');
        candidates.push({ ...specification, payload: trained.payload, trainingMs, before: trained.before, after: trained.after, metadata: trained.metadata,
            developmentMetrics, crossEntropy: developmentMetrics.crossEntropy, developmentResults,
            development: developmentResults.map(result => result.score) });
    } catch (error) { failures.push({ stage: specification.id, error: errorText(error) }); }
}
// No test answer or test metric participates in candidate selection.
const selected = candidates.length ? selectDevelopmentCandidate(candidates) : undefined;
let developmentQuality: ReturnType<typeof evaluateTrainingValidation> | null = null;
let qualityError: string | null = null;
if (selected && native) {
    try {
        const validation = releaseManifest && dataset.version === 3
            ? buildTrainingRequest(dataset, releaseManifest.identity.options, releaseManifest.releaseId).validation
            : validateTrainingValidation(dataset.train, { version: 1, cases: dataset.development.map(record => ({
                id: record.id, family: record.family, sources: record.sources, expected: record.expected,
                messages: record.messages.filter(message => message.role !== 'evidence'), acceptedAnswers: record.acceptedAnswers,
                evidence: record.evidence.map((source, index) => ({ ...source, id: source.id + ':' + source.startLine + '-' + source.endLine + ':' + index })),
            })) });
        developmentQuality = evaluateTrainingValidation(selected.payload, validation, native.replyChatModel);
    } catch (error) { qualityError = errorText(error); }
}
if (qualityError) failures.push({ stage: 'development-quality-evaluation', error: qualityError });
let testResults: ReturnType<typeof runNative> = [];
let trainingResults: ReturnType<typeof runNative> = [];
let testMetrics: unknown = null;
if (selected && native) {
    // Teacher-forced token accuracy and free-running complete answers are separate diagnostics.
    // These training answers do not select the candidate; only development results do.
    trainingResults = runNative(selected.payload, dataset.train);
    if (trainingResults.some(result => result.score.error)) failures.push({ stage: 'training-generation', error: 'native generation failed for a training example' });
    if (!developmentOnly) {
        testResults = runNative(selected.payload, finalTestRecords);
        try { testMetrics = native.evaluateChatModel(selected.payload, finalTestRecords); }
        catch (error) { failures.push({ stage: 'test-metrics', error: errorText(error) }); }
    }
    await writeFile(resolve(output, 'model.cgchat'), selected.payload);
}
const sourceDirectory = resolve(fileURLToPath(root));
let sourceCommit: string | null = null, sourceDirty: boolean | null = null;
try {
    const git = (...args: string[]) => execFileSync('git', ['-c', 'safe.directory=' + sourceDirectory.replace(/\\/g, '/'), ...args], { cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
    sourceCommit = git('rev-parse', 'HEAD');
    sourceDirty = git('status', '--porcelain').length > 0;
} catch (error) { failures.push({ stage: 'source-provenance', error: errorText(error) }); }
const implementationFiles = [
    'src/chat/chat_model.c', 'src/chat/chat_prompt.c', 'src/chat/chat_evaluation.c', 'src/chat/chat_codec.c',
    'src/neural/neural_model.c', 'src/neural/neural_math.c', 'src/neural/neural_training.c', 'src/neural/neural_generation.c',
    'src/neural/neural_vocabulary.c', 'src/internal/chat_internal.h', 'src/internal/neural_internal.h',
    'include/centroid_gai_chat.h', 'include/centroid_gai_neural.h', 'persistence/api/src/chat-native.ts',
    'persistence/api/src/chat/dataset.ts', 'persistence/api/src/chat/evidence.ts', 'persistence/api/src/evaluation/chat.ts', 'persistence/api/src/evaluation/factuality.ts',
    'persistence/api/src/chat/training-quality.ts', 'persistence/shared/chat-quality.ts', 'persistence/api/src/training/workflow.ts',
    'persistence/api/src/evaluation/codebase-data.ts', 'persistence/api/src/evaluation/source-baseline.ts',
    'persistence/api/src/training/preflight.ts',
];
const implementationSha256 = Object.fromEntries(await Promise.all(implementationFiles.map(async file =>
    [file, sha256(await readFile(new URL(file, root)))])));
const candidateReports = candidates.map(({ payload, development, ...candidate }) => ({ ...candidate,
    artifactSha256: sha256(payload), summary: summarizeDialogueScores(development) }));
const repositoryReviews = dataset.version === 3 ? [...dataset.train, ...dataset.development, ...dataset.test].map(record => record.review) : null;
const reviewDeclared = repositoryReviews ? {
    unreviewed: repositoryReviews.filter(review => !review).length,
    candidate: repositoryReviews.filter(review => review?.status === 'candidate').length,
    approved: repositoryReviews.filter(review => review?.status === 'approved').length,
    rejected: repositoryReviews.filter(review => review?.status === 'rejected').length,
    reviewerKinds: { human: repositoryReviews.filter(review => review?.reviewer.kind === 'human').length,
        agent: repositoryReviews.filter(review => review?.reviewer.kind === 'agent').length },
    allRecordsApprovedByDeclaredHuman: repositoryReviews.every(review => review?.status === 'approved' && review.reviewer.kind === 'human'),
    reviewerIdentityVerified: false, reviewerIndependenceVerified: false,
} : null;
const report = {
    version: 3, scoringVersion: factualityScoringVersion, datasetVersion: dataset.version, createdAt: new Date().toISOString(), purpose: dataset.purpose, description: dataset.description, reviewDeclared,
    sourceCommit, sourceDirty, implementationSha256, hashes, datasetBytesSha256: sha256(datasetBytes),
    datasetRelease: releaseManifest ? { releaseId: releaseManifest.releaseId, reviewPolicy: releaseManifest.identity.options.reviewPolicy,
        verifiedFiles: releaseManifest.files } : null,
    sourceDocuments: dataset.sourceDocuments.map(source => ({ ...source, sha256: sha256(source.text) })),
    protocolVersion: selected?.metadata.protocolVersion ?? null, tokenizerVersion: selected?.metadata.tokenizerVersion ?? null,
    hardware: { platform: platform(), arch: arch(), cpu: cpus()[0]?.model, node: process.version },
    protocol: { candidates: specifications.slice(0, candidateCount), generation: generationSettings,
        selection: 'development exact answers, then development cross entropy, then stable candidate ID',
        finalTestUsedForSelection: false, finalTestEvaluated: !developmentOnly, trainingGenerationUsedForSelection: false,
        answerSupport: 'complete normalized evidence-unit equality only; no semantic entailment',
        actionScoring: 'fixed whole-response refusal/clarification phrases; other nonempty output is an answer attempt, even when incorrect; route accuracy is not factual correctness',
        baseline: 'deterministic complete-unit lexical ranking with conservative refusal/clarification; question/history/evidence only, no gold label input' },
    candidates: candidateReports, selectedCandidate: selected?.id ?? null,
    selectedArtifactSha256: selected ? sha256(selected.payload) : null,
    developmentQualityGate: { required: qualityRequired, evaluatedOn: 'development', report: developmentQuality, error: qualityError },
    scorerControls,
    trainingGeneration: { purpose: 'memorization diagnostic, not held-out generalization', results: trainingResults,
        summary: selected ? summarizeDialogueScores(trainingResults.map(result => result.score)) : null },
    testMetrics, results: testResults, sourceOnlyBaseline: sourceBaseline,
    summary: { evaluationScope: developmentOnly ? 'training-and-development' : 'training-development-and-final-test',
        native: selected && !developmentOnly ? summarizeDialogueScores(testResults.map(result => result.score)) : null,
        sourceOnly: developmentOnly ? null : summarizeDialogueScores(sourceBaseline.test.map(result => result.score)),
        trainingGeneration: selected ? summarizeDialogueScores(trainingResults.map(result => result.score)) : null,
        development: { native: selected ? summarizeDialogueScores(selected.development) : null,
            sourceOnly: summarizeDialogueScores(sourceBaseline.development.map(result => result.score)) },
        qualityRequired, developmentQualityPassed: developmentQuality?.passed ?? null, qualityError,
        pilotReady: false, reason: dataset.version === 2
            ? 'authored synthetic fixture; no independent human claim review or realistic coverage'
            : 'repository extraction diagnostic; review metadata is declared and production readiness is not evaluated',
        failures, elapsedMs: performance.now() - startedAt },
    limitations: ['Exact answer checks reject valid paraphrases and do not verify semantic entailment.',
        dataset.version === 2 ? 'Source spans establish provenance, not truth; fictional facts are only valid inside this fixture.'
            : 'Source hashes and exact spans establish snapshot provenance; they do not establish truth or source freshness.',
        dataset.version === 2 ? 'Small template-based splits share task templates; disjoint source/family IDs do not establish broad generalization.'
            : 'Disjoint repository source/family splits do not establish semantic generalization or reviewer independence.',
        'The diagnostic supplies evidence directly and does not evaluate production retrieval or the full conversation service.',
        'Scorer positive controls insert accepted gold outputs to test the measurement code; they are not model predictions.',
        'Action recognition accepts only a fixed set of complete refusal/clarification phrases and does not perform semantic classification.',
        'Free-running training-answer scores measure reproduction of seen examples; they are not held-out accuracy.',
        'Training is from scratch with frozen word vocabulary; held-out entities can become unknown tokens.',
        'Neither successful termination nor lower training loss establishes factual reliability.'],
};
await writeFile(resolve(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(report.summary));
if (!selected || failures.length || testResults.some(result => result.score.error) || (qualityRequired && (!developmentQuality?.passed || qualityError)))
    process.exitCode = 1;
