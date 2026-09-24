import assert from "node:assert/strict";
import test from "node:test";
import {createRequire} from "node:module";

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

test("native callbacks reject invalid arguments and remain usable after failures", () => {
    const require = createRequire(import.meta.url);
    const binding = require("../build/Release/centroid_gai_native.node");
    const model = trainNativeModel("native failures preserve model ownership.");
    assert.throws(() => binding.trainModel(), /training text/i);
    assert.throws(() => binding.trainModel(123), /UTF-8 string/i);
    assert.throws(() => binding.trainModel("text", {seed: -1n}), /configuration/i);
    assert.throws(() => binding.trainModel("text", {dimensions: "bad"}), /configuration/i);
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
