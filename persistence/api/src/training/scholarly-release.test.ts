import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test, { type TestContext } from 'node:test';
import { scholarlyFixtureCollector, scholarlyFixturePaper } from './scholarly-fixtures.ts';
import { buildScholarlyCorpus, verifyScholarlyPapers } from './scholarly-verification.ts';
import { createScholarlyArtifacts, parseScholarlyPublishedHead, serializeScholarlyPublishedHead, verifyScholarlyArtifacts,
    type ScholarlyArtifactIdentity, type ScholarlyPublishedHead } from './scholarly-release.ts';

const hash = (value: string | Buffer) => createHash('sha256').update(value).digest('hex');
const signal = () => new AbortController().signal;
const score = (crossEntropy: number, accuracy: number) => ({ tokens: 101, unknownTokens: 2, crossEntropy, accuracy });

async function fixture(t: TestContext) {
    const directory = await mkdtemp(join(tmpdir(), 'cgai-scholarly-artifacts-'));
    t.after(() => rm(directory, { recursive: true, force: true }));
    const papers = Array.from({ length: 6 }, (_, index) => scholarlyFixturePaper(index));
    const admission = await verifyScholarlyPapers(papers, scholarlyFixtureCollector(papers), signal());
    assert.deepEqual(admission.excluded, []);
    const { corpus, benchmark } = buildScholarlyCorpus(admission.accepted);
    // These files exercise the metadata/hash boundary, and are explicitly not valid native models.
    const checkpoint = 'Fake test checkpoint bytes: native validity is tested by the C11 adapter.\n';
    await writeFile(join(directory, 'checkpoint.txt'), checkpoint);
    await writeFile(join(directory, 'model.cgnn'), 'Fake test binary bytes, not a compiled neural model.\n');
    const identity: ScholarlyArtifactIdentity = { version: 1, releaseId: hash('test scholarly release'), parentReleaseId: null,
        corpusSha256: corpus.sha256, benchmarkSha256: benchmark.sha256, checkpointSha256: hash(checkpoint),
        nativeSha256: hash('test native executable identity'), learningRate: 0.005, epochs: 1, steps: 100,
        promotedAt: '2026-10-02T12:00:00.000Z' };
    const before = score(4, 0.1), after = score(3.9, 0.11);
    const quality = { version: 1, corpusSha256: identity.corpusSha256, benchmarkSha256: identity.benchmarkSha256,
        nativeSha256: identity.nativeSha256, scope: 'attributed-source-excerpts-and-next-token-regression', scientificTruth: 'not-assessed',
        mode: 'rebuild-training-vocabulary', passed: true, failures: [],
        before: { development: before, test: before }, after: { development: after, test: after },
        candidate: { version: 1, before: { training: before, development: before }, after: { training: after, development: after }, epochs: 1, steps: 100 },
        replay: { required: true, verified: true, sha256: identity.checkpointSha256 },
        ignoredRawSnapshot: { text: '<article>RAW SNAPSHOT MUST STAY LOCAL</article>' } };
    return { directory, corpus, identity, quality };
}
async function exported(t: TestContext) {
    const context = await fixture(t);
    const hashes = await createScholarlyArtifacts(context.directory, context.identity, context.corpus, context.quality, signal());
    const head: ScholarlyPublishedHead = { ...context.identity, manifestSha256: hash('local evidence manifest'), ...hashes };
    return { ...context, head };
}

test('compiled release metadata publishes only citations, counters, metrics and hashes without raw evidence', async t => {
    const context = await exported(t);
    assert.deepEqual((await readdir(context.directory)).sort(), ['checkpoint.txt', 'citations.tsv', 'metrics.tsv', 'model.cgnn', 'release.tsv']);
    await verifyScholarlyArtifacts(context.directory, context.head);
    const metadata = (await Promise.all(['citations.tsv', 'metrics.tsv', 'release.tsv'].map(name => readFile(join(context.directory, name), 'utf8')))).join('');
    assert(!metadata.includes('RAW SNAPSHOT MUST STAY LOCAL'));
    assert(!metadata.includes('<article'));
    assert(!metadata.includes('ignoredRawSnapshot'));
    assert(!metadata.includes('scientificTruth":'));
    for (const passage of context.corpus.passages) assert(!metadata.includes(passage.quote));
    for (const paper of context.corpus.papers) {
        assert(metadata.includes(paper.doi));
        assert(metadata.includes(paper.title));
        assert(metadata.includes(paper.licenseUrl));
        assert(metadata.includes(paper.fullText.sha256));
        for (const reference of paper.verification.citedReferences) {
            assert(metadata.includes(reference.doi)); assert(metadata.includes(reference.metadataSha256));
            assert(!metadata.includes(reference.snapshot.text));
        }
    }
    assert.equal(hash(await readFile(join(context.directory, 'model.cgnn'))), context.head.binarySha256);
    assert.equal(hash(await readFile(join(context.directory, 'release.tsv'))), context.head.artifactManifestSha256);
});

