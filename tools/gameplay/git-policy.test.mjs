import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { copyFile, mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, isAbsolute, join, relative, resolve, sep } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const admitted = [
    'data/chat/factual-dialogues-v2.json',
    'data/chat/learning-sanity-v1.json',
    'data/chat/train-request-v1.json',
    'data/research/autotrain.json',
    ...['train.txt', 'heldout.txt', 'checkpoint.txt', 'metrics.tsv', 'recipe.cmake', 'toolchain.txt'].map(name =>
        'models/neural/order-v1/' + name),
    'models/scholarly/current.tsv',
    'models/scholarly/toolchain.txt',
    ...['checkpoint.txt', 'model.cgnn', 'release.tsv', 'citations.tsv', 'metrics.tsv'].map(name =>
        'models/scholarly/releases/future/' + name),
    ...['contract.tsv', 'tasks.tsv', 'modules.tsv', 'bark-catalog.tsv', 'bark-scenarios.tsv', 'intent-catalog.tsv', 'intent-scenarios.tsv'].map(name => 'data/gameplay/composed-v1/' + name),
    'models/gameplay/composed-v1/current.tsv',
    ...['checkpoint.txt', 'model.cggp', 'report.tsv', 'release.tsv'].map(name => 'models/gameplay/composed-v1/releases/future/' + name),
    'data/gameplay/barks-v1/contract.tsv',
    'data/gameplay/barks-v1/catalog.tsv',
    'data/gameplay/barks-v1/scenarios.tsv',
    'models/gameplay/barks-v1/current.tsv',
    ...['checkpoint.txt', 'model.cgnn', 'report.tsv', 'release.tsv'].map(name =>
        'models/gameplay/barks-v1/releases/future/' + name),
];
const raw = [
    'data/raw.json', 'data/future/contract.tsv', 'data/future-config.json',
    'data/chat/raw.json', 'data/chat/future-fixture.json', 'data/chat/train-request-v2.json',
    'data/chat/factual-dialogues-v2.json.tmp', 'data/chat/raw/paper.xml',
    ...['dataset.json', 'manifest.json', 'sources.json', 'split-assignments.json', 'train-request.json'].map(name =>
        'data/chat/releases/repository-v1/' + name),
    'data/chat/reviews/repository-v1-findings.json',
    'data/research/paper.json', 'data/research/paper.xml', 'data/research/raw/paper.txt',
    'models/raw.json', 'models/future/model.cgnn', 'models/future/checkpoint.txt',
    'models/neural/order-v1/raw.json', 'models/neural/order-v1/manifest.json',
    'models/neural/order-v1/train.json', 'models/neural/order-v1/future.tsv',
    'models/neural/order-v1/nested/checkpoint.txt', 'models/neural/future/checkpoint.txt',
    'models/scholarly/current.json', 'models/scholarly/releases/future/dataset.json',
    'models/scholarly/releases/future/train.txt', 'models/scholarly/releases/future/nested/model.cgnn',
    'models/scholarly/releases/.pending-future/model.cgnn',
    'data/gameplay/composed-v1/raw.json', 'data/gameplay/composed-v1/descriptor.tsv',
    'models/gameplay/composed-v1/.workflow.lock', 'models/gameplay/composed-v1/current.json',
    'models/gameplay/composed-v1/releases/future/raw.json', 'models/gameplay/composed-v1/releases/future/train.txt',
    'models/gameplay/composed-v1/releases/.pending-future/model.cggp',
    'models/gameplay/composed-v1/releases/future/nested/model.cggp',
    'models/gameplay/composed-v1/releases/future/model.cgnn', 'scratch.cggp',
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
    const result = git(root, ['ls-files', '-z', '--', 'data', 'models']);
    assert.equal(result.status, 0, result.stderr);
    return ignored(root, result.stdout.split('\0').filter(Boolean));
}

