import assert from 'node:assert/strict';
import test from 'node:test';
import type { ChatExample, ChatReply } from '../../../shared/chat.ts';
import type { ChatTrainingValidation } from '../../../shared/chat-quality.ts';
import { evaluateTrainingValidation, normalizeAnswer, validateTrainingValidation, validationMessages } from './training-quality.ts';

const examples: ChatExample[] = [{ id: 'train', messages: [{ role: 'user', content: 'Training prompt' }], answer: 'Training response.' }];
function suite(): ChatTrainingValidation {
    return { version: 1, cases: [
        { id: 'held-answer', familyId: 'held-fact', sourceId: 'held-doc', messages: [{ role: 'user', content: 'Is the amber relay enabled?' }],
            expected: 'answer', acceptedAnswers: ['The amber relay is not enabled.'], evidence: [{ id: 'held-doc', excerpt: 'The amber relay is not enabled. The violet relay is enabled.' }] },
        { id: 'held-abstain', familyId: 'held-unknown', messages: [{ role: 'user', content: 'What is the amber relay temperature?' }],
            expected: 'abstain', acceptedAnswers: ['I do not have supporting evidence.'] },
    ] };
}
const reply = (content: string, changes: Partial<ChatReply> = {}): ChatReply => ({ content, finishReason: 'eos', generatedTokens: 8,
    promptTokens: 32, unknownTokens: 0, droppedMessages: 0, evidenceTokens: 12, droppedEvidence: 0, ...changes });
const successful = (messages: readonly { content: string }[]) => messages.at(-1)!.content.includes('temperature') ?
    reply('I do not have supporting evidence.') : reply('the amber relay is not enabled .');

test('required validation admits held-out answer and abstention coverage and owns the admitted suite', () => {
    const value = suite();
    const admitted = validateTrainingValidation(examples, value);
    assert.notEqual(admitted, value);
    assert.deepEqual(admitted, value);
    assert.throws(() => validateTrainingValidation(examples, undefined), /requires validation/);
    assert.throws(() => validateTrainingValidation(examples, { version: 1, cases: [value.cases[0], { ...value.cases[0], id: 'other',
        messages: [{ role: 'user', content: 'A different fact question?' }] }] }), /answerable and unanswerable/);
    assert.throws(() => validateTrainingValidation(examples, { version: 1, cases: Array(65).fill(value.cases[0]) }), /2\.\.64/);
});

test('IDs, normalized dialogues, and optional family/source lineage cannot cross the training boundary', () => {
    const value = suite();
    const change = (fields: object) => ({ ...value, cases: [{ ...value.cases[0], ...fields }, value.cases[1]] });
    assert.throws(() => validateTrainingValidation(examples, change({ id: 'train' })), /ID.*leaks/);
    assert.throws(() => validateTrainingValidation(examples, change({ messages: [{ role: 'user', content: '  TRAINING\nPROMPT ' }] })), /dialogue leaks/);
    const punctuationTrain = [{ messages: [{ role: 'user' as const, content: 'Is red?' }], answer: 'yes' }];
    assert.throws(() => validateTrainingValidation(punctuationTrain, change({ messages: [{ role: 'user', content: 'is red ?' }] })), /dialogue leaks/);
    const withLineage = [{ ...examples[0]!, family: 'held-fact', sources: ['unrelated'] }];
    assert.throws(() => validateTrainingValidation(withLineage, value), /family leaks/);
    const withSourceId = [{ ...examples[0]!, sourceId: 'held-doc' }];
    const withSources = [{ ...examples[0]!, sources: ['held-doc'] }];
    assert.throws(() => validateTrainingValidation(withSourceId, value), /source leaks/);
    assert.throws(() => validateTrainingValidation(withSources, value), /source leaks/);
    assert.throws(() => validateTrainingValidation(examples, { ...value, cases: [value.cases[0], { ...value.cases[0], id: 'different-id' }] }), /dialogue.*duplicated/);
});

test('counterfactual training cases may share questions when their supplied evidence differs', () => {
    const counterfactual: ChatExample[] = ['enabled', 'disabled'].map((state, index) => ({ id: `counterfactual-${index}`,
        messages: [{ role: 'evidence', content: `Training relay is ${state}.` }, { role: 'user', content: 'Is the training relay enabled?' }],
        answer: `Training relay is ${state}.` }));
    assert.equal(validateTrainingValidation(counterfactual, suite()).cases.length, 2);
    assert.throws(() => validateTrainingValidation([...counterfactual, { ...counterfactual[0]!, id: 'duplicate' }], suite()), /duplicate normalized training/);
    const value = suite();
    assert.throws(() => validateTrainingValidation(counterfactual, { ...value, cases: [{ ...value.cases[0],
        messages: [{ role: 'user', content: 'is the training relay enabled ?' }] }, value.cases[1]] }), /dialogue leaks/);
});

