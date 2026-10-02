/** Generate a bounded C11 task package with deliberately incomplete teacher and acceptance gates. */
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve, join } from 'node:path';
import { pathToFileURL } from 'node:url';

const columns = ['task_id', 'name', 'output_count', 'training_cases', 'development_cases', 'test_cases',
    'minimum_development_accuracy', 'minimum_test_accuracy', 'teacher_complete', 'acceptance_complete', 'catalog', 'scenarios', 'task_features'];
function bounded(value, name, minimum, maximum) {
    if (!Number.isSafeInteger(value) || value < minimum || value > maximum) throw new Error('invalid scaffold ' + name);
    return value;
}
/** Generate source outside the protected authored-data boundary; refuse every existing destination. */
export async function scaffoldGameplayTask(options = {}) {
    const name = options.name;
    if (typeof name !== 'string' || !/^[a-z][a-z0-9_]{0,31}$/.test(name)) throw new Error('invalid scaffold task name');
    if (!options.output) throw new Error('an explicit scaffold output directory is required');
    const output = resolve(options.output);
    const taskId = bounded(options.taskId ?? 2, 'task ID', 0, 7);
    const outputCount = bounded(options.outputCount ?? 4, 'output count', 2, 64);
    const featureCount = bounded(options.featureCount ?? 9, 'feature count', 1, 16);
    const features = bounded(options.taskFeatures ?? 2 ** featureCount - 1, 'feature mask', 1, 2 ** featureCount - 1);
    await mkdir(output, { recursive: false });
    const prefix = name.toUpperCase();
    const header = `/** @file ${name}_task.h @brief Typed adapter for the ${name} gameplay head. */\n` +
        `#ifndef CGAI_${prefix}_TASK_H\n#define CGAI_${prefix}_TASK_H\n` +
        '#include "centroid_gai_gameplay.h"\n#include <stdbool.h>\n' +
        `#define ${prefix}_TASK_ID ${taskId}U\n#define ${prefix}_OUTPUT_COUNT ${outputCount}U\n` +
        `#define ${prefix}_FEATURE_COUNT ${featureCount}U\n#define ${prefix}_TASK_FEATURES ${features}U\n` +
        `typedef struct ${name}_state {\n    uint32_t observations[${featureCount}];\n} ${name}_state;\n` +
        `/** Convert bounded observations using the host's aligned bundle configuration. */\n` +
        `bool ${name}_example(const cgai_gameplay_config *config, const ${name}_state *state,\n` +
        `                    uint32_t target, cgai_gameplay_example *example);\n` +
        `/** Pending authored teacher: returns false until a complete rule is supplied. */\n` +
        `bool ${name}_teacher(const ${name}_state *state, uint32_t *target);\n#endif\n`;
    const source = `/** @file ${name}_task.c @brief Bounded typed adapter and incomplete authored teacher. */\n` +
        `#include "${name}_task.h"\n#include <stddef.h>\n` +
        `bool ${name}_example(const cgai_gameplay_config *config, const ${name}_state *state,\n` +
        `                    uint32_t target, cgai_gameplay_example *example) {\n` +
        '    cgai_gameplay_example candidate = {0};\n    size_t index;\n' +
        `    if (!config || !state || !example || config->feature_count != ${prefix}_FEATURE_COUNT ||\n` +
        `        config->task_count <= ${prefix}_TASK_ID || config->output_counts[${prefix}_TASK_ID] != ${prefix}_OUTPUT_COUNT ||\n` +
        `        config->task_features[${prefix}_TASK_ID] != ${prefix}_TASK_FEATURES || target >= ${prefix}_OUTPUT_COUNT)\n        return false;\n` +
        `    for (index = 0; index < ${prefix}_FEATURE_COUNT; ++index) {\n` +
        '        if (state->observations[index] >= config->cardinalities[index]) return false;\n' +
        '        candidate.state.values[index] = state->observations[index];\n    }\n' +
        `    candidate.task = ${prefix}_TASK_ID;\n    candidate.target = target;\n    *example = candidate;\n    return true;\n}\n` +
        `bool ${name}_teacher(const ${name}_state *state, uint32_t *target) {\n` +
        '    (void)state;\n    (void)target;\n    /* Implement an authored teacher and independent acceptance scenarios first. */\n    return false;\n}\n';
    const row = [taskId, name, outputCount, 1, 1, 1, 0.95, 0.95, 0, 0, name + '-catalog.tsv', name + '-scenarios.tsv', features];
    const files = {
        [name + '_task.h']: header, [name + '_task.c']: source,
        'descriptor.tsv': columns.join('\t') + '\n' + row.join('\t') + '\n',
        'contract.tsv': 'key\tvalue\ncontract_version\t1\ntask_name\t' + name +
            '\nteacher_status\tpending\nacceptance_status\tpending\nfeature_count\t' + featureCount +
            '\noutput_count\t' + outputCount + '\n',
        [name + '-catalog.tsv']: 'output_id\tname\ttext\n' + Array.from({ length: outputCount }, (_, index) =>
            index + '\t' + (index === 0 ? 'abstain' : 'option' + index) + '\t-\n').join(''),
        [name + '-scenarios.tsv']: 'case_id\tfamily_id\tsplit\ttarget\tstate\n',
        'check.mjs': `import { readFile } from 'node:fs/promises';\n` +
            `const root = new URL('./', import.meta.url);\n` +
            `const fields = Object.fromEntries((await readFile(new URL('contract.tsv', root), 'utf8')).trim().split('\\n').slice(1).map(line => line.split('\\t')));\n` +
            `if (fields.teacher_status !== 'complete' || fields.acceptance_status !== 'complete') throw new Error('teacher and acceptance policy are incomplete');\n` +
            `const descriptorRows = (await readFile(new URL('descriptor.tsv', root), 'utf8')).trim().split('\\n');\n` +
            `const columns = ${JSON.stringify(columns)};\n` +
            `if (descriptorRows.length !== 2 || descriptorRows[0] !== columns.join('\\t')) throw new Error('invalid task package descriptor');\n` +
            `const descriptorCells = descriptorRows[1].split('\\t');\n` +
            `if (descriptorCells[8] !== '1' || descriptorCells[9] !== '1') throw new Error('task descriptor remains incomplete');\n` +
            `const rows = (await readFile(new URL('${name}-scenarios.tsv', root), 'utf8')).trim().split('\\n');\n` +
            `if (rows.length < 4) throw new Error('authored training, development and test scenarios are required');\n` +
            `const splits = new Set(rows.slice(1).map(line => line.split('\\t')[2]));\n` +
            `if (!['training', 'development', 'test'].every(split => splits.has(split))) throw new Error('all independent splits are required');\n` +
            `process.stdout.write('task package is ready for registry and native teacher integration\\n');\n`,
    };
    for (const [filename, contents] of Object.entries(files)) await writeFile(join(output, filename), contents, { flag: 'wx' });
    return { output, taskId, name, files: Object.keys(files) };
}

