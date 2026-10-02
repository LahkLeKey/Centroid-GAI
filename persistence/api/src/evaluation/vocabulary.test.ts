import assert from 'node:assert/strict';
import test from 'node:test';
import { readFileSync } from 'node:fs';
import type { ChatDialogueMessage } from '../../../shared/chat.ts';
import { validateFactualDataset, type FactualDialogueDataset, type FactualDialogueRecord } from '../chat/dataset.ts';
import { evaluateVocabulary } from './vocabulary.ts';

function record(id: string, answer: string, excerpt = answer, messages?: readonly ChatDialogueMessage[]): FactualDialogueRecord {
    return { id, family: id, category: 'direct', sources: [id], answer, expected: 'answer', acceptedAnswers: [answer], requiredClaims: [answer],
        evidence: [{ id, excerpt, startLine: 1, endLine: 1 }],
        messages: messages ?? [{ role: 'evidence', content: excerpt }, { role: 'user', content: 'known' }] };
}
function fixture(): FactualDialogueDataset {
    return { version: 2, purpose: 'synthetic-engineering', description: 'Representation unit fixture', sourceDocuments: [],
        train: [record('train', 'known value.')], development: [record('development', 'NewIdentifier42')], test: [record('test', '18446744073709551615')] };
}

test('held-out names never extend the frozen training vocabulary and all counts are reproducible', () => {
    const dataset = fixture();
    const report = evaluateVocabulary(dataset);
    assert.equal(report.vocabulary.lexicalSize, 3);
    assert.equal(report.vocabulary.nativeTotalSize, 10);
    assert.equal(report.vocabulary.unchangedAfterHeldOutAndProbes, true);
    assert.equal(report.splits.train.records[0]!.frozenWord.answer.exactRoundTripWithFrozenIds, true);
    const heldOut = report.splits.development.records[0]!;
    assert.deepEqual(heldOut.frozenWord.answer.unknownSpellings, ['newidentifier42']);
    assert.equal(heldOut.frozenWord.answer.unknownTokens, 1);
    assert.equal(heldOut.frozenWord.promptTokensBeforeAdmission, 7);
    assert.equal(heldOut.frozenWord.promptUnknownTokensBeforeAdmission, 1);
    assert.equal(heldOut.frozenWord.answer.tokensIncludingEos, 2);
    assert.equal(heldOut.promptControls, 5);
    assert.deepEqual(report, evaluateVocabulary(dataset));
    dataset.test.push(record('another-heldout', 'CompletelyDifferentEntity'));
    const changed = evaluateVocabulary(dataset);
    assert.equal(report.vocabulary.sha256, changed.vocabulary.sha256);
    assert.notEqual(report.datasetSha256, changed.datasetSha256);
});

test('word coverage includes training evidence and answers; C-locale case folding preserves Unicode distinctions', () => {
    const dataset = fixture();
    dataset.train = [record('train', 'LearnedAnswer', 'Éclair ΩValue')];
    dataset.test = [record('test', 'learnedanswer'), record('unicode', 'éclair Ωvalue')];
    const result = evaluateVocabulary(dataset).splits.test.records;
    assert.equal(result[0]!.frozenWord.answer.unknownTokens, 0);
    assert.deepEqual(result[1]!.frozenWord.answer.unknownSpellings, ['éclair']);
    assert.equal(result[1]!.hypotheticalBytes.answer.unknownTokens, 0);
    assert.equal(result[1]!.hypotheticalBytes.answer.exactRoundTrip, true);
});

test('exact bytes preserve filenames, numbers and Unicode while the oracle exposes actual source coordinates', () => {
    const dataset = fixture();
    const answer = 'src/Ω/Widget42.hpp';
    dataset.test = [record('unicode-path', answer, `🚀 ${answer} remains available.`),
        record('unsupported', 'invented', 'known value.')];
    const [quoted, unsupported] = evaluateVocabulary(dataset).splits.test.records;
    assert.equal(quoted!.hypotheticalBytes.answer.tokens, Buffer.byteLength(answer));
    assert.equal(quoted!.hypotheticalBytes.answer.exactRoundTrip, true);
    assert.equal(quoted!.frozenWord.answer.exactRoundTripWithFrozenIds, false);
    assert.deepEqual(quoted!.sourceCopyOracle.spans, [{ sourceId: 'unicode-path', sourceStartLine: 1, sourceEndLine: 1,
        startByte: Buffer.byteLength('🚀 '), endByte: Buffer.byteLength(`🚀 ${answer}`), exactRoundTrip: true }]);
    assert.equal(unsupported!.sourceCopyOracle.representable, false);
    assert.match(quoted!.sourceCopyOracle.label, /gold-informed/);
});

