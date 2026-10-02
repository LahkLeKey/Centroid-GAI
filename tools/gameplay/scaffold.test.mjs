import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { checkGameplayScaffold, scaffoldGameplayTask } from './scaffold.mjs';
import { parseTaskRegistry } from './workflow.mjs';

const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
async function fixture(t, options = {}) {
    const parent = await mkdtemp(join(tmpdir(), 'cgai-gameplay-scaffold-'));
    t.after(async () => {
        assert.ok(parent.startsWith(join(tmpdir(), 'cgai-gameplay-scaffold-')));
        await rm(parent, { recursive: true, force: true });
    });
    const output = join(parent, 'encounter');
    const result = await scaffoldGameplayTask({ name: 'encounter', output, ...options });
    return { parent, output, result };
}
test('third task scaffold supplies typed C11 source and closed authored acceptance', async t => {
    const { output, result } = await fixture(t);
    assert.equal(result.taskId, 2);
    assert.deepEqual(result.files.sort(), ['check.mjs', 'contract.tsv', 'descriptor.tsv', 'encounter-catalog.tsv',
        'encounter-scenarios.tsv', 'encounter_task.c', 'encounter_task.h']);
    assert.match(await readFile(join(output, 'encounter_task.h'), 'utf8'), /ENCOUNTER_TASK_ID 2U/);
    assert.match(await readFile(join(output, 'encounter_task.c'), 'utf8'), /candidate\.task = ENCOUNTER_TASK_ID/);
    await assert.rejects(checkGameplayScaffold(output), /teacher and acceptance policy are incomplete/);
    const checked = spawnSync(process.execPath, [join(output, 'check.mjs')], { encoding: 'utf8', windowsHide: true });
    assert.equal(checked.status, 1);
    assert.match(checked.stderr, /teacher and acceptance policy are incomplete/);
});
test('scaffolding never overwrites an existing task package', async t => {
    const { output } = await fixture(t);
    const original = await readFile(join(output, 'encounter_task.c'));
    await assert.rejects(scaffoldGameplayTask({ name: 'encounter', output }), { code: 'EEXIST' });
    assert.deepEqual(await readFile(join(output, 'encounter_task.c')), original);
});
test('invalid names and shape values fail before creating a destination', async () => {
    for (const options of [{ name: '../unsafe' }, { name: 'new-task' }, { name: 'task', taskId: 8 },
        { name: 'task', outputCount: 65 }, { name: 'task', featureCount: 17 }, { name: 'task', taskFeatures: 512 }])
        await assert.rejects(scaffoldGameplayTask({ output: 'unused-invalid-scaffold', ...options }), /invalid scaffold/);
    await assert.rejects(scaffoldGameplayTask({ name: 'task' }), /explicit scaffold output/);
});
test('registry accepts a complete third task without hardcoding task names', async t => {
    const { output } = await fixture(t);
    const original = await readFile(join(repository, 'data/gameplay/composed-v1/tasks.tsv'), 'utf8');
    const row = (await readFile(join(output, 'descriptor.tsv'), 'utf8')).split('\n')[1];
    await assert.rejects(async () => parseTaskRegistry(original + row + '\n'), /incomplete: encounter/);
    const cells = row.split('\t');
    cells[8] = cells[9] = '1';
    const tasks = parseTaskRegistry(original + cells.join('\t') + '\n');
    assert.equal(tasks.length, 3);
    assert.equal(tasks[2].name, 'encounter');
    assert.equal(tasks[2].output_count, 4);
    assert.equal(tasks[2].task_features, 511);
});
test('flag changes alone cannot pass a package without all frozen scenario splits', async t => {
    const { output } = await fixture(t);
    const contract = join(output, 'contract.tsv');
    await writeFile(contract, (await readFile(contract, 'utf8')).replaceAll('\tpending\n', '\tcomplete\n'));
    await assert.rejects(checkGameplayScaffold(output), /descriptor remains incomplete/);
    const generated = spawnSync(process.execPath, [join(output, 'check.mjs')], { encoding: 'utf8', windowsHide: true });
    assert.equal(generated.status, 1);
    assert.match(generated.stderr, /descriptor remains incomplete/);
    const descriptor = join(output, 'descriptor.tsv');
    const rows = (await readFile(descriptor, 'utf8')).trim().split('\n');
    const cells = rows[1].split('\t'); cells[8] = cells[9] = '1';
    await writeFile(descriptor, rows[0] + '\n' + cells.join('\t') + '\n');
    await assert.rejects(checkGameplayScaffold(output), /all independent splits/);
});

const compiler = process.env.CGAI_C11_COMPILER ?? 'clang';
const compilerProbe = spawnSync(compiler, ['--version'], { encoding: 'utf8', windowsHide: true });
test('generated adapter compiles as C11 and validates a configurable third head',
    { skip: compilerProbe.error || compilerProbe.status !== 0 }, async t => {
        const { parent, output } = await fixture(t);
        const harness = `#include "encounter_task.h"\n#include <assert.h>\n` +
            `int main(void) {\n    cgai_gameplay_config config = {0};\n    encounter_state state = {0};\n` +
            `    cgai_gameplay_example example = {0};\n    uint32_t target = 99;\n    size_t index;\n` +
            `    config.task_count = 3;\n    config.feature_count = 9;\n    config.output_counts[2] = 4;\n` +
            `    config.task_features[2] = 511;\n    for (index = 0; index < 9; ++index) config.cardinalities[index] = 2;\n` +
            `    assert(encounter_example(&config, &state, 3, &example));\n    assert(example.task == 2 && example.target == 3);\n` +
            `    assert(!encounter_teacher(&state, &target) && target == 99);\n    state.observations[0] = 2;\n` +
            `    assert(!encounter_example(&config, &state, 3, &example));\n    assert(example.task == 2 && example.target == 3);\n` +
            `    return 0;\n}\n`;
        const harnessPath = join(parent, 'adapter_test.c');
        const executable = join(parent, process.platform === 'win32' ? 'adapter_test.exe' : 'adapter_test');
        await writeFile(harnessPath, harness);
        const compiled = spawnSync(compiler, ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
            '-I' + join(repository, 'include'), '-I' + output, join(output, 'encounter_task.c'), harnessPath, '-o', executable],
        { encoding: 'utf8', windowsHide: true, timeout: 30000 });
        assert.ifError(compiled.error); assert.equal(compiled.status, 0, compiled.stderr);
        const checked = spawnSync(executable, [], { encoding: 'utf8', windowsHide: true, timeout: 30000 });
        assert.ifError(checked.error); assert.equal(checked.status, 0, checked.stderr);
    });
