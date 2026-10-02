/** Immutable bark releases; all numerical training, scoring and timing run through C11. */
import { createHash, randomUUID } from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { cpus, hostname, platform, arch } from 'node:os';
import { dirname, join, resolve, relative } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { copyFile, lstat, mkdir, open, readFile, readdir, rename, unlink, writeFile } from 'node:fs/promises';

const execute = promisify(execFile);
const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const artifactNames = ['checkpoint.txt', 'model.cgnn', 'report.tsv'];
const identityNames = ['contract_sha256', 'catalog_sha256', 'scenarios_sha256', 'generator_sha256',
    'runtime_sha256', 'trainer_sha256', 'source_identity_sha256'];
const manifestNames = ['protocol', 'contract_version', 'release_id', 'parent_release_id',
    'checkpoint_sha256', 'model_sha256', 'report_sha256', ...identityNames, 'native_sha256',
    'learning_rate', 'epochs', 'steps'];
const headNames = [...manifestNames, 'release_sha256'];
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const hashPattern = /^[a-f0-9]{64}$/;
const numberPattern = /^-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?$/;

function fail(message) { throw new Error(message); }
function safeText(value, maximum = 512) {
    if (typeof value !== 'string' || !value || Buffer.byteLength(value) > maximum || /[\x00-\x1f\x7f]/.test(value))
        fail('invalid one-line bark metadata');
    return value;
}
function numeric(value, name, integer = false, minimum = 0, maximum = Number.MAX_SAFE_INTEGER) {
    if (typeof value !== 'string' || !numberPattern.test(value)) fail('invalid bark number: ' + name);
    const parsed = Number(value);
    if (!Number.isFinite(parsed) || parsed < minimum || parsed > maximum || (integer && !Number.isSafeInteger(parsed)))
        fail('out-of-range bark number: ' + name);
    return parsed;
}
function pairs(text, header = false, maximum = 65536) {
    if (Buffer.byteLength(text) > maximum || !text.endsWith('\n') || /[\x00-\x08\x0b-\x1f\x7f]/.test(text))
        fail('invalid LF bark TSV document');
    const rows = text.slice(0, -1).split('\n');
    if (header && rows.shift() !== 'key\tvalue') fail('invalid bark contract header');
    const result = Object.create(null);
    for (const row of rows) {
        const cells = row.split('\t');
        if (cells.length !== 2 || !/^[a-z][a-z0-9_]*$/.test(cells[0]) || Object.hasOwn(result, cells[0]))
            fail('duplicate or invalid bark TSV key');
        result[cells[0]] = safeText(cells[1], 4096);
    }
    return result;
}
function schema(fields, names) {
    if (Object.keys(fields).length !== names.length || names.some(name => !Object.hasOwn(fields, name)))
        fail('invalid bark TSV schema');
}
function serialize(fields, names) {
    schema(fields, names);
    return names.map(name => name + '\t' + safeText(String(fields[name]), 4096) + '\n').join('');
}
async function boundedFile(path, maximum = 65536) {
    const info = await lstat(path);
    if (!info.isFile() || info.size < 1 || info.size > maximum) fail('invalid bark file: ' + path);
    const bytes = await readFile(path);
    if (bytes.length !== info.size || bytes.length > maximum) fail('bark file changed while reading: ' + path);
    return bytes;
}
async function fileHash(path, maximum) { return digest(await boundedFile(path, maximum)); }
async function sourceFiles(root, directory) {
    const entries = await readdir(join(root, directory), { withFileTypes: true });
    const found = [];
    for (const entry of entries.sort((a, b) => a.name.localeCompare(b.name, 'en'))) {
        const child = directory + '/' + entry.name;
        if (entry.isDirectory()) found.push(...await sourceFiles(root, child));
        else if (entry.isFile() && /\.[ch]$/.test(entry.name)) found.push(child);
        else if (entry.isSymbolicLink()) fail('symlinks are not admitted in bark source identity: ' + child);
    }
    return found;
}
async function sourceGroup(root, paths) {
    const unique = [...new Set(paths)].sort();
    if (!unique.length) fail('empty bark source identity');
    const rows = [];
    for (const path of unique) {
        const bytes = await boundedFile(join(root, path), 4 * 1024 * 1024);
        const text = bytes.toString('utf8');
        if (!Buffer.from(text, 'utf8').equals(bytes)) fail('invalid UTF-8 bark implementation source: ' + path);
        rows.push(path + '\t' + digest(text.replaceAll('\r\n', '\n')) + '\n');
    }
    return digest(rows.join(''));
}

