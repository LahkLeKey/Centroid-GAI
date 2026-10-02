import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import test from 'node:test';
import { recordReviewHash, sourceReviewHash, validateDataset, validateFactualDataset, validateRepositoryDataset,
    type RepositoryDialogueDataset, type RepositoryDialogueRecord, type RepositoryDialogueSource } from './dataset.ts';

const sha = (text: string) => createHash('sha256').update(text, 'utf8').digest('hex');
function fixture(): RepositoryDialogueDataset {
    const dataset: RepositoryDialogueDataset = { version: 3, purpose: 'repository-engineering', description: 'Repository snapshot validator fixture.',
        sourceDocuments: [], train: [], development: [], test: [] };
    for (const [index, split] of (['train', 'development', 'test'] as const).entries()) {
        const answer = split + ' cache ttl is ' + (index + 12) + ' seconds.';
        const text = answer + '\nAdditional source context.\n';
        const source: RepositoryDialogueSource = { id: split + '-source', text, provenance: { kind: 'repository', repository: 'Centroid-GAI',
            commit: 'a'.repeat(40), path: 'docs/' + split + '.md', sha256: sha(text), snapshot: 'workspace' } };
        dataset.sourceDocuments.push(source);
        dataset[split].push({ id: split + '-question', family: split + '-cache', category: 'direct', sources: [source.id],
            messages: [{ role: 'evidence', content: answer }, { role: 'user', content: 'What is ' + split + ' cache ttl?' }], answer,
            expected: 'answer', acceptedAnswers: [answer], requiredClaims: [answer],
            evidence: [{ id: source.id, excerpt: answer, startLine: 1, endLine: 1 }] });
    }
    return dataset;
}
function withReview(record: RepositoryDialogueRecord, source: RepositoryDialogueSource, split: 'train' | 'development' | 'test' = 'train'): RepositoryDialogueRecord {
    return { ...record, review: { status: 'approved', split, reviewer: { kind: 'agent', id: 'validator-test-agent' },
        reviewedAt: '2026-10-01T12:00:00.000Z', recordSha256: recordReviewHash(record),
        sourceSha256: { [source.id]: source.provenance.sha256 }, sourceBindingsSha256: { [source.id]: sourceReviewHash(source) },
        notes: 'Agent review of an authored validator fixture, not a human review.' } };
}

test('v3 accepts unreviewed workspace candidates with exact source bytes and preserves both validation entry points', () => {
    const dataset = fixture();
    assert.equal(validateDataset(dataset).dataset.version, 3);
    assert.equal(validateFactualDataset(dataset).dataset.version, 3);
    const result = validateRepositoryDataset(dataset);
    assert.deepEqual(result.dataset, dataset);
    assert.equal(result.hashes.sources?.length, 64);
    assert.deepEqual(result.hashes, validateRepositoryDataset(structuredClone(dataset)).hashes);
});

test('repository provenance rejects traversal, absolute paths, NUL, invalid commits, and mismatched exact bytes', () => {
    const invalidProvenance = [
        { path: '../secret.txt' }, { path: 'docs/../../secret.txt' }, { path: '/docs/a.md' }, { path: 'docs//a.md' },
        { path: 'docs/./a.md' }, { path: 'C:/docs/a.md' }, { path: 'docs\\a.md' }, { path: 'docs/a\0.md' },
        { commit: 'HEAD' }, { commit: 'a'.repeat(39) }, { sha256: 'b'.repeat(64) }, { snapshot: 'unverified' },
        { repository: 'repo\0name' },
    ];
    for (const change of invalidProvenance) {
        const dataset = fixture();
        const source = dataset.sourceDocuments[0]!;
        dataset.sourceDocuments[0] = { ...source, provenance: { ...source.provenance, ...change } } as RepositoryDialogueSource;
        assert.throws(() => validateRepositoryDataset(dataset), /repository source provenance/, JSON.stringify(change));
    }
    const changedBytes = fixture();
    changedBytes.sourceDocuments[0] = { ...changedBytes.sourceDocuments[0]!, text: changedBytes.sourceDocuments[0]!.text.replace(/\n/g, '\r\n') };
    assert.throws(() => validateRepositoryDataset(changedBytes), /content hash/);
});

test('repository source paths stay in one split despite changed content, source IDs, commit, or path case', () => {
    const dataset = fixture();
    const testSource = dataset.sourceDocuments[2]!;
    dataset.sourceDocuments[2] = { ...testSource, provenance: { ...testSource.provenance,
        path: dataset.sourceDocuments[0]!.provenance.path.toUpperCase(), commit: 'b'.repeat(64), snapshot: 'commit' } };
    assert.throws(() => validateRepositoryDataset(dataset), /source path leakage/);
});

test('existing source content, family, and exact span isolation also apply to repository records', () => {
    const family = fixture();
    family.test[0] = { ...family.test[0]!, family: family.train[0]!.family };
    assert.throws(() => validateRepositoryDataset(family), /family leakage/);
    const content = fixture();
    const text = content.sourceDocuments[0]!.text;
    content.sourceDocuments[2] = { ...content.sourceDocuments[2]!, text,
        provenance: { ...content.sourceDocuments[2]!.provenance, sha256: sha(text) } };
    assert.throws(() => validateRepositoryDataset(content), /source content leakage/);
    const span = fixture();
    span.test[0] = { ...span.test[0]!, evidence: [{ ...span.test[0]!.evidence[0]!, endLine: 2 }] };
    assert.throws(() => validateRepositoryDataset(span), /exact source span/);
});

