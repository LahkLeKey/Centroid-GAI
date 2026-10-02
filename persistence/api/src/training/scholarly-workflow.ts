/** Unattended scholarly releases: freeze evidence, run C11 training, gate, then move the head. */
import { createHash, randomUUID } from 'node:crypto';
import { constants } from 'node:fs';
import { copyFile, mkdir, open, readFile, rename, rm, stat, writeFile } from 'node:fs/promises';
import { hostname } from 'node:os';
import { join, resolve } from 'node:path';
import type { ScholarlyBenchmark, ScholarlyCollector, ScholarlyCorpus, ScholarlyPaper } from './scholarly-types.ts';
import { buildScholarlyCorpus, verifyScholarlyPapers } from './scholarly-verification.ts';
import { nativeScholarlyCandidate, nativeScholarlyMetrics, type NativeScholarlyMetrics, type ScholarlyNative } from './scholarly-native.ts';
import { createScholarlyArtifacts, parseScholarlyPublishedHead, serializeScholarlyPublishedHead,
    verifyScholarlyArtifacts, type ScholarlyPublishedHead } from './scholarly-release.ts';

export interface ScholarlyTrainingPolicy {
    readonly queries: readonly string[];
    readonly paperLimit: number;
    readonly maximumCorpusPapers: number;
    readonly minimumPapers: number;
    readonly minimumReferences: number;
    readonly maximumPassagesPerPaper: number;
    readonly baselineEpochs: number;
    readonly epochsPerCycle: number;
    readonly learningRate: number;
    readonly minimumDevelopmentImprovement: number;
    readonly maximumUnknownRate: number;
    readonly maximumTestRegression: number;
}
export const defaultScholarlyPolicy: ScholarlyTrainingPolicy = Object.freeze({
    queries: ['"machine learning" AND OPEN_ACCESS:Y AND IN_EPMC:Y sort_cited:y', '"language model" AND OPEN_ACCESS:Y AND IN_EPMC:Y sort_cited:y'],
    paperLimit: 12, maximumCorpusPapers: 24, minimumPapers: 6, minimumReferences: 2,
    maximumPassagesPerPaper: 32, baselineEpochs: 1, epochsPerCycle: 1, learningRate: 0.005,
    minimumDevelopmentImprovement: 0.000001, maximumUnknownRate: 0.25, maximumTestRegression: 0.0000000001,
});
export interface ScholarlyHead {
    readonly version: 1;
    readonly releaseId: string;
    readonly parentReleaseId: string | null;
    readonly corpusSha256: string;
    readonly benchmarkSha256: string;
    readonly checkpointSha256: string;
    readonly manifestSha256: string;
    readonly nativeSha256: string;
    readonly learningRate: number;
    readonly epochs: number;
    readonly steps: number;
    readonly promotedAt: string;
}
export interface ScholarlyCycleResult {
    readonly version: 1;
    readonly status: 'promoted' | 'rejected' | 'blocked';
    readonly runId: string;
    readonly releaseId: string | null;
    readonly reasons: readonly string[];
}
const digest = (value: string | Buffer) => createHash('sha256').update(value).digest('hex');
const json = (value: unknown) => JSON.stringify(value, null, 2) + '\n';
const object = (value: unknown): value is Record<string, unknown> => !!value && typeof value === 'object' && !Array.isArray(value);
const hashString = (value: unknown): value is string => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const absent = (error: unknown) => object(error) && error.code === 'ENOENT';
export const scholarlyUnknownRate = (metrics: NativeScholarlyMetrics) => metrics.unknownTokens / (metrics.tokens - 1);