/** Read the authored policy and all relevant implementation identities without using a native tool. */
export async function barkSourceIdentity(root = repository) {
    root = resolve(root);
    const data = 'data/gameplay/barks-v1/';
    const policy = pairs((await boundedFile(join(root, data, 'contract.tsv'))).toString('utf8'), true);
    if (policy.contract_version !== '1' || policy.task_name !== 'barks-v1' ||
        policy.target_mode !== 'independent-next-token-v1' || policy.no_line_marker !== '-')
        fail('unsupported bark task contract');
    const fields = {
        contract_sha256: await fileHash(join(root, data, 'contract.tsv')),
        catalog_sha256: await fileHash(join(root, data, 'catalog.tsv')),
        scenarios_sha256: await fileHash(join(root, data, 'scenarios.tsv')),
        generator_sha256: await sourceGroup(root, ['tools/bark/bark_fixture.c', 'tools/bark/bark_fixture.h']),
        runtime_sha256: await sourceGroup(root, [
            ...await sourceFiles(root, 'src/bark'), ...await sourceFiles(root, 'src/neural'),
            ...await sourceFiles(root, 'src/core'), ...await sourceFiles(root, 'src/internal'),
            'include/centroid_gai.h', 'include/centroid_gai_neural.h', 'include/centroid_gai_bark.h']),
        trainer_sha256: await sourceGroup(root, [...await sourceFiles(root, 'tools/bark'), 'tools/bark/workflow.mjs']),
    };
    fields.source_identity_sha256 = digest(serialize(fields, identityNames.slice(0, -1)));
    return { fields, policy: parsedPolicy(policy) };
}

function parsedPolicy(fields) {
    const integers = ['initial_epochs', 'additional_epochs', 'training_cases', 'development_cases', 'test_cases',
        'embedding_dimensions', 'hidden_dimensions', 'centroid_count', 'context_window', 'seed',
        'maximum_model_bytes', 'maximum_session_bytes', 'benchmark_samples', 'benchmark_session_pool', 'benchmark_workers'];
    const floats = ['learning_rate', 'routing_temperature', 'gradient_clip', 'maximum_p95_us', 'maximum_p99_us', 'minimum_development_accuracy',
        'minimum_test_accuracy', 'minimum_development_improvement', 'test_regression_tolerance'];
    const result = {};
    for (const name of integers) result[name] = numeric(fields[name], name, true, 1);
    for (const name of floats) result[name] = numeric(fields[name], name);
    if (result.initial_epochs > 10000 || result.additional_epochs > 10000 || result.learning_rate <= 0 ||
        result.learning_rate > 1 || result.minimum_development_accuracy > 1 || result.minimum_test_accuracy > 1 ||
        result.minimum_development_accuracy !== result.minimum_test_accuracy || result.benchmark_workers !== 1 ||
        result.gradient_clip !== 5 || result.routing_temperature < 0.01 || result.routing_temperature > 100 ||
        result.embedding_dimensions > 64 || result.hidden_dimensions > 128 || result.centroid_count > 128 || result.context_window !== 4)
        fail('unsupported bark promotion policy');
    return result;
}

const metricNames = ['cases', 'correct', 'abstained', 'cross_entropy'];
const splitNames = ['training', 'development', 'test'];
const reportNames = ['version', 'contract_version', 'scope', 'hardware', 'compiler', 'promotion_passed',
    'model_limit_bytes', 'session_limit_bytes', 'p95_limit_us', 'p99_limit_us', 'minimum_accuracy',
    'minimum_development_improvement', 'test_regression_tolerance',
    ...['before', 'after'].flatMap(phase => splitNames.flatMap(split => metricNames.map(metric => phase + '_' + split + '_' + metric))),
    'mask_violations', 'repeat_violations', 'model_bytes', 'session_bytes', 'parameter_bytes', 'parameter_count',
    'vocabulary_size', 'optimizer_bytes', 'sample_count', 'session_pool', 'workers', 'p50_us', 'p95_us', 'p99_us', 'maximum_us'];