async function fixture(t) {
    const temporary = resolve(tmpdir());
    const prefix = join(temporary, 'centroid-gameplay-git-');
    const root = resolve(await mkdtemp(prefix));
    const child = relative(temporary, root);
    assert.ok(root.startsWith(prefix) && child && child !== '..' && !child.startsWith('..' + sep) && !isAbsolute(child));
    t.after(() => rm(root, { recursive: true, force: true }));
    assert.equal(git(root, ['init', '--quiet']).status, 0);
    await copyFile(join(repository, '.gitignore'), join(root, '.gitignore'));
    await copyFile(join(repository, '.gitattributes'), join(root, '.gitattributes'));
    return root;
}

test('raw data and unapproved model artifacts are ignored by default', async t => {
    const root = await fixture(t);
    assert.deepEqual(ignored(root, raw), new Set(raw));
});

test('only named authored data fixtures and compact model proofs are admitted', async t => {
    const root = await fixture(t);
    assert.deepEqual(ignored(root, admitted), new Set());
});

test('force-added raw data and model files are detectable through the artifact boundary', async t => {
    const root = await fixture(t);
    const rawPaths = ['data/gameplay/barks-v1/raw.json',
        'data/chat/releases/repository-v1/dataset.json', 'models/neural/order-v1/raw.json'];
    for (const rawPath of rawPaths) {
        await mkdir(join(root, dirname(rawPath)), { recursive: true });
        await writeFile(join(root, rawPath), '{"raw":"local fixture"}\n');
        assert.equal(git(root, ['add', '--force', '--', rawPath]).status, 0);
    }
    assert.deepEqual(trackedRaw(root), new Set(rawPaths));
    const contract = 'data/gameplay/composed-v1/contract.tsv';
    await mkdir(join(root, dirname(contract)), { recursive: true });
    await writeFile(join(root, contract), 'key\tvalue\n');
    assert.equal(git(root, ['add', '--', contract]).status, 0);
    assert.deepEqual(trackedRaw(root), new Set(rawPaths));
});

test('repository-tracked data and models obey the raw artifact boundary', () => {
    assert.deepEqual(trackedRaw(repository), new Set(),
        'force-added payloads violate the authored-fixture/artifact boundary');
});

test('gameplay portable compiled weights and hash-bound text use LF', async t => {
    const root = await fixture(t);
    const gameplayArtifacts = admitted.filter(path => path.startsWith('data/gameplay/') || path.startsWith('models/gameplay/'));
    const result = git(root, ['check-attr', '-z', 'text', 'eol', 'filter', '--', ...gameplayArtifacts]);
    assert.equal(result.status, 0, result.stderr);
    const fields = result.stdout.split('\0').filter(Boolean);
    const attributes = new Map();
    for (let index = 0; index < fields.length; index += 3)
        attributes.set(fields[index] + ':' + fields[index + 1], fields[index + 2]);
    for (const path of gameplayArtifacts) {
        if (path.endsWith('.cgnn')) {
            assert.equal(attributes.get(path + ':text'), 'unset');
            assert.equal(attributes.get(path + ':filter'), 'unset');
        } else {
            assert.equal(attributes.get(path + ':text'), 'set');
            assert.equal(attributes.get(path + ':eol'), 'lf');
        }
    }
});

test('data hardening preserves source and package configuration JSON', async t => {
    const root = await fixture(t);
    assert.deepEqual(ignored(root, [
        'tools/bark/git-policy.test.mjs', 'tools/bark/workflow.mjs',
        'include/centroid_gai_bark.h', 'data/chat/factual-dialogues-v2.json',
        'data/research/autotrain.json', 'models/scholarly/current.tsv',
        'persistence/api/package.json', 'persistence/api/tsconfig.json',
        'persistence/db/migrations/app/20260925T0831_initial_artifacts/migration.json',
        'examples/knowledge/codebase.json', 'tools/knowledge/source-config.json',
        'persistence/api/.env.example', 'persistence/db/.env.example', 'compose.env.example',
    ]), new Set());
    const generated = ['scratch.cgnn', 'build/bark/raw.json', 'build-lint/CMakeCache.txt',
        'build-docs/docs/html/index.html', 'cmake-build-debug/CMakeCache.txt',
        '.env.local', 'persistence/api/.env.production', 'compose-ci.log'];
    assert.deepEqual(ignored(root, generated), new Set(generated));
});