test('conflicting evidence can be an explicitly labeled abstention without inventing a resolved answer', () => {
    const dataset = fixture();
    const record = dataset.test[0]!;
    const source = dataset.sourceDocuments[2]!;
    const text = source.text + 'test cache ttl is 99 seconds.\n';
    dataset.sourceDocuments[2] = { ...source, text, provenance: { ...source.provenance, sha256: sha(text) } };
    const other = { id: source.id, excerpt: 'test cache ttl is 99 seconds.', startLine: 3, endLine: 3 };
    dataset.test[0] = { ...record, category: 'conflict', expected: 'abstain', answer: 'The supplied evidence conflicts.',
        acceptedAnswers: ['The supplied evidence conflicts.'], requiredClaims: [], evidence: [...record.evidence, other],
        messages: [...record.messages.slice(0, -1), { role: 'evidence', content: other.excerpt }, record.messages.at(-1)!] };
    assert.equal(validateRepositoryDataset(dataset).dataset.test[0]!.category, 'conflict');
});

test('approved reviews bind current record fields and exact complete source hashes', () => {
    const dataset = fixture();
    dataset.train[0] = withReview(dataset.train[0]!, dataset.sourceDocuments[0]!);
    assert.equal(validateRepositoryDataset(dataset).dataset.train[0]!.review?.reviewer.kind, 'agent');
    const changedRecord = structuredClone(dataset);
    changedRecord.train[0] = { ...changedRecord.train[0]!, family: 'new-family' };
    assert.throws(() => validateRepositoryDataset(changedRecord), /review record hash is stale/);
    const changedContext = structuredClone(dataset);
    const source = changedContext.sourceDocuments[0]!;
    const text = source.text + 'New source context changes applicability.\n';
    changedContext.sourceDocuments[0] = { ...source, text, provenance: { ...source.provenance, sha256: sha(text) } };
    assert.throws(() => validateRepositoryDataset(changedContext), /review source hashes are stale/);
    const missingSource = structuredClone(dataset);
    missingSource.train[0] = { ...missingSource.train[0]!, review: { ...missingSource.train[0]!.review!, sourceSha256: {} } };
    assert.throws(() => validateRepositoryDataset(missingSource), /source hashes are stale or incomplete/);
});

test('review hashes ignore review metadata and object-key ordering while retaining every other field', () => {
    const dataset = fixture();
    const record = dataset.train[0]!;
    const reviewed = withReview(record, dataset.sourceDocuments[0]!);
    assert.equal(recordReviewHash(record), recordReviewHash(reviewed));
    const reversed = Object.fromEntries(Object.entries(record).reverse()) as unknown as RepositoryDialogueRecord;
    assert.equal(recordReviewHash(record), recordReviewHash(reversed));
    assert.notEqual(recordReviewHash(record), recordReviewHash({ ...record, category: 'citation' }));
});

test('changing source provenance invalidates prior approval even when source text is unchanged', () => {
    for (const change of [{ path: 'docs/renamed.md' }, { repository: 'Another-repository' },
        { commit: 'b'.repeat(64) }, { snapshot: 'commit' as const }]) {
        const dataset = fixture();
        const source = dataset.sourceDocuments[0]!;
        dataset.train[0] = withReview(dataset.train[0]!, source);
        dataset.sourceDocuments[0] = { ...source, provenance: { ...source.provenance, ...change } };
        assert.throws(() => validateRepositoryDataset(dataset), /review source bindings are stale/);
    }
    const dataset = fixture();
    const source = dataset.sourceDocuments[0]!;
    const reordered = { provenance: { ...source.provenance }, text: source.text, id: source.id };
    assert.equal(sourceReviewHash(source), sourceReviewHash(reordered));
});

test('moving an approved source and family group to another split invalidates the original review', () => {
    const dataset = fixture();
    dataset.train[0] = withReview(dataset.train[0]!, dataset.sourceDocuments[0]!, 'train');
    const formerTrain = dataset.train;
    dataset.train = dataset.test;
    dataset.test = formerTrain;
    assert.throws(() => validateRepositoryDataset(dataset), /review split binding is stale/);
});

test('review metadata requires an explicit reviewer kind, valid timestamp, notes, and source bindings', () => {
    for (const change of [
        { reviewer: { kind: 'reviewed', id: 'unknown' } }, { reviewer: { kind: 'agent', id: '' } },
        { reviewedAt: '2026-02-30T12:00:00.000Z' }, { notes: '' }, { status: 'ready' }, { recordSha256: 'short' },
        { sourceSha256: { 'train-source': 'b'.repeat(64) } },
        { sourceBindingsSha256: { 'train-source': 'b'.repeat(64) } },
    ]) {
        const dataset = fixture();
        const record = withReview(dataset.train[0]!, dataset.sourceDocuments[0]!);
        dataset.train[0] = { ...record, review: { ...record.review!, ...change } } as RepositoryDialogueRecord;
        assert.throws(() => validateRepositoryDataset(dataset), /review/);
    }
    for (const status of ['candidate', 'rejected'] as const) {
        const dataset = fixture();
        const record = withReview(dataset.train[0]!, dataset.sourceDocuments[0]!);
        dataset.train[0] = { ...record, review: { ...record.review!, status } };
        assert.equal(validateRepositoryDataset(dataset).dataset.train[0]!.review?.status, status);
    }
});