/** Strictly validate compact native measurements and independently recompute their promotion decision. */
export function parseBarkReport(text, policy) {
    const raw = pairs(text);
    schema(raw, reportNames);
    if (raw.version !== '1' || raw.contract_version !== '1' || raw.scope !== 'bounded-synthetic-bark-v1')
        fail('unsupported bark report');
    safeText(raw.hardware);
    safeText(raw.compiler);
    const report = { hardware: raw.hardware, compiler: raw.compiler };
    for (const name of reportNames.filter(name => !['version', 'contract_version', 'scope', 'hardware', 'compiler'].includes(name))) {
        const integral = /(?:cases|correct|abstained|bytes|count|size|violations)$/.test(name) ||
            ['promotion_passed', 'session_pool', 'workers'].includes(name);
        report[name] = numeric(raw[name], name, integral);
    }
    const limits = {
        model_limit_bytes: policy.maximum_model_bytes, session_limit_bytes: policy.maximum_session_bytes,
        p95_limit_us: policy.maximum_p95_us, p99_limit_us: policy.maximum_p99_us,
        minimum_accuracy: policy.minimum_development_accuracy,
        minimum_development_improvement: policy.minimum_development_improvement,
        test_regression_tolerance: policy.test_regression_tolerance,
        sample_count: policy.benchmark_samples, session_pool: policy.benchmark_session_pool, workers: policy.benchmark_workers,
    };
    for (const [name, value] of Object.entries(limits))
        if (report[name] !== value) fail('native bark report differs from authored policy: ' + name);
    for (const phase of ['before', 'after']) for (const split of splitNames) {
        const prefix = phase + '_' + split + '_';
        if (report[prefix + 'cases'] !== policy[split + '_cases'] ||
            report[prefix + 'correct'] > report[prefix + 'cases'] || report[prefix + 'abstained'] > report[prefix + 'cases'])
            fail('invalid bark scenario metric');
    }
    if (report.promotion_passed > 1 || report.parameter_count < 1 || report.vocabulary_size < 3 || report.model_bytes < 1 ||
        report.session_bytes < 1 || report.parameter_bytes !== report.parameter_count * 8 ||
        !(report.p50_us <= report.p95_us && report.p95_us <= report.p99_us && report.p99_us <= report.maximum_us))
        fail('invalid bark resource or percentile metric');
    const passed = splitNames.slice(1).every(split =>
        report['after_' + split + '_correct'] / report['after_' + split + '_cases'] >= policy['minimum_' + split + '_accuracy'] &&
        report['after_' + split + '_correct'] >= report['before_' + split + '_correct']) &&
        report.before_development_cross_entropy - report.after_development_cross_entropy > policy.minimum_development_improvement &&
        report.after_test_cross_entropy - report.before_test_cross_entropy <= policy.test_regression_tolerance &&
        report.model_bytes <= policy.maximum_model_bytes && report.session_bytes <= policy.maximum_session_bytes &&
        report.p95_us <= policy.maximum_p95_us && report.p99_us <= policy.maximum_p99_us &&
        report.mask_violations === 0 && report.repeat_violations === 0 && report.optimizer_bytes === 0;
    if (report.promotion_passed !== Number(passed)) fail('native bark report promotion decision is inconsistent');
    return report;
}

