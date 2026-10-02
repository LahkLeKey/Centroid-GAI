import assert from 'node:assert/strict';
import test from 'node:test';
import type { ChatReply } from '../../../shared/chat.ts';
import { buildSanityControls, loadSanityFixture, runLearningSanity, scoreSanityReply, validateSanityFixture } from './learning-sanity.ts';

const reply = (changes: Partial<ChatReply> = {}): ChatReply => ({ content: 'red', finishReason: 'eos', generatedTokens: 1,
    promptTokens: 7, unknownTokens: 0, droppedMessages: 0, evidenceTokens: 3, droppedEvidence: 0, ...changes });

test('fixture has thirteen varied training cases and evidence controls with explicitly authored changed labels', async () => {
    const fixture = await loadSanityFixture();
    assert.equal(fixture.cases.length, 13);
    assert.deepEqual(new Set(fixture.cases.map(record => record.category)),
        new Set(['direct', 'evidence', 'follow-up', 'correction', 'abstention', 'clarification']));
    const controls = buildSanityControls(fixture);
    assert.equal(controls.filter(control => control.kind === 'swapped').length, 3);
    assert.equal(controls.filter(control => control.kind === 'removed').length, 3);
    for (const control of controls) {
        const source = fixture.cases.find(record => record.id === control.sourceCaseId)!;
        const target = fixture.cases.find(record => record.id === control.targetCaseId)!;
        assert.deepEqual(control.messages.filter(message => message.role !== 'evidence'), source.messages.filter(message => message.role !== 'evidence'));
        assert.deepEqual(control.messages, target.messages);
        assert.notEqual(control.answer, source.answer);
    }
});

test('fixture rejects duplicate prompts and unlabeled or non-evidence control transformations', async () => {
    const fixture = await loadSanityFixture();
    const duplicate = structuredClone(fixture);
    duplicate.cases[1] = { ...duplicate.cases[1]!, messages: duplicate.cases[0]!.messages };
    assert.throws(() => validateSanityFixture(duplicate), /duplicate normalized/);
    const missing = structuredClone(fixture);
    missing.controls[0]!.targetCaseId = 'not-authored';
    assert.throws(() => validateSanityFixture(missing), /cases required/);
    const changedQuestion = structuredClone(fixture);
    changedQuestion.controls[0]!.targetCaseId = 'direct-blue';
    assert.throws(() => validateSanityFixture(changedQuestion), /change evidence only/);
    const omitted = structuredClone(fixture);
    omitted.cases.pop();
    assert.throws(() => validateSanityFixture(omitted), /exactly 13/);
});

test('exact output still fails without EOS, with context/vocabulary loss, or with missing counters', () => {
    assert.equal(scoreSanityReply('red', reply()).passed, true);
    for (const changes of [{ finishReason: 'length' }, { finishReason: 'repetition' }, { unknownTokens: 1 },
        { droppedMessages: 1 }, { droppedEvidence: 1 }, { promptTokens: NaN }, { evidenceTokens: 8 }, { content: 'red invented' }] as const)
        assert.equal(scoreSanityReply('red', reply(changes)).passed, false, JSON.stringify(changes));
    const missing = reply();
    delete (missing as { droppedEvidence?: number }).droppedEvidence;
    assert.equal(scoreSanityReply('red', missing).passed, false);
    assert.equal(scoreSanityReply('red', reply({ finishReason: 'length' })).exact, true);
    assert.equal(scoreSanityReply('red', reply({ promptTokens: 0, generatedTokens: 0, evidenceTokens: 0 })).passed, false);
    assert.equal(scoreSanityReply('red', reply({ generatedTokens: 0 })).passed, false);
    assert.equal(scoreSanityReply('red', reply({ evidenceTokens: 0 }), true).passed, false);
});

test('native load and training failures remain explicit failed attempts rather than losing the report', async () => {
    const unavailable = await runLearningSanity({ epochs: 1, loadBackend: async () => { throw new Error('addon unavailable'); } });
    assert.equal(unavailable.passed, false);
    assert.equal(unavailable.backendError, 'addon unavailable');
    assert.equal(unavailable.attempts[0]!.status, 'failed');
    assert.equal(unavailable.attempts[0]!.artifactSha256, null);
    const failed = await runLearningSanity({ epochs: 1, loadBackend: async () => ({
        trainChatModel() { throw new Error('training failed'); }, replyChatModel() { throw new Error('reply should not run'); },
    }) });
    assert.equal(failed.passed, false);
    assert.equal(failed.attempts[0]!.error, 'training failed');
    assert.equal(failed.summary.cases, 0);
    assert.deepEqual(JSON.parse(JSON.stringify(failed)), failed);
});

test('epoch overrides remain bounded before loading a backend', async () => {
    let loaded = false;
    for (const epochs of [0, -1, 2001, 1.5, Infinity, NaN])
        await assert.rejects(runLearningSanity({ epochs, loadBackend: async () => { loaded = true; throw new Error('unexpected'); } }), /1\.\.2000/);
    assert.equal(loaded, false);
});

test('the actual native small model reproduces all thirteen known examples and six evidence controls with EOS', async () => {
    const result = await runLearningSanity({ epochs: 600 });
    assert.equal(result.passed, true);
    assert.deepEqual(result.summary, { cases: 13, exactAnswers: 13, eosAnswers: 13, vocabularyClean: 13, contextComplete: 13, passedCases: 13, passed: true });
    assert.equal(result.evidenceControls.passedCases, 6);
    assert.equal(result.evidenceControls.changedAnswers, 6);
    const attempt = result.attempts[0]!;
    assert.equal(attempt.status, 'complete');
    assert.equal(attempt.metadata?.protocolVersion, 2);
    assert.equal(attempt.metadata?.tokenizerVersion, 1);
    assert.equal(attempt.after?.accuracy, 1);
    assert.equal(attempt.after?.unknownTokens, 0);
    assert.ok(attempt.after!.crossEntropy < attempt.before!.crossEntropy);
    assert.match(attempt.artifactSha256!, /^[a-f0-9]{64}$/);
    assert.match(result.fixtureSha256, /^[a-f0-9]{64}$/);
    assert.equal(result.protocol.heldOutGeneralizationMeasured, false);
    assert.equal(result.protocol.productionConfigChanged, false);
});