/** Return explicit gate failures; bibliographic evidence is never assigned a truth probability. */
export function scholarlyQualityFailures(beforeDevelopment: NativeScholarlyMetrics, afterDevelopment: NativeScholarlyMetrics,
    beforeTest: NativeScholarlyMetrics, afterTest: NativeScholarlyMetrics, policy: ScholarlyTrainingPolicy): string[] {
    [beforeDevelopment, afterDevelopment, beforeTest, afterTest].forEach(nativeScholarlyMetrics);
    const failures: string[] = [];
    if (beforeDevelopment.tokens !== afterDevelopment.tokens || beforeTest.tokens !== afterTest.tokens)
        failures.push('evaluation token counts changed');
    if (beforeDevelopment.crossEntropy - afterDevelopment.crossEntropy <= policy.minimumDevelopmentImprovement)
        failures.push('development loss did not improve beyond the required margin');
    if (afterTest.crossEntropy > beforeTest.crossEntropy + policy.maximumTestRegression)
        failures.push('fixed regression benchmark loss increased');
    if (afterDevelopment.accuracy < beforeDevelopment.accuracy || afterTest.accuracy < beforeTest.accuracy)
        failures.push('held-out greedy accuracy decreased');
    for (const [label, before, after] of [['development', beforeDevelopment, afterDevelopment], ['test', beforeTest, afterTest]] as const) {
        if (scholarlyUnknownRate(after) > policy.maximumUnknownRate)
            failures.push(`${label} unknown-token rate exceeds ${policy.maximumUnknownRate}`);
        if (after.unknownTokens > before.unknownTokens)
            failures.push(`${label} vocabulary coverage decreased`);
    }
    return failures;
}

export function validateScholarlyPolicy(value: unknown): ScholarlyTrainingPolicy {
    if (!object(value)) throw new Error('scholarly policy must be an object');
    if (Object.keys(value).some(key => !Object.hasOwn(defaultScholarlyPolicy, key))) throw new Error('unknown scholarly policy setting');
    const policy = { ...defaultScholarlyPolicy, ...value } as ScholarlyTrainingPolicy;
    if (!Array.isArray(policy.queries) || policy.queries.length < 1 || policy.queries.length > 8 ||
        policy.queries.some(query => typeof query !== 'string' || query.length < 3 || query.length > 500 || /[\0\r\n]/.test(query)))
        throw new Error('provide 1..8 bounded public scholarly queries');
    for (const [key, lower, upper] of [['paperLimit', 6, 24], ['maximumCorpusPapers', 6, 48], ['minimumPapers', 6, 24],
        ['minimumReferences', 2, 8], ['maximumPassagesPerPaper', 1, 32], ['baselineEpochs', 1, 10000], ['epochsPerCycle', 1, 10000]] as const) {
        if (!Number.isSafeInteger(policy[key]) || policy[key] < lower || policy[key] > upper)
            throw new Error(`${key} must be an integer in ${lower}..${upper}`);
    }
    if (policy.minimumPapers > policy.paperLimit || policy.maximumCorpusPapers < policy.paperLimit)
        throw new Error('minimumPapers <= paperLimit <= maximumCorpusPapers is required');
    for (const [key, lower, upper] of [['learningRate', Number.MIN_VALUE, 1], ['minimumDevelopmentImprovement', 0, 1],
        ['maximumUnknownRate', 0, 0.25], ['maximumTestRegression', 0, 0.001]] as const) {
        if (typeof policy[key] !== 'number' || !Number.isFinite(policy[key]) || policy[key] < lower || policy[key] > upper)
            throw new Error(`invalid ${key}`);
    }
    return structuredClone(policy);
}

async function readJson(path: string): Promise<unknown> {
    if ((await stat(path)).size > 64 * 1024 * 1024) throw new Error('scholarly state JSON exceeds 64 MiB');
    return JSON.parse(await readFile(path, 'utf8'));
}
async function maybeJson(path: string): Promise<unknown | null> {
    try { return await readJson(path); } catch (error) { if (absent(error)) return null; throw error; }
}
async function atomicJson(path: string, value: unknown, signal?: AbortSignal): Promise<void> {
    return atomicText(path, json(value), signal);
}
async function atomicText(path: string, value: string, signal?: AbortSignal): Promise<void> {
    const temporary = `${path}.${randomUUID()}.tmp`;
    try {
        const file = await open(temporary, 'wx');
        try { await file.writeFile(value, 'utf8'); await file.sync(); } finally { await file.close(); }
        signal?.throwIfAborted();
        await rename(temporary, path);
    } finally { await rm(temporary, { force: true }).catch(() => {}); }
}
function headValue(value: unknown): ScholarlyHead {
    if (!object(value) || value.version !== 1 || !hashString(value.releaseId) ||
        !(value.parentReleaseId === null || hashString(value.parentReleaseId)) || !hashString(value.corpusSha256) ||
        !hashString(value.benchmarkSha256) || !hashString(value.checkpointSha256) || !hashString(value.manifestSha256) || !hashString(value.nativeSha256) ||
        typeof value.learningRate !== 'number' || !Number.isFinite(value.learningRate) || value.learningRate <= 0 || value.learningRate > 1 ||
        !Number.isSafeInteger(value.epochs) || (value.epochs as number) < 1 || !Number.isSafeInteger(value.steps) ||
        (value.steps as number) < 2 || typeof value.promotedAt !== 'string') throw new Error('invalid scholarly model head');
    return value as unknown as ScholarlyHead;
}

