import assert from 'node:assert/strict';
import test from 'node:test';
import type { ChatModelMetadata, ChatReply, ChatSource } from '../../../shared/chat.ts';
import { evidenceUnits, normalizeEvidence } from './evidence.ts';
import { checkGroundedReply, selectEvidence } from './grounding.ts';

const metadata: ChatModelMetadata = { engineKind: 'neural-centroid-chat', formatVersion: 2, protocolVersion: 2,
    tokenizerVersion: 1, config: { promptWindow: 160, evidenceWindow: 80 }, vocabularySize: 20, parameterCount: 100 };
const source: ChatSource = { id: 'source-1', path: 'docs/limits.md', excerpt: 'Automatic training is not enabled.\nExplicit training requires reviewed examples.' };
const selection = () => selectEvidence('Is automatic training enabled?', [source], metadata);
const reply: ChatReply = { content: 'automatic training is not enabled .', finishReason: 'eos', generatedTokens: 6,
    promptTokens: 20, droppedMessages: 0, unknownTokens: 0, evidenceTokens: 8, droppedEvidence: 0 };

test('grounding preserves original quote spelling and valid source offsets', () => {
    const result = checkGroundedReply(reply, selection());
    assert.equal(result.grounding.status, 'quoted');
    assert.deepEqual(result.sources, [source]);
    const claim = result.grounding.claims[0]!;
    assert.equal(source.excerpt.slice(claim.start, claim.end), claim.quote);
    assert.match(result.content!, /Automatic training is not enabled\./);
});

test('whole-unit verification rejects removed negation, partial quotes, new facts and invented citations', () => {
    for (const content of ['automatic training is enabled .', 'training is not enabled .',
        'automatic training is not enabled . it will be enabled tomorrow .',
        'automatic training is not enabled . [invented-source]']) {
        const result = checkGroundedReply({ ...reply, content }, selection());
        assert.equal(result.grounding.status, 'fallback');
        assert.equal(result.content, undefined);
        assert.deepEqual(result.grounding.claims, []);
    }
});

test('unretained evidence, unknown vocabulary and interrupted output cannot pass grounding', () => {
    for (const patch of [{ unknownTokens: 1 }, { droppedEvidence: 1 }, { evidenceTokens: 0 }, { finishReason: 'length' as const }])
        assert.equal(checkGroundedReply({ ...reply, ...patch }, selection()).grounding.status, 'fallback');
    const { evidenceTokens: _tokens, droppedEvidence: _dropped, ...legacy } = reply;
    assert.equal(checkGroundedReply(legacy, selection()).grounding.status, 'fallback');
});

test('evidence selection rejects irrelevant and over-budget units and requires new protocol', () => {
    assert.equal(selectEvidence('unknown unrelated planet', [source], metadata).entries.length, 0);
    assert.equal(selectEvidence('automatic training', [source], { ...metadata, config: { evidenceWindow: 3 } }).entries.length, 0);
    assert.equal(selectEvidence('automatic training', [source], { ...metadata, protocolVersion: 1 }).entries.length, 0);
    assert.deepEqual(selectEvidence('automatic training', [source, source], metadata).messages,
        selectEvidence('automatic training', [source], metadata).messages);
});

test('evidence normalization preserves identifiers, numbers, negation and punctuation', () => {
    assert.notEqual(normalizeEvidence('Version 1 is supported.'), normalizeEvidence('Version 2 is supported.'));
    assert.notEqual(normalizeEvidence('not safe'), normalizeEvidence('safe'));
    const text = '  First sentence.\n  Second sentence! Tail';
    for (const unit of evidenceUnits(text)) assert.equal(text.slice(unit.start, unit.end), unit.text);
});

test('soft-wrapped source qualifiers cannot be omitted from a checked quotation', () => {
    const wrapped = { ...source, excerpt: 'Automatic training is enabled\nonly in isolated simulations.' };
    const selected = selectEvidence('automatic training', [wrapped], metadata);
    assert.equal(selected.entries.length, 1);
    const checked = checkGroundedReply({ ...reply, content: 'automatic training is enabled' }, selected);
    assert.equal(checked.grounding.status, 'fallback');
    assert.equal(checkGroundedReply({ ...reply, content: wrapped.excerpt }, selected).grounding.status, 'quoted');
});
