/** Immutable gameplay releases; all numerical training, scoring and timing run through C11. */
import { createHash, randomUUID } from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { cpus, hostname, platform, arch } from 'node:os';
import { parseBarkReport } from '../bark/workflow.mjs';
import { dirname, join, resolve, relative } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { copyFile, lstat, mkdir, open, readFile, readdir, rename, unlink, writeFile } from 'node:fs/promises';

const execute = promisify(execFile);
const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const artifactNames = ['checkpoint.txt', 'model.cggp', 'report.tsv'];
const identityNames = ['contract_sha256', 'registry_sha256', 'fixtures_sha256', 'generator_sha256',
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
        fail('invalid one-line gameplay metadata');
    return value;
}
function numeric(value, name, integer = false, minimum = 0, maximum = Number.MAX_SAFE_INTEGER) {
    if (typeof value !== 'string' || !numberPattern.test(value)) fail('invalid gameplay number: ' + name);
    const parsed = Number(value);
    if (!Number.isFinite(parsed) || parsed < minimum || parsed > maximum || (integer && !Number.isSafeInteger(parsed)))
        fail('out-of-range gameplay number: ' + name);
    return parsed;
}
function pairs(text, header = false, maximum = 65536) {
    if (Buffer.byteLength(text) > maximum || !text.endsWith('\n') || /[\x00-\x08\x0b-\x1f\x7f]/.test(text))
        fail('invalid LF gameplay TSV document');
    const rows = text.slice(0, -1).split('\n');
    if (header && rows.shift() !== 'key\tvalue') fail('invalid gameplay contract header');
    const result = Object.create(null);
    for (const row of rows) {
        const cells = row.split('\t');
        if (cells.length !== 2 || !/^[a-z][a-z0-9_]*$/.test(cells[0]) || Object.hasOwn(result, cells[0]))
            fail('duplicate or invalid gameplay TSV key');
        result[cells[0]] = safeText(cells[1], 4096);
    }
    return result;
}
function schema(fields, names) {
    if (Object.keys(fields).length !== names.length || names.some(name => !Object.hasOwn(fields, name)))
        fail('invalid gameplay TSV schema');
}
function serialize(fields, names) {
    schema(fields, names);
    return names.map(name => name + '\t' + safeText(String(fields[name]), 4096) + '\n').join('');
}
async function boundedFile(path, maximum = 65536) {
    const info = await lstat(path);
    if (!info.isFile() || info.size < 1 || info.size > maximum) fail('invalid gameplay file: ' + path);
    const bytes = await readFile(path);
    if (bytes.length !== info.size || bytes.length > maximum) fail('gameplay file changed while reading: ' + path);
    return bytes;
}
async function fileHash(path, maximum) { return digest(await boundedFile(path, maximum)); }
/** Exact source dependencies prevent unrelated legacy models invalidating this recipe. */
export const GAMEPLAY_SOURCE_FILES = {
    runtime: ['include/centroid_gai.h', 'include/centroid_gai_gameplay.h',
        'src/core/error.c', 'src/core/model_random.c', 'src/internal/error.h', 'src/internal/model_random.h',
        'src/gameplay/gameplay_internal.h', 'src/gameplay/gameplay_model.c',
        'src/gameplay/gameplay_session.c', 'src/gameplay/gameplay_forward.c',
        'src/gameplay/gameplay_backward.c', 'src/gameplay/gameplay_training.c',
        'src/gameplay/gameplay_codec.c'],
    generator: ['tools/gameplay/gameplay_tasks.c', 'tools/gameplay/gameplay_tasks.h'],
    trainer: ['tools/gameplay/workflow.mjs', 'tools/gameplay/gameplay_tool.h',
        'tools/gameplay/gameplay_main.c', 'tools/gameplay/gameplay_training.c', 'tools/gameplay/gameplay_initialization.c', 'tools/gameplay/gameplay_report.c',
        'tools/gameplay/gameplay_evaluation.c', 'tools/gameplay/gameplay_evaluation.h', 'tools/gameplay/gameplay_benchmark.c'],
};
async function sourceGroup(root, paths) {
    const unique = [...new Set(paths)].sort();
    if (!unique.length) fail('empty gameplay source identity');
    const rows = [];
    for (const path of unique) {
        const bytes = await boundedFile(join(root, path), 4 * 1024 * 1024);
        const text = bytes.toString('utf8');
        if (!Buffer.from(text, 'utf8').equals(bytes)) fail('invalid UTF-8 gameplay source: ' + path);
        rows.push(path + '\t' + digest(text.replaceAll('\r\n', '\n')) + '\n');
    }
    return digest(rows.join(''));
}
const taskColumns = ['task_id', 'name', 'output_count', 'training_cases', 'development_cases', 'test_cases',
    'minimum_development_accuracy', 'minimum_test_accuracy', 'teacher_complete', 'acceptance_complete', 'catalog', 'scenarios', 'task_features'];
