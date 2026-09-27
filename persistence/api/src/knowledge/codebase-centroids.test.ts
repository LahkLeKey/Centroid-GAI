import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, relative } from 'node:path';
import test from 'node:test';
import { gzipSync, gunzipSync } from 'node:zlib';
import { sha256 } from '../evaluation/codebase-data.ts';
import type { Document } from '../evaluation/codebase-data.ts';
import { prepareCodebase, sourceCategory } from './codebase-corpus.ts';
import { buildCodebaseRelease, loadCodebaseSpatial, queryCodebaseRelease, verifyCodebaseRelease } from './codebase-release.ts';
import { buildSpatialIndex, exhaustive, nearest } from './spatial.ts';
import type { SpatialPoint } from './spatial.ts';

const commit = 'a'.repeat(40);
function document(text: string, paths: string[]): Document {
    return { text, sha256: sha256(text), sources: paths.map(path => ({ path, commit, blob: sha256(path) })) };
}

test('global exact deduplication trains a chunk once across documents, source aliases, and categories', () => {
    const common = Array.from({ length: 32 }, (_, line) => `shared declaration line ${line}`).join('\n');
    const docs = [document(`${common}\ncore implementation`, ['src/core.c', 'tests/core.c']),
        document(`${common}\npublic declaration`, ['include/core.h'])];
    const prepared = prepareCodebase(docs);
    assert.equal(prepared.statistics.inputChunks, 6);
    assert.equal(prepared.statistics.uniqueChunks, 3);
    assert.equal(prepared.statistics.duplicatesRemoved, 3);
    const chunks = prepared.shards.flatMap(shard => shard.chunks);
    assert.equal(new Set(chunks.map(chunk => chunk.sha256)).size, 3);
    const shared = chunks.find(chunk => chunk.text === common)!;
    assert.equal(shared.sources.length, 3);
    assert.deepEqual(shared.categories, ['native-core', 'native-tests', 'public-api']);
    assert.equal(shared.category, 'native-core');
    assert(shared.sources.every(source => source.startLine === 1 && source.endLine === 32));
    assert.deepEqual(prepared, prepareCodebase([...docs].reverse()));
    assert.equal(sourceCategory('persistence/api/README.md'), 'documentation');
    assert.equal(sourceCategory('persistence/api/native/addon.c'), 'native-bridge');
    assert.equal(sourceCategory('persistence/db/src/client.ts'), 'database');
    assert.throws(() => prepareCodebase([document('x'.repeat(128 * 1024 + 1), ['src/large.c'])]), /budget/);
});

test('spatial pruning agrees with exhaustive queries including ties, category filters, and arbitrary vectors', () => {
    const points: SpatialPoint[] = Array.from({ length: 96 }, (_, index) => ({
        id: `p${String(index).padStart(3, '0')}`, category: index < 48 ? 'left' : 'right', centroid: index, observations: '1',
        vector: [Math.floor(index / 2) * 10, (index % 3) * 0.1, 0],
    }));
    points[1]!.vector = [...points[0]!.vector];
    const index = buildSpatialIndex([...points].reverse());
    assert.deepEqual(index, buildSpatialIndex(points));
    for (const vector of [[0, 0, 0], [255, 0.2, 0], [-12, 4, 1], ...points.map(point => point.vector)]) {
        for (const category of [undefined, 'left', 'right', 'absent']) {
            for (const limit of [1, 5, 100]) {
                assert.deepEqual(nearest(index, vector, limit, category, 'p000').neighbors,
                    exhaustive(points, vector, limit, category, 'p000'));
            }
        }
    }
    assert.equal(nearest(index, [0, 0, 0], 1).neighbors[0]?.id, 'p000');
    assert(nearest(index, [0, 0, 0], 5).comparisons < points.length);
    assert.throws(() => nearest(index, [0, 0], 5), /Invalid/);
    assert.throws(() => nearest(index, [NaN, 0, 0], 5), /Invalid/);
    assert.throws(() => nearest(index, [0, 0, 0], 0), /Invalid/);
    assert.throws(() => buildSpatialIndex([points[0]!, points[0]!]), /unique/);
});

