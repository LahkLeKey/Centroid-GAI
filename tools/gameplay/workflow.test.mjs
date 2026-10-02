/** Immutable release lifecycle and failure-preserves-head checks without numerical mocks in production. */
import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { hostname, tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { mkdir, mkdtemp, readFile, readdir, rm, copyFile, writeFile } from 'node:fs/promises';
import { GAMEPLAY_SOURCE_FILES, gameplaySourceIdentity, parseGameplayReport, parseTaskRegistry, runGameplayWorkflow, verifyLegacyBarkRelease } from './workflow.mjs';

const sourceRoot = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const hash = value => createHash('sha256').update(value).digest('hex');
const nativeHash = '1'.repeat(64);
const execute = promisify(execFile);

async function fixture(t) {
    const root = await mkdtemp(join(tmpdir(), 'cgai-gameplay-workflow-'));
    t.after(async () => {
        assert.ok(root.startsWith(join(tmpdir(), 'cgai-gameplay-workflow-')));
        await rm(root, { recursive: true, force: true });
    });
    const data = 'data/gameplay/composed-v1';
    await mkdir(join(root, data), { recursive: true });
    for (const filename of ['contract.tsv', 'tasks.tsv', 'modules.tsv', 'bark-catalog.tsv', 'bark-scenarios.tsv', 'intent-catalog.tsv', 'intent-scenarios.tsv'])
        await copyFile(join(sourceRoot, data, filename), join(root, data, filename));
    for (const filename of [...GAMEPLAY_SOURCE_FILES.runtime, ...GAMEPLAY_SOURCE_FILES.generator, ...GAMEPLAY_SOURCE_FILES.trainer]) {
        await mkdir(dirname(join(root, filename)), { recursive: true });
        await writeFile(join(root, filename), '/* immutable source fixture */\n');
    }
    const identity = await gameplaySourceIdentity(root);
    const native = new FakeNative(identity.policy);
    return { root, state: join(root, 'models/gameplay/composed-v1'), work: join(root, 'build/gameplay'),
        native, identity, hardware: 'test CPU; test C11 build' };
}

function checkpointReal(value) {
    const exponent = Math.floor(Math.log2(value));
    return '0x' + (value / 2 ** exponent).toString(16) + 'p' + (exponent >= 0 ? '+' : '') + exponent;
}
function checkpoint(epochs, policy) {
    return 'CGAI-GAMEPLAY-CHECKPOINT 1\n' +
        ['embedding_dimensions', 'hidden_dimensions', 'module_count', 'centroids_per_module', 'feature_count', 'seed']
            .map(name => name + ' ' + policy[name] + '\n').join('') +
        'task_count ' + policy.tasks.length + '\nrouting_temperature ' + checkpointReal(policy.routing_temperature) + '\n' +
        policy.cardinalities.map(value => 'cardinality ' + value + '\n').join('') +
        policy.tasks.map(task => 'output_count ' + task.output_count + '\ntask_modules ' + task.module_mask + '\ntask_features ' + task.task_features + '\n').join('') +
        'parameter_count 64\noptimizer_present ' + (epochs ? 1 : 0) + '\n' +
        'training_step ' + epochs * policy.training_records_per_epoch + '\ntraining_epochs ' + epochs + '\nrecipe fixture\n';
}
function epochs(text) { return Number(/^training_epochs ([0-9]+)$/m.exec(text)[1]); }
function reportFields(policy, beforeEpochs = 0, afterEpochs = policy.initial_epochs, rejected = false) {
    const fields = {
        version: 1, contract_version: 1, scope: 'bounded-synthetic-gameplay-v1', hardware: 'test CPU; test C11 build',
        compiler: 'test C11 compiler', promotion_passed: rejected ? 0 : 1, task_count: policy.tasks.length, module_count: policy.modules.length,
        model_limit_bytes: policy.maximum_model_bytes, session_limit_bytes: policy.maximum_session_bytes,
        p95_limit_us: policy.maximum_p95_us, p99_limit_us: policy.maximum_p99_us,
        paired_p95_limit_us: policy.maximum_paired_p95_us, paired_p99_limit_us: policy.maximum_paired_p99_us,
        minimum_module_share: policy.minimum_module_share, minimum_ablation_change: policy.minimum_ablation_change,
        minimum_development_improvement: policy.minimum_development_improvement, test_regression_tolerance: policy.test_regression_tolerance,
    };
    for (const task of policy.tasks) fields['minimum_' + task.name + '_accuracy'] = Math.max(task.minimum_development_accuracy, task.minimum_test_accuracy);
    for (const [phase, passes] of [['before', beforeEpochs], ['after', afterEpochs]]) {
        for (const task of policy.tasks) {
            const prefix = phase + '_' + task.name + '_';
            const cases = task.training_cases + task.development_cases + task.test_cases;
            for (const split of ['training', 'development', 'test']) {
                fields[prefix + split + '_cases'] = task[split + '_cases'];
                fields[prefix + split + '_correct'] = passes ? task[split + '_cases'] : 0;
                fields[prefix + split + '_abstained'] = 0;
                fields[prefix + split + '_cross_entropy'] = 100 / (passes + 1);
            }
            fields[prefix + 'host_checks'] = 2 ** task.output_count * task.output_count * 2 ** policy.modules.length + cases;
            fields[prefix + 'mask_violations'] = fields[prefix + 'repeat_violations'] = 0;
            fields[prefix + 'composition_cases'] = cases;
            for (const module of policy.modules) {
                const group = prefix + 'module' + module.module_id + '_';
                fields[group + 'weight'] = fields[group + 'contribution'] = 1 / policy.modules.length;
                fields[group + 'ablation_cross_entropy'] = 100 / (passes + 1) + 0.1;
                fields[group + 'ablation_change'] = 0.1;
            }
        }
        fields[phase + '_module_violations'] = 0;
        fields[phase + '_bark_invariance_cases'] = policy.bark_invariance_cases;
        fields[phase + '_bark_invariance_correct'] = passes ? policy.bark_invariance_cases : 0;
        fields[phase + '_simulator_cases'] = policy.simulator_cases;
        for (const outcome of ['legal', 'survived', 'objective_successes', 'stable'])
            fields[phase + '_simulator_' + outcome] = passes ? policy.simulator_cases : 0;
    }
    if (rejected) fields.after_bark_development_correct = policy.tasks[0].development_cases - 1;
    Object.assign(fields, { model_bytes: 2048, session_bytes: 1024, parameter_bytes: 512, parameter_count: 64, optimizer_bytes: 0,
        encoder_multiply_adds: 9216, outer_coordinates: 64, inner_coordinates: 1024, maximum_head_logits: 288,
        maximum_head_multiply_adds: policy.module_count * policy.hidden_dimensions * Math.max(...policy.tasks.map(task => task.output_count)) });
    for (const workload of [...policy.tasks.map(task => task.name), 'paired']) {
        fields[workload + '_sample_count'] = policy.benchmark_samples;
        fields[workload + '_session_pool'] = policy.benchmark_session_pool;
        fields[workload + '_workers'] = policy.benchmark_workers;
        fields[workload + '_forward_passes_per_sample'] = workload === 'paired' ? 2 : 1;
        fields[workload + '_p50_us'] = 2; fields[workload + '_p95_us'] = 3;
        fields[workload + '_p99_us'] = 4; fields[workload + '_maximum_us'] = 5;
    }
    return fields;
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
    const accepted = await runGameplayWorkflow(options);
    return { options, accepted, bytes: await pointer(options) };
}

test('bootstrap publishes only compact artifacts after exact fresh replay and native scoring', async t => {
    const options = await fixture(t);
    const accepted = await runGameplayWorkflow(options);
    assert.equal(accepted.status, 'promoted');
    assert.equal(accepted.head.epochs, String(options.identity.policy.initial_epochs));
    assert.equal(accepted.head.steps, String(options.identity.policy.initial_epochs * 384));
    assert.equal(accepted.head.parent_release_id, '-');
    assert.equal(accepted.head.native_sha256, nativeHash);
    assert.deepEqual(options.native.calls.map(call => call[0]), ['init', 'export', 'step', 'export', 'report', 'replay', 'score']);
    const directory = join(options.state, 'releases', accepted.head.release_id);
    assert.deepEqual((await readdir(directory)).sort(), ['checkpoint.txt', 'model.cggp', 'release.tsv', 'report.tsv']);
    assert.ok(!(await pointer(options)).includes(Buffer.from('\r')));
    assert.equal(accepted.head.release_sha256, hash(await readFile(join(directory, 'release.tsv'))));
});

test('offline verify performs no native operations and checks the accepted report', async t => {
    const { options, accepted } = await bootstrap(t);
    const verified = await runGameplayWorkflow({ ...options, command: 'verify', native: undefined, tool: undefined });
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
    const result = await runGameplayWorkflow({ ...options, command: 'verify', native });
    assert.equal(result.status, 'verified');
    assert.deepEqual(calls, [['score', join(options.state, 'releases', accepted.head.release_id, 'model.cggp')]]);
    assert.deepEqual(await pointer(options), bytes);
});

test('explicit native verify score failure leaves the accepted pointer untouched', async t => {
    const { options, bytes } = await bootstrap(t);
    options.native.fail = 'score';
    await assert.rejects(runGameplayWorkflow({ ...options, command: 'verify' }), /injected native score failure/);
    assert.deepEqual(await pointer(options), bytes);
});

test('continuation defaults to twenty additional passes and binds its immutable parent', async t => {
    const { options, accepted } = await bootstrap(t);
    const continued = await runGameplayWorkflow(options);
    assert.equal(continued.status, 'promoted');
    assert.equal(continued.head.epochs, String(Number(accepted.head.epochs) + options.identity.policy.additional_epochs));
    assert.equal(continued.head.parent_release_id, accepted.head.release_id);
    assert.notEqual(continued.head.release_id, accepted.head.release_id);
    const verified = await runGameplayWorkflow({ ...options, command: 'verify' });
    assert.equal(verified.head.release_id, continued.head.release_id);
});

test('explicit additional epochs are replayed from the original initialization', async t => {
    const { options, accepted } = await bootstrap(t);
    const continued = await runGameplayWorkflow({ ...options, epochs: 3 });
    assert.equal(continued.head.epochs, String(Number(accepted.head.epochs) + 3));
    const bytes = await pointer(options);
    const replay = await runGameplayWorkflow({ ...options, command: 'replay' });
    assert.equal(replay.status, 'replayed');
    assert.deepEqual(await pointer(options), bytes);
});

test('failed promotion retains the incumbent and rejected candidate only in local work', async t => {
    const { options, bytes } = await bootstrap(t);
    options.native.reject = true;
    const rejected = await runGameplayWorkflow(options);
    assert.equal(rejected.status, 'rejected');
    assert.deepEqual(await pointer(options), bytes);
    assert.equal((await readdir(join(options.state, 'releases'))).length, 1);
    assert.ok((await readdir(rejected.stage)).includes('report.tsv'));
});

for (const command of ['step', 'export', 'report', 'replay', 'score']) {
    test('native ' + command + ' failure preserves the accepted head', async t => {
        const { options, bytes } = await bootstrap(t);
        options.native.fail = command;
        await assert.rejects(runGameplayWorkflow(options), /injected native/);
        assert.deepEqual(await pointer(options), bytes);
        options.native.fail = null;
        assert.equal((await runGameplayWorkflow({ ...options, command: 'verify' })).status, 'verified');
    });
}

test('nonidentical fresh replay cannot publish a candidate', async t => {
    const { options, bytes } = await bootstrap(t);
    options.native.badReplay = true;
    await assert.rejects(runGameplayWorkflow(options), /exact checkpoint replay failed/);
    assert.deepEqual(await pointer(options), bytes);
});

for (const filename of ['model.cggp', 'report.tsv', 'checkpoint.txt']) {
    test('stage ' + filename + ' tamper after validation cannot change the accepted head', async t => {
        const { options, bytes } = await bootstrap(t);
        options.native.after = async (command, args) => {
            if (command === 'score') {
                const path = join(dirname(args[1]), filename);
                await writeFile(path, Buffer.concat([await readFile(path), Buffer.from('changed after validation\n')]));
            }
        };
        await assert.rejects(runGameplayWorkflow(options), /candidate artifacts changed|copied gameplay artifact hash mismatch/);
        assert.deepEqual(await pointer(options), bytes);
    });
}

for (const filename of ['checkpoint.txt', 'model.cggp', 'report.tsv', 'release.tsv']) {
    test('offline verification rejects modified ' + filename, async t => {
        const { options, accepted } = await bootstrap(t);
        const path = join(options.state, 'releases', accepted.head.release_id, filename);
        await writeFile(path, Buffer.concat([await readFile(path), Buffer.from('tampered\n')]));
        await assert.rejects(runGameplayWorkflow({ ...options, command: 'verify' }), /hash mismatch/);
    });
}

for (const filename of ['data/gameplay/composed-v1/contract.tsv', 'data/gameplay/composed-v1/bark-catalog.tsv',
    'data/gameplay/composed-v1/intent-scenarios.tsv', 'tools/gameplay/gameplay_tasks.c', 'src/gameplay/gameplay_model.c',
    'tools/gameplay/gameplay_training.c', 'tools/gameplay/workflow.mjs']) {
    test('changed source ' + filename + ' blocks verify and continuation before native execution', async t => {
        const { options, bytes } = await bootstrap(t);
        const path = join(options.root, filename);
        const suffix = filename.endsWith('.tsv') ? 'changed_source_marker\tchanged\n' : '/* changed */\n';
        await writeFile(path, Buffer.concat([await readFile(path), Buffer.from(suffix)]));
        const count = options.native.calls.length;
        await assert.rejects(runGameplayWorkflow({ ...options, command: 'verify' }), /source identity changed/);
        await assert.rejects(runGameplayWorkflow(options), /source identity changed/);
        assert.equal(options.native.calls.length, count);
        assert.deepEqual(await pointer(options), bytes);
    });
}

test('rebuilt executable can continue only after byte-exact accepted checkpoint replay', async t => {
    const { options, accepted } = await bootstrap(t);
    options.native.hash = '2'.repeat(64);
    const count = options.native.calls.length;
    const continued = await runGameplayWorkflow(options);
    assert.equal(continued.status, 'promoted');
    assert.equal(continued.head.native_sha256, options.native.hash);
    assert.equal(continued.head.parent_release_id, accepted.head.release_id);
    assert.deepEqual(options.native.calls.slice(count).map(call => call[0]), ['replay', 'step', 'export', 'report', 'replay', 'score']);
});

test('rebuilt executable replay can validate the accepted recipe without changing its provenance', async t => {
    const { options, accepted, bytes } = await bootstrap(t);
    options.native.hash = '2'.repeat(64);
    const replay = await runGameplayWorkflow({ ...options, command: 'replay' });
    assert.equal(replay.status, 'replayed');
    assert.equal(replay.head.native_sha256, accepted.head.native_sha256);
    assert.deepEqual(await pointer(options), bytes);
});

test('incompatible rebuilt executable is rejected before any training updates', async t => {
    const { options, bytes } = await bootstrap(t);
    options.native.hash = '2'.repeat(64);
    options.native.badReplay = true;
    const count = options.native.calls.length;
    await assert.rejects(runGameplayWorkflow(options), /cannot exactly replay the accepted checkpoint/);
    assert.deepEqual(options.native.calls.slice(count).map(call => call[0]), ['replay']);
    await assert.rejects(runGameplayWorkflow({ ...options, command: 'replay' }), /exact checkpoint replay failed/);
    assert.deepEqual(await pointer(options), bytes);
});

test('executable changing during compatibility proof cannot reach training updates', async t => {
    const { options, bytes } = await bootstrap(t);
    options.native.hash = '2'.repeat(64);
    options.native.after = async command => {
        if (command === 'replay') options.native.hash = '3'.repeat(64);
    };
    const count = options.native.calls.length;
    await assert.rejects(runGameplayWorkflow(options), /source or native executable changed/);
    assert.deepEqual(options.native.calls.slice(count).map(call => call[0]), ['replay']);
    assert.deepEqual(await pointer(options), bytes);
});

test('source mutation during native work fails the final publication check', async t => {
    const { options, bytes } = await bootstrap(t);
    options.native.after = async command => {
        if (command === 'score') await writeFile(join(options.root, 'src/gameplay/gameplay_training.c'), '/* changed during training */\n');
    };
    await assert.rejects(runGameplayWorkflow(options), /changed during training/);
    assert.deepEqual(await pointer(options), bytes);
});

test('an external pointer edit during training is preserved instead of overwritten', async t => {
    const { options, bytes } = await bootstrap(t);
    const external = Buffer.concat([bytes, Buffer.from('external\tpointer-change\n')]);
    options.native.after = async command => {
        if (command === 'score') await writeFile(join(options.state, 'current.tsv'), external);
    };
    await assert.rejects(runGameplayWorkflow(options), /pointer changed during native work/);
    assert.deepEqual(await pointer(options), external);
    assert.equal((await readdir(join(options.state, 'releases'))).length, 1);
});

test('a newly created external pointer blocks bootstrap publication and stays intact', async t => {
    const options = await fixture(t);
    const external = Buffer.from('external\tnew-pointer\n');
    options.native.after = async command => {
        if (command === 'score') await writeFile(join(options.state, 'current.tsv'), external);
    };
    await assert.rejects(runGameplayWorkflow(options), /pointer changed during native work/);
    assert.deepEqual(await pointer(options), external);
    assert.ok(!(await readdir(options.state)).includes('releases'));
});

test('immutable release collision preserves the previous pointer', async t => {
    const { options, accepted, bytes } = await bootstrap(t);
    const passes = Number(accepted.head.epochs) + options.identity.policy.additional_epochs;
    const id = hash(hash(checkpoint(passes, options.identity.policy)) + '\n' + accepted.head.source_identity_sha256 + '\n');
    await mkdir(join(options.state, 'releases', id));
    await assert.rejects(runGameplayWorkflow(options), /immutable gameplay release already exists/);
    assert.deepEqual(await pointer(options), bytes);
});

test('live local PID lock rejects another training run without changing its owner', async t => {
    const options = await fixture(t);
    await mkdir(options.state, { recursive: true });
    const lock = 'version\t1\npid\t' + process.pid + '\nhost\t' + hostname() + '\ntoken\tlive-test\n';
    await writeFile(join(options.state, '.workflow.lock'), lock);
    await assert.rejects(runGameplayWorkflow(options), /already running/);
    assert.equal(await readFile(join(options.state, '.workflow.lock'), 'utf8'), lock);
    assert.equal(options.native.calls.length, 0);
});

test('foreign-host lock is never recovered automatically', async t => {
    const options = await fixture(t);
    await mkdir(options.state, { recursive: true });
    const lock = 'version\t1\npid\t2147483647\nhost\tforeign-host-' + hostname() + '\ntoken\tforeign-test\n';
    await writeFile(join(options.state, '.workflow.lock'), lock);
    await assert.rejects(runGameplayWorkflow(options), /another host/);
    assert.equal(await readFile(join(options.state, '.workflow.lock'), 'utf8'), lock);
});

test('dead same-host PID lock requires explicit removal and preserves the accepted head', async t => {
    const { options, bytes } = await bootstrap(t);
    const text = 'version\t1\npid\t2147483647\nhost\t' + hostname() + '\ntoken\tdead-test\n';
    await writeFile(join(options.state, '.workflow.lock'), text);
    await assert.rejects(runGameplayWorkflow(options), /stale gameplay workflow lock; remove .* after confirming/);
    assert.equal(await readFile(join(options.state, '.workflow.lock'), 'utf8'), text);
    assert.deepEqual(await pointer(options), bytes);
});

test('missing release and invalid epoch controls fail before numerical training', async t => {
    const options = await fixture(t);
    await assert.rejects(runGameplayWorkflow({ ...options, command: 'verify' }), /no accepted gameplay release/);
    for (const value of [0, -1, 1.5, 10001, NaN])
        await assert.rejects(runGameplayWorkflow({ ...options, epochs: value }), /epochs must/);
    assert.equal(options.native.calls.length, 0);
});

test('report parser rejects unknown, duplicate, nonfinite, negative and control-bearing fields', async t => {
    const options = await fixture(t);
    const base = reportText(reportFields(options.identity.policy));
    assert.equal(parseGameplayReport(base, options.identity.policy).promotion_passed, 1);
    for (const malformed of [
        base + 'unknown\t1\n', base + 'version\t1\n', base.replace('bark_p99_us\t4\n', 'bark_p99_us\tNaN\n'),
        base.replace('bark_p99_us\t4\n', 'bark_p99_us\tInfinity\n'), base.replace('model_bytes\t2048\n', 'model_bytes\t-1\n'),
        base.replace('hardware\ttest CPU; test C11 build\n', 'hardware\tbad\u0000label\n'),
        base.replaceAll('\n', '\r\n'), base.slice(0, -1),
    ]) assert.throws(() => parseGameplayReport(malformed, options.identity.policy));
});

test('invalid authored AdamW decay fails before native initialization or training', async t => {
    const options = await fixture(t);
    const contract = join(options.root, 'data/gameplay/composed-v1/contract.tsv');
    const original = await readFile(contract, 'utf8');
    for (const value of ['-0.1', '0', '0.1', '1.1', 'NaN', 'Infinity']) {
        await writeFile(contract, original.replace(/^weight_decay\t[^\n]+$/m, 'weight_decay\t' + value));
        await assert.rejects(runGameplayWorkflow(options), /weight_decay|unsupported gameplay promotion policy/);
    }
    assert.equal(options.native.calls.length, 0);
});

test('report parser rejects inconsistent gate claims and policy changes', async t => {
    const options = await fixture(t);
    const base = reportText(reportFields(options.identity.policy));
    assert.throws(() => parseGameplayReport(base.replace('bark_p99_us\t4\n', 'bark_p99_us\t1001\n')
        .replace('bark_maximum_us\t5\n', 'bark_maximum_us\t1002\n'), options.identity.policy), /decision is inconsistent/);
    assert.throws(() => parseGameplayReport(base.replace('p99_limit_us\t1000\n', 'p99_limit_us\t2000\n'), options.identity.policy), /authored policy/);
    assert.throws(() => parseGameplayReport(base.replace('optimizer_bytes\t0\n', 'optimizer_bytes\t512\n'), options.identity.policy), /decision is inconsistent/);
    assert.throws(() => parseGameplayReport(base.replace('after_bark_test_correct\t12\n', 'after_bark_test_correct\t13\n'), options.identity.policy), /scenario metric/);
});

test('ambiguous pointer fields cannot be interpreted as an accepted release', async t => {
    const { options, bytes } = await bootstrap(t);
    const path = join(options.state, 'current.tsv');
    await writeFile(path, Buffer.concat([bytes, Buffer.from('protocol\t1\n')]));
    await assert.rejects(runGameplayWorkflow({ ...options, command: 'verify' }), /duplicate or invalid/);
    await writeFile(path, Buffer.concat([bytes, Buffer.from('unknown\t1\n')]));
    await assert.rejects(runGameplayWorkflow({ ...options, command: 'verify' }), /schema/);
});

test('implementation LF and CRLF bytes yield the same portable source identity', async t => {
    const options = await fixture(t);
    const paths = GAMEPLAY_SOURCE_FILES.runtime.concat(GAMEPLAY_SOURCE_FILES.trainer, GAMEPLAY_SOURCE_FILES.generator);
    const before = await gameplaySourceIdentity(options.root);
    for (const path of paths) {
        const file = join(options.root, path);
        await writeFile(file, (await readFile(file, 'utf8')).replaceAll('\n', '\r\n'));
    }
    assert.deepEqual((await gameplaySourceIdentity(options.root)).fields, before.fields);
    const contract = join(options.root, 'data/gameplay/composed-v1/contract.tsv');
    await writeFile(contract, (await readFile(contract, 'utf8')).replaceAll('\n', '\r\n'));
    await assert.rejects(gameplaySourceIdentity(options.root), /invalid LF/);
});

test('unrelated neural and legacy bark sources cannot invalidate the composed recipe', async t => {
    const options = await fixture(t);
    const before = await gameplaySourceIdentity(options.root);
    for (const filename of ['src/neural/neural_training.c', 'src/bark/bark_model.c', 'src/internal/neural_internal.h',
        'src/core/vocabulary.c', 'tools/gameplay/scaffold.mjs']) {
        await mkdir(dirname(join(options.root, filename)), { recursive: true });
        await writeFile(join(options.root, filename), '/* unrelated source addition */\n');
    }
    assert.deepEqual((await gameplaySourceIdentity(options.root)).fields, before.fields);
});

test('a complete third registry head uses the same release lifecycle and policy parser', async t => {
    const options = await fixture(t);
    const data = join(options.root, 'data/gameplay/composed-v1');
    const registry = join(data, 'tasks.tsv');
    await writeFile(registry, (await readFile(registry, 'utf8')) +
        '2\tencounter\t4\t4\t2\t2\t0.95\t0.95\t1\t1\tencounter-catalog.tsv\tencounter-scenarios.tsv\t511\n');
    for (const name of ['encounter-catalog.tsv', 'encounter-scenarios.tsv'])
        await writeFile(join(data, name), 'authored\tthird-task\n');
    const modules = join(data, 'modules.tsv');
    await writeFile(modules, (await readFile(modules, 'utf8')).replaceAll('\t3\n', '\t7\n'));
    const contract = join(data, 'contract.tsv');
    await writeFile(contract, (await readFile(contract, 'utf8')).replace('task_count\t2\n', 'task_count\t3\n')
        .replace('task_output_counts\t9,7\n', 'task_output_counts\t9,7,4\n')
        .replace('task_module_masks\t3,3\n', 'task_module_masks\t3,3,3\n')
        .replace('task_feature_masks\t15,499\n', 'task_feature_masks\t15,499,511\n'));
    const identity = await gameplaySourceIdentity(options.root);
    options.native = new FakeNative(identity.policy);
    const accepted = await runGameplayWorkflow(options);
    assert.equal(accepted.status, 'promoted');
    assert.equal(accepted.report.after_encounter_test_correct, 2);
    assert.equal((await runGameplayWorkflow({ ...options, command: 'verify' })).status, 'verified');
    assert.equal((await runGameplayWorkflow({ ...options, command: 'replay' })).status, 'replayed');
});

for (const mutation of [
    fields => { fields.after_bark_module0_weight = 0.01; fields.after_bark_module1_weight = 0.99; },
    fields => { fields.after_intent_module0_ablation_change = 0; },
    fields => { fields.after_simulator_survived -= 1; },
    fields => { fields.after_bark_invariance_correct -= 1; },
    fields => { fields.after_module_violations = 1; },
]) test('material composition and executable scenario failures independently block promotion', async t => {
    const options = await fixture(t);
    const fields = reportFields(options.identity.policy);
    mutation(fields);
    assert.throws(() => parseGameplayReport(reportText(fields), options.identity.policy), /decision is inconsistent/);
    fields.promotion_passed = 0;
    assert.equal(parseGameplayReport(reportText(fields), options.identity.policy).promotion_passed, 0);
});

async function legacyFixture(t) {
    const options = await fixture(t);
    const state = join(options.root, 'models/gameplay/barks-v1');
    const original = join(sourceRoot, 'models/gameplay/barks-v1');
    const head = await readFile(join(original, 'current.tsv'), 'utf8');
    const release = /^release_id\t([a-f0-9]{64})$/m.exec(head)[1];
    const directory = join(state, 'releases', release);
    await mkdir(directory, { recursive: true });
    await copyFile(join(original, 'current.tsv'), join(state, 'current.tsv'));
    for (const name of ['checkpoint.txt', 'model.cgnn', 'report.tsv', 'release.tsv'])
        await copyFile(join(original, 'releases', release, name), join(directory, name));
    return { root: options.root, state, directory, head };
}
test('historical bark verification validates original bytes independently of current source recipes', async t => {
    const options = await legacyFixture(t);
    const result = await runGameplayWorkflow({ ...options, command: 'verify-legacy' });
    assert.equal(result.status, 'historical-verified');
    assert.equal(result.compatibility, 'unproved');
    assert.equal(await readFile(join(options.state, 'current.tsv'), 'utf8'), options.head);
    await assert.rejects(runGameplayWorkflow({ ...options, command: 'verify-legacy', native: {} }), /does not prove executable replay compatibility/);
});
for (const artifact of ['checkpoint.txt', 'model.cgnn', 'report.tsv', 'release.tsv'])
    test('historical verification rejects tampered ' + artifact, async t => {
        const options = await legacyFixture(t);
        await writeFile(join(options.directory, artifact), Buffer.concat([await readFile(join(options.directory, artifact)), Buffer.from('tampered\n')]));
        await assert.rejects(verifyLegacyBarkRelease(options), /hash mismatch/);
    });

test('compiled C11 tool publishes and exactly replays the composed two-task bundle',
    { skip: !process.env.CGAI_GAMEPLAY_TOOL }, async t => {
        const options = await fixture(t);
        const request = { root: sourceRoot, state: options.state, work: options.work,
            hardware: options.hardware, tool: resolve(process.env.CGAI_GAMEPLAY_TOOL) };
        const trained = await runGameplayWorkflow(request);
        assert.equal(trained.status, 'promoted');
        assert.equal(trained.report.after_bark_development_correct, 12);
        assert.equal(trained.report.after_bark_test_correct, 12);
        assert.ok(trained.report.after_intent_development_correct >= 31);
        assert.ok(trained.report.after_intent_test_correct >= 31);
        assert.equal(trained.report.after_simulator_legal, 256);
        assert.equal(trained.report.after_bark_invariance_correct, 2304);
        const bytes = await pointer(options);
        assert.equal((await runGameplayWorkflow({ ...request, command: 'verify' })).status, 'verified');
        assert.equal((await runGameplayWorkflow({ ...request, command: 'replay' })).status, 'replayed');
        assert.deepEqual(await pointer(options), bytes);
        if (process.env.CGAI_COMPOSED_DEMO) {
            const demo = resolve(process.env.CGAI_COMPOSED_DEMO);
            const model = join(options.state, 'releases', trained.head.release_id, 'model.cggp');
            const run = args => execute(demo, args, { timeout: 30000, maxBuffer: 1024 * 1024, windowsHide: true });
            const [first, repeated] = await Promise.all([run([model, '42', '8']), run([model, '42', '8'])]);
            assert.equal(first.stdout, repeated.stdout);
            validateHostDemo(first.stdout, 8);
            validateHostDemo((await run([model, '987654', '64'])).stdout, 64);
            for (const steps of ['0', '65'])
                await assert.rejects(run([model, '42', steps]), error => error.code === 2 && /usage: cgai_composed_demo/.test(error.stderr));
            assert.deepEqual(await pointer(options), bytes);
        }
    });

function validateHostDemo(output, steps) {
    const permitted = { idle: ['abstain'], greet: ['abstain', 'greetwarm', 'greetplain', 'warnhostile'],
        threat: ['abstain', 'threatcalm', 'threaturgent'], victory: ['abstain', 'victory'],
        discovery: ['abstain', 'discover'], retreat: ['abstain', 'retreat'] };
    const blocks = output.trim().split(/(?=tick=)/);
    assert.equal(blocks.length, steps);
    let recent = 'abstain';
    for (const [tick, block] of blocks.entries()) {
        const scene = /^tick=([0-9]+) event=([a-z]+) danger=(low|high)\r?\n/.exec(block);
        const bark = /^  bark=([a-z]+) passes=([01]) modules=([0-9.]+)\/([0-9.]+)(?: line=[^\r\n]+)?\r?$/m.exec(block);
        const intent = /^  intent=([a-z]+) passes=([01]) modules=([0-9.]+)\/([0-9.]+)\r?$/m.exec(block);
        assert.ok(scene && bark && intent, 'complete typed host output');
        assert.equal(Number(scene[1]), tick);
        assert.ok(permitted[scene[2]].includes(bark[1]), 'event-admitted authored line');
        if (bark[1] !== 'abstain') { assert.notEqual(bark[1], recent); recent = bark[1]; }
        assert.ok(['abstain', 'wait', 'hold', 'approach', 'seekcover', 'engage', 'retreat'].includes(intent[1]));
        assert.match(block, /^  controller health=[01] distance=[01] survived=1 objective=1\r?$/m);
    }
}