function table(text, columns, maximumRows = 100000) {
    if (!text.endsWith('\n') || /[\x00-\x08\x0b-\x1f\x7f]/.test(text)) fail('invalid LF gameplay table');
    const lines = text.slice(0, -1).split('\n');
    if (lines.shift() !== columns.join('\t') || lines.length > maximumRows) fail('invalid gameplay table header');
    return lines.map(line => {
        const cells = line.split('\t');
        if (cells.length !== columns.length) fail('invalid gameplay table row');
        return Object.fromEntries(columns.map((name, index) => [name, safeText(cells[index], 4096)]));
    });
}
function fileName(value) {
    if (!/^[a-z][a-z0-9_-]*\.tsv$/.test(value)) fail('invalid registry fixture filename');
    return value;
}
/** Validate arbitrary bounded task descriptors; incomplete scaffold teachers fail closed. */
export function parseTaskRegistry(text) {
    const tasks = table(text, taskColumns, 8);
    if (tasks.length < 1) fail('empty gameplay task registry');
    const names = new Set();
    return tasks.map((raw, index) => {
        if (raw.task_id !== String(index) || !/^[a-z][a-z0-9_]{0,31}$/.test(raw.name) || raw.name === 'paired' || names.has(raw.name))
            fail('invalid or duplicate gameplay task identity');
        names.add(raw.name);
        if (raw.teacher_complete !== '1' || raw.acceptance_complete !== '1') fail('gameplay teacher and acceptance policy are incomplete: ' + raw.name);
        const task = { ...raw };
        for (const key of ['task_id', 'output_count', 'training_cases', 'development_cases', 'test_cases'])
            task[key] = numeric(raw[key], key, true, key === 'task_id' ? 0 : key === 'output_count' ? 2 : 1,
                key === 'output_count' ? 64 : key === 'task_id' ? 7 : 100000);
        for (const key of ['minimum_development_accuracy', 'minimum_test_accuracy'])
            task[key] = numeric(raw[key], key, false, 0, 1);
        task.catalog = fileName(raw.catalog);
        task.scenarios = fileName(raw.scenarios);
        task.task_features = numeric(raw.task_features, 'task_features', true, 1, 65535);
        return task;
    });
}
function listNumbers(value, name, length, minimum, maximum) {
    const parts = value?.split(',');
    if (parts?.length !== length) fail('invalid gameplay contract array: ' + name);
    return parts.map(item => numeric(item, name, true, minimum, maximum));
}
function parsedPolicy(fields, tasks, modules) {
    if (fields.contract_version !== '1' || fields.task_name !== 'composed-v1' || fields.target_mode !== 'hierarchical-task-id-v1')
        fail('unsupported gameplay composition contract');
    if (fields.teacher_complete !== '1' || fields.acceptance_complete !== '1') fail('composition teacher and acceptance policy are incomplete');
    const integers = ['initial_epochs', 'additional_epochs', 'training_records_per_epoch',
        'embedding_dimensions', 'hidden_dimensions', 'module_count', 'centroids_per_module', 'feature_count', 'seed',
        'maximum_model_bytes', 'maximum_session_bytes', 'bark_invariance_cases', 'simulator_cases', 'benchmark_samples', 'benchmark_session_pool', 'benchmark_workers'];
    const floats = ['learning_rate', 'weight_decay', 'routing_temperature', 'gradient_clip', 'maximum_p95_us', 'maximum_p99_us',
        'minimum_development_improvement', 'test_regression_tolerance', 'minimum_module_share', 'minimum_ablation_change',
        'maximum_paired_p95_us', 'maximum_paired_p99_us'];
    const policy = { tasks, modules };
    for (const key of integers) policy[key] = numeric(fields[key], key, true, 1);
    for (const key of floats) policy[key] = numeric(fields[key], key);
    if (policy.initial_epochs > 10000 || policy.additional_epochs > 10000 || policy.learning_rate <= 0 || policy.learning_rate > 1 || policy.weight_decay > 1 ||
        policy.benchmark_workers !== 1 || policy.gradient_clip !== 5 || policy.weight_decay !== 0.05 || policy.routing_temperature < 0.01 || policy.routing_temperature > 100 ||
        policy.embedding_dimensions > 64 || policy.hidden_dimensions > 64 || policy.module_count > 8 || policy.centroids_per_module > 32 ||
        policy.feature_count > 16 || modules.length !== policy.module_count || policy.minimum_module_share > 1)
        fail('unsupported gameplay promotion policy');
    policy.cardinalities = listNumbers(fields.cardinalities, 'cardinalities', policy.feature_count, 1, 64);
    policy.task_feature_masks = tasks.map(task => task.task_features);
    if (policy.task_feature_masks.some(mask => mask > 2 ** policy.feature_count - 1)) fail('task uses unknown feature bits');
    for (const task of tasks) {
        task.module_mask = modules.reduce((mask, module) => mask | ((module.task_mask & (1 << task.task_id)) ? 1 << module.module_id : 0), 0);
        if (!task.module_mask) fail('gameplay task has no eligible centroid module');
    }
    if (numeric(fields.task_count, 'task_count', true, 1, 8) !== tasks.length) fail('contract task count differs from registry');
    for (const [name, expected] of [['task_output_counts', tasks.map(task => task.output_count)],
        ['task_module_masks', tasks.map(task => task.module_mask)], ['task_feature_masks', policy.task_feature_masks]]) {
        const actual = listNumbers(fields[name], name, tasks.length, 1, 65535);
        if (actual.some((value, index) => value !== expected[index])) fail('contract task shape differs from registry: ' + name);
    }
    for (const name of ['bark_invariance_min_accuracy', 'simulator_min_legal', 'simulator_min_survival', 'simulator_min_objective', 'simulator_min_stability'])
        if (numeric(fields[name], name) !== 1) fail('unsupported gameplay supplemental acceptance policy');
    return policy;
}
/** Registry-aware identities pin authored fixtures and explicit canonical-LF implementation dependencies. */
export async function gameplaySourceIdentity(root = repository) {
    root = resolve(root);
    const data = 'data/gameplay/composed-v1/';
    const contractBytes = await boundedFile(join(root, data, 'contract.tsv'));
    const taskBytes = await boundedFile(join(root, data, 'tasks.tsv'));
    const moduleBytes = await boundedFile(join(root, data, 'modules.tsv'));
    const tasks = parseTaskRegistry(taskBytes.toString('utf8'));
    const modules = table(moduleBytes.toString('utf8'), ['module_id', 'name', 'task_mask'], 8).map((raw, index) => {
        if (raw.module_id !== String(index) || !/^[a-z][a-z0-9_]{0,31}$/.test(raw.name)) fail('invalid gameplay module identity');
        return { module_id: index, name: raw.name, task_mask: numeric(raw.task_mask, 'task_mask', true, 1, 2 ** tasks.length - 1) };
    });
    if (new Set(modules.map(module => module.name)).size !== modules.length) fail('duplicate gameplay module identity');
    const policy = parsedPolicy(pairs(contractBytes.toString('utf8'), true), tasks, modules);
    const fixturePaths = tasks.flatMap(task => [data + task.catalog, data + task.scenarios]);
    const fields = {
        contract_sha256: digest(contractBytes), registry_sha256: digest(taskBytes + '\n' + moduleBytes),
        fixtures_sha256: await sourceGroup(root, fixturePaths),
        generator_sha256: await sourceGroup(root, GAMEPLAY_SOURCE_FILES.generator),
        runtime_sha256: await sourceGroup(root, GAMEPLAY_SOURCE_FILES.runtime),
        trainer_sha256: await sourceGroup(root, GAMEPLAY_SOURCE_FILES.trainer),
    };
    fields.source_identity_sha256 = digest(serialize(fields, identityNames.slice(0, -1)));
    return { fields, policy };
}