function parseManifest(text, names = manifestNames) {
    const fields = pairs(text, false, 32768);
    schema(fields, names);
    if (fields.protocol !== '1' || fields.contract_version !== '1') fail('unsupported bark release protocol');
    for (const name of names.filter(name => name.endsWith('_sha256') || name === 'release_id'))
        if (!hashPattern.test(fields[name])) fail('invalid bark release hash');
    if (fields.parent_release_id !== '-' && !hashPattern.test(fields.parent_release_id)) fail('invalid bark parent release');
    numeric(fields.learning_rate, 'learning_rate', false, Number.MIN_VALUE, 1);
    numeric(fields.epochs, 'epochs', true, 1, 10000);
    numeric(fields.steps, 'steps', true, 1);
    if (fields.release_id !== releaseId(fields.checkpoint_sha256, fields.source_identity_sha256)) fail('invalid bark content ID');
    return fields;
}
function releaseId(checkpoint, source) { return digest(checkpoint + '\n' + source + '\n'); }
function hexadecimalReal(text) {
    const parts = /^([+-]?)0x([0-9a-f]+)(?:\.([0-9a-f]+))?p([+-]?[0-9]+)$/.exec(text);
    if (!parts) fail('invalid checkpoint hexadecimal real');
    let mantissa = Number.parseInt(parts[2], 16);
    for (let index = 0; index < (parts[3]?.length ?? 0); ++index)
        mantissa += Number.parseInt(parts[3][index], 16) / 16 ** (index + 1);
    const value = (parts[1] === '-' ? -1 : 1) * mantissa * 2 ** Number(parts[4]);
    if (!Number.isFinite(value)) fail('nonfinite checkpoint hexadecimal real');
    return value;
}
function progress(bytes, policy) {
    const text = bytes.toString('utf8');
    if (!text.startsWith('CGAI-CHECKPOINT 1\n')) fail('invalid native bark checkpoint');
    const counter = name => {
        const matches = [...text.matchAll(new RegExp('^' + name + ' ([0-9]+)$', 'gm'))];
        if (matches.length !== 1) fail('invalid checkpoint progress');
        return numeric(matches[0][1], name, true);
    };
    for (const name of ['embedding_dimensions', 'hidden_dimensions', 'centroid_count', 'context_window', 'seed'])
        if (counter(name) !== policy[name]) fail('native bark checkpoint differs from authored shape: ' + name);
    const routing = [...text.matchAll(/^routing_temperature ([^\s]+)$/gm)];
    if (routing.length !== 1 || hexadecimalReal(routing[0][1]) !== policy.routing_temperature)
        fail('native bark checkpoint differs from authored routing temperature');
    return { epochs: counter('training_epochs'), steps: counter('training_step') };
}
function settings(options) {
    const root = resolve(options.root ?? repository);
    return { ...options, root, state: resolve(root, options.state ?? 'models/gameplay/barks-v1'),
        work: resolve(root, options.work ?? 'build/bark') };
}
async function readHead(state) {
    try { return parseManifest((await boundedFile(join(state, 'current.tsv'), 32768)).toString('utf8'), headNames); }
    catch (error) { if (error.code === 'ENOENT') return null; throw error; }
}

async function verifyAccepted(options, identity) {
    const head = await readHead(options.state);
    if (!head) fail('no accepted bark release');
    for (const name of identityNames)
        if (head[name] !== identity.fields[name]) fail('bark source identity changed: ' + name);
    if (Number(head.learning_rate) !== identity.policy.learning_rate) fail('bark learning rate changed');
    const directory = join(options.state, 'releases', head.release_id);
    const manifestBytes = await boundedFile(join(directory, 'release.tsv'), 32768);
    if (digest(manifestBytes) !== head.release_sha256) fail('bark release manifest hash mismatch');
    const manifest = parseManifest(manifestBytes.toString('utf8'));
    for (const name of manifestNames) if (manifest[name] !== head[name]) fail('bark head differs from release manifest');
    const limits = { 'checkpoint.txt': 256 * 1024 * 1024, 'model.cgnn': 64 * 1024 * 1024, 'report.tsv': 65536 };
    const artifacts = {};
    for (const filename of artifactNames) {
        artifacts[filename] = await boundedFile(join(directory, filename), limits[filename]);
        const field = filename === 'checkpoint.txt' ? 'checkpoint_sha256' : filename === 'model.cgnn' ? 'model_sha256' : 'report_sha256';
        if (digest(artifacts[filename]) !== head[field]) fail('bark artifact hash mismatch: ' + filename);
    }
    const report = parseBarkReport(artifacts['report.tsv'].toString('utf8'), identity.policy);
    if (report.promotion_passed !== 1) fail('accepted bark release lacks a passing report');
    const counters = progress(artifacts['checkpoint.txt'], identity.policy);
    if (counters.epochs !== Number(head.epochs) || counters.steps !== Number(head.steps) ||
        counters.steps !== counters.epochs * identity.policy.training_cases) fail('bark checkpoint progress differs from recipe');
    return { head, directory, report };
}

