import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import test, { type TestContext } from 'node:test';
import { validateRepositoryDataset, type RepositoryDialogueDataset } from '../chat/dataset.ts';
import { validateTrainingValidation } from '../chat/training-quality.ts';
import { buildRepositoryCorpus } from './repository-corpus.ts';
import { applyReviews, createReviewTemplate, defaultReleaseOptions, exportDatasetRelease, sha256, verifyDatasetRelease, writeSnapshot } from './workflow.ts';

function fixture(): RepositoryDialogueDataset {
    const value = JSON.parse(readFileSync(new URL('../../../../data/chat/factual-dialogues-v2.json', import.meta.url), 'utf8'));
    value.version = 3;
    value.purpose = 'repository-engineering';
    value.sourceDocuments.forEach((source: { id: string; text: string; provenance: unknown }) => {
        source.provenance = { kind: 'repository', repository: 'unit-test-fixture', path: `fixtures/${source.id}.txt`,
            commit: 'a'.repeat(40), sha256: sha256(source.text), snapshot: 'workspace' };
    });
    return validateRepositoryDataset(value).dataset;
}
const engineering = { ...defaultReleaseOptions, reviewPolicy: 'agent-or-human' as const };
function reviewed(kind: 'human' | 'agent' = 'agent'): RepositoryDialogueDataset {
    const value = fixture();
    const review = createReviewTemplate(value, { kind, id: 'test-reviewer' });
    review.decisions.forEach(decision => { decision.status = 'approved'; decision.notes = 'Unit-test review decision.'; });
    return applyReviews(value, review);
}
async function temporary(t: TestContext): Promise<string> {
    const directory = await mkdtemp(join(tmpdir(), 'cgai-training-workflow-'));
    t.after(() => rm(directory, { recursive: true, force: true }));
    return directory;
}

test('review templates approve nothing; decisions are tied to both corpus and individual record', () => {
    const value = fixture();
    const template = createReviewTemplate(value, { kind: 'agent', id: 'reviewer' });
    const pending = applyReviews(value, template);
    assert(pending.train.every(record => record.review?.status === 'candidate'));
    const stale = structuredClone(template);
    stale.decisions[0]!.recordSha256 = 'b'.repeat(64);
    assert.throws(() => applyReviews(value, stale), /stale review/);
    value.description += ' changed';
    assert.throws(() => applyReviews(value, template), /exact dataset/);
});

test('unknown/duplicate decisions and missing approval notes cannot be applied', () => {
    for (const mode of ['unknown', 'duplicate', 'notes', 'human-kind'] as const) {
        const value = fixture(), review = createReviewTemplate(value, { kind: 'agent', id: 'reviewer' });
        if (mode === 'unknown') review.decisions[0]!.id = 'absent';
        if (mode === 'duplicate') review.decisions.push(review.decisions[0]!);
        if (mode === 'notes') { review.decisions[0]!.status = 'approved'; review.decisions[0]!.notes = ' '; }
        if (mode === 'human-kind') (review.reviewer as { kind: string }).kind = 'automatically-human';
        assert.throws(() => applyReviews(value, review));
    }
});

test('export requires approvals under the selected policy and leaves no partial directory on rejection', async t => {
    const directory = await temporary(t);
    await assert.rejects(exportDatasetRelease(fixture(), join(directory, 'unreviewed'), engineering), /no approved train/);
    await assert.rejects(exportDatasetRelease(reviewed(), join(directory, 'agent')), /human review policy/);
    const human = await exportDatasetRelease(reviewed('human'), join(directory, 'human'));
    assert.equal(human.review.human, 18);
    assert.equal(human.review.agent, 0);
    assert.equal(human.review.allReviewsDeclaredHuman, true);
    assert.equal(human.review.reviewerIdentityVerified, false);
});

