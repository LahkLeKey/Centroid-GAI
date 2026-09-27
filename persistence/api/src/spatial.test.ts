import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import test from 'node:test';
import { createNativeSpatialIndex } from './native.ts';
import { createNativeCentroidSearch } from './knowledge/native-spatial.ts';
import { buildSpatialIndex, exhaustive, nearest } from './knowledge/spatial.ts';
import type { SpatialPoint } from './knowledge/spatial.ts';

test('C, TypeScript tree, and exhaustive search agree for arbitrary queries, categories, and ties', () => {
    const points: SpatialPoint[] = Array.from({ length: 96 }, (_, row) => ({ id: `row-${String(row).padStart(3, '0')}`,
        category: row % 2 ? 'odd' : 'even', centroid: row, observations: '1',
        vector: [Math.floor(row / 2) * 10, (row % 3) * 0.1, 0] }));
    points[1]!.vector = [...points[0]!.vector];
    const native = createNativeCentroidSearch([...points].reverse());
    const reference = buildSpatialIndex(points);
    for (const vector of [[0, 0, 0], [235, 0.12, 0], [-5, 1, 9], ...points.map(point => point.vector)]) {
        for (const category of [undefined, 'even', 'odd', 'absent']) for (const limit of [1, 5, 100]) {
            const result = native.search(vector, limit, category, 'row-000');
            assert.deepEqual(result.neighbors, exhaustive(points, vector, limit, category, 'row-000'));
            assert.deepEqual(result.neighbors, nearest(reference, vector, limit, category, 'row-000').neighbors);
        }
    }
    assert.equal(native.search([0, 0, 0], 1).neighbors[0]?.id, 'row-000');
    assert(native.search([0, 0, 0], 5).comparisons < points.length);
});

test('native owner copies inputs and rejects malformed query shape and category/exclusion options', () => {
    const vectors = [[0, 0], [1, 1]];
    const categories = [0, 1];
    const index = createNativeSpatialIndex(vectors, categories);
    vectors[0]![0] = 100;
    categories[0] = 63;
    assert.equal(index.query([0, 0], 1, 0).neighbors[0]?.index, 0);
    assert.equal(index.query([0, 0], 1, 63).neighbors.length, 0);
    assert.throws(() => index.query([0], 1));
    assert.throws(() => index.query([NaN, 0], 1));
    assert.throws(() => index.query([1e101, 0], 1));
    assert.throws(() => index.query([0, 0], 101));
    assert.throws(() => index.query([0, 0], 1, 64));
    assert.throws(() => index.query([0, 0], 1, undefined, 2));
    assert.throws(() => createNativeSpatialIndex([[0], [0, 1]], [0, 0]));
});

test('raw Node boundary validates handle branding, typed arrays, offsets, and native finite checks', () => {
    const binding = createRequire(import.meta.url)('../build/Release/centroid_gai_native.node');
    const vectors = new Float64Array([999, 0, 0, 1, 1, 999]).subarray(1, 5);
    const categories = new Uint32Array([999, 0, 1, 999]).subarray(1, 3);
    const owner = binding.createSpatialIndex(vectors, categories);
    const options = new Uint32Array([1, 0xffffffff, 0xffffffff]);
    assert.equal(JSON.parse(binding.querySpatialIndex(owner, new Float64Array([0, 0]), options)).neighbors[0].index, 0);
    assert.throws(() => binding.querySpatialIndex({}, new Float64Array([0, 0]), options), /handle/);
    assert.throws(() => binding.querySpatialIndex(owner, new Float64Array([0]), options), /dimension/);
    assert.throws(() => binding.querySpatialIndex(owner, new Float64Array([0, 0]), new Uint32Array([1])), /three/);
    assert.throws(() => binding.createSpatialIndex([0, 0], new Uint32Array([0])), /typed/);
    assert.throws(() => binding.createSpatialIndex(new Float32Array([0]), new Uint32Array([0])), /typed/);
    assert.throws(() => binding.createSpatialIndex(new Float64Array([Infinity]), new Uint32Array([0])), /coordinate/);
    assert.throws(() => binding.createSpatialIndex(new Float64Array([0]), new Uint32Array([64])), /category/);
    assert.throws(() => binding.createSpatialIndex(new Float64Array(new SharedArrayBuffer(8)), new Uint32Array([0])), /shared/);
});