/** An injectable adapter executes fixed native argument arrays without a shell. */
export function createBarkNative(tool) {
    const executable = resolve(tool);
    return {
        async fingerprint() { return fileHash(executable, 64 * 1024 * 1024); },
        async run(args) {
            const result = await execute(executable, args.map(String), { timeout: 300000, maxBuffer: 4 * 1024 * 1024, windowsHide: true });
            return result.stdout;
        },
    };
}
async function nativeFor(options) {
    if (options.native) return options.native;
    if (options.tool) return createBarkNative(resolve(options.root, options.tool));
    const suffix = platform() === 'win32' ? '.exe' : '';
    for (const location of ['build/deterministic/Release/', 'build/deterministic/', 'build/release/Release/', 'build/release/', 'build/dev/']) {
        const candidate = join(options.root, location, 'cgai_bark_tool' + suffix);
        try { if ((await lstat(candidate)).isFile()) return createBarkNative(candidate); }
        catch (error) { if (error.code !== 'ENOENT') throw error; }
    }
    fail('compiled cgai_bark_tool missing; pass --tool or build the C11 target');
}

function lockDocument(token) {
    return serialize({ version: '1', pid: String(process.pid), host: hostname(), token }, ['version', 'pid', 'host', 'token']);
}
async function withLock(state, operation) {
    await mkdir(state, { recursive: true });
    const path = join(state, '.workflow.lock');
    const token = randomUUID();
    let handle;
    try { handle = await open(path, 'wx'); }
    catch (error) {
        if (error.code !== 'EEXIST') throw error;
        const bytes = await boundedFile(path, 4096);
        const lock = pairs(bytes.toString('utf8'), false, 4096);
        schema(lock, ['version', 'pid', 'host', 'token']);
        if (lock.version !== '1' || lock.host !== hostname()) fail('bark workflow is locked by another host');
        const pid = numeric(lock.pid, 'lock pid', true, 1, 2147483647);
        try { process.kill(pid, 0); fail('bark workflow is already running'); }
        catch (probe) { if (probe.code !== 'ESRCH') throw probe; }
        fail('stale bark workflow lock; remove ' + path + ' after confirming recorded same-host PID ' + pid + ' has exited');
    }
    await handle.writeFile(lockDocument(token));
    await handle.close();
    try { return await operation(); }
    finally {
        const lock = pairs((await boundedFile(path, 4096)).toString('utf8'), false, 4096);
        if (lock.token === token) await unlink(path);
    }
}

async function staging(options) {
    await mkdir(options.work, { recursive: true });
    const directory = join(options.work, 'run-' + randomUUID());
    await mkdir(directory);
    return directory;
}
async function unchanged(options, identity, native, fingerprint) {
    const current = await barkSourceIdentity(options.root);
    if (current.fields.source_identity_sha256 !== identity.fields.source_identity_sha256 || await native.fingerprint() !== fingerprint)
        fail('bark source or native executable changed during training');
}
async function publish(options, stage, manifest) {
    const releases = join(options.state, 'releases');
    await mkdir(releases, { recursive: true });
    const temporary = join(releases, '.pending-' + randomUUID());
    const destination = join(releases, manifest.release_id);
    try { await lstat(destination); fail('immutable bark release already exists'); }
    catch (error) { if (error.code !== 'ENOENT') throw error; }
    await mkdir(temporary);
    for (const filename of [...artifactNames, 'release.tsv']) await copyFile(join(stage, filename), join(temporary, filename));
    const copiedManifest = await boundedFile(join(temporary, 'release.tsv'), 32768);
    if (!copiedManifest.equals(Buffer.from(serialize(manifest, manifestNames)))) fail('copied bark release manifest changed');
    for (const [filename, field, maximum] of [
        ['checkpoint.txt', 'checkpoint_sha256', 256 * 1024 * 1024],
        ['model.cgnn', 'model_sha256', 64 * 1024 * 1024], ['report.tsv', 'report_sha256', 65536],
    ]) if (await fileHash(join(temporary, filename), maximum) !== manifest[field])
        fail('copied bark artifact hash mismatch: ' + filename);
    await rename(temporary, destination);
    const head = { ...manifest, release_sha256: digest(copiedManifest) };
    const pointer = join(options.state, '.current-' + randomUUID() + '.tmp');
    await writeFile(pointer, serialize(head, headNames), { flag: 'wx' });
    await rename(pointer, join(options.state, 'current.tsv'));
    return head;
}