const metricNames = ['cases', 'correct', 'abstained', 'cross_entropy'];
const splitNames = ['training', 'development', 'test'];
const qualityCounters = ['module_violations', 'bark_invariance_cases', 'bark_invariance_correct', 'simulator_cases',
    'simulator_legal', 'simulator_survived', 'simulator_objective_successes', 'simulator_stable'];
const moduleMetrics = ['weight', 'contribution', 'ablation_cross_entropy', 'ablation_change'];
const timingMetrics = ['sample_count', 'session_pool', 'workers', 'forward_passes_per_sample', 'p50_us', 'p95_us', 'p99_us', 'maximum_us'];
function reportNames(policy) {
    return ['version', 'contract_version', 'scope', 'hardware', 'compiler', 'promotion_passed', 'task_count', 'module_count',
        'model_limit_bytes', 'session_limit_bytes', 'p95_limit_us', 'p99_limit_us', 'paired_p95_limit_us', 'paired_p99_limit_us',
        'minimum_module_share', 'minimum_ablation_change', 'minimum_development_improvement', 'test_regression_tolerance',
        ...policy.tasks.map(task => 'minimum_' + task.name + '_accuracy'),
        ...['before', 'after'].flatMap(phase => [
            ...policy.tasks.flatMap(task => [
                ...splitNames.flatMap(split => metricNames.map(metric => phase + '_' + task.name + '_' + split + '_' + metric)),
                ...['host_checks', 'mask_violations', 'repeat_violations', 'composition_cases'].map(metric => phase + '_' + task.name + '_' + metric),
                ...policy.modules.flatMap(module => moduleMetrics.map(metric => phase + '_' + task.name + '_module' + module.module_id + '_' + metric)),
            ]), ...qualityCounters.map(metric => phase + '_' + metric),
        ]),
        'model_bytes', 'session_bytes', 'parameter_bytes', 'parameter_count', 'optimizer_bytes',
        'encoder_multiply_adds', 'outer_coordinates', 'inner_coordinates', 'maximum_head_logits', 'maximum_head_multiply_adds',
        ...[...policy.tasks.map(task => task.name), 'paired'].flatMap(workload => timingMetrics.map(metric => workload + '_' + metric)),
    ];
}
/** Validate native quality independently, using a generic task and module registry. */
export function parseGameplayReport(text, policy) {
    const raw = pairs(text);
    const names = reportNames(policy);
    schema(raw, names);
    if (raw.version !== '1' || raw.contract_version !== '1' || raw.scope !== 'bounded-synthetic-gameplay-v1')
        fail('unsupported gameplay report');
    const report = { hardware: safeText(raw.hardware), compiler: safeText(raw.compiler) };
    for (const name of names.filter(name => !['version', 'contract_version', 'scope', 'hardware', 'compiler'].includes(name))) {
        const integral = /(?:cases|correct|abstained|bytes|count|violations|checks|legal|survived|successes|stable|session_pool|workers|coordinates|logits|multiply_adds|forward_passes_per_sample)$/.test(name) || name === 'promotion_passed';
        report[name] = numeric(raw[name], name, integral);
    }
    const limits = {
        task_count: policy.tasks.length, module_count: policy.modules.length,
        model_limit_bytes: policy.maximum_model_bytes, session_limit_bytes: policy.maximum_session_bytes,
        p95_limit_us: policy.maximum_p95_us, p99_limit_us: policy.maximum_p99_us,
        paired_p95_limit_us: policy.maximum_paired_p95_us, paired_p99_limit_us: policy.maximum_paired_p99_us,
        minimum_module_share: policy.minimum_module_share, minimum_ablation_change: policy.minimum_ablation_change,
        minimum_development_improvement: policy.minimum_development_improvement, test_regression_tolerance: policy.test_regression_tolerance,
    };
    for (const task of policy.tasks) limits['minimum_' + task.name + '_accuracy'] = Math.max(task.minimum_development_accuracy, task.minimum_test_accuracy);
    for (const workload of [...policy.tasks.map(task => task.name), 'paired']) {
        limits[workload + '_sample_count'] = policy.benchmark_samples;
        limits[workload + '_session_pool'] = policy.benchmark_session_pool;
        limits[workload + '_workers'] = policy.benchmark_workers;
        limits[workload + '_forward_passes_per_sample'] = workload === 'paired' ? 2 : 1;
    }
    for (const [name, value] of Object.entries(limits))
        if (report[name] !== value) fail('native gameplay report differs from authored policy: ' + name);
    if (report.promotion_passed > 1 || report.parameter_count < 1 || report.model_bytes < 1 || report.session_bytes < 1 ||
        report.parameter_bytes !== report.parameter_count * 8 || report.encoder_multiply_adds < 1 || report.outer_coordinates < 1 ||
        report.inner_coordinates < 1 || report.maximum_head_logits < 1 || report.maximum_head_multiply_adds < 1) fail('invalid gameplay resource metric');
    let passed = report.model_bytes <= policy.maximum_model_bytes && report.session_bytes <= policy.maximum_session_bytes && report.optimizer_bytes === 0;
    for (const workload of [...policy.tasks.map(task => task.name), 'paired']) {
        const prefix = workload + '_';
        if (!(report[prefix + 'p50_us'] <= report[prefix + 'p95_us'] && report[prefix + 'p95_us'] <= report[prefix + 'p99_us'] &&
            report[prefix + 'p99_us'] <= report[prefix + 'maximum_us'])) fail('invalid gameplay percentile ordering');
        passed &&= report[prefix + 'p95_us'] <= (workload === 'paired' ? policy.maximum_paired_p95_us : policy.maximum_p95_us) &&
            report[prefix + 'p99_us'] <= (workload === 'paired' ? policy.maximum_paired_p99_us : policy.maximum_p99_us);
    }
    for (const phase of ['before', 'after']) {
        for (const task of policy.tasks) {
            const prefix = phase + '_' + task.name + '_';
            const cases = splitNames.reduce((count, split) => count + task[split + '_cases'], 0);
            for (const split of splitNames) {
                const section = prefix + split + '_';
                if (report[section + 'cases'] !== task[split + '_cases'] || report[section + 'correct'] > report[section + 'cases'] ||
                    report[section + 'abstained'] > report[section + 'cases']) fail('invalid gameplay scenario metric');
            }
            if (report[prefix + 'composition_cases'] !== cases || report[prefix + 'host_checks'] !== 2 ** task.output_count * task.output_count * 2 ** policy.modules.length + cases)
                fail('invalid gameplay composition workload count');
            let totalWeight = 0, totalContribution = 0;
            for (const module of policy.modules) {
                const group = prefix + 'module' + module.module_id + '_';
                const eligible = !!(task.module_mask & (1 << module.module_id));
                const weight = report[group + 'weight'], share = report[group + 'contribution'];
                if (weight > 1 || share > 1 || (!eligible && (weight !== 0 || share !== 0))) fail('invalid gameplay module shares');
                totalWeight += weight; totalContribution += share;
                if (phase === 'after' && eligible)
                    passed &&= weight >= policy.minimum_module_share && share >= policy.minimum_module_share && report[group + 'ablation_change'] >= policy.minimum_ablation_change;
            }
            if (Math.abs(totalWeight - 1) > 1e-9 || Math.abs(totalContribution - 1) > 1e-9) fail('gameplay module shares are not normalized');
            if (phase === 'after') {
                passed &&= report[prefix + 'mask_violations'] === 0 && report[prefix + 'repeat_violations'] === 0;
                for (const split of ['development', 'test'])
                    passed &&= report[prefix + split + '_correct'] / task[split + '_cases'] >= task['minimum_' + split + '_accuracy'] &&
                        report[prefix + split + '_correct'] >= report['before_' + task.name + '_' + split + '_correct'];
                passed &&= report['before_' + task.name + '_development_cross_entropy'] - report[prefix + 'development_cross_entropy'] > policy.minimum_development_improvement &&
                    report[prefix + 'test_cross_entropy'] - report['before_' + task.name + '_test_cross_entropy'] <= policy.test_regression_tolerance;
            }
        }
        const prefix = phase + '_';
        if (report[prefix + 'bark_invariance_cases'] !== policy.bark_invariance_cases ||
            report[prefix + 'bark_invariance_correct'] > report[prefix + 'bark_invariance_cases'] || report[prefix + 'simulator_cases'] !== policy.simulator_cases ||
            ['legal', 'survived', 'objective_successes', 'stable'].some(metric => report[prefix + 'simulator_' + metric] > report[prefix + 'simulator_cases']))
            fail('invalid gameplay supplemental workload');
        if (phase === 'after') passed &&= report[prefix + 'module_violations'] === 0 && report[prefix + 'bark_invariance_correct'] === report[prefix + 'bark_invariance_cases'] &&
            ['legal', 'survived', 'objective_successes', 'stable'].every(metric => report[prefix + 'simulator_' + metric] === report[prefix + 'simulator_cases']);
    }
    if (report.promotion_passed !== Number(passed)) fail('native gameplay report promotion decision is inconsistent');
    return report;
}

