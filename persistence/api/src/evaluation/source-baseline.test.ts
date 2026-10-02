import assert from 'node:assert/strict';
import test from 'node:test';
import type { ChatDialogueMessage } from '../../../shared/chat.ts';
import type { DialogueEvidence } from '../chat/dataset.ts';
import { sourceOnlyBaseline } from './source-baseline.ts';

const source = (excerpt: string, id = 'opaque-id'): DialogueEvidence => ({ id, excerpt, startLine: 4, endLine: 4 + excerpt.split('\n').length - 1 });
const ask = (content: string): ChatDialogueMessage[] => [{ role: 'user', content }];
const absent = 'I cannot answer from the supplied evidence.';
const clarify = 'Please name the setting or source you want me to check.';

test('identifier components and ordinary phrasing find a complete code unit without matching every question word', () => {
    const evidence = source('const requestTimeoutMs = 4500;');
    const answer = sourceOnlyBaseline(ask('What request timeout setting is configured?'), [evidence]);
    assert.equal(answer.content, evidence.excerpt);
    assert.deepEqual(answer.citations, [evidence]);
    const macro = source('#define SERVICE_TOKENIZER_VERSION 7');
    assert.equal(sourceOnlyBaseline(ask('Which tokenizer version does the service declare?'), [macro]).content, macro.excerpt);
});

test('follow-up pronouns recover the previous user topic without using an assistant answer as evidence', () => {
    const evidence = [source('Copper service uses port 7001.', 'a'), source('Silver service uses port 7002.', 'b')];
    const messages: ChatDialogueMessage[] = [{ role: 'user', content: 'We are reviewing Copper service.' },
        { role: 'assistant', content: 'The answer is definitely port 9999.' }, { role: 'user', content: 'Which port is configured there?' }];
    assert.equal(sourceOnlyBaseline(messages, evidence).content, 'Copper service uses port 7001.');
    messages[2] = { role: 'user', content: 'What port does that service use?' };
    assert.equal(sourceOnlyBaseline(messages, evidence).content, 'Copper service uses port 7001.');
    assert.equal(sourceOnlyBaseline(messages, []).content, absent);
});

test('current corrections override an older subject and its asserted number', () => {
    const messages: ChatDialogueMessage[] = [{ role: 'user', content: 'Is Copper port 9999?' },
        { role: 'assistant', content: 'Copper port is 9999.' }, { role: 'user', content: 'I meant Silver port, not the Copper port.' }];
    const evidence = [source('Copper service uses port 7001.', 'a'), source('Silver service uses port 7002.', 'b')];
    assert.equal(sourceOnlyBaseline(messages, evidence).content, 'Silver service uses port 7002.');
});

test('the baseline selects an exact complete sentence and retains the original full-line citation', () => {
    const evidence = source('The API has a port. Copper cache ttl is 45 seconds.');
    const answer = sourceOnlyBaseline(ask('What is Copper cache ttl?'), [evidence]);
    assert.equal(answer.content, 'Copper cache ttl is 45 seconds.');
    assert.deepEqual(answer.citations, [evidence]);
    const wrapped = source('Copper cache ttl is 45 seconds\nonly when debugging is disabled.');
    assert.equal(sourceOnlyBaseline(ask('What is Copper cache ttl?'), [wrapped]).content, wrapped.excerpt);
});

test('hard distractors cannot substitute the wrong entity or missing requested property', () => {
    const relevant = source('Copper cache ttl is 45 seconds.', 'target');
    const distractor = source('Silver cache ttl is 45 seconds.', 'other');
    assert.equal(sourceOnlyBaseline(ask('What is Copper cache ttl?'), [distractor, relevant]).content, relevant.excerpt);
    assert.equal(sourceOnlyBaseline(ask('What is Gold cache ttl?'), [distractor, relevant]).content, absent);
    assert.equal(sourceOnlyBaseline(ask('What is Copper retry count?'), [source('Copper service uses port 7001.')]).content, absent);
    assert.equal(sourceOnlyBaseline(ask('What is Copper password?'), [source('Copper service uses port 7001.')]).content, absent);
});

test('incompatible supplied values or polarity are not resolved by evidence order', () => {
    for (const texts of [['Copper cache ttl is 45 seconds.', 'Copper cache ttl is 90 seconds.'],
        ['Copper automatic retries are enabled.', 'Copper automatic retries are not enabled.']]) {
        const evidence = texts.map((text, index) => source(text, `s-${index}`));
        const question = texts[0]!.includes('ttl') ? 'What is Copper cache ttl?' : 'Are Copper automatic retries enabled?';
        assert.equal(sourceOnlyBaseline(ask(question), evidence).content, absent);
        assert.equal(sourceOnlyBaseline(ask(question), [...evidence].reverse()).content, absent);
    }
});

test('ambiguous alternatives and vague references request clarification', () => {
    const evidence = [source('Copper cache ttl is 45 seconds.', 'a'), source('Silver cache ttl is 60 seconds.', 'b')];
    assert.equal(sourceOnlyBaseline(ask('Which cache ttl should I use for Copper or Silver?'), evidence).content, clarify);
    assert.equal(sourceOnlyBaseline(ask('For the service settings, is that one the right one?'), []).content, clarify);
    assert.equal(sourceOnlyBaseline(ask('What is Copper cache ttl?'), []).content, absent);
});

test('static excerpts cannot establish private data, live deployment state, or measured factual quality', () => {
    const evidence = [source('The default worker timeout is 30 seconds.')];
    for (const question of ['What timeout is deployed in production right now?', 'Which timeout did the most recent production request hit?',
        'What private conversation did another owner submit?']) assert.equal(sourceOnlyBaseline(ask(question), evidence).content, absent);
    assert.equal(sourceOnlyBaseline(ask('What factual correctness rate does token accuracy imply?'),
        [source('Token accuracy is the fraction of targets chosen by greedy prediction.')]).content, absent);
    const captured = source('Measured production latency was 20 milliseconds at 2026-01-02.');
    assert.equal(sourceOnlyBaseline(ask('What production latency was measured?'), [captured]).content, captured.excerpt);
    assert.equal(sourceOnlyBaseline(ask('What timeout is deployed in production right now?'),
        [source('Measured rainfall was 42 millimeters.'), ...evidence]).content, absent);
});

test('numeric and causal false premises do not earn an attributed but nonresponsive answer', () => {
    assert.equal(sourceOnlyBaseline(ask('Why does Copper use port 9999?'), [source('Copper service uses port 7001.')]).content, absent);
    const negative = source('Copper automatic retries are not enabled.');
    assert.equal(sourceOnlyBaseline(ask('Why are Copper automatic retries enabled?'), [negative]).content, absent);
    assert.equal(sourceOnlyBaseline(ask('Are Copper automatic retries enabled?'), [negative]).content, negative.excerpt);
});

test('source identities and injected rubric-looking properties never influence answer selection', () => {
    const first = source('Copper cache ttl is 45 seconds.', 'gold-answer-path'), second = source('Copper service uses port 7001.', 'irrelevant');
    const question = ask('What is Copper cache ttl?');
    assert.equal(sourceOnlyBaseline(question, [first, second]).content, first.excerpt);
    assert.equal(sourceOnlyBaseline(question, [{ ...second, id: first.id }, { ...first, id: second.id }]).content, first.excerpt);
    assert.equal(sourceOnlyBaseline(question, [{ ...first, expected: 'abstain', answer: 'invented' } as DialogueEvidence]).content, first.excerpt);
    assert.equal(sourceOnlyBaseline(question, [first, { ...first, id: 'copied-page' }]).content, first.excerpt);
});