async function train(options, identity) {
    const head = await readHead(options.state);
    const incumbent = head ? await verifyAccepted(options, identity) : null;
    const native = await nativeFor(options);
    const fingerprint = await native.fingerprint();
    if (!hashPattern.test(fingerprint)) fail('invalid native bark fingerprint');
    const epochs = options.epochs ?? (head ? identity.policy.additional_epochs : identity.policy.initial_epochs);
    if (!Number.isSafeInteger(epochs) || epochs < 1 || epochs > 10000 || epochs + Number(head?.epochs ?? 0) > 10000)
        fail('bark epochs must be 1..10000 and total replay epochs must not exceed 10000');
    const hardware = safeText(options.hardware ?? (cpus()[0]?.model ?? 'unknown CPU') + '; ' + platform() + ' ' + arch());
    const stage = await staging(options);
    const initial = join(stage, 'initial.txt');
    const baseline = join(stage, 'baseline.cgnn');
    const checkpoint = join(stage, 'checkpoint.txt');
    const model = join(stage, 'model.cgnn');
    const reportPath = join(stage, 'report.tsv');
    if (incumbent) {
        await copyFile(join(incumbent.directory, 'checkpoint.txt'), initial);
        await copyFile(join(incumbent.directory, 'model.cgnn'), baseline);
    } else {
        await native.run(['init', initial]);
        await native.run(['export', initial, baseline]);
    }
    progress(await boundedFile(initial, 256 * 1024 * 1024), identity.policy);
    const initialHash = await fileHash(initial, 256 * 1024 * 1024);
    const baselineHash = await fileHash(baseline, 64 * 1024 * 1024);
    if (head && (initialHash !== head.checkpoint_sha256 || baselineHash !== head.model_sha256))
        fail('copied bark incumbent artifacts changed');
    const rate = String(identity.policy.learning_rate);
    if (head && head.native_sha256 !== fingerprint) {
        const compatible = join(stage, 'compatible-replay.txt');
        await native.run(['replay', initial, compatible, rate]);
        if (await fileHash(compatible, 256 * 1024 * 1024) !== head.checkpoint_sha256)
            fail('current bark executable cannot exactly replay the accepted checkpoint');
        await unchanged(options, identity, native, fingerprint);
    }
    await native.run(['step', initial, checkpoint, String(epochs), rate]);
    const checkpointHash = await fileHash(checkpoint, 256 * 1024 * 1024);
    const counters = progress(await boundedFile(checkpoint, 256 * 1024 * 1024), identity.policy);
    if (counters.epochs !== Number(head?.epochs ?? 0) + epochs || counters.steps !== counters.epochs * identity.policy.training_cases)
        fail('native bark checkpoint counters differ from the independent training recipe');
    await native.run(['export', checkpoint, model]);
    const modelHash = await fileHash(model, 64 * 1024 * 1024);
    await native.run(['report', model, baseline, reportPath, hardware]);
    const reportBytes = await boundedFile(reportPath);
    const reportHash = digest(reportBytes);
    const report = parseBarkReport(reportBytes.toString('utf8'), identity.policy);
    if (await fileHash(baseline, 64 * 1024 * 1024) !== baselineHash ||
        await fileHash(initial, 256 * 1024 * 1024) !== initialHash) fail('staged bark incumbent artifacts changed');
    if (report.promotion_passed !== 1) return { status: 'rejected', head: head ?? undefined, report, stage };
    const replay = join(stage, 'replayed.txt');
    await native.run(['replay', checkpoint, replay, rate]);
    if (await fileHash(checkpoint, 256 * 1024 * 1024) !== checkpointHash ||
        await fileHash(replay, 256 * 1024 * 1024) !== checkpointHash) fail('bark exact checkpoint replay failed');
    await native.run(['score', model]);
    if (await fileHash(model, 64 * 1024 * 1024) !== modelHash || await fileHash(reportPath) !== reportHash)
        fail('staged bark candidate artifacts changed after validation');
    await unchanged(options, identity, native, fingerprint);
    const manifest = {
        protocol: '1', contract_version: '1', release_id: releaseId(checkpointHash, identity.fields.source_identity_sha256),
        parent_release_id: head?.release_id ?? '-', checkpoint_sha256: checkpointHash,
        model_sha256: modelHash, report_sha256: reportHash,
        ...identity.fields, native_sha256: fingerprint, learning_rate: rate,
        epochs: String(counters.epochs), steps: String(counters.steps),
    };
    await writeFile(join(stage, 'release.tsv'), serialize(manifest, manifestNames), { flag: 'wx' });
    return { status: 'promoted', head: await publish(options, stage, manifest), report, stage };
}