test('byte expansion consumes context and drops whole evidence while word lengths can still fit', () => {
    const dataset = fixture();
    dataset.test = [record('long-evidence', 'x'.repeat(300)),
        record('long-question', 'known', 'known', [{ role: 'user', content: 'x'.repeat(300) }])];
    const [evidence, question] = evaluateVocabulary(dataset).splits.test.records;
    assert.equal(evidence!.frozenWord.promptOverflowAt256, 0);
    assert.equal(evidence!.frozenWord.admissionAtNativeDefaults.droppedEvidence, 0);
    assert.equal(evidence!.hypotheticalBytes.promptOverflowAt256, 54); // 300 + five-byte question + five controls.
    assert.equal(evidence!.hypotheticalBytes.admissionAtNativeDefaults.droppedEvidence, 1);
    assert.equal(evidence!.hypotheticalBytes.admissionAtNativeDefaults.retainedEvidenceTokens, 0);
    assert.equal(question!.hypotheticalBytes.admissionAtNativeDefaults.currentQuestionRejected, true);
    assert.equal(question!.hypotheticalBytes.admissionAtNativeDefaults.retainedTokens, null);
    assert.equal(question!.frozenWord.admissionAtNativeDefaults.currentQuestionRejected, false);
});

test('default admission prioritizes current evidence, preserves full turns, and excludes omitted unknowns', () => {
    const dataset = fixture();
    const retained = 'known '.repeat(70).trim(); // 72 slots, fits evidence budget.
    const omitted = 'UnseenEvidence '.repeat(10).trim(); // 12 slots, cumulative budget overflow.
    dataset.test = [record('priority', 'known', retained, [
        { role: 'user', content: 'known '.repeat(50).trim() },
        { role: 'assistant', content: 'value '.repeat(50).trim() },
        { role: 'evidence', content: retained }, { role: 'evidence', content: omitted }, { role: 'user', content: 'known' },
    ])];
    const admission = evaluateVocabulary(dataset).splits.test.records[0]!.frozenWord.admissionAtNativeDefaults;
    assert.deepEqual(admission, { currentQuestionRejected: false, retainedTokens: 76, retainedUnknownTokens: 0,
        retainedEvidenceTokens: 72, droppedMessages: 3, droppedEvidence: 1 });
});

test('invalid UTF-16 is disclosed as lossy at the UTF-8 boundary rather than claiming universal roundtrip', () => {
    const dataset = fixture();
    dataset.test = [record('invalid-surrogate', '\ud800')];
    const result = evaluateVocabulary(dataset).splits.test.records[0]!;
    assert.equal(result.hypotheticalBytes.answer.exactRoundTrip, false);
    assert.equal(result.hypotheticalBytes.answer.unknownTokens, 0);
    assert.equal(result.sourceCopyOracle.representable, false);
    assert.deepEqual(result.frozenWord.answer.unknownSpellings, ['�']);
});

test('adversarial probes guarantee unseen tokens, truthful copy labels and deterministic hashes', () => {
    const dataset = fixture();
    const report = evaluateVocabulary(dataset);
    assert.deepEqual(report.probes.map(probe => probe.kind), ['identifier', 'filepath', 'number', 'unicode', 'byte-expansion']);
    for (const probe of report.probes) {
        assert.ok(probe.coverage.frozenWord.answer.unknownTokens > 0, probe.kind);
        assert.equal(probe.coverage.hypotheticalBytes.answer.exactRoundTrip, true, probe.kind);
        assert.equal(probe.coverage.hypotheticalBytes.answer.unknownTokens, 0, probe.kind);
        assert.equal(probe.coverage.sourceCopyOracle.representable, true, probe.kind);
    }
    dataset.train.push(record('formerly-probe', report.probes[0]!.text));
    const updated = evaluateVocabulary(dataset);
    assert.notEqual(updated.probes[0]!.text, report.probes[0]!.text);
    assert.ok(updated.probes[0]!.coverage.frozenWord.answer.unknownTokens > 0);
    assert.match(report.vocabulary.sha256, /^[a-f0-9]{64}$/);
    assert.ok(report.limitations.some(line => line.includes('not answer accuracy')));
});

test('validated tracked datasets produce serializable representation reports without loading the addon', () => {
    const input = JSON.parse(readFileSync(new URL('../../../../data/chat/factual-dialogues-v2.json', import.meta.url), 'utf8'));
    const { dataset } = validateFactualDataset(input);
    const report = evaluateVocabulary(dataset);
    assert.equal(report.splits.test.summary.cases, dataset.test.length);
    assert.deepEqual(JSON.parse(JSON.stringify(report)), report);
    assert.equal(report.vocabulary.unchangedAfterHeldOutAndProbes, true);
});
