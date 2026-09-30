import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {cpSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';

const compiler = fileURLToPath(new URL('./compile-native-knowledge.ts', import.meta.url));
const marker = '/* Curated static knowledge baseline; maintain these C sources directly. */';
const releaseHash = 'a'.repeat(64);

function compile(source: string, output: string, topPerCategory?: number) {
    return spawnSync(
        process.execPath,
        [
            compiler, '--source-c', source, '--output', output,
            ...(topPerCategory === undefined ? []
                                             : [ '--top-per-category', String(topPerCategory) ])
        ],
        {encoding : 'utf8'});
}

function snapshot(directory: string): Record<string, string> {
    const result: Record<string, string> = {};
    for (const entry of readdirSync(directory, {withFileTypes : true})) {
        const path = join(directory, entry.name);
        if (entry.isDirectory()) {
            for (const [name, contents] of Object.entries(snapshot(path)))
                result[`${entry.name}/${name}`] = contents;
        } else {
            result[entry.name] = readFileSync(path, 'utf8');
        }
    }
    return result;
}

test('checked-in centroid C implementations regenerate byte-for-byte', t => {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-regenerate-'));
    t.after(() => rmSync(directory, {recursive : true, force : true}));
    const sourceDirectory = fileURLToPath(new URL('../../../../src/knowledge/', import.meta.url));
    const output = join(directory, 'knowledge_catalog.c');
    const result = compile(join(sourceDirectory, 'knowledge_catalog.c'), output);
    assert.equal(result.status, 0, result.stderr);
    for (const name of ['knowledge_catalog.c', 'knowledge_catalog.h'])
        assert.equal(readFileSync(join(directory, name), 'utf8'),
                     readFileSync(join(sourceDirectory, name), 'utf8').replaceAll('\r\n', '\n'));
    const expected = snapshot(join(sourceDirectory, 'knowledge_catalog'));
    for (const name of Object.keys(expected))
        expected[name] = expected[name]!.replaceAll('\r\n', '\n');
    assert.deepEqual(snapshot(join(directory, 'knowledge_catalog')), expected);
});

test('reject duplicate and out-of-range sparse C coordinates', t => {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-sparse-'));
    t.after(() => rmSync(directory, {recursive : true, force : true}));
    const sourceDirectory = fileURLToPath(new URL('../../../../src/knowledge/', import.meta.url));
    const source = join(directory, 'knowledge_catalog.c');
    cpSync(join(sourceDirectory, 'knowledge_catalog.c'), source);
    cpSync(join(sourceDirectory, 'knowledge_catalog'), join(directory, 'knowledge_catalog'),
           {recursive : true});
    const category = join(directory, 'knowledge_catalog/00-build-and-configuration.c');
    const original = readFileSync(category, 'utf8');
    for (const axis of [0, 32]) {
        writeFileSync(category, original.replace('[0 /* configuration */] = 1.0F,',
            `[0 /* configuration */] = 1.0F, [${axis}] = 0.5F,`));
        const result = compile(source, join(directory, 'output/knowledge_catalog.c'));
        assert.notEqual(result.status, 0);
        assert.match(result.stderr, /Invalid vector component/);
    }
});

for (const legacyFragments of [false, true]) {
    test(
        `migrate ${
            legacyFragments ? 'include fragments' : 'monolithic C'} and regenerate losslessly`,
        t => {
            const directory = mkdtempSync(join(tmpdir(), 'cgai-compile-'));
            t.after(() => rmSync(directory, {recursive : true, force : true}));
            const output = join(directory, 'knowledge_catalog.c');
            const categoryDirectory = join(directory, 'knowledge_catalog');
            const legacyCategoryDirectory = join(directory, 'static_knowledge_data');
            mkdirSync(categoryDirectory);
            if (legacyFragments)
                mkdirSync(legacyCategoryDirectory);
            const values = [ 0, 1, -0.125, 1e-20, ...Array<number>(28).fill(0.25) ];
            const vector =
                values.map(value => `${value === 0 || value === 1 ? `${value}.0` : value}F`)
                    .join(', ');
            const otherValues = values.map((value, index) => index === 0 ? 0.5 : value);
            const otherVector =
                otherValues.map(value => `${value === 0 || value === 1 ? `${value}.0` : value}F`)
                    .join(', ');
            const thirdValues = values.map((value, index) => index === 0 ? -0.5 : value);
            const thirdVector =
                thirdValues.map(value => `${value === 0 || value === 1 ? `${value}.0` : value}F`)
                    .join(', ');
            // Category order differs from stable-ID order to exercise the global row registry.
            const rows = [
                `    {"build-000:0000", 1U, UINT64_C(18446744073709551615), "Build label with \\"quotes\\"", {${
                    vector}}},`,
                `    {"build-000:0001", 1U, UINT64_C(11), "Lower build label", {${thirdVector}}},`,
                `    {"web-000:0000", 0U, UINT64_C(2), "Web label", {${otherVector}}},`
            ];
            if (legacyFragments) {
                writeFileSync(join(legacyCategoryDirectory, 'category-01.inc'),
                              `${marker}\n${rows[0]}\n${rows[1]}\n`);
                writeFileSync(join(legacyCategoryDirectory, 'category-00.inc'),
                              `${marker}\n${rows[2]}\n`);
            }
            writeFileSync(output, [
            marker,
            'const char *const cgai_static_knowledge_categories[] = {',
            '    "Web Application", "Build and Configuration",', '};',
            'const char *const cgai_static_knowledge_category_descriptions[] = {',
            '    "Web description", "Build description",', '};',
            'const cgai_static_knowledge_centroid cgai_static_knowledge_data[] = {',
            ...(legacyFragments ? [
                '#include "static_knowledge_data/category-01.inc"',
                '#include "static_knowledge_data/category-00.inc"'
            ] : rows), '};',
            'const size_t cgai_static_knowledge_data_count = 3U;',
            'const size_t cgai_static_knowledge_dimensions = 32U;',
            `const char cgai_static_knowledge_release_sha256_value[] = "${releaseHash}";`, ''
        ].join('\n'));

            const migrated = compile(output, output);
            assert.equal(migrated.status, 0, migrated.stderr);
            assert.deepEqual(readdirSync(categoryDirectory).sort(),
                             [ '00-web-application.c', '01-build-and-configuration.c' ]);
            const registry = readFileSync(output, 'utf8');
            assert(registry.includes('{0U, 2U, 1U, 0U}'));
            assert(registry.includes('{2U, 1U, 0U, 0U}'));
            assert(!registry.includes('*const cgai_static_knowledge_data[]'));
            assert(registry.includes(releaseHash));
            const build =
                readFileSync(join(categoryDirectory, '01-build-and-configuration.c'), 'utf8');
            assert(build.includes('UINT64_C(18446744073709551615)'));
            assert(build.includes('Build label with \\"quotes\\"'));
            assert(build.includes('static const cgai_static_knowledge_centroid centroids[]'));
            assert(build.includes('const cgai_knowledge_module *cgai_knowledge_build(void)'));
            const generatedVector =
                build
                    .match(
                        /\.vector = (?:\(const float\[CGAI_STATIC_KNOWLEDGE_DIMENSIONS\]\))?\{([^{}]+)\}/)!
                        [1]!.split(',')
                    .map(value => Number(value.trim().slice(0, -1)));
            assert.deepEqual(generatedVector,
                             values.map(value => Number(Math.fround(value).toPrecision(9))));
            const capped = compile(output, output, 1);
            assert.equal(capped.status, 0, capped.stderr);
            assert.match(capped.stdout, /"points":2/);
            const cappedBuild =
                readFileSync(join(categoryDirectory, '01-build-and-configuration.c'), 'utf8');
            assert(cappedBuild.includes('build-000:0000'));
            assert(!cappedBuild.includes('build-000:0001'));
            const before = snapshot(directory);
            const regenerated = compile(output, output);
            assert.equal(regenerated.status, 0, regenerated.stderr);
            assert.deepEqual(snapshot(directory), before);

            const categoryPath = join(categoryDirectory, '00-web-application.c');
            writeFileSync(categoryPath,
                          readFileSync(categoryPath, 'utf8').replace(marker, '/* Hand edited */'));
            const protectedFiles = snapshot(directory);
            const rejected = compile(output, output);
            assert.notEqual(rejected.status, 0);
            assert.match(rejected.stderr, /Refusing to overwrite non-generated category file/);
            assert.deepEqual(snapshot(directory), protectedFiles);
        });
}