/** Package checks require explicit complete flags and all three frozen splits. */
export async function checkGameplayScaffold(output) {
    const contract = Object.fromEntries((await readFile(join(resolve(output), 'contract.tsv'), 'utf8'))
        .trim().split('\n').slice(1).map(line => line.split('\t')));
    if (contract.teacher_status !== 'complete' || contract.acceptance_status !== 'complete')
        throw new Error('teacher and acceptance policy are incomplete');
    const rows = (await readFile(join(resolve(output), 'descriptor.tsv'), 'utf8')).trim().split('\n');
    if (rows[0] !== columns.join('\t') || rows.length !== 2) throw new Error('invalid task package descriptor');
    const values = Object.fromEntries(columns.map((name, index) => [name, rows[1].split('\t')[index]]));
    if (values.teacher_complete !== '1' || values.acceptance_complete !== '1') throw new Error('task descriptor remains incomplete');
    const scenarios = (await readFile(join(resolve(output), values.scenarios), 'utf8')).trim().split('\n').slice(1);
    const splits = new Set(scenarios.map(line => line.split('\t')[2]));
    if (!['training', 'development', 'test'].every(split => splits.has(split))) throw new Error('all independent splits are required');
    return { status: 'ready', task: values.name };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
    try {
        const args = process.argv.slice(2);
        if (args[0] === 'check') console.log((await checkGameplayScaffold(args[1])).status);
        else {
            const options = {};
            for (let index = 0; index < args.length; index += 2) {
                const mapping = { '--name': 'name', '--output': 'output', '--task-id': 'taskId', '--output-count': 'outputCount', '--feature-count': 'featureCount', '--task-features': 'taskFeatures' };
                const key = mapping[args[index]];
                if (!key || !args[index + 1] || Object.hasOwn(options, key)) throw new Error('invalid scaffold option');
                options[key] = ['name', 'output'].includes(key) ? args[index + 1] : Number(args[index + 1]);
            }
            const result = await scaffoldGameplayTask(options);
            console.log('scaffolded ' + result.name + ' at ' + result.output + '; teacher and acceptance gates remain closed');
        }
    } catch (error) { console.error('gameplay scaffold: ' + error.message); process.exitCode = 1; }
}
