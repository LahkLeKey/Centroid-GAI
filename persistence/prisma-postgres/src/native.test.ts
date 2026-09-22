import assert from "node:assert/strict";
import test from "node:test";

import {
    generateNativeModel,
    inspectNativeModel,
    nativePersistenceSchema,
    trainNativeModel,
} from "./native.ts";

test("native C ABI trains, inspects, and generates", () => {
    const model = trainNativeModel("native postgres models persist. native models generate.", {
        dimensions : 12,
        centroidCount : 4,
        contextWindow : 2,
        seed : 42n,
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