function parseManifest(text, names = manifestNames) {
    const fields = pairs(text, false, 32768);
    schema(fields, names);
    if (fields.protocol !== '1' || fields.contract_version !== '1') fail('unsupported gameplay release protocol');
    for (const name of names.filter(name => name.endsWith('_sha256') || name === 'release_id'))
        if (!hashPattern.test(fields[name])) fail('invalid gameplay release hash');
    if (fields.parent_release_id !== '-' && !hashPattern.test(fields.parent_release_id)) fail('invalid gameplay parent release');
    numeric(fields.learning_rate, 'learning_rate', false, Number.MIN_VALUE, 1);
    numeric(fields.epochs, 'epochs', true, 1, 10000);
    numeric(fields.steps, 'steps', true, 1);
    if (fields.release_id !== releaseId(fields.checkpoint_sha256, fields.source_identity_sha256)) fail('invalid gameplay content ID');
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
    if (!text.startsWith('CGAI-GAMEPLAY-CHECKPOINT 1\n') || text.includes('\r')) fail('invalid native gameplay checkpoint');
    const counter = name => {
        const matches = [...text.matchAll(new RegExp('^' + name + ' ([0-9]+)$', 'gm'))];
        if (matches.length !== 1) fail('invalid checkpoint progress');
        return numeric(matches[0][1], name, true);
    };
    for (const name of ['embedding_dimensions', 'hidden_dimensions', 'module_count', 'centroids_per_module', 'feature_count', 'seed'])
        if (counter(name) !== policy[name]) fail('native gameplay checkpoint differs from authored shape: ' + name);
    if (counter('task_count') !== policy.tasks.length) fail('native checkpoint differs from authored task registry');
    const array = (name, expected) => {
        const matches = [...text.matchAll(new RegExp('^' + name + ' ([0-9]+)$', 'gm'))];
        if (matches.length !== expected.length || matches.some((match, index) => numeric(match[1], name, true) !== expected[index]))
            fail('native checkpoint differs from authored array: ' + name);
    };
    array('cardinality', policy.cardinalities);
    array('output_count', policy.tasks.map(task => task.output_count));
    array('task_modules', policy.tasks.map(task => task.module_mask));
    array('task_features', policy.task_feature_masks);
    if (counter('parameter_count') < 1 || counter('optimizer_present') > 1) fail('invalid native checkpoint parameter state');
    const routing = [...text.matchAll(/^routing_temperature ([^\s]+)$/gm)];
    if (routing.length !== 1 || hexadecimalReal(routing[0][1]) !== policy.routing_temperature)
        fail('native gameplay checkpoint differs from authored routing temperature');
    return { epochs: counter('training_epochs'), steps: counter('training_step') };
}
function settings(options) {
    const root = resolve(options.root ?? repository);
    return { ...options, root, state: resolve(root, options.state ?? 'models/gameplay/composed-v1'),
        work: resolve(root, options.work ?? 'build/gameplay') };
}
async function readHead(state) {
    try { return parseManifest((await boundedFile(join(state, 'current.tsv'), 32768)).toString('utf8'), headNames); }
    catch (error) { if (error.code === 'ENOENT') return null; throw error; }
}
async function headSnapshot(state) {
    try { return await boundedFile(join(state, 'current.tsv'), 32768); }
    catch (error) { if (error.code === 'ENOENT') return null; throw error; }
}
async function unchangedHead(state, expected) {
    const current = await headSnapshot(state);
    if (expected === null ? current !== null : current === null || !current.equals(expected))
        fail('accepted gameplay pointer changed during native work; external pointer preserved');
}