test('durable published heads round-trip with an exact schema and reject malformed TSV', async t => {
    const context = await exported(t), text = serializeScholarlyPublishedHead(context.head);
    assert.deepEqual(parseScholarlyPublishedHead(text), context.head);
    assert(!text.includes('\r'));
    for (const invalid of [text + 'version\t1\n', text + 'raw.snapshot\tforbidden\n', text.replace('steps\t100', 'steps\tInfinity'),
        text.replace('version\t1', 'version\t01'), text.replace('version\t1', 'version\t\\q'), text.replace(/\n/g, '\r\n'),
        text.slice(0, -1), text.replace('format\tscholarly-head-v1', 'format\tscholarly-head-v2'), text + '\0'])
        assert.throws(() => parseScholarlyPublishedHead(invalid));
    assert.throws(() => serializeScholarlyPublishedHead({ ...context.head, binarySha256: 'bad hash' }));
    assert.throws(() => serializeScholarlyPublishedHead({ ...context.head, promotedAt: 'yesterday' }));
});

test('citation metadata escapes tabs, newlines and literal backslashes while preserving LF records', async t => {
    const context = await fixture(t), first = context.corpus.papers[0]!;
    const corpus = { ...context.corpus, papers: [{ ...first, title: 'Title\twith\nline\rbreak \\ literal',
        authors: ['Author\tName \\ Example'] }, ...context.corpus.papers.slice(1)] };
    await createScholarlyArtifacts(context.directory, context.identity, corpus, context.quality, signal());
    const citations = await readFile(join(context.directory, 'citations.tsv'), 'utf8');
    assert(citations.includes('Title\\twith\\nline\\rbreak \\\\ literal'));
    assert(citations.includes('Author\\tName \\\\ Example'));
    assert(!citations.includes('\r'));
    assert(citations.trimEnd().split('\n').every(line => line.split('\t').length === 2));
});

test('identical compact exports can be reused while changed existing metadata fails closed', async t => {
    const context = await exported(t);
    assert.deepEqual(await createScholarlyArtifacts(context.directory, context.identity, context.corpus, context.quality, signal()),
        { binarySha256: context.head.binarySha256, artifactManifestSha256: context.head.artifactManifestSha256 });
    await writeFile(join(context.directory, 'citations.tsv'), 'Changed existing citation metadata.\n');
    await assert.rejects(createScholarlyArtifacts(context.directory, context.identity, context.corpus, context.quality, signal()),
        /immutable scholarly artifact differs: citations.tsv/);
});

test('every compiled release file and identity is verified against the compact head', async t => {
    for (const name of ['checkpoint.txt', 'model.cgnn', 'citations.tsv', 'metrics.tsv', 'release.tsv']) await t.test(name, async subtest => {
        const context = await exported(subtest);
        await writeFile(join(context.directory, name), 'Tampered model artifact bytes.\n');
        await assert.rejects(verifyScholarlyArtifacts(context.directory, context.head),
            name === 'release.tsv' ? /artifact manifest hash mismatch/ : /artifact hash mismatch/);
    });
    await t.test('head identity', async subtest => {
        const context = await exported(subtest);
        await assert.rejects(verifyScholarlyArtifacts(context.directory, { ...context.head, benchmarkSha256: hash('different held-out benchmark') }),
            /artifact identity mismatch/);
    });
    await t.test('missing compiled model', async subtest => {
        const context = await exported(subtest);
        await rm(join(context.directory, 'model.cgnn'));
        await assert.rejects(verifyScholarlyArtifacts(context.directory, context.head), { code: 'ENOENT' });
    });
});

test('nonfinite metrics, inconsistent counters and unverified replay cannot produce release metadata', async t => {
    const updates = [
        (quality: Awaited<ReturnType<typeof fixture>>['quality']) => ({ ...quality, after: { ...quality.after,
            development: { ...quality.after.development, crossEntropy: Infinity } } }),
        (quality: Awaited<ReturnType<typeof fixture>>['quality']) => ({ ...quality, candidate: { ...quality.candidate, epochs: 2 } }),
        (quality: Awaited<ReturnType<typeof fixture>>['quality']) => ({ ...quality, replay: { ...quality.replay, verified: false } }),
        (quality: Awaited<ReturnType<typeof fixture>>['quality']) => ({ ...quality, passed: false }),
    ];
    for (const update of updates) {
        const context = await fixture(t);
        await assert.rejects(createScholarlyArtifacts(context.directory, context.identity, context.corpus, update(context.quality), signal()));
        assert.deepEqual((await readdir(context.directory)).sort(), ['checkpoint.txt', 'model.cgnn']);
    }
});

test('wrong checkpoint identity, cancellation and oversized metadata fail before publication', async t => {
    await t.test('checkpoint identity', async subtest => {
        const context = await fixture(subtest);
        await assert.rejects(createScholarlyArtifacts(context.directory, { ...context.identity, checkpointSha256: hash('another checkpoint') },
            context.corpus, context.quality, signal()), /checkpoint hash mismatch/);
    });
    await t.test('cancelled export', async subtest => {
        const context = await fixture(subtest), controller = new AbortController();
        controller.abort(new Error('test cancelled scholarly export'));
        await assert.rejects(createScholarlyArtifacts(context.directory, context.identity, context.corpus, context.quality, controller.signal),
            /test cancelled scholarly export/);
        assert.deepEqual((await readdir(context.directory)).sort(), ['checkpoint.txt', 'model.cgnn']);
    });
    await t.test('oversized manifest', async subtest => {
        const context = await exported(subtest);
        await writeFile(join(context.directory, 'release.tsv'), 'x'.repeat(32769));
        await assert.rejects(verifyScholarlyArtifacts(context.directory, context.head), /size limit/);
    });
});
