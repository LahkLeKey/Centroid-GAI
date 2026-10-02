import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync } from 'node:fs';
import { mkdir, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { basename, dirname, join, resolve } from 'node:path';
import test, { type TestContext } from 'node:test';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import { scholarlyFixtureCollector, scholarlyFixturePaper } from './scholarly-fixtures.ts';
import { NativeScholarlyCli, nativeScholarlyCandidate, nativeScholarlyMetrics,
    type NativeScholarlyCandidate, type NativeScholarlyMetrics, type ScholarlyNative } from './scholarly-native.ts';
import type { ScholarlyCollector, ScholarlyPaper } from './scholarly-types.ts';
import { parseScholarlyPublishedHead, type ScholarlyPublishedHead } from './scholarly-release.ts';
import { defaultScholarlyPolicy, ScholarlyTrainingWorkflow, validateScholarlyPolicy,
    type ScholarlyCycleResult } from './scholarly-workflow.ts';

const hash = (value: string | Buffer) => createHash('sha256').update(value).digest('hex');
const signal = () => new AbortController().signal;
const clock = () => Date.UTC(2026, 9, 2, 12);
const document = async <T>(path: string): Promise<T> => JSON.parse(await readFile(path, 'utf8')) as T;
const publishedHead = async (state: string): Promise<ScholarlyPublishedHead> =>
    parseScholarlyPublishedHead(await readFile(join(state, 'current.tsv'), 'utf8'));
const compactFiles = ['checkpoint.txt', 'model.cgnn', 'release.tsv', 'citations.tsv', 'metrics.tsv'] as const;

interface FakeCheckpoint {
    epochs: number;
    steps: number;
    training: NativeScholarlyMetrics;
    development: NativeScholarlyMetrics;
    test: NativeScholarlyMetrics;
}
const metrics = (crossEntropy: number, accuracy = 0.1, unknownTokens = 0): NativeScholarlyMetrics =>
    ({ tokens: 101, unknownTokens, crossEntropy, accuracy });

/** Deterministic boundary fixture; it makes no claim to be a trained native artifact. */
class FakeNative implements ScholarlyNative {
    readonly identityPath: string;
    readonly candidateEpochs: number[] = [];
    initCount = 0;
    replayCount = 0;
    exportCount = 0;
    mode: 'improve' | 'test-regression' | 'excess-unknown' = 'improve';
    replayMismatch = false;
    cancelDuringReplay: (() => void) | undefined;
    afterReplay: ((output: string) => Promise<void>) | undefined;
    failExport = false;
    cancelDuringExport: (() => void) | undefined;
    constructor(identityPath: string) { this.identityPath = identityPath; }
    async fingerprint(): Promise<string> { return hash(await readFile(this.identityPath)); }
    async init(_train: string, checkpoint: string, pending: AbortSignal): Promise<void> {
        pending.throwIfAborted(); this.initCount++;
        const state: FakeCheckpoint = { epochs: 0, steps: 0,
            training: metrics(4), development: metrics(4), test: metrics(4) };
        await writeFile(checkpoint, JSON.stringify(state) + '\n', { flag: 'wx' });
    }
    async score(checkpoint: string, text: string, pending: AbortSignal): Promise<NativeScholarlyMetrics> {
        pending.throwIfAborted();
        const state = await document<FakeCheckpoint>(checkpoint);
        return { ...(basename(text) === 'train.txt' ? state.training : basename(text) === 'test.txt' ? state.test : state.development) };
    }
    async candidate(parent: string, train: string, development: string, output: string,
        epochs: number, _rate: number, pending: AbortSignal): Promise<NativeScholarlyCandidate> {
        pending.throwIfAborted(); this.candidateEpochs.push(epochs);
        const state = await document<FakeCheckpoint>(parent);
        const before = { training: await this.score(parent, train, pending), development: await this.score(parent, development, pending) };
        const improved = (previous: NativeScholarlyMetrics): NativeScholarlyMetrics =>
            metrics(previous.crossEntropy - epochs * 0.1, previous.accuracy + epochs * 0.01);
        const next: FakeCheckpoint = { epochs: state.epochs + epochs, steps: state.steps + epochs * 100,
            training: improved(state.training), development: improved(state.development), test: improved(state.test) };
        if (this.mode === 'test-regression') next.test = metrics(state.test.crossEntropy + 0.1, state.test.accuracy);
        if (this.mode === 'excess-unknown') {
            next.development = { ...next.development, unknownTokens: 30 };
            next.test = { ...next.test, unknownTokens: 30 };
        }
        await writeFile(output, JSON.stringify(next) + '\n', { flag: 'wx' });
        return { version: 1, before, after: { training: next.training, development: next.development }, epochs: next.epochs, steps: next.steps };
    }
    async replay(checkpoint: string, _train: string, output: string, _rate: number, pending: AbortSignal): Promise<void> {
        pending.throwIfAborted(); this.replayCount++;
        await writeFile(output, this.replayMismatch ? 'deliberate replay mismatch\n' : await readFile(checkpoint), { flag: 'wx' });
        this.cancelDuringReplay?.();
        await this.afterReplay?.(output);
    }
    async export(checkpoint: string, output: string, pending: AbortSignal): Promise<void> {
        pending.throwIfAborted(); this.exportCount++;
        if (this.failExport) throw new Error('Injected native binary export failure');
        // Interface fixture bytes deliberately differ from an actual CGNN model.
        await writeFile(output, Buffer.from('SCHOLARLY-FAKE-BINARY\0' + hash(await readFile(checkpoint))), { flag: 'wx' });
        this.cancelDuringExport?.();
        pending.throwIfAborted();
    }
}

async function fixture(t: TestContext, papers: readonly ScholarlyPaper[] = Array.from({ length: 6 }, (_, index) => scholarlyFixturePaper(index))) {
    const directory = await mkdtemp(join(tmpdir(), 'cgai-scholarly-workflow-'));
    t.after(() => rm(directory, { recursive: true, force: true }));
    const identity = join(directory, 'fake-native-identity.txt');
    await writeFile(identity, 'Deterministic scholarly native interface test fixture.\n');
    const native = new FakeNative(identity);
    const registry = scholarlyFixtureCollector(papers);
    const collector: ScholarlyCollector = { ...registry, async collect(_queries, maximum, pending) {
        pending.throwIfAborted(); return { papers: papers.slice(0, maximum), excluded: [] };
    } };
    const state = join(directory, 'state');
    const workflow = new ScholarlyTrainingWorkflow({ directory: state, collector, native,
        policy: { baselineEpochs: 3, epochsPerCycle: 2 }, clock });
    return { directory, state, native, collector, papers, workflow };
}

async function archivedDecision(state: string, result: ScholarlyCycleResult): Promise<ScholarlyCycleResult> {
    return document<ScholarlyCycleResult>(join(state, result.status === 'promoted' ? 'releases' : 'runs',
        result.status === 'promoted' ? result.releaseId! : result.runId, 'decision.json'));
}

test('unattended admission promotes a complete content-bound release and atomically publishes its head', async t => {
    const context = await fixture(t);
    const result = await context.workflow.cycle(signal());
    assert.equal(result.status, 'promoted', result.reasons.join('; '));
    assert.deepEqual(await archivedDecision(context.state, result), result);
    const head = await publishedHead(context.state);
    assert.equal(head.releaseId, result.releaseId);
    assert.equal(head.parentReleaseId, null);
    assert.equal(head.epochs, 3);
    assert.equal(head.steps, 300);
    assert.equal(head.nativeSha256, await context.native.fingerprint());
    const release = join(context.state, 'releases', head.releaseId);
    const manifestBytes = await readFile(join(release, 'manifest.json'));
    const manifest = JSON.parse(manifestBytes.toString('utf8')) as {
        files: Record<string, string>; checkpointSha256: string; scientificTruth: string;
    };
    assert.equal(hash(manifestBytes), head.manifestSha256);
    assert.equal(manifest.checkpointSha256, head.checkpointSha256);
    assert.equal(manifest.scientificTruth, 'not-assessed');
    assert.equal(hash(await readFile(join(release, 'model.cgnn'))), head.binarySha256);
    assert.equal(hash(await readFile(join(release, 'release.tsv'))), head.artifactManifestSha256);
    const publicMetadata = (await Promise.all(['release.tsv', 'citations.tsv', 'metrics.tsv']
        .map(name => readFile(join(release, name), 'utf8')))).join('\n');
    const corpus = await document<{ passages: { quote: string }[] }>(join(release, 'dataset.json'));
    assert(corpus.passages.length > 0);
    for (const passage of corpus.passages) assert(!publicMetadata.includes(passage.quote), 'compact metadata excludes source quotations');
    assert(!publicMetadata.includes('<article') && !publicMetadata.includes('"texts"'), 'raw XML and dataset JSON stay private');
    for (const name of compactFiles) assert(existsSync(join(release, name)), name);
    assert.equal(context.native.exportCount, 1);
    assert.deepEqual(Object.keys(manifest.files).sort(), ['admission.json', 'benchmark.json', 'checkpoint.txt', 'dataset.json',
        'decision.json', 'development.txt', 'quality.json', 'test.txt', 'train.txt']);
    for (const [name, expected] of Object.entries(manifest.files)) assert.equal(hash(await readFile(join(release, name))), expected, name);
    const admission = await document<{ accepted: { verification: { citedReferences: unknown[]; scientificTruth: string } }[]; excluded: unknown[] }>(
        join(release, 'admission.json'));
    assert.equal(admission.accepted.length, 6);
    assert.equal(admission.excluded.length, 0);
    assert(admission.accepted.every(paper => paper.verification.citedReferences.length === 2 && paper.verification.scientificTruth === 'not-assessed'));
    const quality = await document<{ passed: boolean; mode: string }>(join(release, 'quality.json'));
    assert.equal(quality.passed, true);
    assert.equal(quality.mode, 'rebuild-training-vocabulary');
    assert.equal(context.native.initCount, 1);
    assert(!(await readdir(context.state)).some(name => name.endsWith('.tmp') || name === '.loop.lock'));
    assert.deepEqual(await readdir(join(context.state, 'runs')), []);
});

test('same-corpus cycles retain the exact benchmark and continue by epochsPerCycle', async t => {
    const context = await fixture(t);
    const first = await context.workflow.cycle(signal());
    assert.equal(first.status, 'promoted', first.reasons.join('; '));
    const pinned = await readFile(join(context.state, 'benchmark.json'));
    const firstHead = await publishedHead(context.state);
    const second = await context.workflow.cycle(signal());
    assert.equal(second.status, 'promoted', second.reasons.join('; '));
    const secondHead = await publishedHead(context.state);
    assert.equal(secondHead.parentReleaseId, firstHead.releaseId);
    assert.equal(secondHead.corpusSha256, firstHead.corpusSha256);
    assert.equal(secondHead.benchmarkSha256, firstHead.benchmarkSha256);
    assert.equal(secondHead.epochs, 5);
    assert.equal(secondHead.steps, 500);
    assert.deepEqual(await readFile(join(context.state, 'benchmark.json')), pinned);
    assert.deepEqual(context.native.candidateEpochs, [3, 2]);
    assert.equal(context.native.initCount, 1);
    assert.deepEqual(await archivedDecision(context.state, second), second);
    assert.equal((await document<{ mode: string }>(join(context.state, 'releases', secondHead.releaseId, 'quality.json'))).mode, 'continue');
});

test('verified batches below corpus minimum accumulate without publishing until coverage is sufficient', async t => {
    const context = await fixture(t);
    let discoveries = 0;
    const collector: ScholarlyCollector = { ...context.collector, async collect(_queries, _maximum, pending) {
        pending.throwIfAborted();
        const start = discoveries++ === 0 ? 0 : 3;
        return { papers: context.papers.slice(start, start + 3), excluded: [] };
    } };
    const workflow = new ScholarlyTrainingWorkflow({ directory: context.state, collector, native: context.native,
        policy: { baselineEpochs: 3, epochsPerCycle: 2 }, clock });
    const first = await workflow.cycle(signal());
    assert.equal(first.status, 'blocked');
    assert.match(first.reasons.join('; '), /6 independent source groups/);
    assert.equal((await document<unknown[]>(join(context.state, 'catalog.json'))).length, 3);
    assert.equal(context.native.initCount, 0);
    assert(!existsSync(join(context.state, 'current.tsv')));
    assert.deepEqual(await archivedDecision(context.state, first), first);
    const second = await workflow.cycle(signal());
    assert.equal(second.status, 'promoted', second.reasons.join('; '));
    assert.equal((await document<unknown[]>(join(context.state, 'catalog.json'))).length, 6);
    assert.equal(context.native.initCount, 1);
    assert.deepEqual(await archivedDecision(context.state, second), second);
});

test('a candidate regression is archived and leaves the accepted head and fixed benchmark unchanged', async t => {
    const context = await fixture(t);
    assert.equal((await context.workflow.cycle(signal())).status, 'promoted');
    const accepted = await readFile(join(context.state, 'current.tsv'));
    const benchmark = await readFile(join(context.state, 'benchmark.json'));
    context.native.mode = 'test-regression';
    const result = await context.workflow.cycle(signal());
    assert.equal(result.status, 'rejected');
    assert(result.reasons.some(reason => /regression benchmark loss increased/.test(reason)));
    assert.deepEqual(await readFile(join(context.state, 'current.tsv')), accepted);
    assert.deepEqual(await readFile(join(context.state, 'benchmark.json')), benchmark);
    assert.deepEqual(await archivedDecision(context.state, result), result);
    assert(existsSync(join(context.state, 'runs', result.runId, 'checkpoint.txt')));
    assert.equal((await readdir(join(context.state, 'releases'))).length, 1);
});

test('excess unknown-token coverage rejects measured candidates and replay mismatch blocks publication', async t => {
    for (const failure of ['unknown', 'replay'] as const) await t.test(failure, async subtest => {
        const context = await fixture(subtest);
        assert.equal((await context.workflow.cycle(signal())).status, 'promoted');
        const accepted = await readFile(join(context.state, 'current.tsv'));
        if (failure === 'unknown') context.native.mode = 'excess-unknown';
        else context.native.replayMismatch = true;
        const result = await context.workflow.cycle(signal());
        assert.equal(result.status, failure === 'unknown' ? 'rejected' : 'blocked');
        assert(result.reasons.some(reason => failure === 'unknown' ? /unknown-token rate exceeds/.test(reason) : /replay hash mismatch/.test(reason)));
        assert.deepEqual(await readFile(join(context.state, 'current.tsv')), accepted);
        assert.deepEqual(await archivedDecision(context.state, result), result);
        assert.equal((await readdir(join(context.state, 'releases'))).length, 1);
    });
});

test('provider outages and unresolved bibliographic references never automatically approve sources', async t => {
    await t.test('discovery outage', async subtest => {
        const context = await fixture(subtest);
        const collector: ScholarlyCollector = { ...context.collector, async collect() { throw new Error('Scholarly provider unavailable'); } };
        const workflow = new ScholarlyTrainingWorkflow({ directory: context.state, collector, native: context.native, clock });
        const result = await workflow.cycle(signal());
        assert.equal(result.status, 'blocked');
        assert.match(result.reasons.join('; '), /provider unavailable/);
        assert(!existsSync(join(context.state, 'current.tsv')));
        assert.equal(context.native.initCount, 0);
        assert.deepEqual(await archivedDecision(context.state, result), result);
    });
    await t.test('reference lookup outage quarantines the exact papers', async subtest => {
        const context = await fixture(subtest);
        const collector: ScholarlyCollector = { ...context.collector, async crossref(doi, pending) {
            if (doi === '10.9999/reference-b') throw new Error('Reference registry is unavailable');
            return context.collector.crossref(doi, pending);
        } };
        const workflow = new ScholarlyTrainingWorkflow({ directory: context.state, collector, native: context.native, clock });
        const result = await workflow.cycle(signal());
        assert.equal(result.status, 'blocked');
        assert(!existsSync(join(context.state, 'current.tsv')));
        const admission = await document<{ accepted: unknown[]; excluded: { reasons: string[] }[] }>(join(context.state, 'runs', result.runId, 'admission.json'));
        assert.equal(admission.accepted.length, 0);
        assert.equal(admission.excluded.length, 6);
        assert(admission.excluded.every(paper => paper.reasons.some(reason => /quarantined/.test(reason))));
        assert.equal(context.native.initCount, 0);
    });
    await t.test('one reference cannot meet unattended admission policy', async subtest => {
        const papers = Array.from({ length: 6 }, (_, index) => scholarlyFixturePaper(index, { references: ['10.9999/reference-a'] }));
        const context = await fixture(subtest, papers);
        const result = await context.workflow.cycle(signal());
        assert.equal(result.status, 'blocked');
        const admission = await document<{ accepted: unknown[]; excluded: { reasons: string[] }[] }>(join(context.state, 'runs', result.runId, 'admission.json'));
        assert.equal(admission.accepted.length, 0);
        assert(admission.excluded.every(paper => paper.reasons.some(reason => /Fewer than 2/.test(reason))));
        assert(!existsSync(join(context.state, 'current.tsv')));
        assert.equal(context.native.initCount, 0);
    });
});

test('accepted checkpoint or release manifest tampering blocks the next cycle without changing its head', async t => {
    for (const name of ['checkpoint.txt', 'manifest.json'] as const) await t.test(name, async subtest => {
        const context = await fixture(subtest);
        const first = await context.workflow.cycle(signal());
        assert.equal(first.status, 'promoted');
        const accepted = await readFile(join(context.state, 'current.tsv'));
        await writeFile(join(context.state, 'releases', first.releaseId!, name), 'tampered accepted artifact\n');
        const result = await context.workflow.cycle(signal());
        assert.equal(result.status, 'blocked');
        assert.match(result.reasons.join('; '), name === 'checkpoint.txt' ? /(?:checkpoint|artifact) hash mismatch/ : /manifest hash mismatch/);
        assert.deepEqual(await readFile(join(context.state, 'current.tsv')), accepted);
        assert.deepEqual(await archivedDecision(context.state, result), result);
        assert.equal(context.native.candidateEpochs.length, 1);
    });
});

test('a checkout containing only compact accepted artifacts cannot reacquire a different evaluation corpus', async t => {
    const context = await fixture(t);
    const first = await context.workflow.cycle(signal());
    assert.equal(first.status, 'promoted');
    const head = await publishedHead(context.state);
    const checkout = join(context.directory, 'clean-checkout');
    const release = join(checkout, 'releases', head.releaseId);
    await mkdir(release, { recursive: true });
    for (const name of compactFiles)
        await writeFile(join(release, name), await readFile(join(context.state, 'releases', head.releaseId, name)));
    const accepted = await readFile(join(context.state, 'current.tsv'));
    await writeFile(join(checkout, 'current.tsv'), accepted);
    let acquisitions = 0;
    const collector: ScholarlyCollector = { ...context.collector, async collect() {
        acquisitions++; throw new Error('Clean checkout must not start acquisition');
    } };
    const workflow = new ScholarlyTrainingWorkflow({ directory: checkout, collector, native: context.native, clock });
    const result = await workflow.cycle(signal());
    assert.equal(result.status, 'blocked');
    assert.match(result.reasons.join('; '), /local.*evidence|restore.*evidence|benchmark|ENOENT/i);
    assert.equal(acquisitions, 0);
    assert.equal(context.native.initCount, 1);
    assert.deepEqual(context.native.candidateEpochs, [3]);
    assert.deepEqual(await readFile(join(checkout, 'current.tsv')), accepted);
    assert.deepEqual(await archivedDecision(checkout, result), result);
});

test('missing private evidence blocks continuation before acquisition and preserves the compact head', async t => {
    for (const name of ['benchmark.json', 'train.txt', 'development.txt', 'test.txt', 'dataset.json'] as const)
        await t.test(name, async subtest => {
            const context = await fixture(subtest);
            const first = await context.workflow.cycle(signal());
            assert.equal(first.status, 'promoted');
            const accepted = await readFile(join(context.state, 'current.tsv'));
            const path = name === 'benchmark.json' ? join(context.state, name) :
                join(context.state, 'releases', first.releaseId!, name);
            await rm(path);
            let acquisitions = 0;
            const collector: ScholarlyCollector = { ...context.collector, async collect() {
                acquisitions++; throw new Error('Missing evidence must be detected before acquisition');
            } };
            const workflow = new ScholarlyTrainingWorkflow({ directory: context.state, collector, native: context.native, clock });
            const result = await workflow.cycle(signal());
            assert.equal(result.status, 'blocked');
            assert.match(result.reasons.join('; '), /local.*evidence|restore.*evidence|benchmark|ENOENT/i);
            assert.equal(acquisitions, 0);
            assert.equal(context.native.initCount, 1);
            assert.deepEqual(context.native.candidateEpochs, [3]);
            assert.deepEqual(await readFile(join(context.state, 'current.tsv')), accepted);
            assert.deepEqual(await archivedDecision(context.state, result), result);
        });
});

test('tampering compact model or TSV metadata blocks continuation before acquisition', async t => {
    for (const name of ['model.cgnn', 'release.tsv', 'citations.tsv', 'metrics.tsv'] as const)
        await t.test(name, async subtest => {
            const context = await fixture(subtest);
            const first = await context.workflow.cycle(signal());
            assert.equal(first.status, 'promoted');
            const accepted = await readFile(join(context.state, 'current.tsv'));
            await writeFile(join(context.state, 'releases', first.releaseId!, name), 'tampered public artifact\n');
            let acquisitions = 0;
            const collector: ScholarlyCollector = { ...context.collector, async collect() {
                acquisitions++; throw new Error('Artifact tampering must be detected before acquisition');
            } };
            const workflow = new ScholarlyTrainingWorkflow({ directory: context.state, collector, native: context.native, clock });
            const result = await workflow.cycle(signal());
            assert.equal(result.status, 'blocked');
            assert.match(result.reasons.join('; '), /hash mismatch|artifact|manifest/i);
            assert.equal(acquisitions, 0);
            assert.deepEqual(context.native.candidateEpochs, [3]);
            assert.deepEqual(await readFile(join(context.state, 'current.tsv')), accepted);
            assert.deepEqual(await archivedDecision(context.state, result), result);
        });
});

test('binary export failure or cancellation never replaces an accepted compact head', async t => {
    for (const failure of ['export-failure', 'cancel-export'] as const) await t.test(failure, async subtest => {
        const context = await fixture(subtest);
        const first = await context.workflow.cycle(signal());
        assert.equal(first.status, 'promoted');
        const accepted = await readFile(join(context.state, 'current.tsv'));
        const controller = new AbortController();
        if (failure === 'export-failure') context.native.failExport = true;
        else context.native.cancelDuringExport = () => controller.abort(new Error('Cycle cancelled during binary export'));
        const result = await context.workflow.cycle(controller.signal);
        assert.equal(result.status, 'blocked');
        assert.match(result.reasons.join('; '), failure === 'export-failure' ? /export failure/ : /cancelled/);
        assert.deepEqual(await readFile(join(context.state, 'current.tsv')), accepted);
        assert.equal((await readdir(join(context.state, 'releases'))).length, 1);
        assert.deepEqual(await archivedDecision(context.state, result), result);
    });
});

test('exporting an accepted compact release is idempotent and performs no acquisition or training', async t => {
    const context = await fixture(t);
    const first = await context.workflow.cycle(signal());
    assert.equal(first.status, 'promoted');
    const accepted = await readFile(join(context.state, 'current.tsv'));
    const head = await publishedHead(context.state);
    let acquisitions = 0;
    const collector: ScholarlyCollector = { ...context.collector, async collect() {
        acquisitions++; throw new Error('Offline export must not acquire sources');
    } };
    const workflow = new ScholarlyTrainingWorkflow({ directory: context.state, collector, native: context.native, clock });
    assert.deepEqual(await workflow.exportAccepted(signal()), head);
    assert.deepEqual(await workflow.exportAccepted(signal()), head);
    assert.equal(acquisitions, 0);
    assert.equal(context.native.initCount, 1);
    assert.deepEqual(context.native.candidateEpochs, [3]);
    assert.equal(context.native.exportCount, 1);
    assert.deepEqual(await readFile(join(context.state, 'current.tsv')), accepted);
});

test('an existing JSON head migrates offline after exact replay without modifying its private evidence', async t => {
    const context = await fixture(t);
    const first = await context.workflow.cycle(signal());
    assert.equal(first.status, 'promoted');
    const head = await publishedHead(context.state);
    const release = join(context.state, 'releases', head.releaseId);
    const privateNames = ['manifest.json', 'dataset.json', 'benchmark.json', 'train.txt', 'development.txt', 'test.txt',
        'checkpoint.txt', 'quality.json', 'admission.json', 'decision.json'];
    const privateHashes = await Promise.all(privateNames.map(async name => [name, hash(await readFile(join(release, name)))] as const));
    const { binarySha256: _binary, artifactManifestSha256: _artifacts, ...legacyHead } = head;
    await writeFile(join(context.state, 'current.json'), JSON.stringify(legacyHead) + '\n');
    await rm(join(context.state, 'current.tsv'));
    for (const name of compactFiles.filter(name => name !== 'checkpoint.txt')) await rm(join(release, name));
    let acquisitions = 0;
    const collector: ScholarlyCollector = { ...context.collector, async collect() {
        acquisitions++; throw new Error('Offline migration must not acquire sources');
    } };
    const workflow = new ScholarlyTrainingWorkflow({ directory: context.state, collector, native: context.native, clock });
    const migrated = await workflow.exportAccepted(signal());
    assert.deepEqual(migrated, head);
    assert.equal(acquisitions, 0);
    assert.equal(context.native.initCount, 1);
    assert.deepEqual(context.native.candidateEpochs, [3]);
    assert.equal(context.native.replayCount, 2);
    assert.equal(context.native.exportCount, 2);
    for (const [name, expected] of privateHashes) assert.equal(hash(await readFile(join(release, name))), expected, name);
    const accepted = await readFile(join(context.state, 'current.tsv'));
    assert.deepEqual(await workflow.exportAccepted(signal()), migrated);
    assert.equal(context.native.exportCount, 2);
    assert.deepEqual(await readFile(join(context.state, 'current.tsv')), accepted);
});

test('failed offline migration preserves the legacy accepted head', async t => {
    for (const failure of ['replay-mismatch', 'export-failure', 'cancel-export'] as const)
        await t.test(failure, async subtest => {
            const context = await fixture(subtest);
            const first = await context.workflow.cycle(signal());
            assert.equal(first.status, 'promoted');
            const head = await publishedHead(context.state);
            const { binarySha256: _binary, artifactManifestSha256: _artifacts, ...legacyHead } = head;
            const accepted = Buffer.from(JSON.stringify(legacyHead) + '\n');
            await writeFile(join(context.state, 'current.json'), accepted);
            await rm(join(context.state, 'current.tsv'));
            for (const name of compactFiles.filter(name => name !== 'checkpoint.txt'))
                await rm(join(context.state, 'releases', head.releaseId, name));
            const checkpoint = await readFile(join(context.state, 'releases', head.releaseId, 'checkpoint.txt'));
            const controller = new AbortController();
            if (failure === 'replay-mismatch') context.native.replayMismatch = true;
            else if (failure === 'export-failure') context.native.failExport = true;
            else context.native.cancelDuringExport = () => controller.abort(new Error('Migration cancelled during export'));
            await assert.rejects(context.workflow.exportAccepted(controller.signal),
                failure === 'replay-mismatch' ? /replay hash mismatch/ : failure === 'export-failure' ? /export failure/ : /cancelled/);
            assert.deepEqual(await readFile(join(context.state, 'current.json')), accepted);
            assert(!existsSync(join(context.state, 'current.tsv')));
            assert.deepEqual(await readFile(join(context.state, 'releases', head.releaseId, 'checkpoint.txt')), checkpoint);
            assert.equal(context.native.initCount, 1);
            assert.deepEqual(context.native.candidateEpochs, [3]);
        });
});

test('pre-publication I/O failures archive a blocked decision without leaving a false promotion', async t => {
    await t.test('release directory creation fails on the first cycle', async subtest => {
        const context = await fixture(subtest);
        context.native.afterReplay = async () => { await writeFile(join(context.state, 'releases'), 'fault: this path is a file\n'); };
        const result = await context.workflow.cycle(signal());
        assert.equal(result.status, 'blocked');
        assert.match(result.reasons.join('; '), /EEXIST|ENOTDIR/);
        assert(!existsSync(join(context.state, 'current.tsv')));
        assert.deepEqual(await archivedDecision(context.state, result), result);
        assert(existsSync(join(context.state, 'runs', result.runId, 'checkpoint.txt')));
    });
    await t.test('manifest write fails while an accepted head remains intact', async subtest => {
        const context = await fixture(subtest);
        const first = await context.workflow.cycle(signal());
        assert.equal(first.status, 'promoted');
        const accepted = await readFile(join(context.state, 'current.tsv'));
        context.native.afterReplay = async output => { await mkdir(join(dirname(output), 'manifest.json')); };
        const result = await context.workflow.cycle(signal());
        assert.equal(result.status, 'blocked');
        assert.match(result.reasons.join('; '), /EEXIST|EISDIR|EACCES|EPERM/);
        assert.deepEqual(await readFile(join(context.state, 'current.tsv')), accepted);
        assert.deepEqual(await archivedDecision(context.state, result), result);
        assert.equal((await readdir(join(context.state, 'releases'))).length, 1);
        assert(existsSync(join(context.state, 'releases', first.releaseId!, 'checkpoint.txt')));
    });
});

test('cancellation before acquisition or after replay cannot publish a new head', async t => {
    await t.test('already cancelled', async subtest => {
        const context = await fixture(subtest);
        const controller = new AbortController(); controller.abort(new Error('Cycle cancelled before acquisition'));
        const result = await context.workflow.cycle(controller.signal);
        assert.equal(result.status, 'blocked');
        assert.match(result.reasons.join('; '), /cancelled/);
        assert(!existsSync(join(context.state, 'current.tsv')));
        assert.equal(context.native.initCount, 0);
        assert.deepEqual(await archivedDecision(context.state, result), result);
    });
    await t.test('cancel before final publication', async subtest => {
        const context = await fixture(subtest);
        assert.equal((await context.workflow.cycle(signal())).status, 'promoted');
        const accepted = await readFile(join(context.state, 'current.tsv'));
        const controller = new AbortController();
        context.native.cancelDuringReplay = () => controller.abort(new Error('Cycle cancelled after replay'));
        const result = await context.workflow.cycle(controller.signal);
        assert.equal(result.status, 'blocked');
        assert.match(result.reasons.join('; '), /cancelled/);
        assert.deepEqual(await readFile(join(context.state, 'current.tsv')), accepted);
        assert.deepEqual(await archivedDecision(context.state, result), result);
    });
});

test('unattended policy bounds require two references and cap accepted unknown-token rate', () => {
    assert.equal(validateScholarlyPolicy({ minimumReferences: 2, maximumUnknownRate: 0 }).minimumReferences, 2);
    assert.equal(validateScholarlyPolicy({ maximumUnknownRate: 0.25 }).maximumUnknownRate, 0.25);
    for (const minimumReferences of [0, 1, 2.5, 9, Number.NaN])
        assert.throws(() => validateScholarlyPolicy({ minimumReferences }), /minimumReferences/);
    for (const maximumUnknownRate of [-0.1, 0.250001, 1, Number.NaN, Number.POSITIVE_INFINITY])
        assert.throws(() => validateScholarlyPolicy({ maximumUnknownRate }), /maximumUnknownRate/);
    assert.throws(() => validateScholarlyPolicy({ epochsPerCycle: 10001 }), /epochsPerCycle/);
    assert.throws(() => validateScholarlyPolicy({ minimumPapers: 12, paperLimit: 6 }), /minimumPapers/);
    assert.throws(() => validateScholarlyPolicy({ queries: ['sensitive\nquery'] }), /queries/);
    assert.throws(() => validateScholarlyPolicy({ undeclaredConfidence: 0.99 }), /unknown scholarly/);
    assert.equal(defaultScholarlyPolicy.minimumReferences, 2);
});

test('native metric and candidate schemas reject nonfinite or impossible quality measurements', () => {
    for (const invalid of [{ tokens: 1 }, { unknownTokens: 101 }, { crossEntropy: Number.NaN },
        { crossEntropy: Number.POSITIVE_INFINITY }, { crossEntropy: -1 }, { accuracy: 1.01 }])
        assert.throws(() => nativeScholarlyMetrics({ ...metrics(1), ...invalid }), /invalid native/);
    const valid: NativeScholarlyCandidate = { version: 1, before: { training: metrics(2), development: metrics(2) },
        after: { training: metrics(1), development: metrics(1) }, epochs: 1, steps: 100 };
    assert.deepEqual(nativeScholarlyCandidate(valid), valid);
    for (const invalid of [{ epochs: 0 }, { steps: 1 }, { after: { training: metrics(1), development: metrics(Number.NaN) } }])
        assert.throws(() => nativeScholarlyCandidate({ ...valid, ...invalid }), /invalid native/);
});

const requestedExecutable = process.env.CGAI_EXECUTABLE ? resolve(process.env.CGAI_EXECUTABLE) : undefined;
if (requestedExecutable && !existsSync(requestedExecutable)) throw new Error('CGAI_EXECUTABLE does not exist');
const nativeExecutable = requestedExecutable ?? ['../../../../build/deterministic/Release/cgai.exe', '../../../../build/deterministic/cgai',
    '../../../../build/dev/Release/cgai.exe', '../../../../build/dev/cgai', '../../../../build/Release/cgai.exe', '../../../../build/cgai']
    .map(relative => fileURLToPath(new URL(relative, import.meta.url))).find(existsSync);

test('compiled C11 adapter trains, replays, and exports a loadable model with held-out unknown words',
    { skip: !nativeExecutable }, async t => {
        const directory = await mkdtemp(join(tmpdir(), 'cgai-scholarly-native-'));
        t.after(() => rm(directory, { recursive: true, force: true }));
        const native = new NativeScholarlyCli(nativeExecutable!);
        const train = join(directory, 'train.txt'), development = join(directory, 'development.txt');
        const initial = join(directory, 'initial.txt'), checkpoint = join(directory, 'checkpoint.txt'), replay = join(directory, 'replayed.txt');
        await writeFile(train, 'a b left b a right '.repeat(16));
        await writeFile(development, 'b a right a b left '.repeat(7) + 'unseen');
        assert.equal(await native.fingerprint(), hash(await readFile(nativeExecutable!)));
        const pending = signal();
        await native.init(train, initial, pending);
        const trainingBefore = await native.score(initial, train, pending);
        const developmentBefore = await native.score(initial, development, pending);
        assert.equal(developmentBefore.unknownTokens, 1);
        const result = await native.candidate(initial, train, development, checkpoint, 2, 0.015, pending);
        assert.deepEqual(result.before, { training: trainingBefore, development: developmentBefore });
        assert.deepEqual(result.after.training, await native.score(checkpoint, train, pending));
        assert.deepEqual(result.after.development, await native.score(checkpoint, development, pending));
        assert.equal(result.epochs, 2);
        assert.equal(result.steps, 194);
        assert.equal(result.after.training.unknownTokens, 0);
        assert.equal(result.after.development.unknownTokens, 1);
        assert(result.after.training.crossEntropy < trainingBefore.crossEntropy);
        await native.replay(checkpoint, train, replay, 0.015, pending);
        assert.equal(hash(await readFile(checkpoint)), hash(await readFile(replay)));
        const model = join(directory, 'model.cgnn');
        await native.export(checkpoint, model, pending);
        assert.equal((await readFile(model)).subarray(0, 8).toString('ascii'), 'CGAINN1\0');
        const loaded = await promisify(execFile)(nativeExecutable!, ['neural-evaluate', model, development],
            { encoding: 'utf8', windowsHide: true, timeout: 30000 });
        const scored = /targets=(\d+) cross-entropy=([\d.]+) perplexity=[\d.]+ accuracy=([\d.]+)% unknown=(\d+)/.exec(loaded.stdout);
        assert(scored, loaded.stdout);
        assert.equal(Number(scored[1]), result.after.development.tokens);
        assert.equal(Number(scored[4]), result.after.development.unknownTokens);
        assert(Math.abs(Number(scored[2]) - result.after.development.crossEntropy) <= 0.0000005);
        assert(Math.abs(Number(scored[3]) / 100 - result.after.development.accuracy) <= 0.00005);
    });