async function verifyAccepted(options, identity) {
    const head = await readHead(options.state);
    if (!head) fail('no accepted gameplay release');
    for (const name of identityNames)
        if (head[name] !== identity.fields[name]) fail('gameplay source identity changed: ' + name);
    if (Number(head.learning_rate) !== identity.policy.learning_rate) fail('gameplay learning rate changed');
    const directory = join(options.state, 'releases', head.release_id);
    const manifestBytes = await boundedFile(join(directory, 'release.tsv'), 32768);
    if (digest(manifestBytes) !== head.release_sha256) fail('gameplay release manifest hash mismatch');
    const manifest = parseManifest(manifestBytes.toString('utf8'));
    for (const name of manifestNames) if (manifest[name] !== head[name]) fail('gameplay head differs from release manifest');
    const limits = { 'checkpoint.txt': 256 * 1024 * 1024, 'model.cggp': 64 * 1024 * 1024, 'report.tsv': 65536 };
    const artifacts = {};
    for (const filename of artifactNames) {
        artifacts[filename] = await boundedFile(join(directory, filename), limits[filename]);
        const field = filename === 'checkpoint.txt' ? 'checkpoint_sha256' : filename === 'model.cggp' ? 'model_sha256' : 'report_sha256';
        if (digest(artifacts[filename]) !== head[field]) fail('gameplay artifact hash mismatch: ' + filename);
    }
    const report = parseGameplayReport(artifacts['report.tsv'].toString('utf8'), identity.policy);
    if (report.promotion_passed !== 1) fail('accepted gameplay release lacks a passing report');
    const counters = progress(artifacts['checkpoint.txt'], identity.policy);
    if (counters.epochs !== Number(head.epochs) || counters.steps !== Number(head.steps) ||
        counters.steps !== counters.epochs * identity.policy.training_records_per_epoch) fail('gameplay checkpoint progress differs from recipe');
    return { head, directory, report };
}

