import assert from 'node:assert/strict';
import test from 'node:test';
import { readFileSync } from 'node:fs';
import { validateFactualDataset } from '../chat/dataset.ts';
import { ABSTENTION, EVIDENCE_ABSTENTION, CLARIFICATION, classifyObservedAction, evaluateScorerControls,
    scoreDialogue, selectDevelopmentCandidate, sourceOnlyBaseline, summarizeDialogueScores, type FactualObservation } from './factuality.ts';

const dataset = () => validateFactualDataset(JSON.parse(readFileSync(new URL('../../../../data/chat/factual-dialogues-v2.json', import.meta.url), 'utf8'))).dataset;

test('matching keywords cannot earn answer or source support after changing a number, negation or adding a claim', () => {
    const record = dataset().test[0]!;
    const exact = scoreDialogue(record, { content: record.answer, finishReason: 'eos', citations: record.evidence });
    assert(exact.exactMatch && exact.exactSourceSupport && exact.citationSupportsCompleteAnswer);
    for (const content of [record.answer.replace('32', '33'), record.answer.replace('is', 'is not'), record.answer + ' It is always safe.']) {
        const score = scoreDialogue(record, { content, finishReason: 'eos', citations: record.evidence });
        assert.equal(score.exactMatch, false);
        assert.equal(score.exactSourceSupport, false);
        assert.equal(score.citationSupportsCompleteAnswer, false);
    }
});

test('citation provenance is independent from answer correctness and rejects invented coordinates', () => {
    const record = dataset().test[0]!;
    const score = scoreDialogue(record, { content: record.answer, finishReason: 'eos', citations: [{ ...record.evidence[0]!, startLine: 2 }] });
    assert.equal(score.exactMatch, true);
    assert.equal(score.validCitations, 0);
    assert.equal(score.citationSupportsCompleteAnswer, false);
});

test('a faithfully copied distractor remains the wrong answer and wrong evidence for the question', () => {
    const record = dataset().test.find(item => item.category === 'distractor')!;
    const distractor = record.evidence[1]!;
    const score = scoreDialogue(record, { content: distractor.excerpt, finishReason: 'sources', citations: [distractor] });
    assert.equal(score.exactSourceSupport, true);
    assert.equal(score.exactMatch, false);
    assert.equal(score.goldEvidenceSelected, false);
    assert.equal(score.irrelevantEvidenceSelected, true);
    const summary = summarizeDialogueScores([score]);
    assert.equal(summary.answerEvidenceCoverage.rate, 0);
    assert.equal(summary.irrelevantEvidenceSelectionRate.rate, 1);
});

test('a wrapped qualifier cannot be omitted to acquire exact source support', () => {
    const original = dataset().test[0]!;
    const excerpt = 'Reed cache ttl is 32 seconds\nonly when debug mode is disabled.';
    const record = { ...original, answer: excerpt, acceptedAnswers: [excerpt], requiredClaims: [excerpt],
        evidence: [{ ...original.evidence[0]!, excerpt, endLine: 2 }] };
    assert.equal(scoreDialogue(record, { content: 'Reed cache ttl is 32 seconds', finishReason: 'eos' }).exactSourceSupport, false);
    assert.equal(scoreDialogue(record, { content: excerpt, finishReason: 'eos' }).exactSourceSupport, true);
});

test('abstention denominators distinguish appropriate refusal from refusal of answerable questions', () => {
    const records = dataset().test;
    const scores = [scoreDialogue(records[0]!, { content: ABSTENTION, finishReason: 'abstained' }),
        scoreDialogue(records[4]!, { content: ABSTENTION, finishReason: 'abstained' })];
    const summary = summarizeDialogueScores(scores);
    assert.deepEqual(summary.correctAnswerRate, { numerator: 0, denominator: 1, rate: 0 });
    assert.deepEqual(summary.correctAbstentionRate, { numerator: 1, denominator: 1, rate: 1 });
    assert.deepEqual(summary.unnecessaryAbstentionRate, { numerator: 1, denominator: 1, rate: 1 });
    assert.equal(summary.exactSourceSupportRate.rate, null);
    assert.equal(summary.usage.measuredCases, 0);
});