/** Train additional deterministic passes, verify a compact release, or replay its exact accepted recipe.
 * native is an optional test adapter with fingerprint() and run(args); it performs every numerical operation. */
export async function runBarkWorkflow(request = {}) {
    const options = settings(request);
    const identity = await barkSourceIdentity(options.root);
    const command = options.command ?? 'train';
    if (command === 'verify') {
        const verified = await verifyAccepted(options, identity);
        if (options.tool || options.native) {
            const native = await nativeFor(options);
            await native.run(['score', join(verified.directory, 'model.cgnn')]);
        }
        return { status: 'verified', head: verified.head, report: verified.report };
    }
    if (!['train', 'replay'].includes(command)) fail('unknown bark workflow command');
    return withLock(options.state, async () => {
        if (command === 'train') return train(options, identity);
        const accepted = await verifyAccepted(options, identity);
        const native = await nativeFor(options);
        const fingerprint = await native.fingerprint();
        if (!hashPattern.test(fingerprint)) fail('invalid native bark fingerprint');
        const stage = await staging(options);
        const output = join(stage, 'replayed.txt');
        await native.run(['replay', join(accepted.directory, 'checkpoint.txt'), output, accepted.head.learning_rate]);
        if (await fileHash(output, 256 * 1024 * 1024) !== accepted.head.checkpoint_sha256) fail('bark exact checkpoint replay failed');
        await native.run(['score', join(accepted.directory, 'model.cgnn')]);
        await unchanged(options, identity, native, fingerprint);
        return { status: 'replayed', head: accepted.head, report: accepted.report, stage };
    });
}

function cliOptions(arguments_) {
    const command = arguments_.shift() ?? 'train';
    const result = { command };
    if (command === '--help' || command === 'help') return { help: true };
    for (let index = 0; index < arguments_.length; index += 2) {
        const flag = arguments_[index];
        const value = arguments_[index + 1];
        if (!['--state', '--work', '--tool', '--epochs', '--hardware'].includes(flag) || value === undefined)
            fail('unknown or incomplete bark workflow option: ' + flag);
        const name = flag.slice(2);
        if (Object.hasOwn(result, name)) fail('duplicate bark workflow option: ' + flag);
        result[name] = name === 'epochs' ? numeric(value, name, true, 1, 10000) : value;
    }
    if (command !== 'train' && Object.hasOwn(result, 'epochs')) fail('--epochs applies only to train');
    return result;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
    try {
        const options = cliOptions(process.argv.slice(2));
        if (options.help) process.stdout.write('Usage: node tools/bark/workflow.mjs train|verify|replay [--state directory] [--work directory] [--tool executable] [--epochs additional-passes] [--hardware label]\nverify without --tool checks hashes offline; verify --tool additionally loads and scores accepted weights with that build.\n');
        else {
            const result = await runBarkWorkflow(options);
            process.stdout.write(result.status + (result.head ? ' ' + result.head.release_id + ' epochs=' + result.head.epochs + ' steps=' + result.head.steps : '') + '\n');
            if (result.status === 'rejected') process.stdout.write('candidate retained locally at ' + relative(repository, result.stage) + '; accepted pointer unchanged\n');
        }
    } catch (error) {
        process.stderr.write('bark workflow: ' + error.message + '\n');
        process.exitCode = 1;
    }
}