test('released request uses approved training and held-out development only and verifies without source checkout', async t => {
    const directory = join(await temporary(t), 'release');
    const data = reviewed();
    const manifest = await exportDatasetRelease(data, directory, engineering);
    assert.deepEqual(await verifyDatasetRelease(directory), manifest);
    assert.deepEqual(manifest.counts, { train: 6, development: 6, test: 6 });
    assert.equal(manifest.review.allReviewsDeclaredHuman, false);
    const request = JSON.parse(await readFile(join(directory, 'train-request.json'), 'utf8'));
    const validation = validateTrainingValidation(request.examples, request.validation);
    assert.deepEqual(request.examples.map((record: { id: string }) => record.id), data.train.map(record => record.id));
    assert.deepEqual(validation.cases.map(record => record.id), data.development.map(record => record.id));
    assert(data.test.every(record => !JSON.stringify(request).includes(record.id)));
    assert.equal(request.provenance.datasetRelease, manifest.releaseId);
});

test('rejected rows are excluded with an audit list and unused snapshots are pruned', async t => {
    const data = reviewed(), review = createReviewTemplate(data, { kind: 'agent', id: 'second-reviewer' });
    review.decisions = [review.decisions[0]!];
    review.decisions[0]!.status = 'rejected';
    review.decisions[0]!.notes = 'Question is ambiguous.';
    const directory = join(await temporary(t), 'release');
    const manifest = await exportDatasetRelease(applyReviews(data, review), directory, engineering);
    assert.equal(manifest.counts.train, 5);
    assert.deepEqual(manifest.identity.excluded, [{ id: data.train[0]!.id, reason: 'rejected' }]);
    await verifyDatasetRelease(directory);
});

test('release output is reproducible, refuses overwrite, and detects edited bytes', async t => {
    const parent = await temporary(t), first = join(parent, 'first'), second = join(parent, 'second');
    const data = reviewed();
    const a = await exportDatasetRelease(data, first, engineering), b = await exportDatasetRelease(data, second, engineering);
    assert.equal(a.releaseId, b.releaseId);
    assert.deepEqual(a.files, b.files);
    await assert.rejects(exportDatasetRelease(data, first, engineering), /EEXIST/);
    await writeFile(join(first, 'train-request.json'), '{}\n');
    await assert.rejects(verifyDatasetRelease(first), /checksum mismatch/);
});

test('updating a request checksum cannot hide request drift from the released dataset', async t => {
    const directory = join(await temporary(t), 'release');
    const manifest = await exportDatasetRelease(reviewed(), directory, engineering);
    const content = '{}\n';
    await writeFile(join(directory, 'train-request.json'), content);
    manifest.files['train-request.json'] = sha256(content);
    await writeFile(join(directory, 'manifest.json'), JSON.stringify(manifest));
    await assert.rejects(verifyDatasetRelease(directory), /derivation mismatch/);
});

test('invalid model windows fail before writing and snapshots cannot overwrite inputs', async t => {
    const directory = await temporary(t);
    await assert.rejects(exportDatasetRelease(reviewed(), join(directory, 'invalid'), { ...engineering,
        config: { ...engineering.config, promptWindow: 250, responseWindow: 64 } }), /native limits/);
    const path = join(directory, 'snapshot.json');
    await writeSnapshot(path, { version: 1 });
    await assert.rejects(writeSnapshot(path, { version: 2 }), /EEXIST/);
    assert.deepEqual(JSON.parse(await readFile(path, 'utf8')), { version: 1 });
});

test('a repository release rebuilt from authored seeds verifies and retains fixed split sizes', async t => {
    const root = fileURLToPath(new URL('../../../../', import.meta.url));
    const candidates = await buildRepositoryCorpus(root);
    const review = createReviewTemplate(candidates, { kind: 'agent', id: 'unit-test-reviewer' });
    review.decisions.forEach(decision => {
        decision.status = 'approved';
        decision.notes = 'Test-only approval of deterministic authored seed records.';
    });
    const directory = join(await temporary(t), 'repository-release');
    const exported = await exportDatasetRelease(applyReviews(candidates, review), directory, engineering);
    const manifest = await verifyDatasetRelease(directory);
    assert.deepEqual(manifest, exported);
    assert.deepEqual(manifest.counts, { train: 120, development: 40, test: 40 });
    assert.deepEqual(manifest.review, { human: 0, agent: 200, allReviewsDeclaredHuman: false, reviewerIdentityVerified: false });
    assert.equal(manifest.identity.excluded.length, 0);
});