test('the source baseline compatibility export supplies strict answers or recognized safe non-answers on training controls', () => {
    for (const record of dataset().train) {
        const reply = sourceOnlyBaseline(record.messages, record.evidence);
        const score = scoreDialogue(record, reply);
        assert.equal(record.expected === 'answer' ? score.correctAnswer : score.safeNonAnswer, true, record.id);
    }
    const record = dataset().train[0]!;
    assert.equal(classifyObservedAction(sourceOnlyBaseline(record.messages, []).content), 'abstain');
    const unrelated = [{ ...record.evidence[0]!, excerpt: 'Amber service uses port 4100.' }];
    assert.equal(classifyObservedAction(sourceOnlyBaseline(record.messages, unrelated).content), 'abstain');
});

test('development selection ranks complete answer results before loss and does not consume final-test labels', () => {
    const record = dataset().development[0]!;
    const correct = scoreDialogue(record, { content: record.answer, finishReason: 'eos' });
    const wrong = scoreDialogue(record, { content: 'invented', finishReason: 'eos' });
    assert.equal(selectDevelopmentCandidate([{ id: 'memorized', development: [wrong], crossEntropy: 0.01 },
        { id: 'grounded', development: [correct], crossEntropy: 10 }]).id, 'grounded');
    assert.equal(selectDevelopmentCandidate([{ id: 'b', development: [correct], crossEntropy: 1 },
        { id: 'a', development: [correct], crossEntropy: 1 }]).id, 'a');
    assert.throws(() => selectDevelopmentCandidate([]), /no successful/);
});

test('unknown and dropped context metrics remain observable independently of answer success', () => {
    const record = dataset().test[0]!;
    const summary = summarizeDialogueScores([scoreDialogue(record, { content: record.answer, finishReason: 'eos',
        unknownTokens: 3, droppedMessages: 2, droppedEvidence: 1, evidenceTokens: 8, promptTokens: 20 })]);
    assert.equal(summary.usage.unknownTokens, 3);
    assert.equal(summary.usage.droppedMessages, 2);
    assert.equal(summary.usage.droppedEvidence, 1);
    assert.equal(summary.usage.evidenceTokens, 8);
});

test('canonical abstention decisions receive action credit independently from exact gold wording', () => {
    const record = dataset().test.find(item => item.category === 'abstention')!;
    const score = scoreDialogue(record, { content: EVIDENCE_ABSTENTION, finishReason: 'eos' });
    assert.equal(score.exactMatch, false);
    assert.equal(score.correctAbstention, false);
    assert.equal(score.exactAbstentionWording, false);
    assert.equal(score.expectedAction, 'abstain');
    assert.equal(score.observedAction, 'abstain');
    assert.equal(score.actionCorrect, true);
    assert.equal(score.safeNonAnswer, true);
    const summary = summarizeDialogueScores([score]);
    assert.deepEqual(summary.abstentionActionRecall, { numerator: 1, denominator: 1, rate: 1 });
    assert.equal(summary.exactAbstentionWordingRate.rate, 0);
    assert.equal(summary.perCategory.abstention?.actionAccuracy.rate, 1);
});

test('legacy clarification gold wording remains distinct from the requested clarification action', () => {
    const record = dataset().test.find(item => item.category === 'clarification')!;
    assert.equal(record.answer, ABSTENTION);
    const literalGold = scoreDialogue(record, { content: record.answer, finishReason: 'eos' });
    assert.equal(literalGold.exactMatch, true);
    assert.equal(literalGold.expectedAction, 'clarify');
    assert.equal(literalGold.observedAction, 'abstain');
    assert.equal(literalGold.actionCorrect, false);
    assert.equal(literalGold.safeNonAnswer, true);
    const clarification = scoreDialogue(record, { content: CLARIFICATION, finishReason: 'eos' });
    assert.equal(clarification.actionCorrect, true);
    assert.equal(clarification.exactMatch, false);
    const summary = summarizeDialogueScores([literalGold, clarification]);
    assert.deepEqual(summary.clarificationActionRecall, { numerator: 1, denominator: 2, rate: 0.5 });
});

