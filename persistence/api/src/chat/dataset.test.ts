import assert from 'node:assert/strict';
import test from 'node:test';
import { readFileSync } from 'node:fs';
import { validateDataset, validateFactualDataset } from './dataset.ts';
import { validateTrainingValidation } from './training-quality.ts';

const fixture = () => JSON.parse(readFileSync(new URL('../../../../data/chat/factual-dialogues-v2.json', import.meta.url), 'utf8'));
test('dialogue fixture has separate source/family splits and stable identities', () => {
    const first = validateDataset(fixture());
    assert.deepEqual(first.hashes, validateDataset(fixture()).hashes);
    for (const kind of ['family', 'sources', 'messages', 'id'] as const) {
        const value = fixture();
        value.test[0][kind] = value.train[0][kind];
        assert.throws(() => validateDataset(value));
    }
});

test('synthetic factual fixture includes every diagnostic category in every split', () => {
    const { dataset } = validateFactualDataset(fixture());
    for (const split of [dataset.train, dataset.development, dataset.test]) {
        assert.deepEqual(new Set(split.map(record => record.category)), new Set(['direct', 'follow-up', 'correction', 'distractor', 'abstention', 'clarification']));
        assert.equal(split.filter(record => record.expected === 'answer').length, 4);
        assert.equal(split.filter(record => record.expected === 'abstain').length, 2);
    }
});

test('factual rubrics reject fabricated spans, missing provenance and gold-only evidence', () => {
    for (const mutate of [
        (data: ReturnType<typeof fixture>) => { data.test[0].evidence[0].startLine = 2; },
        (data: ReturnType<typeof fixture>) => { data.test[0].evidence[0].excerpt = 'invented'; },
        (data: ReturnType<typeof fixture>) => { data.sourceDocuments[0].provenance.kind = 'reviewed'; },
        (data: ReturnType<typeof fixture>) => { data.test[0].messages.shift(); },
        (data: ReturnType<typeof fixture>) => { data.test[0].acceptedAnswers = ['partial']; },
        (data: ReturnType<typeof fixture>) => { data.test[0].messages[0] = null; },
    ]) {
        const data = fixture();
        mutate(data);
        assert.throws(() => validateFactualDataset(data));
    }
});

test('answer rubrics require complete units so an answer cannot remove source negation', () => {
    const data = fixture();
    const record = data.train[0];
    const source = data.sourceDocuments.find((item: { id: string }) => item.id === record.sources[0]);
    source.text = 'Amber cache ttl is not 12 seconds.';
    record.evidence[0].excerpt = source.text;
    record.messages[0].content = source.text;
    record.answer = '12 seconds.';
    record.acceptedAnswers = [record.answer];
    record.requiredClaims = [record.answer];
    assert.throws(() => validateFactualDataset(data), /complete supplied source/);
});

test('version-one datasets remain available but cannot silently use factual evaluation', () => {
    const data = fixture();
    data.version = 1;
    assert.equal(validateDataset(data).dataset.version, 1);
    assert.throws(() => validateFactualDataset(data), /version 2/);
});

test('renaming a source cannot bypass the source-content split boundary', () => {
    const data = fixture();
    const record = data.test[0];
    const source = data.sourceDocuments.find((item: { id: string }) => item.id === record.sources[0]);
    source.text = data.sourceDocuments[0].text;
    assert.throws(() => validateFactualDataset(data), /source content leakage/);
});

test('native-equivalent punctuation spacing cannot bypass dialogue leakage checks', () => {
    const data = fixture();
    data.test[0].messages = data.train[0].messages.map((message: { role: string; content: string }) => ({
        ...message, content: message.content.replace(/[.?!]/g, ' $&'),
    }));
    assert.throws(() => validateFactualDataset(data), /duplicate normalized dialogue/);
});

test('a multi-sentence span cannot become a single accepted evidence unit', () => {
    const data = fixture();
    const record = data.train[0];
    const source = data.sourceDocuments.find((item: { id: string }) => item.id === record.sources[0]);
    source.text += ' This applies only in staging.';
    record.evidence[0].excerpt = source.text;
    record.messages[0].content = source.text;
    record.answer = source.text;
    record.acceptedAnswers = [source.text];
    record.requiredClaims = [source.text];
    assert.throws(() => validateFactualDataset(data), /complete supplied source/);
});

test('the tracked API training request contains the same training and held-out development corpus', () => {
    const request = JSON.parse(readFileSync(new URL('../../../../data/chat/train-request-v1.json', import.meta.url), 'utf8'));
    const { dataset } = validateFactualDataset(fixture());
    assert.deepEqual(request.examples, dataset.train);
    const validation = validateTrainingValidation(request.examples, request.validation);
    assert.deepEqual(validation.cases.map(record => record.id), dataset.development.map(record => record.id));
    for (const [index, record] of validation.cases.entries()) {
        assert.deepEqual(record.messages, dataset.development[index]!.messages.filter(message => message.role !== 'evidence'));
        assert.deepEqual(record.evidence, dataset.development[index]!.evidence);
        assert.deepEqual(record.acceptedAnswers, dataset.development[index]!.acceptedAnswers);
    }
});