/** Exclusive local lock; a crashed owner is recoverable after verifying its PID is absent. */
async function acquireLock(directory: string): Promise<() => Promise<void>> {
    const path = join(directory, '.loop.lock');
    const owner = json({ pid: process.pid, host: hostname(), token: randomUUID() });
    for (let attempt = 0; attempt < 2; attempt++) {
        try {
            await writeFile(path, owner, { flag: 'wx', encoding: 'utf8' });
            return async () => { if (await readFile(path, 'utf8') === owner) await rm(path); };
        } catch (error) {
            if (!object(error) || error.code !== 'EEXIST') throw error;
            const previous = await readFile(path, 'utf8');
            const record: unknown = JSON.parse(previous);
            if (!object(record) || record.host !== hostname() || !Number.isSafeInteger(record.pid) || (record.pid as number) <= 0)
                throw new Error('scholarly training state is locked');
            try { process.kill(record.pid as number, 0); throw new Error('scholarly training state is locked'); }
            catch (probe) { if (!object(probe) || probe.code !== 'ESRCH') throw probe; }
            if (await readFile(path, 'utf8') === previous) await rm(path);
        }
    }
    throw new Error('scholarly training lock acquisition failed');
}

function boundedCatalog(previous: unknown, fresh: readonly ScholarlyPaper[], benchmark: ScholarlyBenchmark | null,
    maximum: number): ScholarlyPaper[] {
    if (previous !== null && (!Array.isArray(previous) || previous.length > 48)) throw new Error('invalid scholarly catalog');
    const pinned = benchmark?.papers ?? [];
    const papers = [...pinned, ...fresh, ...((previous ?? []) as ScholarlyPaper[])];
    const seen = new Set<string>();
    return papers.filter(paper => {
        if (!object(paper) || typeof paper.doi !== 'string') throw new Error('invalid scholarly catalog paper');
        const key = paper.doi.toLowerCase().trim();
        if (seen.has(key)) return false;
        seen.add(key); return true;
    }).slice(0, maximum);
}
async function fileHash(path: string): Promise<string> { return digest(await readFile(path)); }

