import assert from 'node:assert/strict';
import test from 'node:test';
import type { FactualDialogueDataset, FactualDialogueRecord } from '../chat/dataset.ts';
import { validateTrainingPreflight } from './preflight.ts';

function record(id: string, content = 'known'): FactualDialogueRecord {
    return { id, family: id, category: 'direct', sources: [], answer: content, messages: [{ role: 'user', content }],
        expected: 'answer', acceptedAnswers: [content], requiredClaims: [content], evidence: [] };
}
function dataset(train = record('train')): FactualDialogueDataset {
    return { version: 2, purpose: 'synthetic-engineering', description: 'Preflight unit fixture', sourceDocuments: [],
        train: [train], development: [record('development', 'novel')], test: [record('test', 'unseen')] };
}

test('preflight counts native controls, parameters, separator bytes and answer EOS without holding out vocabulary', () => {
    const result = validateTrainingPreflight(dataset());
    assert.equal(result.vocabularySize, 8);
    assert.equal(result.parameterCount, 8 * 8 + 16 * 192 * 8 + 16 + 16 * 16 + 16 * 8);
    assert.deepEqual(result.windows, { embedding: 8, hidden: 16, centroids: 16, prompt: 160, response: 32, evidence: 80 });
    assert.equal(result.training.corpusTokens, 2);
    assert.equal(result.training.corpusBytesIncludingSeparators, 12);
    assert.equal(result.training.answerTargetsIncludingEos, 2);
    assert.equal(result.heldOut.development[0]!.retainedUnknownTokens, 1);
    assert.equal(result.heldOut.test[0]!.answerUnknownTokens, 1);
    assert.equal(result.training.prompts[0]!.retainedUnknownTokens, 0);
});

test('training current questions and complete evidence must fit while held-out losses remain diagnostic', () => {
    const evidence: FactualDialogueRecord = { ...record('evidence'), messages: [
        { role: 'evidence', content: 'known '.repeat(8).trim() }, { role: 'user', content: 'known' }],
    };
    assert.throws(() => validateTrainingPreflight(dataset(evidence), { promptWindow: 16, responseWindow: 1, evidenceWindow: 8 }), /evidence would be dropped/);
    assert.doesNotThrow(() => validateTrainingPreflight(dataset(evidence), { promptWindow: 16, responseWindow: 1, evidenceWindow: 10 }));
    assert.throws(() => validateTrainingPreflight(dataset(record('question', 'known '.repeat(14).trim())), { promptWindow: 16, responseWindow: 1 }), /current question/);
    const data = dataset();
    data.test = [evidence, record('question', 'known '.repeat(14).trim())];
    const result = validateTrainingPreflight(data, { promptWindow: 16, responseWindow: 1, evidenceWindow: 8 });
    assert.equal(result.heldOut.test[0]!.droppedEvidence, 1);
    assert.equal(result.heldOut.test[1]!.currentQuestionRejected, true);
});

test('automatic evidence budget matches C for tiny shapes and overlarge explicit budgets fail', () => {
    const result = validateTrainingPreflight(dataset(), { promptWindow: 4, responseWindow: 1 });
    assert.equal(result.windows.evidence, 0);
    assert.throws(() => validateTrainingPreflight(dataset(), { promptWindow: 4, responseWindow: 1, evidenceWindow: 1 }), /native context/);
});

test('native vocabulary and parameter allocation limits fail before publication', () => {
    const wide = { ...record('wide'), answer: Array.from({ length: 8185 }, (_, index) => `word${index}`).join(' ') };
    // 8185 unique answer words, one prompt word and seven reserved controls exceed 8192.
    assert.throws(() => validateTrainingPreflight(dataset(wide)), /vocabulary exceeds 8192/);
    assert.throws(() => validateTrainingPreflight(dataset(), { embeddingDimensions: 64, hiddenDimensions: 128,
        centroidCount: 128, promptWindow: 192, responseWindow: 64 }), /parameter count exceeds 2000000/);
});

test('the complete training corpus is bounded before native allocation, including single spellings', () => {
    assert.throws(() => validateTrainingPreflight(dataset({ ...record('long-token'), answer: 'x'.repeat(1048577) })), /spelling exceeds 1 MiB/);
    assert.throws(() => validateTrainingPreflight(dataset({ ...record('long-text'), answer: ' '.repeat(16777216) })), /16 MiB including separators/);
    const many = { ...record('many-targets'), answer: 'x '.repeat(1000000) };
    assert.throws(() => validateTrainingPreflight(dataset(many)), /token limit of 1000000/);
});

test('native dimensions and scalar types are checked without accepting coercions', () => {
    for (const config of [null, [], true, { embeddingDimensions: 65 }, { hiddenDimensions: 129 }, { centroidCount: 129 },
        { promptWindow: 255, responseWindow: 2 }, { responseWindow: 0 }, { evidenceWindow: -1 }, { evidenceWindow: 1.5 },
        { routingTemperature: 0.001 }, { routingTemperature: NaN }, { seed: '-1' }, { seed: '18446744073709551616' }, { promptWindow: '160' }])
        assert.throws(() => validateTrainingPreflight(dataset(), config as never));
    assert.throws(() => validateTrainingPreflight(dataset({ ...record('nul'), answer: 'invalid\0text' })), /NUL-free/);
    assert.throws(() => validateTrainingPreflight(dataset({ ...record('role'), messages: [{ role: 'assistant', content: 'known' }] })), /dialogue order/);
});
