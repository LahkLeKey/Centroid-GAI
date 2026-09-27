/** Offline repeat-training and held-out next-token diagnostics for a committed repository snapshot. */
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { arch, endianness } from 'node:os';
import { dirname, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { generateNativeModel, inspectNativeContents, inspectNativeModel, matchNativePatterns, trainNativeModel } from '../native.ts';
import { cLocaleTokens, documentProbes, loadSnapshot, sha256 } from './codebase-data.ts';
import { referenceModel, summarize } from './metrics.ts';

const { values } = parseArgs({ options: { snapshot: { type: 'string' }, output: { type: 'string' } } });
if (!values.snapshot || !values.output) throw new Error('Usage: node codebase.ts --snapshot <directory> --output <new-directory>');
const snapshot = loadSnapshot(resolve(values.snapshot));
const output = resolve(values.output);
const config = { dimensions: 32, contextWindow: 3, seed: 42n };
const probes = [...documentProbes(snapshot.train, 'recall'), ...documentProbes(snapshot.validation, 'challenge')];
assert(probes.some(probe => probe.slice === 'challenge'), 'Need held-out word probes');
const tokens = cLocaleTokens(snapshot.text);
const vocabulary = new Set(tokens);
const expectedExamples = BigInt(tokens.length + 1); // One training call, including its final EOS.
const trainingContexts = new Set(tokens.slice(config.contextWindow).map((target, offset) =>
    JSON.stringify([...tokens.slice(offset, offset + config.contextWindow), target])));
const score = (rows: Parameters<typeof summarize>[0]) => ({
    recall: summarize(rows.filter(row => row.probe.slice === 'recall')),
    heldOut: summarize(rows.filter(row => row.probe.slice === 'challenge')),
    heldOutNovelTransitions: summarize(rows.filter(row => row.probe.slice === 'challenge' &&
        !trainingContexts.has(JSON.stringify([...cLocaleTokens(row.probe.prompt), row.probe.expected])))),
});

// Refuse to overwrite a prior run; artifacts and report remain paired for review.
mkdirSync(dirname(output), { recursive: true });
mkdirSync(output, { recursive: false });
const models = [24, 72].map(centroidCount => {
    const settings = { ...config, centroidCount };
    const payload = trainNativeModel(snapshot.text, settings);
    assert(payload.equals(trainNativeModel(snapshot.text, settings)), 'Repeat training differs');
    const metadata = inspectNativeModel(payload);
    assert.equal(metadata.examplesSeen, expectedExamples, 'Native tokenizer/training count mismatch');
    assert.equal(metadata.vocabularySize, BigInt(vocabulary.size + 3), 'Native vocabulary mismatch');
    const summary = inspectNativeContents(payload, 'summary');
    let observations = 0n;
    for (let offset = 0; offset < summary.initializedCentroids; offset += 100) {
        for (const centroid of inspectNativeContents(payload, 'centroids', offset, 100).items) {
            assert(Number.isFinite(centroid.norm), 'Nonfinite centroid');
            observations += BigInt(centroid.observations);
        }
    }
    assert.equal(observations, expectedExamples, 'Centroid observation totals differ');
    const artifact = `codebase-${centroidCount}.cgai`;
    writeFileSync(resolve(output, artifact), payload, { flag: 'wx' });
    const reloaded = readFileSync(resolve(output, artifact));
    assert(payload.equals(reloaded), 'Artifact reload differs');
    const checksumSha256 = sha256(payload);
    const rows = probes.map(probe => {
        const result = matchNativePatterns(reloaded, probe.prompt, 1);
        assert(result.matches.every(match => Number.isFinite(match.squaredDistance)), 'Nonfinite distance');
        return { probe, ranked: result.matches[0]?.targets.items.slice(0, 5).map(item => item.token) ?? [],
            expectedInVocabulary: vocabulary.has(probe.expected), unknownTokens: result.unknownTokens,
            contextTokens: result.context.length };
    });
    const generations = ['How do I train a model?', 'Where is the tokenizer implemented?',
        'How are model artifacts verified?'].map(prompt => {
        const continuation = generateNativeModel(reloaded, prompt, 32, 0, 42n);
        assert.equal(continuation, generateNativeModel(payload, prompt, 32, 0, 42n), 'Generation repeat/reload differs');
        return { prompt, continuation };
    });
    assert.equal(sha256(reloaded), checksumSha256, 'Inference mutated model');
    console.log(`Verified ${centroidCount} centroids: ${metadata.examplesSeen} examples, ${rows.length} probes`);
    return { name: `codebase-${centroidCount}`, settings, artifact, checksumSha256, artifactBytes: payload.length,
        metadata, summary, integrityPassed: true, scores: score(rows), rows, generations };
});
const references = [0, 3].map(window => {
    // Match the native concatenated corpus and its single EOS boundary exactly.
    const predict = referenceModel([snapshot.text], window, cLocaleTokens);
    const rows = probes.map(probe => ({ probe, ranked: predict(probe.prompt).slice(0, 5),
        expectedInVocabulary: vocabulary.has(probe.expected) }));
    return { name: window ? 'backoff-3' : 'unigram', scores: score(rows), rows };
});
const report = { version: 1, createdAt: new Date().toISOString(),
    purpose: 'Training integrity and next-token diagnostics; does not establish question answering or source retrieval.',
    sourceCommit: snapshot.manifest.source.commit, snapshotManifestSha256: snapshot.manifestSha256,
    corpusSha256: sha256(snapshot.text), documents: snapshot.manifest.documents, rejected: snapshot.manifest.rejected,
    environment: { node: process.version, architecture: arch(), byteOrder: endianness(),
        nativeAddonSha256: sha256(readFileSync(new URL('../../build/Release/centroid_gai_native.node', import.meta.url))) },
    evaluatorSha256: Object.fromEntries(['codebase.ts', 'codebase-data.ts', 'metrics.ts'].map(name =>
        [name, sha256(readFileSync(new URL(name, import.meta.url)))])),
    probePolicy: 'Up to 8 evenly spaced word targets per document; 3 preceding tokens; punctuation targets excluded.',
    splitPolicy: 'Exact document hashes disjoint; novel-transition slice excludes context+target sequences seen in training. Near-duplicate files may remain.',
    models, references };
writeFileSync(resolve(output, 'report.json'), JSON.stringify(report, (_, value: unknown) =>
    typeof value === 'bigint' ? value.toString() : value, 2) + '\n', { flag: 'wx' });
console.table([...models, ...references].map(model => ({ model: model.name,
    recallTop1: model.scores.recall.top1, heldOutTop1: model.scores.heldOut.top1,
    heldOutTop5: model.scores.heldOut.top5, novelTop1: model.scores.heldOutNovelTransitions.top1 })));
console.log(`Report: ${resolve(output, 'report.json')}`);