test('action recognition uses only complete global phrases and cannot be supplied through gold labels or metadata', () => {
    assert.equal(classifyObservedAction('  I DO NOT have supporting evidence for that claim.  '), 'abstain');
    assert.equal(classifyObservedAction(CLARIFICATION), 'clarify');
    assert.equal(classifyObservedAction(''), 'empty');
    assert.equal(classifyObservedAction(ABSTENTION + ' The port is 9999.'), 'answer');
    const original = dataset().test.find(item => item.expected === 'abstain')!;
    const arbitraryGold = { ...original, answer: 'The port is 9999.', acceptedAnswers: ['The port is 9999.'] };
    const score = scoreDialogue(arbitraryGold, { content: arbitraryGold.answer, finishReason: 'abstained',
        action: 'abstain' } as FactualObservation);
    assert.equal(score.exactMatch, true);
    assert.equal(score.observedAction, 'answer');
    assert.equal(score.actionCorrect, false);
    assert.equal(score.safeNonAnswer, false);
    const answerRecord = dataset().test[0]!;
    const unsupported = scoreDialogue(answerRecord, { content: 'Invented claim.', finishReason: 'eos' });
    assert.equal(unsupported.actionCorrect, true, 'answer action means attempt only');
    assert.equal(unsupported.correctAnswer, false);
    assert.equal(unsupported.exactSourceSupport, false);
});

test('errors receive no answer, action, claim, termination, or provenance credit even with golden content', () => {
    const record = dataset().test[0]!;
    for (const failure of [{ error: 'transport failed', finishReason: 'eos' }, { error: '', finishReason: 'eos' }, { finishReason: 'error' }]) {
        const score = scoreDialogue(record, { content: record.answer, citations: record.evidence, ...failure });
        assert.equal(score.exactMatch, false);
        assert.equal(score.actionCorrect, false);
        assert.equal(score.exactSourceSupport, false);
        assert.equal(score.validCitations, 0);
        assert.equal(score.citationSupportsCompleteAnswer, false);
        assert.equal(score.goldEvidenceSelected, false);
        assert.equal(score.requiredClaimTextPresent, false);
        assert.equal(score.naturalTermination, false);
        assert.equal(summarizeDialogueScores([score]).errors, 1);
    }
});

test('a citation can cover a complete accepted unit inside a longer exact source span', () => {
    const original = dataset().test[0]!;
    const excerpt = 'The following configuration is current. ' + original.answer + ' Scope is staging.';
    const record = { ...original, evidence: [{ ...original.evidence[0]!, excerpt }] };
    const score = scoreDialogue(record, { content: record.answer, citations: record.evidence, finishReason: 'sources' });
    assert.equal(score.exactSourceSupport, true);
    assert.equal(score.citationSupportsCompleteAnswer, true);
    assert.equal(score.goldEvidenceSelected, true);
    assert.equal(score.irrelevantEvidenceSelected, false);
    assert.equal(evaluateScorerControls([record]).passed, true);
});

test('gold-output controls validate strict credit and separately disclose corpus action conflicts', () => {
    const corpus = dataset();
    const records = [...corpus.train, ...corpus.development, ...corpus.test];
    const controls = evaluateScorerControls(records);
    assert.equal(controls.passed, true);
    assert.equal(controls.cases, 18);
    assert.deepEqual(controls.failures, []);
    assert.equal(controls.rubricActionConflicts.length, 3);
    assert(controls.rubricActionConflicts.every(conflict => conflict.expectedAction === 'clarify' && conflict.observedAction === 'abstain'));
    const original = records[0]!;
    const invalid = { ...original, evidence: [{ ...original.evidence[0]!, excerpt: 'Unrelated source.' }] };
    assert.equal(evaluateScorerControls([invalid]).passed, false);
    assert.equal(evaluateScorerControls([]).passed, false);
});
