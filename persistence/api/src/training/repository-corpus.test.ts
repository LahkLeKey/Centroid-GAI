import assert from 'node:assert/strict';
import test from 'node:test';
import { execFile } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFile, mkdir, mkdtemp, readFile, rm, symlink, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import { validateRepositoryDataset } from '../chat/dataset.ts';
import { validateTrainingValidation } from '../chat/training-quality.ts';
import { cLocaleTokens } from '../evaluation/codebase-data.ts';
import { buildRepositoryCorpus } from './repository-corpus.ts';
import { repositorySeeds } from './repository-seeds.ts';

const root = fileURLToPath(new URL('../../../..', import.meta.url));
const exec = promisify(execFile);
async function removeFixture(path: string): Promise<void> {
    assert.equal(dirname(resolve(path)), resolve(tmpdir()));
    await rm(path, { recursive: true, force: true });
}
async function fixture(excludeIncludes = false) {
    const directory = await mkdtemp(join(tmpdir(), 'cgai-corpus-'));
    try {
    for (const path of new Set(repositorySeeds.map(seed => seed.path))) {
        if (excludeIncludes && path.startsWith('include/')) continue;
        const destination = resolve(directory, path);
        await mkdir(dirname(destination), { recursive: true });
        await copyFile(resolve(root, path), destination);
    }
    await exec('git', ['-C', directory, 'init', '--quiet']);
    await exec('git', ['-C', directory, '-c', 'user.name=Corpus fixture', '-c', 'user.email=corpus@example.invalid', '-c', 'commit.gpgsign=false',
        'commit', '--quiet', '--allow-empty', '-m', 'Corpus fixture base']);
    return directory;
    } catch (error) { await removeFixture(directory); throw error; }
}

test('repository candidates are deterministic, bounded, source-disjoint and valid development admission', async () => {
    const first = await buildRepositoryCorpus(root), second = await buildRepositoryCorpus(root);
    assert.deepEqual(first, second);
    assert.equal(first.version, 3);
    assert.deepEqual([first.train.length, first.development.length, first.test.length], [120, 40, 40]);
    assert.equal(new Set(repositorySeeds.map(seed => seed.id)).size, 50);
    const records = [...first.train, ...first.development, ...first.test];
    assert.equal(records.some(record => record.review !== undefined), false, 'generation never fabricates review approval');
    for (const category of ['direct', 'follow-up', 'correction', 'citation', 'abstention', 'distractor', 'conflict', 'clarification'])
        assert.ok(records.some(record => record.category === category), category);
    for (const document of first.sourceDocuments) {
        assert.equal(document.text, await readFile(resolve(root, document.provenance.path), 'utf8'));
        assert.equal(document.provenance.sha256, createHash('sha256').update(document.text).digest('hex'));
        assert.equal(document.provenance.snapshot, 'workspace');
    }
    const sourceSplits = new Map<string, string>();
    for (const split of ['train', 'development', 'test'] as const) for (const record of first[split]) {
        for (const source of record.sources) {
            assert.ok(!sourceSplits.has(source) || sourceSplits.get(source) === split);
            sourceSplits.set(source, split);
        }
        assert.ok(cLocaleTokens(record.answer).length + 1 <= 64, record.id);
        assert.ok(record.messages.reduce((sum, message) => sum + cLocaleTokens(message.content).length + 2, 1) <= 192, record.id);
    }
    assert.equal(validateRepositoryDataset(first).dataset, first);
    const validation = validateTrainingValidation(first.train, { version: 1, cases: first.development.map(record => ({
        id: record.id, family: record.family, sources: record.sources, messages: record.messages.filter(message => message.role !== 'evidence'),
        expected: record.expected, acceptedAnswers: record.acceptedAnswers, evidence: record.evidence,
    })) });
    assert.equal(validation.cases.length, 40);
});

test('source drift with missing or nonunique authored anchors fails closed', async () => {
    const directory = await fixture();
    try {
        const path = resolve(directory, repositorySeeds[0]!.path), original = await readFile(path, 'utf8');
        await writeFile(path, original.replace(repositorySeeds[0]!.anchor, 'cmake_minimum_required(VERSION 3.99)'));
        await assert.rejects(buildRepositoryCorpus(directory), /Missing or nonunique source anchor: cmake-minimum/);
        await writeFile(path, `${original}\n${repositorySeeds[0]!.anchor}\n`);
        await assert.rejects(buildRepositoryCorpus(directory), /Missing or nonunique source anchor: cmake-minimum/);
    } finally { await removeFixture(directory); }
});

test('a source directory symlink cannot import content from outside the repository', async () => {
    const directory = await fixture(true), outside = await mkdtemp(join(tmpdir(), 'cgai-corpus-outside-'));
    try {
        for (const path of new Set(repositorySeeds.filter(seed => seed.path.startsWith('include/')).map(seed => seed.path)))
            await copyFile(resolve(root, path), join(outside, path.slice('include/'.length)));
        await symlink(outside, join(directory, 'include'), 'junction');
        await assert.rejects(buildRepositoryCorpus(directory), /source symlink escapes repository root/);
    } finally { await removeFixture(directory); await removeFixture(outside); }
});