/** An injectable adapter executes fixed native argument arrays without a shell. */
export function createGameplayNative(tool) {
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
    if (options.tool) return createGameplayNative(resolve(options.root, options.tool));
    const suffix = platform() === 'win32' ? '.exe' : '';
    for (const location of ['build/deterministic/Release/', 'build/deterministic/', 'build/release/Release/', 'build/release/', 'build/dev/']) {
        const candidate = join(options.root, location, 'cgai_gameplay_tool' + suffix);
        try { if ((await lstat(candidate)).isFile()) return createGameplayNative(candidate); }
        catch (error) { if (error.code !== 'ENOENT') throw error; }
    }
    fail('compiled cgai_gameplay_tool missing; pass --tool or build the C11 target');
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
        if (lock.version !== '1' || lock.host !== hostname()) fail('gameplay workflow is locked by another host');
        const pid = numeric(lock.pid, 'lock pid', true, 1, 2147483647);
        try { process.kill(pid, 0); fail('gameplay workflow is already running'); }
        catch (probe) { if (probe.code !== 'ESRCH') throw probe; }
        fail('stale gameplay workflow lock; remove ' + path + ' after confirming recorded same-host PID ' + pid + ' has exited');
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
    const current = await gameplaySourceIdentity(options.root);
    if (current.fields.source_identity_sha256 !== identity.fields.source_identity_sha256 || await native.fingerprint() !== fingerprint)
        fail('gameplay source or native executable changed during training');
}
async function publish(options, stage, manifest, snapshot) {
    await unchangedHead(options.state, snapshot);
    const releases = join(options.state, 'releases');
    await mkdir(releases, { recursive: true });
    const temporary = join(releases, '.pending-' + randomUUID());
    const destination = join(releases, manifest.release_id);
    try { await lstat(destination); fail('immutable gameplay release already exists'); }
    catch (error) { if (error.code !== 'ENOENT') throw error; }
    await mkdir(temporary);
    for (const filename of [...artifactNames, 'release.tsv']) await copyFile(join(stage, filename), join(temporary, filename));
    const copiedManifest = await boundedFile(join(temporary, 'release.tsv'), 32768);
    if (!copiedManifest.equals(Buffer.from(serialize(manifest, manifestNames)))) fail('copied gameplay release manifest changed');
    for (const [filename, field, maximum] of [
        ['checkpoint.txt', 'checkpoint_sha256', 256 * 1024 * 1024],
        ['model.cggp', 'model_sha256', 64 * 1024 * 1024], ['report.tsv', 'report_sha256', 65536],
    ]) if (await fileHash(join(temporary, filename), maximum) !== manifest[field])
        fail('copied gameplay artifact hash mismatch: ' + filename);
    await rename(temporary, destination);
    const head = { ...manifest, release_sha256: digest(copiedManifest) };
    const pointer = join(options.state, '.current-' + randomUUID() + '.tmp');
    await writeFile(pointer, serialize(head, headNames), { flag: 'wx' });
    await unchangedHead(options.state, snapshot);
    await rename(pointer, join(options.state, 'current.tsv'));
    return head;
}

