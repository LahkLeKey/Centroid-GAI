import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { copyFile, mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const admitted = [
    'data/gameplay/barks-v1/contract.tsv',
    'data/gameplay/barks-v1/catalog.tsv',
    'data/gameplay/barks-v1/scenarios.tsv',
    'models/gameplay/barks-v1/current.tsv',
    ...['checkpoint.txt', 'model.cgnn', 'report.tsv', 'release.tsv'].map(name =>
        'models/gameplay/barks-v1/releases/future/' + name),
];
const raw = [
    'data/gameplay/raw.json',
    'data/gameplay/raw.tsv',
    'data/gameplay/generated/examples.json',
    'data/gameplay/barks-v1/raw.json',
    'data/gameplay/barks-v1/scenarios.json',
    'data/gameplay/barks-v1/scenarios.tsv.tmp',
    'data/gameplay/barks-v1/nested/contract.tsv',
    'data/gameplay/future/contract.tsv',
    'models/gameplay/current.tsv',
    'models/gameplay/barks-v1/current.json',
    'models/gameplay/barks-v1/.workflow.lock',
    'models/gameplay/barks-v1/.current-future.tmp',
    'models/gameplay/barks-v1/model.cgnn',
    'models/gameplay/barks-v1/runs/checkpoint.txt',
    'models/gameplay/barks-v1/releases/future/raw.json',
    'models/gameplay/barks-v1/releases/.pending-future/model.cgnn',
    'models/gameplay/barks-v1/releases/.pending-future/checkpoint.txt',
    'models/gameplay/barks-v1/releases/.pending-future/model.cgnn',
    'models/gameplay/barks-v1/releases/.pending-future/checkpoint.txt',
    'models/gameplay/barks-v1/releases/future/train.txt',
    'models/gameplay/barks-v1/releases/future/other.cgnn',
    'models/gameplay/barks-v1/releases/future/nested/checkpoint.txt',
    'models/gameplay/barks-v1/releases/future/nested/model.cgnn',
    'models/gameplay/barks-v1/releases/future/nested/report.tsv',
    'models/gameplay/barks-v1/releases/future/nested/release.tsv',
    'models/gameplay/future/releases/release/model.cgnn',
];

function git(root, args, input) {
    const result = spawnSync('git', ['-c', 'safe.directory=' + root.replaceAll('\\', '/'), ...args],
        { cwd: root, input, encoding: 'utf8', windowsHide: true });
    assert.ifError(result.error);
    assert.ok(result.status === 0 || result.status === 1, result.stderr);
    return result;
}

function ignored(root, paths) {
    if (!paths.length) return new Set();
    const result = git(root, ['check-ignore', '--no-index', '--stdin', '-z'], paths.join('\0') + '\0');
    return new Set(result.stdout.split('\0').filter(Boolean));
}

function trackedRaw(root) {
    const result = git(root, ['ls-files', '-z', '--', 'data/gameplay', 'models/gameplay']);
    assert.equal(result.status, 0, result.stderr);
    return ignored(root, result.stdout.split('\0').filter(Boolean));
}

async function fixture(t) {
    const root = await mkdtemp(join(tmpdir(), 'centroid-bark-git-'));
    t.after(() => rm(root, { recursive: true, force: true }));
    assert.equal(git(root, ['init', '--quiet']).status, 0);
    await copyFile(join(repository, '.gitignore'), join(root, '.gitignore'));
    await copyFile(join(repository, '.gitattributes'), join(root, '.gitattributes'));
    return root;
}

test('gameplay raw payloads and unapproved artifacts are ignored by default', async t => {
    const root = await fixture(t);
    assert.deepEqual(ignored(root, raw), new Set(raw));
});

test('only authored gameplay fixtures and named compact releases are admitted', async t => {
    const root = await fixture(t);
    assert.deepEqual(ignored(root, admitted), new Set());
});

test('force-added raw gameplay files are still detectable through the artifact boundary', async t => {
    const root = await fixture(t);
    const rawPath = 'data/gameplay/barks-v1/raw.json';
    await mkdir(join(root, dirname(rawPath)), { recursive: true });
    await writeFile(join(root, rawPath), '{"raw":"local fixture"}\n');
    assert.equal(git(root, ['add', '--force', '--', rawPath]).status, 0);
    assert.deepEqual(trackedRaw(root), new Set([rawPath]));
    const contract = admitted[0];
    await writeFile(join(root, contract), 'key\tvalue\n');
    assert.equal(git(root, ['add', '--', contract]).status, 0);
    assert.deepEqual(trackedRaw(root), new Set([rawPath]));
});

test('repository-tracked gameplay files obey the raw artifact boundary', () => {
    assert.deepEqual(trackedRaw(repository), new Set(),
        'force-added gameplay payloads violate the authored-fixture/artifact boundary');
});

test('gameplay compiled bytes remain binary and hash-bound text uses LF', async t => {
    const root = await fixture(t);
    const result = git(root, ['check-attr', '-z', 'text', 'eol', 'filter', '--', ...admitted]);
    assert.equal(result.status, 0, result.stderr);
    const fields = result.stdout.split('\0').filter(Boolean);
    const attributes = new Map();
    for (let index = 0; index < fields.length; index += 3)
        attributes.set(fields[index] + ':' + fields[index + 1], fields[index + 2]);
    for (const path of admitted) {
        if (path.endsWith('.cgnn')) {
            assert.equal(attributes.get(path + ':text'), 'unset');
            assert.equal(attributes.get(path + ':filter'), 'unset');
        } else {
            assert.equal(attributes.get(path + ':text'), 'set');
            assert.equal(attributes.get(path + ':eol'), 'lf');
        }
    }
});

test('gameplay hardening preserves source files and existing authored fixtures', async t => {
    const root = await fixture(t);
    assert.deepEqual(ignored(root, [
        'tools/bark/git-policy.test.mjs', 'tools/bark/workflow.mjs',
        'include/centroid_gai_bark.h', 'data/chat/factual-dialogues-v2.json',
        'data/research/autotrain.json', 'models/scholarly/current.tsv',
    ]), new Set());
    assert.deepEqual(ignored(root, ['scratch.cgnn', 'build/bark/raw.json']),
        new Set(['scratch.cgnn', 'build/bark/raw.json']));
});
