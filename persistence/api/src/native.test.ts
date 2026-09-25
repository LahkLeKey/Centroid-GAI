import assert from "node:assert/strict";
import { createRequire } from "node:module";
import test from "node:test";

import {
    generateNativeModel,
    inspectNativeModel,
    inspectNativeContents,
    mergeNativeModels,
    nativePersistenceSchema,
    trainNativeModel,
} from "./native.ts";

test('native contents are paged, lossless, and centroid distributions match observations', () => {
    const model = trainNativeModel('alpha beta alpha "quoted" \\ paths.', { dimensions: 8, centroidCount: 3, seed: 99n });
    const summary = inspectNativeContents(model, 'summary');
    assert.equal(summary.seed, '99');
    assert.equal(summary.initializedCentroids, 3);
    const vocabulary = inspectNativeContents(model, 'vocabulary', 0, 100);
    assert.equal(vocabulary.items.length, vocabulary.total);
    assert.equal(vocabulary.items.reduce((sum, item) => sum + BigInt(item.count), 0n), BigInt(summary.examplesSeen));
    const second = inspectNativeContents(model, 'vocabulary', 2, 2);
    assert.deepEqual(second.items, vocabulary.items.slice(2, 4));
    assert.deepEqual(inspectNativeContents(model, 'vocabulary', 1000).items, []);
    const centroids = inspectNativeContents(model, 'centroids');
    for (const centroid of centroids.items) {
        const detail = inspectNativeContents(model, 'centroid', 0, 100, centroid.id);
        assert.equal(detail.vector.length, 8);
        assert.equal(detail.tokens.items.reduce((sum, item) => sum + BigInt(item.count), 0n), BigInt(detail.observations));
        assert.equal(detail.tokens.total, centroid.distinctTargets);
        assert.ok(detail.vector.every(Number.isFinite));
    }
    assert.throws(() => inspectNativeContents(model, 'centroid', 0, 25, 99), /centroid/);
    assert.throws(() => inspectNativeContents(model, 'summary', 0, 101), /page/);
});

test('native merges preserve counts, support compaction, and reject incompatible seeds', () => {
    const a = trainNativeModel('alpha shared alpha', { dimensions: 8, centroidCount: 2, seed: 42n });
    const b = trainNativeModel('beta shared beta beta', { dimensions: 8, centroidCount: 3, seed: 42n });
    const copy = Buffer.from(a);
    const merged = mergeNativeModels([a, b]);
    const compact = mergeNativeModels([a, b], 1);
    assert.equal(inspectNativeContents(merged, 'summary').initializedCentroids, 5);
    assert.equal(inspectNativeContents(compact, 'summary').initializedCentroids, 1);
    assert.equal(inspectNativeModel(compact).examplesSeen, inspectNativeModel(a).examplesSeen + inspectNativeModel(b).examplesSeen);
    assert.deepEqual(inspectNativeContents(merged, 'vocabulary'), inspectNativeContents(compact, 'vocabulary'));
    assert.deepEqual(a, copy);
    assert.deepEqual(mergeNativeModels([a, b], 1), compact);
    assert.equal(typeof generateNativeModel(compact, 'shared', 10, 0), 'string');
    const incompatible = trainNativeModel('seed differs', { dimensions: 8, seed: 43n });
    assert.throws(() => mergeNativeModels([a, incompatible]), /seed/);
    assert.throws(() => mergeNativeModels([a, b], 6), /target/);
    assert.throws(() => mergeNativeModels([]), /1 to 32/);
    assert.throws(() => mergeNativeModels([a], -1), /unsigned/);
});

test("native C ABI trains, inspects, and generates", () => {
    const model = trainNativeModel("native postgres models persist. native models generate.", {
        dimensions: 12,
        centroidCount: 4,
        contextWindow: 2,
        seed: 42n,
    });
    const metadata = inspectNativeModel(model);

    assert.equal(metadata.formatVersion, 1);
    assert.equal(metadata.dimensions, 12);
    assert.ok(metadata.examplesSeen > 0n);
    assert.equal(typeof generateNativeModel(model, "native", 8, 0, 42n), "string");
    assert.equal(typeof nativePersistenceSchema, "object");
});

test("native training accepts the default configuration", () => {
    const model = trainNativeModel("default configuration remains usable.");
    assert.ok(inspectNativeModel(model).examplesSeen > 0n);
});

test("native generation is deterministic for a fixed seed", () => {
    const model = trainNativeModel("deterministic generation uses the supplied seed.");
    const first = generateNativeModel(model, "deterministic", 8, 0.7, 99n);
    const second = generateNativeModel(model, "deterministic", 8, 0.7, 99n);
    assert.equal(first, second);
});

test("native inspection rejects invalid model bytes", () => {
    assert.throws(
        () => inspectNativeModel(Buffer.from("not a Centroid-GAI model")),
        /invalid|unsupported|model/i,
    );
});

test("native callbacks reject invalid arguments and remain usable after failures", () => {
    const require = createRequire(import.meta.url);
    const binding = require("../build/Release/centroid_gai_native.node");
    const model = trainNativeModel("native failures preserve model ownership.");
    assert.throws(() => binding.trainModel(), /training text/i);
    assert.throws(() => binding.trainModel(123), /UTF-8 string/i);
    assert.throws(() => binding.trainModel("text", { seed: -1n }), /configuration/i);
    assert.throws(() => binding.trainModel("text", { dimensions: "bad" }), /configuration/i);
    assert.throws(() => binding.inspectModel(), /model Buffer/i);
    assert.throws(() => binding.inspectModel("bad"), /model Buffer/i);
    assert.throws(() => binding.generateModel(model), /requires/i);
    assert.throws(() => binding.generateModel(model, 123, 8, 0), /UTF-8 string/i);
    assert.throws(() => binding.generateModel(model, "native", 1000001, 0), /arguments/i);
    assert.throws(() => binding.generateModel(model, "native", 8, 0, -1n), /arguments/i);
    assert.throws(() => binding.generateModel(model, "native", 8, NaN), /arguments/i);
    assert.equal(binding.generateModel(model, "native", 0, 0), "");
    assert.equal(typeof binding.generateModel(model, "native", 8, 0), "string");
});