async function train(options, identity) {
    const snapshot = await headSnapshot(options.state);
    const head = snapshot === null ? null : parseManifest(snapshot.toString('utf8'), headNames);
    const incumbent = head ? await verifyAccepted(options, identity) : null;
    await unchangedHead(options.state, snapshot);
    const native = await nativeFor(options);
    const fingerprint = await native.fingerprint();
    if (!hashPattern.test(fingerprint)) fail('invalid native gameplay fingerprint');
    const epochs = options.epochs ?? (head ? identity.policy.additional_epochs : identity.policy.initial_epochs);
    if (!Number.isSafeInteger(epochs) || epochs < 1 || epochs > 10000 || epochs + Number(head?.epochs ?? 0) > 10000)
        fail('gameplay epochs must be 1..10000 and total replay epochs must not exceed 10000');
    const hardware = safeText(options.hardware ?? (cpus()[0]?.model ?? 'unknown CPU') + '; ' + platform() + ' ' + arch());
    const stage = await staging(options);
    const initial = join(stage, 'initial.txt');
    const baseline = join(stage, 'baseline.cggp');
    const checkpoint = join(stage, 'checkpoint.txt');
    const model = join(stage, 'model.cggp');
    const reportPath = join(stage, 'report.tsv');
    if (incumbent) {
        await copyFile(join(incumbent.directory, 'checkpoint.txt'), initial);
        await copyFile(join(incumbent.directory, 'model.cggp'), baseline);
    } else {
        await native.run(['init', initial]);
        await native.run(['export', initial, baseline]);
    }
    progress(await boundedFile(initial, 256 * 1024 * 1024), identity.policy);
    const initialHash = await fileHash(initial, 256 * 1024 * 1024);
    const baselineHash = await fileHash(baseline, 64 * 1024 * 1024);
    if (head && (initialHash !== head.checkpoint_sha256 || baselineHash !== head.model_sha256))
        fail('copied gameplay incumbent artifacts changed');
    const rate = String(identity.policy.learning_rate);
    if (head && head.native_sha256 !== fingerprint) {
        const compatible = join(stage, 'compatible-replay.txt');
        await native.run(['replay', initial, compatible, rate]);
        if (await fileHash(compatible, 256 * 1024 * 1024) !== head.checkpoint_sha256)
            fail('current gameplay executable cannot exactly replay the accepted checkpoint');
        await unchanged(options, identity, native, fingerprint);
    }
    await native.run(['step', initial, checkpoint, String(epochs), rate]);
    const checkpointHash = await fileHash(checkpoint, 256 * 1024 * 1024);
    const counters = progress(await boundedFile(checkpoint, 256 * 1024 * 1024), identity.policy);
    if (counters.epochs !== Number(head?.epochs ?? 0) + epochs || counters.steps !== counters.epochs * identity.policy.training_records_per_epoch)
        fail('native gameplay checkpoint counters differ from the independent training recipe');
    await native.run(['export', checkpoint, model]);
    const modelHash = await fileHash(model, 64 * 1024 * 1024);
    await native.run(['report', model, baseline, reportPath, hardware]);
    const reportBytes = await boundedFile(reportPath);
    const reportHash = digest(reportBytes);
    const report = parseGameplayReport(reportBytes.toString('utf8'), identity.policy);
    if (await fileHash(baseline, 64 * 1024 * 1024) !== baselineHash ||
        await fileHash(initial, 256 * 1024 * 1024) !== initialHash) fail('staged gameplay incumbent artifacts changed');
    if (report.promotion_passed !== 1) return { status: 'rejected', head: head ?? undefined, report, stage };
    const replay = join(stage, 'replayed.txt');
    await native.run(['replay', checkpoint, replay, rate]);
    if (await fileHash(checkpoint, 256 * 1024 * 1024) !== checkpointHash ||
        await fileHash(replay, 256 * 1024 * 1024) !== checkpointHash) fail('gameplay exact checkpoint replay failed');
    await native.run(['score', model]);
    if (await fileHash(model, 64 * 1024 * 1024) !== modelHash || await fileHash(reportPath) !== reportHash)
        fail('staged gameplay candidate artifacts changed after validation');
    await unchanged(options, identity, native, fingerprint);
    const manifest = {
        protocol: '1', contract_version: '1', release_id: releaseId(checkpointHash, identity.fields.source_identity_sha256),
        parent_release_id: head?.release_id ?? '-', checkpoint_sha256: checkpointHash,
        model_sha256: modelHash, report_sha256: reportHash,
        ...identity.fields, native_sha256: fingerprint, learning_rate: rate,
        epochs: String(counters.epochs), steps: String(counters.steps),
    };
    await writeFile(join(stage, 'release.tsv'), serialize(manifest, manifestNames), { flag: 'wx' });
    return { status: 'promoted', head: await publish(options, stage, manifest, snapshot), report, stage };
}

/** Train additional deterministic passes, verify a compact release, or replay its exact accepted recipe.
 * native is an optional test adapter with fingerprint() and run(args); it performs every numerical operation. */
export async function runGameplayWorkflow(request = {}) {
    if (request.command === 'verify-legacy') {
        if (request.tool || request.native) fail('historical verification does not prove executable replay compatibility');
        return verifyLegacyBarkRelease(request);
    }
    const options = settings(request);
    const identity = await gameplaySourceIdentity(options.root);
    const command = options.command ?? 'train';
    if (command === 'verify') {
        const verified = await verifyAccepted(options, identity);
        if (options.tool || options.native) {
            const native = await nativeFor(options);
            await native.run(['score', join(verified.directory, 'model.cggp')]);
        }
        return { status: 'verified', head: verified.head, report: verified.report };
    }
    if (!['train', 'replay'].includes(command)) fail('unknown gameplay workflow command');
    return withLock(options.state, async () => {
        if (command === 'train') return train(options, identity);
        const accepted = await verifyAccepted(options, identity);
        const native = await nativeFor(options);
        const fingerprint = await native.fingerprint();
        if (!hashPattern.test(fingerprint)) fail('invalid native gameplay fingerprint');
        const stage = await staging(options);
        const output = join(stage, 'replayed.txt');
        await native.run(['replay', join(accepted.directory, 'checkpoint.txt'), output, accepted.head.learning_rate]);
        if (await fileHash(output, 256 * 1024 * 1024) !== accepted.head.checkpoint_sha256) fail('gameplay exact checkpoint replay failed');
        await native.run(['score', join(accepted.directory, 'model.cggp')]);
        await unchanged(options, identity, native, fingerprint);
        return { status: 'replayed', head: accepted.head, report: accepted.report, stage };
    });
}

const legacyIdentityNames = ['contract_sha256', 'catalog_sha256', 'scenarios_sha256', 'generator_sha256',
    'runtime_sha256', 'trainer_sha256', 'source_identity_sha256'];
const legacyManifestNames = ['protocol', 'contract_version', 'release_id', 'parent_release_id',
    'checkpoint_sha256', 'model_sha256', 'report_sha256', ...legacyIdentityNames, 'native_sha256',
    'learning_rate', 'epochs', 'steps'];
const legacyPolicy = Object.freeze({ maximum_model_bytes: 262144, maximum_session_bytes: 65536,
    maximum_p95_us: 500, maximum_p99_us: 1000, minimum_development_accuracy: 0.95, minimum_test_accuracy: 0.95,
    minimum_development_improvement: 0.000001, test_regression_tolerance: 0.000000001,
    benchmark_samples: 2048, benchmark_session_pool: 4, benchmark_workers: 1,
    training_cases: 48, development_cases: 12, test_cases: 12 });