test('admission requires complete evidence units, preserving negation and punctuation', () => {
    const value = suite();
    const target = (answer: string) => ({ ...value, cases: [{ ...value.cases[0], acceptedAnswers: [answer] }, value.cases[1]] });
    assert.throws(() => validateTrainingValidation(examples, target('The amber relay is enabled.')), /complete evidence units/);
    assert.throws(() => validateTrainingValidation(examples, target('relay is not enabled.')), /complete evidence units/);
    assert.throws(() => validateTrainingValidation(examples, target('The amber relay is not enabled')), /complete evidence units/);
    assert.equal(normalizeAnswer('The amber relay is not enabled .'), normalizeAnswer('the amber relay is not enabled.'));
    assert.notEqual(normalizeAnswer('is not enabled'), normalizeAnswer('is enabled'));
});

test('validation evidence is inserted before the current question and cannot be smuggled into dialogue roles', () => {
    const value = suite();
    const record = { ...value.cases[0]!, messages: [{ role: 'user' as const, content: 'Earlier question' },
        { role: 'assistant' as const, content: 'Earlier reply' }, { role: 'user' as const, content: 'Current question' }] };
    assert.deepEqual(validationMessages(record).map(message => message.role), ['user', 'assistant', 'evidence', 'user']);
    assert.equal(validationMessages(record)[2]!.content, record.evidence![0]!.excerpt);
    assert.throws(() => validateTrainingValidation(examples, { ...value, cases: [{ ...record, messages: [
        { role: 'evidence', content: 'Unlabeled evidence' }, { role: 'user', content: 'Current question' }] }, value.cases[1]] }), /evidence belongs/);
});

test('passing report is a hashed bounded development gate, with independent exact support and correctness metrics', () => {
    const value = validateTrainingValidation(examples, suite());
    const report = evaluateTrainingValidation(Buffer.from('candidate'), value, (payload, messages, options) => {
        assert.equal(payload.toString(), 'candidate');
        assert.deepEqual(options, { maxTokens: 128, temperature: 0, seed: '42' });
        return successful(messages);
    });
    assert.equal(report.passed, true);
    assert.equal(report.scope, 'bounded-development-engineering-gate');
    assert.match(report.suiteSha256, /^[a-f0-9]{64}$/);
    assert.equal(report.metrics.exactAnswerAccuracy, 1);
    assert.equal(report.metrics.supportRate, 1);
    assert.equal(report.metrics.abstentionRecall, 1);
    const wrongButQuoted = evaluateTrainingValidation(Buffer.alloc(0), value, (_payload, messages) => messages.at(-1)!.content.includes('temperature') ?
        successful(messages) : reply('The violet relay is enabled.'));
    assert.equal(wrongButQuoted.metrics.supportRate, 1);
    assert.equal(wrongButQuoted.metrics.answerableAccuracy, 0);
    assert.equal(wrongButQuoted.passed, false);
});

test('lexical overlap, false abstention and empty output cannot pass', () => {
    const value = validateTrainingValidation(examples, suite());
    const overlap = evaluateTrainingValidation(Buffer.alloc(0), value, (_payload, messages) => messages.at(-1)!.content.includes('temperature') ?
        successful(messages) : reply('The amber relay is enabled.'));
    assert.equal(overlap.metrics.supportRate, 0);
    assert.equal(overlap.passed, false);
    const abstainAlways = evaluateTrainingValidation(Buffer.alloc(0), value, () => reply('I do not have supporting evidence.'));
    assert.equal(abstainAlways.metrics.falseAbstentionRate, 1);
    assert.equal(abstainAlways.passed, false);
    const empty = evaluateTrainingValidation(Buffer.alloc(0), value, () => reply(''));
    assert.equal(empty.metrics.exactAnswerAccuracy, 0);
    assert.equal(empty.passed, false);
});

test('correct text still fails on unknown tokens, dropped history/evidence, or missing EOS', () => {
    const value = validateTrainingValidation(examples, suite());
    for (const [changes, metric] of [[{ unknownTokens: 1 }, 'unknownTokenRate'], [{ droppedMessages: 1 }, 'droppedCaseRate'],
        [{ droppedEvidence: 1 }, 'droppedEvidenceCaseRate'], [{ finishReason: 'length' }, 'eosRate']] as const) {
        const report = evaluateTrainingValidation(Buffer.alloc(0), value, (_payload, messages) => ({ ...successful(messages), ...changes }));
        assert.equal(report.metrics.exactAnswerAccuracy, 1);
        assert.equal(report.passed, false, metric);
        assert.ok(report.failures.some(failure => failure.startsWith(metric)), metric);
    }
    assert.throws(() => evaluateTrainingValidation(Buffer.alloc(0), value, () => reply('anything', { unknownTokens: 33 })), /accounting/);
    assert.throws(() => evaluateTrainingValidation(Buffer.alloc(0), value, () => reply('anything', { evidenceTokens: 0 })), /explicit valid native evidence counters/);
    for (const field of ['evidenceTokens', 'droppedEvidence'] as const) {
        assert.throws(() => evaluateTrainingValidation(Buffer.alloc(0), value, () => {
            const result = { ...reply('anything') };
            delete result[field];
            return result;
        }), /explicit valid native evidence counters/);
    }
});