/** Real native training fixture: no network/database and no dependence on this checkout's Git contents. */
function fixture(directory: string) {
    const shared = Array.from({ length: 32 }, (_, line) => `shared implementation token ${line}`).join('\n');
    const documents = [document(`${shared}\nCore implementation returns success.`, ['src/core.c']),
        document(`${shared}\nPublic declaration accepts caller memory.`, ['include/api.h'])];
    mkdirSync(directory);
    const content = { 'train.txt': documents.map(doc => `${doc.text}\n\n`).join(''),
        'train.jsonl': documents.map(doc => `${JSON.stringify(doc)}\n`).join(''),
        'validation.txt': '', 'validation.jsonl': '', 'SOURCE_LICENSE.txt': 'MIT\n' };
    for (const [name, text] of Object.entries(content)) writeFileSync(join(directory, name), text);
    writeFileSync(join(directory, 'manifest.json'), JSON.stringify({ schema_version: 1, source: { commit },
        documents: { train: documents.length, validation: 0 }, rejected: [],
        entries: documents.flatMap(doc => doc.sources.map(source => ({ path: source.path, oid: source.blob }))),
        files: Object.fromEntries(Object.entries(content).map(([name, text]) => [name, sha256(text)])) }));
}

test('static native releases reproduce bytes, verify sources and vectors, and reject corrupted or rehashed metadata', () => {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-static-'));
    try {
        const snapshot = join(directory, 'snapshot');
        fixture(snapshot);
        const output = join(directory, 'release');
        const summary = buildCodebaseRelease(snapshot, output);
        assert.equal(summary.duplicatesRemoved, 1);
        assert.equal(summary.categories, 2);
        assert.equal(summary.spatial.exactAgreement, true);
        const release = verifyCodebaseRelease(output);
        assert.deepEqual(loadCodebaseSpatial(output), { index: release.index, graph: release.graph });
        const point = release.index.points[0]!;
        assert.equal(queryCodebaseRelease(release, point.id).cached, true);
        assert.deepEqual(queryCodebaseRelease(release, point.id).neighbors,
            exhaustive(release.index.points, point.vector, 5, undefined, point.id));
        assert.equal(queryCodebaseRelease(release, point.id, 8).cached, false);
        assert.deepEqual(queryCodebaseRelease(release, point.id, 8).neighbors,
            exhaustive(release.index.points, point.vector, 8, undefined, point.id));
        assert.throws(() => buildCodebaseRelease(snapshot, output), /new directory/);
        const repeat = join(directory, 'repeat');
        buildCodebaseRelease(snapshot, repeat);
        for (const entry of readdirSync(output, { recursive: true, withFileTypes: true }).filter(item => item.isFile())) {
            const path = join(entry.parentPath, entry.name);
            assert.deepEqual(readFileSync(path), readFileSync(join(repeat, relative(output, path))));
        }
        const spatialPath = join(output, 'spatial.json');
        const original = readFileSync(spatialPath);
        writeFileSync(spatialPath, 'corrupt');
        assert.throws(() => verifyCodebaseRelease(output), /mismatch/);
        assert.throws(() => loadCodebaseSpatial(output), /mismatch/);
        writeFileSync(spatialPath, original);
        const manifestPath = join(output, 'manifest.json');
        const manifestBytes = readFileSync(manifestPath);
        const manifest = JSON.parse(manifestBytes.toString());
        const altered = JSON.parse(original.toString());
        altered.points[0].vector[0] += 0.5;
        const alteredBytes = Buffer.from(JSON.stringify(altered) + '\n');
        writeFileSync(spatialPath, alteredBytes);
        const entry = manifest.files.find((file: { path: string }) => file.path === 'spatial.json');
        entry.bytes = alteredBytes.length;
        entry.sha256 = sha256(alteredBytes);
        writeFileSync(manifestPath, JSON.stringify(manifest));
        assert.throws(() => verifyCodebaseRelease(output));
        writeFileSync(spatialPath, original);
        writeFileSync(manifestPath, manifestBytes);
        // Correct checksums alone cannot hide broken source line provenance.
        const sourcePath = join(output, manifest.shards[0].source);
        const chunks = gunzipSync(readFileSync(sourcePath)).toString('utf8').trim().split('\n').map(line => JSON.parse(line));
        chunks[0].sources[0].startLine = 0;
        const badSource = gzipSync(chunks.map(chunk => JSON.stringify(chunk) + '\n').join(''));
        writeFileSync(sourcePath, badSource);
        const cleanManifest = JSON.parse(manifestBytes.toString());
        const sourceEntry = cleanManifest.files.find((file: { path: string }) => file.path === manifest.shards[0].source);
        sourceEntry.bytes = badSource.length;
        sourceEntry.sha256 = sha256(badSource);
        writeFileSync(manifestPath, JSON.stringify(cleanManifest));
        assert.throws(() => verifyCodebaseRelease(output));
    } finally { rmSync(directory, { recursive: true, force: true }); }
});