export interface ScholarlyWorkflowOptions {
    readonly directory: string;
    readonly collector: ScholarlyCollector;
    readonly native: ScholarlyNative;
    readonly policy?: Partial<ScholarlyTrainingPolicy>;
    readonly clock?: () => number;
}
/** Each invocation performs one bounded cycle; watch/CI scheduling only repeats this operation. */
export class ScholarlyTrainingWorkflow {
    private readonly directory: string;
    private readonly collector: ScholarlyCollector;
    private readonly native: ScholarlyNative;
    private readonly policy: ScholarlyTrainingPolicy;
    private readonly clock: () => number;
    constructor(options: ScholarlyWorkflowOptions) {
        this.directory = resolve(options.directory); this.collector = options.collector; this.native = options.native;
        this.policy = validateScholarlyPolicy(options.policy ?? {}); this.clock = options.clock ?? Date.now;
    }
    async cycle(signal: AbortSignal): Promise<ScholarlyCycleResult> {
        await mkdir(this.directory, { recursive: true });
        const releaseLock = await acquireLock(this.directory);
        const runId = randomUUID(), run = join(this.directory, 'runs', runId);
        let result: ScholarlyCycleResult;
        try { await mkdir(run, { recursive: true }); result = await this.executeCycle(run, runId, signal); }
        catch (error) {
            result = { version: 1, status: 'blocked', runId, releaseId: null,
                reasons: [error instanceof Error ? error.message : String(error)] };
        } finally { await releaseLock(); }
        if (result.status !== 'promoted') await writeFile(join(run, 'decision.json'), json(result), { flag: 'wx' });
        return result;
    }
    private async readHead(): Promise<ScholarlyHead | ScholarlyPublishedHead | null> {
        const path = join(this.directory, 'current.tsv');
        try {
            if ((await stat(path)).size > 16384) throw new Error('scholarly published head exceeds 16 KiB');
            return parseScholarlyPublishedHead(await readFile(path, 'utf8'));
        } catch (error) { if (!absent(error)) throw error; }
        const legacy = await maybeJson(join(this.directory, 'current.json'));
        return legacy === null ? null : headValue(legacy);
    }
    private async trainingBenchmark(head: ScholarlyHead | null): Promise<ScholarlyBenchmark | null> {
        try {
            if (head && 'artifactManifestSha256' in head)
                await verifyScholarlyArtifacts(join(this.directory, 'releases', head.releaseId), head as ScholarlyPublishedHead);
            const input = await maybeJson(join(this.directory, 'benchmark.json'));
            if (input !== null && (!object(input) || input.version !== 1)) throw new Error('invalid pinned scholarly benchmark');
            const benchmark = input as ScholarlyBenchmark | null;
            if (head) {
                if (!benchmark) throw new Error('local scholarly training evidence is missing; restore the ignored evidence bundle for this release');
                if (head.benchmarkSha256 !== benchmark.sha256) throw new Error('head and pinned benchmark differ');
                await this.verifyAcceptedRelease(head);
                if (await fileHash(join(this.directory, 'benchmark.json')) !==
                    await fileHash(join(this.directory, 'releases', head.releaseId, 'benchmark.json')))
                    throw new Error('local pinned benchmark differs from accepted evidence');
            }
            return benchmark;
        } catch (error) {
            if (absent(error)) throw new Error('local scholarly training evidence is missing; restore the ignored evidence bundle for this release');
            throw error;
        }
    }
    /** Export a previously accepted local model without discovery or new optimizer updates. */
    async exportAccepted(signal: AbortSignal): Promise<ScholarlyPublishedHead> {
        await mkdir(this.directory, { recursive: true });
        const releaseLock = await acquireLock(this.directory);
        try {
            signal.throwIfAborted();
            const head = await this.readHead();
            if (!head) throw new Error('no accepted scholarly model is available to export');
            if ('artifactManifestSha256' in head) {
                await verifyScholarlyArtifacts(join(this.directory, 'releases', head.releaseId), head as ScholarlyPublishedHead);
                return head as ScholarlyPublishedHead;
            }
            await this.trainingBenchmark(head);
            const release = join(this.directory, 'releases', head.releaseId);
            const corpus = await readJson(join(release, 'dataset.json')) as ScholarlyCorpus;
            if (corpus.sha256 !== head.corpusSha256) throw new Error('accepted dataset identity mismatch');
            const quality = await readJson(join(release, 'quality.json'));
            if (!object(quality)) throw new Error('invalid accepted scholarly quality report');
            const staging = join(this.directory, 'runs', `export-${randomUUID()}`);
            await mkdir(staging, { recursive: true });
            const checkpoint = join(staging, 'checkpoint.txt');
            await copyFile(join(release, 'checkpoint.txt'), checkpoint, constants.COPYFILE_EXCL);
            await this.native.replay(checkpoint, join(release, 'train.txt'), join(staging, 'replayed.txt'), head.learningRate, signal);
            if (await fileHash(join(staging, 'replayed.txt')) !== head.checkpointSha256) throw new Error('accepted export replay hash mismatch');
            await this.native.export(checkpoint, join(staging, 'model.cgnn'), signal);
            const { manifestSha256: _privateManifest, ...identity } = head;
            const artifacts = await createScholarlyArtifacts(staging, identity, corpus, {
                ...quality, replay: { required: true, verified: true, sha256: head.checkpointSha256 },
            }, signal);
            for (const name of ['model.cgnn', 'release.tsv', 'citations.tsv', 'metrics.tsv']) {
                signal.throwIfAborted();
                try { await copyFile(join(staging, name), join(release, name), constants.COPYFILE_EXCL); }
                catch (error) {
                    if (!object(error) || error.code !== 'EEXIST' || await fileHash(join(staging, name)) !== await fileHash(join(release, name))) throw error;
                }
            }
            const published = { ...head, ...artifacts };
            await atomicText(join(this.directory, 'current.tsv'), serializeScholarlyPublishedHead(published), signal);
            return published;
        } finally { await releaseLock(); }
    }
    private async executeCycle(run: string, runId: string, signal: AbortSignal): Promise<ScholarlyCycleResult> {
        signal.throwIfAborted();
        const head = await this.readHead();
        const benchmark = await this.trainingBenchmark(head);
        const discovered = await this.collector.collect(this.policy.queries, this.policy.paperLimit, signal);
        const papers = boundedCatalog(await maybeJson(join(this.directory, 'catalog.json')), discovered.papers, benchmark,
            this.policy.maximumCorpusPapers);
        const admission = await verifyScholarlyPapers(papers, this.collector, signal, { minimumReferences: this.policy.minimumReferences });
        await writeFile(join(run, 'admission.json'), json({ discoveryExclusions: discovered.excluded, ...admission }), { flag: 'wx' });
        const heldoutDois = [...(benchmark?.developmentDois ?? []), ...(benchmark?.testDois ?? [])];
        if (heldoutDois.some(doi => !admission.accepted.some(paper => paper.doi === doi)))
            throw new Error('a pinned benchmark paper failed current evidence verification');
        // Verified small batches accumulate toward corpus coverage without approving a model.
        await atomicJson(join(this.directory, 'catalog.json'), admission.accepted);
        const built = buildScholarlyCorpus(admission.accepted, benchmark ?? undefined, {
            minimumPapers: this.policy.minimumPapers, maximumPassagesPerPaper: this.policy.maximumPassagesPerPaper,
        });
        if (!benchmark) await atomicJson(join(this.directory, 'benchmark.json'), built.benchmark);
        await writeFile(join(run, 'dataset.json'), json(built.corpus), { flag: 'wx' });
        await writeFile(join(run, 'benchmark.json'), json(built.benchmark), { flag: 'wx' });
        await Promise.all(Object.entries(built.corpus.texts).map(([split, text]) =>
            writeFile(join(run, `${split}.txt`), text, { flag: 'wx' })));
        return this.trainAndDecide(run, runId, built.corpus, built.benchmark, head, signal);
    }
    private async trainAndDecide(run: string, runId: string, corpus: ScholarlyCorpus, benchmark: ScholarlyBenchmark,
        head: ScholarlyHead | null, signal: AbortSignal): Promise<ScholarlyCycleResult> {
        const nativeSha256 = await this.native.fingerprint();
        const parent = head ? join(this.directory, 'releases', head.releaseId, 'checkpoint.txt') : null;
        if (head && await fileHash(parent!) !== head.checkpointSha256) throw new Error('accepted checkpoint hash mismatch');
        if (head) await this.verifyAcceptedRelease(head);
        const train = join(run, 'train.txt'), development = join(run, 'development.txt'), test = join(run, 'test.txt');
        const resume = !!head && await fileHash(join(this.directory, 'releases', head.releaseId, 'train.txt')) === digest(corpus.texts.train) &&
            head.nativeSha256 === nativeSha256 &&
            head.learningRate === this.policy.learningRate;
        const initial = resume ? parent! : join(run, 'initial.txt');
        if (!resume) await this.native.init(train, initial, signal);
        const beforeDevelopment = nativeScholarlyMetrics(await this.native.score(parent ?? initial, development, signal));
        const beforeTest = nativeScholarlyMetrics(await this.native.score(parent ?? initial, test, signal));
        const checkpoint = join(run, 'checkpoint.txt');
        const candidate = nativeScholarlyCandidate(await this.native.candidate(initial, train, development, checkpoint,
            resume ? this.policy.epochsPerCycle : this.policy.baselineEpochs, this.policy.learningRate, signal));
        const afterDevelopment = nativeScholarlyMetrics(await this.native.score(checkpoint, development, signal));
        const afterTest = nativeScholarlyMetrics(await this.native.score(checkpoint, test, signal));
        if (JSON.stringify(afterDevelopment) !== JSON.stringify(candidate.after.development))
            throw new Error('saved checkpoint score does not match candidate result');
        const reasons = scholarlyQualityFailures(beforeDevelopment, afterDevelopment, beforeTest, afterTest, this.policy);
        const report = { version: 1, policy: this.policy, mode: resume ? 'continue' : 'rebuild-training-vocabulary',
            corpusSha256: corpus.sha256, benchmarkSha256: benchmark.sha256, nativeSha256,
            scope: 'attributed-source-excerpts-and-next-token-regression', scientificTruth: 'not-assessed',
            exactAgreementPassages: corpus.passages.filter(passage => passage.exactAgreementDois.length > 0).length,
            before: { development: beforeDevelopment, test: beforeTest },
            after: { development: afterDevelopment, test: afterTest }, candidate, passed: reasons.length === 0, failures: reasons,
            replay: { required: true, verified: false, sha256: null } };
        await writeFile(join(run, 'quality.json'), json(report), { flag: 'wx' });
        if (reasons.length) return { version: 1, status: 'rejected', runId, releaseId: null, reasons };
        await this.native.replay(checkpoint, train, join(run, 'replayed.txt'), this.policy.learningRate, signal);
        if (await fileHash(checkpoint) !== await fileHash(join(run, 'replayed.txt'))) throw new Error('exact native replay hash mismatch');
        signal.throwIfAborted();
        const checkpointSha256 = await fileHash(checkpoint);
        await writeFile(join(run, 'quality.json'), json({ ...report,
            replay: { required: true, verified: true, sha256: checkpointSha256 } }));
        // Reproducible scratch checkpoints need not duplicate the accepted model in Git.
        await rm(join(run, 'initial.txt'), { force: true });
        await rm(join(run, 'replayed.txt'));
        const identity = { version: 1 as const, parentReleaseId: head?.releaseId ?? null, corpusSha256: corpus.sha256,
            benchmarkSha256: benchmark.sha256, checkpointSha256, nativeSha256, learningRate: this.policy.learningRate,
            epochs: candidate.epochs, steps: candidate.steps };
        const releaseId = digest(JSON.stringify({ ...identity, runId }));
        const decision: ScholarlyCycleResult = { version: 1, status: 'promoted', runId, releaseId, reasons: [] };
        const releaseRoot = join(this.directory, 'releases');
        const release = join(releaseRoot, releaseId);
        let moved = false;
        try {
            await this.native.export(checkpoint, join(run, 'model.cgnn'), signal);
            await writeFile(join(run, 'decision.json'), json(decision), { flag: 'wx' });
            const manifest = { ...identity, releaseId, promotedAt: new Date(this.clock()).toISOString(),
                files: {} as Record<string, string>, policy: this.policy, scientificTruth: 'not-assessed' };
            for (const name of ['dataset.json', 'benchmark.json', 'train.txt', 'development.txt', 'test.txt', 'checkpoint.txt', 'quality.json', 'admission.json', 'decision.json'])
                manifest.files[name] = await fileHash(join(run, name));
            await writeFile(join(run, 'manifest.json'), json(manifest), { flag: 'wx' });
            await mkdir(releaseRoot, { recursive: true });
            const manifestSha256 = await fileHash(join(run, 'manifest.json'));
            const artifacts = await createScholarlyArtifacts(run, { ...identity, releaseId, promotedAt: manifest.promotedAt }, corpus,
                { ...report, replay: { required: true, verified: true, sha256: checkpointSha256 } }, signal);
            await rename(run, release);
            moved = true;
            // Both directories share a filesystem. Publishing the accepted pointer is the commit.
            await atomicText(join(this.directory, 'current.tsv'), serializeScholarlyPublishedHead({ ...identity,
                releaseId, manifestSha256, promotedAt: manifest.promotedAt, ...artifacts }), signal);
            return decision;
        } catch (error) {
            if (moved) await rename(release, run);
            // The outer cycle archives its blocked decision even if preparation failed before rename.
            await rm(join(run, 'decision.json'), { force: true });
            throw error;
        }
    }
    private async verifyAcceptedRelease(head: ScholarlyHead): Promise<void> {
        const directory = join(this.directory, 'releases', head.releaseId);
        if (await fileHash(join(directory, 'manifest.json')) !== head.manifestSha256)
            throw new Error('accepted release manifest hash mismatch');
        const value = await readJson(join(directory, 'manifest.json'));
        if (!object(value) || value.releaseId !== head.releaseId || value.checkpointSha256 !== head.checkpointSha256 ||
            value.corpusSha256 !== head.corpusSha256 || value.benchmarkSha256 !== head.benchmarkSha256 || !object(value.files))
            throw new Error('accepted release identity mismatch');
        for (const name of ['dataset.json', 'benchmark.json', 'train.txt', 'development.txt', 'test.txt', 'checkpoint.txt', 'quality.json', 'admission.json', 'decision.json']) {
            if (!hashString(value.files[name]) || await fileHash(join(directory, name)) !== value.files[name])
                throw new Error(`accepted release hash mismatch: ${name}`);
        }
    }
}