function legacyManifest(text, head = false) {
    const fields = pairs(text, false, 32768);
    schema(fields, head ? [...legacyManifestNames, 'release_sha256'] : legacyManifestNames);
    if (fields.protocol !== '1' || fields.contract_version !== '1') fail('unsupported historical bark manifest');
    for (const name of Object.keys(fields).filter(name => name.endsWith('_sha256') || name === 'release_id'))
        if (!hashPattern.test(fields[name])) fail('invalid historical bark hash');
    if (fields.parent_release_id !== '-' && !hashPattern.test(fields.parent_release_id)) fail('invalid historical parent ID');
    if (fields.release_id !== releaseId(fields.checkpoint_sha256, fields.source_identity_sha256)) fail('invalid historical content ID');
    const identities = Object.fromEntries(legacyIdentityNames.slice(0, -1).map(name => [name, fields[name]]));
    if (fields.source_identity_sha256 !== digest(serialize(identities, legacyIdentityNames.slice(0, -1))))
        fail('inconsistent historical source identity');
    if (Number(fields.learning_rate) !== 0.01 || !numberPattern.test(fields.learning_rate)) fail('unsupported historical learning rate');
    numeric(fields.epochs, 'historical epochs', true, 1, 10000);
    numeric(fields.steps, 'historical steps', true, 1);
    return fields;
}
/** Validate the original hash-bound release and its pinned version-one quality policy without current-recipe checks. */
export async function verifyLegacyBarkRelease(request = {}) {
    const root = resolve(request.root ?? repository);
    const state = resolve(root, request.state ?? 'models/gameplay/barks-v1');
    const head = legacyManifest((await boundedFile(join(state, 'current.tsv'), 32768)).toString('utf8'), true);
    const directory = join(state, 'releases', head.release_id);
    const manifestBytes = await boundedFile(join(directory, 'release.tsv'), 32768);
    if (digest(manifestBytes) !== head.release_sha256) fail('historical bark manifest hash mismatch');
    const manifest = legacyManifest(manifestBytes.toString('utf8'));
    for (const name of legacyManifestNames) if (manifest[name] !== head[name]) fail('historical bark head differs from manifest');
    const artifacts = {};
    for (const [name, field, maximum] of [['checkpoint.txt', 'checkpoint_sha256', 256 * 1024 * 1024],
        ['model.cgnn', 'model_sha256', 64 * 1024 * 1024], ['report.tsv', 'report_sha256', 65536]]) {
        artifacts[name] = await boundedFile(join(directory, name), maximum);
        if (digest(artifacts[name]) !== head[field]) fail('historical bark artifact hash mismatch: ' + name);
    }
    const report = parseBarkReport(artifacts['report.tsv'].toString('utf8'), legacyPolicy);
    if (report.promotion_passed !== 1) fail('historical bark release has no passing report');
    const checkpoint = artifacts['checkpoint.txt'].toString('utf8');
    if (!checkpoint.startsWith('CGAI-CHECKPOINT 1\n')) fail('invalid historical checkpoint');
    for (const [name, expected] of Object.entries({ embedding_dimensions: 16, hidden_dimensions: 16, centroid_count: 16,
        context_window: 4, seed: 42, training_epochs: Number(head.epochs), training_step: Number(head.steps) })) {
        const found = [...checkpoint.matchAll(new RegExp('^' + name + ' ([0-9]+)$', 'gm'))];
        if (found.length !== 1 || numeric(found[0][1], name, true) !== expected) fail('historical checkpoint recipe mismatch');
    }
    if (Number(head.steps) !== Number(head.epochs) * 48) fail('historical checkpoint record count mismatch');
    return { status: 'historical-verified', head, report, compatibility: 'unproved' };
}

function cliOptions(arguments_) {
    const command = arguments_.shift() ?? 'train';
    const result = { command };
    if (command === '--help' || command === 'help') return { help: true };
    for (let index = 0; index < arguments_.length; index += 2) {
        const flag = arguments_[index];
        const value = arguments_[index + 1];
        if (!['--state', '--work', '--tool', '--epochs', '--hardware'].includes(flag) || value === undefined)
            fail('unknown or incomplete gameplay workflow option: ' + flag);
        const name = flag.slice(2);
        if (Object.hasOwn(result, name)) fail('duplicate gameplay workflow option: ' + flag);
        result[name] = name === 'epochs' ? numeric(value, name, true, 1, 10000) : value;
    }
    if (command !== 'train' && Object.hasOwn(result, 'epochs')) fail('--epochs applies only to train');
    return result;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
    try {
        const options = cliOptions(process.argv.slice(2));
        if (options.help) process.stdout.write('Usage: node tools/gameplay/workflow.mjs train|verify|replay|verify-legacy [--state directory] [--work directory] [--tool executable] [--epochs additional-passes] [--hardware label]\nverify checks hashes and pinned policy offline; verify --tool additionally loads and scores accepted weights. verify-legacy validates the original bark release without claiming current-recipe replay compatibility.\n');
        else {
            const result = await runGameplayWorkflow(options);
            process.stdout.write(result.status + (result.head ? ' ' + result.head.release_id + ' epochs=' + result.head.epochs + ' steps=' + result.head.steps : '') + '\n');
            if (result.status === 'rejected') process.stdout.write('candidate retained locally at ' + relative(repository, result.stage) + '; accepted pointer unchanged\n');
        }
    } catch (error) {
        process.stderr.write('gameplay workflow: ' + error.message + '\n');
        process.exitCode = 1;
    }
}
