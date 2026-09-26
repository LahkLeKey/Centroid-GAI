import assert from "node:assert/strict";
import { createRequire } from "node:module";
import test from "node:test";

import {
    generateNativeModel,
    inspectNativeModel,
    inspectNativeContents,
    mergeNativeModels,
    matchNativePatterns,
    nativePersistenceSchema,
    trainNativeModel,
} from "./native.ts";

test('pattern matching ranks actual contexts deterministically without changing the artifact', () => {
    const model = trainNativeModel('alpha beta alpha beta', { dimensions: 32, centroidCount: 8, contextWindow: 1, seed: 42n });
    const before = Buffer.from(model);
    const result = matchNativePatterns(model, 'ignored alpha', 10);
    assert.equal(result.inputTokens, 2);
    assert.deepEqual(result.context, [{ token: 'alpha', known: true }]);
    assert.equal(result.unknownTokens, 0); // Only the used suffix contributes.
    assert.equal(result.matches.length, 5); // Unused reserved rows are excluded.
    assert.deepEqual(result.matches.slice(0, 2).map((match) => match.centroidId), [1, 3]);
    for (const match of result.matches.slice(0, 2)) {
        assert.equal(match.squaredDistance, 0);
        assert.equal(match.targets.items[0]?.token, 'beta');
        assert.equal(match.observations, '1');
    }
    for (let i = 1; i < result.matches.length; i++) assert.ok(result.matches[i]!.squaredDistance >= result.matches[i - 1]!.squaredDistance);
    assert.deepEqual(matchNativePatterns(model, 'alpha', 1).matches, result.matches.slice(0, 1));
    assert.deepEqual(matchNativePatterns(model, 'ignored alpha', 10), result);
    assert.deepEqual(model, before);
    const padded = Buffer.concat([Buffer.from('prefix'), model, Buffer.from('suffix')]);
    assert.deepEqual(matchNativePatterns(padded.subarray(6, 6 + model.length), 'ignored alpha', 10), result);
    const unknown = matchNativePatterns(model, 'notinvocabulary');
    assert.equal(unknown.unknownTokens, 1);
    assert.deepEqual(unknown.context, [{ token: 'notinvocabulary', known: false }]);
    assert.equal(unknown.matches.length, 5);
    for (const text of ['', '   ', 'alpha\0beta', 'x'.repeat(16385)]) assert.throws(() => matchNativePatterns(model, text), /Matching text/);
    for (const limit of [0, 11, -1, 1.5, NaN]) assert.throws(() => matchNativePatterns(model, 'alpha', limit));
    const other = trainNativeModel('gamma delta', { dimensions: 32, centroidCount: 4, contextWindow: 1, seed: 42n });
    const merged = mergeNativeModels([model, other]);
    const match = matchNativePatterns(merged, 'gamma', 1).matches[0]!;
    assert.equal(match.squaredDistance, 0);
    assert.equal(match.targets.items[0]?.token, 'delta');
});

test('native contents are paged, lossless, and centroid distributions match observations', () => {
    const model = trainNativeModel('alpha beta alpha "quoted" \\ paths.', { dimensions: 8, centroidCount: 3, seed: 99n });
    const summary = inspectNativeContents(model, 'summary');
    assert.equal(summary.seed, '99');
    assert.equal(summary.initializedCentroids, 3);
    const vocabulary = inspectNativeContents(model, 'vocabulary', 0, 100);
    const highlights = inspectNativeContents(model, 'highlights', 0, 3);
    const expected = vocabulary.items.filter((item) => item.id >= 3).sort((a, b) => BigInt(a.count) === BigInt(b.count) ? a.id - b.id : BigInt(a.count) > BigInt(b.count) ? -1 : 1);
    assert.deepEqual(highlights.tokens.items, expected.slice(0, 3));
    assert.equal(highlights.tokens.total, expected.length);
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
