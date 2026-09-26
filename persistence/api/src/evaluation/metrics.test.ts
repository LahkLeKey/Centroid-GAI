import assert from 'node:assert/strict';
import test from 'node:test';
import { asciiTokens, referenceModel, summarize } from './metrics.ts';

test('reference counts respect document boundaries, suffix backoff and deterministic lexical ties', () => {
    const predict = referenceModel(['alpha beta', 'gamma delta', 'alpha echo'], 3);
    assert.equal(predict('alpha')[0], 'beta');
    assert.equal(predict('never seen gamma')[0], 'delta');
    assert.equal(predict('beta')[0], '<eos>'); // Must not invent a beta -> gamma transition.
    assert.equal(referenceModel(['alpha beta alpha'], 0)('anything')[0], 'alpha');
    assert.deepEqual(asciiTokens("Next-token isn't CASE sensitive."), ['next', '-', 'token', "isn't", 'case', 'sensitive', '.']);
    assert.throws(() => asciiTokens('non-ascii: é'), /ASCII/);
});

test('accuracy handles missing targets, rank cutoff, and unscored unknown probes', () => {
    const probe = { id: 'a', domain: 'test', slice: 'challenge' as const, prompt: 'alpha', expected: 'beta' };
    const result = summarize([
        { probe, ranked: ['beta'], expectedInVocabulary: true, unknownTokens: 0, contextTokens: 1 },
        { probe, ranked: ['a', 'b', 'c', 'd', 'beta'], expectedInVocabulary: true },
        { probe, ranked: ['a', 'b', 'c', 'd', 'e', 'beta'], expectedInVocabulary: false },
        { probe: { id: 'b', domain: 'unknown', slice: 'unknown', prompt: 'alien' }, ranked: ['beta'], unknownTokens: 1, contextTokens: 1 },
    ]);
    assert.equal(result.scored, 3);
    assert.equal(result.top1, 1 / 3);
    assert.equal(result.top5, 2 / 3);
    assert.equal(result.mrr5, 1.2 / 3);
    assert.equal(result.targetCoverage, 2 / 3);
    assert.equal(result.contextUnknownRate, 0.5);
    assert.equal(summarize([]).top1, null);
});
