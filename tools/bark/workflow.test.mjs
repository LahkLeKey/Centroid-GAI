/** Immutable release lifecycle and failure-preserves-head checks without numerical mocks in production. */
import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { hostname, tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { mkdir, mkdtemp, readFile, readdir, rm, copyFile, writeFile } from 'node:fs/promises';
import { barkSourceIdentity, parseBarkReport, runBarkWorkflow } from './workflow.mjs';

const sourceRoot = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const hash = value => createHash('sha256').update(value).digest('hex');
const nativeHash = '1'.repeat(64);

async function fixture(t) {
    const root = await mkdtemp(join(tmpdir(), 'cgai-bark-workflow-'));
    t.after(async () => {
        assert.ok(root.startsWith(join(tmpdir(), 'cgai-bark-workflow-')));
        await rm(root, { recursive: true, force: true });
    });
    for (const directory of ['data/gameplay/barks-v1', 'include', 'src/bark', 'src/neural',
        'src/core', 'src/internal', 'tools/bark']) await mkdir(join(root, directory), { recursive: true });
    for (const filename of ['contract.tsv', 'catalog.tsv', 'scenarios.tsv'])
        await copyFile(join(sourceRoot, 'data/gameplay/barks-v1', filename), join(root, 'data/gameplay/barks-v1', filename));
    const sources = ['include/centroid_gai.h', 'include/centroid_gai_neural.h', 'include/centroid_gai_bark.h',
        'src/bark/bark_model.c', 'src/neural/neural_training.c', 'src/core/error.c', 'src/internal/neural_internal.h',
        'tools/bark/bark_fixture.c', 'tools/bark/bark_fixture.h', 'tools/bark/bark_training.c', 'tools/bark/workflow.mjs'];
    for (const filename of sources) await writeFile(join(root, filename), '/* immutable source fixture */\n');
    const identity = await barkSourceIdentity(root);
    const native = new FakeNative(identity.policy);
    return { root, state: join(root, 'models/gameplay/barks-v1'), work: join(root, 'build/bark'),
        native, identity, hardware: 'test CPU; test C11 build' };
}

function checkpoint(epochs, policy) {
    return 'CGAI-CHECKPOINT 1\n' +
        ['embedding_dimensions', 'hidden_dimensions', 'centroid_count', 'context_window', 'seed']
            .map(name => name + ' ' + policy[name] + '\n').join('') +
        'routing_temperature 0x1.0000000000000p+0\n' +
        'training_step ' + epochs * policy.training_cases + '\ntraining_epochs ' + epochs + '\nrecipe fixture\n';
}
function epochs(text) { return Number(/^training_epochs ([0-9]+)$/m.exec(text)[1]); }
function reportFields(policy, beforeEpochs = 0, afterEpochs = 200, rejected = false) {
    const fields = {
        version: 1, contract_version: 1, scope: 'bounded-synthetic-bark-v1', hardware: 'test CPU; test C11 build',
        compiler: 'test C11 compiler', promotion_passed: rejected ? 0 : 1,
        model_limit_bytes: policy.maximum_model_bytes, session_limit_bytes: policy.maximum_session_bytes,
        p95_limit_us: policy.maximum_p95_us, p99_limit_us: policy.maximum_p99_us,
        minimum_accuracy: policy.minimum_development_accuracy,
        minimum_development_improvement: policy.minimum_development_improvement,
        test_regression_tolerance: policy.test_regression_tolerance,
    };
    for (const [phase, passes] of [['before', beforeEpochs], ['after', afterEpochs]]) {
        for (const split of ['training', 'development', 'test']) {
            const prefix = phase + '_' + split + '_';
            fields[prefix + 'cases'] = policy[split + '_cases'];
            fields[prefix + 'correct'] = passes ? policy[split + '_cases'] : 0;
            fields[prefix + 'abstained'] = split === 'training' ? 8 : 2;
            fields[prefix + 'cross_entropy'] = 100 / (passes + 1);
        }
    }
    if (rejected) fields.after_development_correct = policy.development_cases - 1;
    return { ...fields, mask_violations: 0, repeat_violations: 0, model_bytes: 2048, session_bytes: 1024,
        parameter_bytes: 512, parameter_count: 64, vocabulary_size: 25, optimizer_bytes: 0,
        sample_count: policy.benchmark_samples, session_pool: policy.benchmark_session_pool,
        workers: policy.benchmark_workers, p50_us: 2, p95_us: 3, p99_us: 4, maximum_us: 5 };
}
function reportText(fields) { return Object.entries(fields).map(([key, value]) => key + '\t' + value + '\n').join(''); }

class FakeNative {
    constructor(policy) { this.policy = policy; this.calls = []; this.reject = false; this.fail = null; this.hash = nativeHash; }
    async fingerprint() { return this.hash; }
    async run(args) {
        this.calls.push(args);
        const [command, input, output, amount] = args;
        if (this.fail === command) throw new Error('injected native ' + command + ' failure');
        if (command === 'init') await writeFile(input, checkpoint(0, this.policy));
        else if (command === 'step') await writeFile(output, checkpoint(epochs(await readFile(input, 'utf8')) + Number(amount), this.policy));
        else if (command === 'export') await writeFile(output, 'weights ' + epochs(await readFile(input, 'utf8')) + '\n');
        else if (command === 'replay') {
            const passes = epochs(await readFile(input, 'utf8'));
            await writeFile(output, checkpoint(passes, this.policy) + (this.badReplay ? 'corrupted\n' : ''));
        } else if (command === 'report') {
            const after = Number((await readFile(input, 'utf8')).split(' ')[1]);
            const before = Number((await readFile(output, 'utf8')).split(' ')[1]);
            const fields = reportFields(this.policy, before, after, this.reject);
            fields.hardware = args[4];
            await writeFile(args[3], reportText(fields));
        } else if (command !== 'score') throw new Error('unknown fake native command');
        if (this.after) await this.after(command, args);
        return '';
    }
}
async function pointer(options) { return readFile(join(options.state, 'current.tsv')); }
async function bootstrap(t) {
    const options = await fixture(t);
    const accepted = await runBarkWorkflow(options);
    return { options, accepted, bytes: await pointer(options) };
}

test('bootstrap publishes only compact artifacts after exact fresh replay and native scoring', async t => {
    const options = await fixture(t);
    const accepted = await runBarkWorkflow(options);
    assert.equal(accepted.status, 'promoted');
    assert.equal(accepted.head.epochs, String(options.identity.policy.initial_epochs));
    assert.equal(accepted.head.steps, String(options.identity.policy.initial_epochs * 48));
    assert.equal(accepted.head.parent_release_id, '-');
    assert.equal(accepted.head.native_sha256, nativeHash);
    assert.deepEqual(options.native.calls.map(call => call[0]), ['init', 'export', 'step', 'export', 'report', 'replay', 'score']);
    const directory = join(options.state, 'releases', accepted.head.release_id);
    assert.deepEqual((await readdir(directory)).sort(), ['checkpoint.txt', 'model.cgnn', 'release.tsv', 'report.tsv']);
    assert.ok(!(await pointer(options)).includes(Buffer.from('\r')));
    assert.equal(accepted.head.release_sha256, hash(await readFile(join(directory, 'release.tsv'))));
});

test('offline verify performs no native operations and checks the accepted report', async t => {
    const { options, accepted } = await bootstrap(t);
    const verified = await runBarkWorkflow({ ...options, command: 'verify', native: undefined, tool: undefined });
    assert.equal(verified.status, 'verified');
    assert.equal(verified.head.release_id, accepted.head.release_id);
});

test('explicit native verify scores accepted weights without requiring its original executable fingerprint', async t => {
    const { options, accepted, bytes } = await bootstrap(t);
    const calls = [];
    const native = {
        fingerprint() { throw new Error('native verify must not require the training executable fingerprint'); },
        async run(args) { calls.push(args); return ''; },
    };
    const result = await runBarkWorkflow({ ...options, command: 'verify', native });
    assert.equal(result.status, 'verified');
    assert.deepEqual(calls, [['score', join(options.state, 'releases', accepted.head.release_id, 'model.cgnn')]]);
    assert.deepEqual(await pointer(options), bytes);
});

test('explicit native verify score failure leaves the accepted pointer untouched', async t => {
    const { options, bytes } = await bootstrap(t);
    options.native.fail = 'score';
    await assert.rejects(runBarkWorkflow({ ...options, command: 'verify' }), /injected native score failure/);
    assert.deepEqual(await pointer(options), bytes);
});

test('continuation defaults to twenty additional passes and binds its immutable parent', async t => {
    const { options, accepted } = await bootstrap(t);
    const continued = await runBarkWorkflow(options);
    assert.equal(continued.status, 'promoted');
    assert.equal(continued.head.epochs, String(Number(accepted.head.epochs) + options.identity.policy.additional_epochs));
    assert.equal(continued.head.parent_release_id, accepted.head.release_id);
    assert.notEqual(continued.head.release_id, accepted.head.release_id);
    const verified = await runBarkWorkflow({ ...options, command: 'verify' });
    assert.equal(verified.head.release_id, continued.head.release_id);
});

test('explicit additional epochs are replayed from the original initialization', async t => {
    const { options, accepted } = await bootstrap(t);
    const continued = await runBarkWorkflow({ ...options, epochs: 3 });
    assert.equal(continued.head.epochs, String(Number(accepted.head.epochs) + 3));
    const bytes = await pointer(options);
    const replay = await runBarkWorkflow({ ...options, command: 'replay' });
    assert.equal(replay.status, 'replayed');
    assert.deepEqual(await pointer(options), bytes);
});

test('failed promotion retains the incumbent and rejected candidate only in local work', async t => {
    const { options, bytes } = await bootstrap(t);
    options.native.reject = true;
    const rejected = await runBarkWorkflow(options);
    assert.equal(rejected.status, 'rejected');
    assert.deepEqual(await pointer(options), bytes);
    assert.equal((await readdir(join(options.state, 'releases'))).length, 1);
    assert.ok((await readdir(rejected.stage)).includes('report.tsv'));
});

for (const command of ['step', 'export', 'report', 'replay', 'score']) {
    test('native ' + command + ' failure preserves the accepted head', async t => {
        const { options, bytes } = await bootstrap(t);
        options.native.fail = command;
        await assert.rejects(runBarkWorkflow(options), /injected native/);
        assert.deepEqual(await pointer(options), bytes);
        options.native.fail = null;
        assert.equal((await runBarkWorkflow({ ...options, command: 'verify' })).status, 'verified');
    });
}

test('nonidentical fresh replay cannot publish a candidate', async t => {
    const { options, bytes } = await bootstrap(t);
    options.native.badReplay = true;
    await assert.rejects(runBarkWorkflow(options), /exact checkpoint replay failed/);
    assert.deepEqual(await pointer(options), bytes);
});

for (const filename of ['model.cgnn', 'report.tsv', 'checkpoint.txt']) {
    test('stage ' + filename + ' tamper after validation cannot change the accepted head', async t => {
        const { options, bytes } = await bootstrap(t);
        options.native.after = async (command, args) => {
            if (command === 'score') {
                const path = join(dirname(args[1]), filename);
                await writeFile(path, Buffer.concat([await readFile(path), Buffer.from('changed after validation\n')]));
            }
        };
        await assert.rejects(runBarkWorkflow(options), /candidate artifacts changed|copied bark artifact hash mismatch/);
        assert.deepEqual(await pointer(options), bytes);
    });
}

for (const filename of ['checkpoint.txt', 'model.cgnn', 'report.tsv', 'release.tsv']) {
    test('offline verification rejects modified ' + filename, async t => {
        const { options, accepted } = await bootstrap(t);
        const path = join(options.state, 'releases', accepted.head.release_id, filename);
        await writeFile(path, Buffer.concat([await readFile(path), Buffer.from('tampered\n')]));
        await assert.rejects(runBarkWorkflow({ ...options, command: 'verify' }), /hash mismatch/);
    });
}

for (const filename of ['data/gameplay/barks-v1/contract.tsv', 'data/gameplay/barks-v1/catalog.tsv',
    'data/gameplay/barks-v1/scenarios.tsv', 'tools/bark/bark_fixture.c', 'src/bark/bark_model.c',
    'src/neural/neural_training.c', 'tools/bark/bark_training.c', 'tools/bark/workflow.mjs']) {
    test('changed source ' + filename + ' blocks verify and continuation before native execution', async t => {
        const { options, bytes } = await bootstrap(t);
        const path = join(options.root, filename);
        const suffix = filename.endsWith('.tsv') ? 'changed_source_marker\tchanged\n' : '/* changed */\n';
        await writeFile(path, Buffer.concat([await readFile(path), Buffer.from(suffix)]));
        const count = options.native.calls.length;
        await assert.rejects(runBarkWorkflow({ ...options, command: 'verify' }), /source identity changed/);
        await assert.rejects(runBarkWorkflow(options), /source identity changed/);
        assert.equal(options.native.calls.length, count);
        assert.deepEqual(await pointer(options), bytes);
    });
}

test('rebuilt executable can continue only after byte-exact accepted checkpoint replay', async t => {
    const { options, accepted } = await bootstrap(t);
    options.native.hash = '2'.repeat(64);
    const count = options.native.calls.length;
    const continued = await runBarkWorkflow(options);
    assert.equal(continued.status, 'promoted');
    assert.equal(continued.head.native_sha256, options.native.hash);
    assert.equal(continued.head.parent_release_id, accepted.head.release_id);
    assert.deepEqual(options.native.calls.slice(count).map(call => call[0]), ['replay', 'step', 'export', 'report', 'replay', 'score']);
});

test('rebuilt executable replay can validate the accepted recipe without changing its provenance', async t => {
    const { options, accepted, bytes } = await bootstrap(t);
    options.native.hash = '2'.repeat(64);
    const replay = await runBarkWorkflow({ ...options, command: 'replay' });
    assert.equal(replay.status, 'replayed');
    assert.equal(replay.head.native_sha256, accepted.head.native_sha256);
    assert.deepEqual(await pointer(options), bytes);
});

test('incompatible rebuilt executable is rejected before any training updates', async t => {
    const { options, bytes } = await bootstrap(t);
    options.native.hash = '2'.repeat(64);
    options.native.badReplay = true;
    const count = options.native.calls.length;
    await assert.rejects(runBarkWorkflow(options), /cannot exactly replay the accepted checkpoint/);
    assert.deepEqual(options.native.calls.slice(count).map(call => call[0]), ['replay']);
    await assert.rejects(runBarkWorkflow({ ...options, command: 'replay' }), /exact checkpoint replay failed/);
    assert.deepEqual(await pointer(options), bytes);
});

test('executable changing during compatibility proof cannot reach training updates', async t => {
    const { options, bytes } = await bootstrap(t);
    options.native.hash = '2'.repeat(64);
    options.native.after = async command => {
        if (command === 'replay') options.native.hash = '3'.repeat(64);
    };
    const count = options.native.calls.length;
    await assert.rejects(runBarkWorkflow(options), /source or native executable changed/);
    assert.deepEqual(options.native.calls.slice(count).map(call => call[0]), ['replay']);
    assert.deepEqual(await pointer(options), bytes);
});

test('source mutation during native work fails the final publication check', async t => {
    const { options, bytes } = await bootstrap(t);
    options.native.after = async command => {
        if (command === 'score') await writeFile(join(options.root, 'src/neural/neural_training.c'), '/* changed during training */\n');
    };
    await assert.rejects(runBarkWorkflow(options), /changed during training/);
    assert.deepEqual(await pointer(options), bytes);
});

test('immutable release collision preserves the previous pointer', async t => {
    const { options, accepted, bytes } = await bootstrap(t);
    const passes = Number(accepted.head.epochs) + options.identity.policy.additional_epochs;
    const id = hash(hash(checkpoint(passes, options.identity.policy)) + '\n' + accepted.head.source_identity_sha256 + '\n');
    await mkdir(join(options.state, 'releases', id));
    await assert.rejects(runBarkWorkflow(options), /immutable bark release already exists/);
    assert.deepEqual(await pointer(options), bytes);
});

test('live local PID lock rejects another training run without changing its owner', async t => {
    const options = await fixture(t);
    await mkdir(options.state, { recursive: true });
    const lock = 'version\t1\npid\t' + process.pid + '\nhost\t' + hostname() + '\ntoken\tlive-test\n';
    await writeFile(join(options.state, '.workflow.lock'), lock);
    await assert.rejects(runBarkWorkflow(options), /already running/);
    assert.equal(await readFile(join(options.state, '.workflow.lock'), 'utf8'), lock);
    assert.equal(options.native.calls.length, 0);
});

test('foreign-host lock is never recovered automatically', async t => {
    const options = await fixture(t);
    await mkdir(options.state, { recursive: true });
    const lock = 'version\t1\npid\t2147483647\nhost\tforeign-host-' + hostname() + '\ntoken\tforeign-test\n';
    await writeFile(join(options.state, '.workflow.lock'), lock);
    await assert.rejects(runBarkWorkflow(options), /another host/);
    assert.equal(await readFile(join(options.state, '.workflow.lock'), 'utf8'), lock);
});

test('dead same-host PID lock requires explicit removal and preserves the accepted head', async t => {
    const { options, bytes } = await bootstrap(t);
    const text = 'version\t1\npid\t2147483647\nhost\t' + hostname() + '\ntoken\tdead-test\n';
    await writeFile(join(options.state, '.workflow.lock'), text);
    await assert.rejects(runBarkWorkflow(options), /stale bark workflow lock; remove .* after confirming/);
    assert.equal(await readFile(join(options.state, '.workflow.lock'), 'utf8'), text);
    assert.deepEqual(await pointer(options), bytes);
});

test('missing release and invalid epoch controls fail before numerical training', async t => {
    const options = await fixture(t);
    await assert.rejects(runBarkWorkflow({ ...options, command: 'verify' }), /no accepted bark release/);
    for (const value of [0, -1, 1.5, 10001, NaN])
        await assert.rejects(runBarkWorkflow({ ...options, epochs: value }), /epochs must/);
    assert.equal(options.native.calls.length, 0);
});

test('report parser rejects unknown, duplicate, nonfinite, negative and control-bearing fields', async t => {
    const options = await fixture(t);
    const base = reportText(reportFields(options.identity.policy));
    assert.equal(parseBarkReport(base, options.identity.policy).promotion_passed, 1);
    for (const malformed of [
        base + 'unknown\t1\n', base + 'version\t1\n', base.replace('p99_us\t4\n', 'p99_us\tNaN\n'),
        base.replace('p99_us\t4\n', 'p99_us\tInfinity\n'), base.replace('model_bytes\t2048\n', 'model_bytes\t-1\n'),
        base.replace('hardware\ttest CPU; test C11 build\n', 'hardware\tbad\u0000label\n'),
        base.replaceAll('\n', '\r\n'), base.slice(0, -1),
    ]) assert.throws(() => parseBarkReport(malformed, options.identity.policy));
});

test('report parser rejects inconsistent gate claims and policy changes', async t => {
    const options = await fixture(t);
    const base = reportText(reportFields(options.identity.policy));
    assert.throws(() => parseBarkReport(base.replace('p99_us\t4\n', 'p99_us\t1001\n')
        .replace('maximum_us\t5\n', 'maximum_us\t1002\n'), options.identity.policy), /decision is inconsistent/);
    assert.throws(() => parseBarkReport(base.replace('p99_limit_us\t1000\n', 'p99_limit_us\t2000\n'), options.identity.policy), /authored policy/);
    assert.throws(() => parseBarkReport(base.replace('optimizer_bytes\t0\n', 'optimizer_bytes\t512\n'), options.identity.policy), /decision is inconsistent/);
    assert.throws(() => parseBarkReport(base.replace('after_test_correct\t12\n', 'after_test_correct\t13\n'), options.identity.policy), /scenario metric/);
});

for (const [key, replacement, diagnostic] of [
    ['centroid_count', 8, /authored shape/], ['seed', 43, /authored shape/],
    ['routing_temperature', 2, /routing temperature/], ['gradient_clip', 6, /unsupported bark promotion policy/],
]) {
    test('compiled recipe cannot silently drift from contract ' + key, async t => {
        const options = await fixture(t);
        const path = join(options.root, 'data/gameplay/barks-v1/contract.tsv');
        const text = await readFile(path, 'utf8');
        await writeFile(path, text.replace(new RegExp('^' + key + '\\t[^\\n]+$', 'm'), key + '\t' + replacement));
        await assert.rejects(runBarkWorkflow(options), diagnostic);
        assert.ok(!options.native.calls.some(call => call[0] === 'step'));
    });
}

test('ambiguous pointer fields cannot be interpreted as an accepted release', async t => {
    const { options, bytes } = await bootstrap(t);
    const path = join(options.state, 'current.tsv');
    await writeFile(path, Buffer.concat([bytes, Buffer.from('protocol\t1\n')]));
    await assert.rejects(runBarkWorkflow({ ...options, command: 'verify' }), /duplicate or invalid/);
    await writeFile(path, Buffer.concat([bytes, Buffer.from('unknown\t1\n')]));
    await assert.rejects(runBarkWorkflow({ ...options, command: 'verify' }), /schema/);
});

test('implementation LF and CRLF bytes yield the same portable source identity', async t => {
    const options = await fixture(t);
    const paths = ['tools/bark/bark_fixture.c', 'tools/bark/bark_fixture.h', 'tools/bark/workflow.mjs',
        'src/bark/bark_model.c', 'src/neural/neural_training.c', 'include/centroid_gai_neural.h'];
    const before = await barkSourceIdentity(options.root);
    for (const path of paths) {
        const file = join(options.root, path);
        await writeFile(file, (await readFile(file, 'utf8')).replaceAll('\n', '\r\n'));
    }
    assert.deepEqual((await barkSourceIdentity(options.root)).fields, before.fields);
    const contract = join(options.root, 'data/gameplay/barks-v1/contract.tsv');
    await writeFile(contract, (await readFile(contract, 'utf8')).replaceAll('\n', '\r\n'));
    await assert.rejects(barkSourceIdentity(options.root), /invalid LF/);
});

test('compiled C11 tool trains, verifies and exactly replays a bounded bark release',
    { skip: !process.env.CGAI_BARK_TOOL }, async t => {
        const options = await fixture(t);
        const request = { root: sourceRoot, state: options.state, work: options.work,
            hardware: options.hardware, tool: resolve(process.env.CGAI_BARK_TOOL) };
        const trained = await runBarkWorkflow(request);
        assert.equal(trained.status, 'promoted');
        assert.equal(trained.report.after_development_correct, 12);
        assert.equal(trained.report.after_test_correct, 12);
        const bytes = await pointer(options);
        assert.equal((await runBarkWorkflow({ ...request, command: 'verify' })).status, 'verified');
        const continued = await runBarkWorkflow(request);
        assert.equal(continued.status, 'rejected');
        assert.ok(continued.report.after_development_cross_entropy < continued.report.before_development_cross_entropy);
        assert.ok(continued.report.after_test_cross_entropy > continued.report.before_test_cross_entropy);
        assert.deepEqual(await pointer(options), bytes);
        assert.equal((await runBarkWorkflow({ ...request, command: 'replay' })).status, 'replayed');
        assert.deepEqual(await pointer(options), bytes);
    });
