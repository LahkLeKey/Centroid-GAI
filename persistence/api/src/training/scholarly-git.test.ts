import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const repository = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));

/** Read ignore rules without consulting or changing the Git index. */
function ignored(paths: readonly string[]): Set<string> {
    const result = spawnSync('git', [
        '-c', `safe.directory=${repository.replaceAll('\\', '/')}`,
        'check-ignore', '--no-index', '--stdin', '-z',
    ], { cwd: repository, input: paths.join('\0') + '\0', encoding: 'utf8', windowsHide: true });
    assert.ifError(result.error);
    assert.ok(result.status === 0 || result.status === 1, `git check-ignore failed: ${result.stderr}`);
    return new Set(result.stdout.split('\0').filter(Boolean));
}

test('scholarly raw state and unapproved release files stay outside Git', () => {
    const paths = [
        'models/scholarly/current.json',
        'models/scholarly/catalog.json',
        'models/scholarly/benchmark.json',
        'models/scholarly/.loop.lock',
        'models/scholarly/current.tsv.tmp',
        'models/scholarly/model.cgnn',
        'models/scholarly/cache/paper.xml',
        'models/scholarly/runs/candidate/checkpoint.txt',
        'models/scholarly/releases/future/dataset.json',
        'models/scholarly/releases/future/benchmark.json',
        'models/scholarly/releases/future/quality.json',
        'models/scholarly/releases/future/manifest.json',
        'models/scholarly/releases/future/admission.json',
        'models/scholarly/releases/future/decision.json',
        'models/scholarly/releases/future/train.txt',
        'models/scholarly/releases/future/development.txt',
        'models/scholarly/releases/future/test.txt',
        'models/scholarly/releases/future/scratch.txt',
        'models/scholarly/releases/future/other.cgnn',
        'models/scholarly/releases/future/raw/paper.json',
        'models/scholarly/releases/future/raw/checkpoint.txt',
        'models/scholarly/releases/future/raw/citations.tsv',
        'models/scholarly/releases/future/raw/model.cgnn',
        'models/scholarly/releases/.pending-future/model.cgnn',
        'models/scholarly/releases/.pending-future/checkpoint.txt',
        'data/research/paper.json',
        'data/research/paper.xml',
        'data/research/raw/paper.json',
        'data/research/future-config.json',
        'data/chat/releases/repository-v1/dataset.json',
        'data/chat/releases/repository-v1/manifest.json',
        'data/chat/releases/repository-v1/sources.json',
        'data/chat/releases/repository-v1/split-assignments.json',
        'data/chat/releases/repository-v1/train-request.json',
        'data/chat/reviews/repository-v1-findings.json',
    ];
    assert.deepEqual(ignored(paths), new Set(paths));
});

test('only approved scholarly artifacts and the training policy are admitted to Git', () => {
    const paths = [
        'models/scholarly/current.tsv',
        'models/scholarly/toolchain.txt',
        'models/scholarly/releases/future/checkpoint.txt',
        'models/scholarly/releases/future/model.cgnn',
        'models/scholarly/releases/future/release.tsv',
        'models/scholarly/releases/future/citations.tsv',
        'models/scholarly/releases/future/metrics.tsv',
        'data/research/autotrain.json',
    ];
    assert.deepEqual(ignored(paths), new Set());
});

test('Git tracked research paths cannot bypass the raw-data boundary with force-add', () => {
    const result = spawnSync('git', ['-c', `safe.directory=${repository.replaceAll('\\', '/')}`,
        'ls-files', '-z', '--', 'models/scholarly', 'data/research'],
    { cwd: repository, encoding: 'utf8', windowsHide: true });
    assert.ifError(result.error);
    assert.equal(result.status, 0, result.stderr);
    const paths = result.stdout.split('\0').filter(Boolean);
    if (paths.length) assert.deepEqual(ignored(paths), new Set(), 'tracked research payloads violate the Git artifact boundary');
});

test('authored fixtures, source configuration and the existing binary ignore rule are preserved', () => {
    const authored = [
        'data/chat/factual-dialogues-v2.json',
        'data/chat/learning-sanity-v1.json',
        'data/chat/train-request-v1.json',
        'models/neural/order-v1/train.txt',
        'models/neural/order-v1/heldout.txt',
        'models/neural/order-v1/checkpoint.txt',
        'persistence/api/package.json',
        'persistence/api/tsconfig.json',
        'persistence/db/migrations/app/20260925T0831_initial_artifacts/migration.json',
    ];
    assert.deepEqual(ignored(authored), new Set());
    assert.deepEqual(ignored(['scratch.cgai', 'scratch.cgnn', 'build/candidate.json']),
        new Set(['scratch.cgai', 'scratch.cgnn', 'build/candidate.json']));
});

test('compiled scholarly models preserve bytes and compact text artifacts use LF', () => {
    const binary = 'models/scholarly/releases/future/model.cgnn';
    const texts = [
        'models/scholarly/current.tsv',
        'models/scholarly/toolchain.txt',
        'models/scholarly/releases/future/checkpoint.txt',
        'models/scholarly/releases/future/release.tsv',
        'models/scholarly/releases/future/citations.tsv',
        'models/scholarly/releases/future/metrics.tsv',
        'data/research/autotrain.json',
    ];
    const result = spawnSync('git', [
        '-c', `safe.directory=${repository.replaceAll('\\', '/')}`,
        'check-attr', '-z', 'text', 'eol', 'filter', '--', binary, ...texts,
    ], { cwd: repository, encoding: 'utf8', windowsHide: true });
    assert.ifError(result.error);
    assert.equal(result.status, 0, result.stderr);
    const fields = result.stdout.split('\0').filter(Boolean);
    const attributes = new Map<string, string>();
    for (let index = 0; index < fields.length; index += 3)
        attributes.set(`${fields[index]}:${fields[index + 1]}`, fields[index + 2]!);
    assert.equal(attributes.get(`${binary}:text`), 'unset');
    assert.equal(attributes.get(`${binary}:filter`), 'unset');
    for (const path of texts) {
        assert.equal(attributes.get(`${path}:text`), 'set');
        assert.equal(attributes.get(`${path}:eol`), 'lf');
    }
});
