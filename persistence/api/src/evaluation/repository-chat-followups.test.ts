import assert from 'node:assert/strict';
import test from 'node:test';
import type { Conversation } from '../../../shared/chat.ts';
import { sha256 } from './codebase-data.ts';
import type { Document } from './codebase-data.ts';
import { scoreRepositoryReply, validateRepositoryFollowups, validateRepositorySuite } from './repository-chat-metrics.ts';
import type { ExpectedReply, RepositoryFollowupSuite } from './repository-chat-metrics.ts';

const identity = { repository: 'test', commit: 'a'.repeat(40), manifestSha256: 'b'.repeat(64) };
const pending = { snapshot: identity, taskId: 'source-task', topicCandidates: ['src/one.ts', 'src/two.ts'] };
function conversation(state: Record<string, unknown>, answer: Record<string, unknown> = {}): Conversation {
    return { scope: 'repository', repositorySnapshot: identity, repositoryState: state, messages: [{ role: 'assistant',
        status: 'complete', finishReason: 'clarification', content: 'Which source file do you mean?', sources: [],
        research: { queries: 0, fetched: 0 }, ...answer }] } as unknown as Conversation;
}
const clarification: ExpectedReply = { outcome: 'clarification', finishReason: 'clarification', noAction: true,
    state: { focusPath: null, candidates: 'multiple', candidateIncludes: ['src/one.ts', 'src/two.ts'] } };

test('clarification scoring requires a real pending choice and never silently accepts abstention or old task action', () => {
    assert(scoreRepositoryReply(clarification, conversation(pending), [], identity).every(check => check.passed));
    const abstention = scoreRepositoryReply(clarification, conversation(pending, { finishReason: 'abstained' }), [], identity);
    assert.equal(abstention.find(check => check.name === 'exact expected reply outcome')?.passed, false);
    const fallback = scoreRepositoryReply(clarification, conversation(pending, { action: { taskId: 'old-task' } }), [], identity);
    assert.equal(fallback.find(check => check.name === 'unresolved or factual topic does not propose an unrelated action')?.passed, false);
    const invented = scoreRepositoryReply(clarification, conversation({ ...pending, focusPath: 'src/invented.ts', topicCandidates: [] }), [], identity);
    assert.equal(invented.find(check => check.name === 'persisted file selection matches intent')?.passed, false);
    assert.equal(invented.find(check => check.name === 'file or topic candidate lifecycle matches intent')?.passed, false);
});

test('selection scoring distinguishes file focus, competing tasks, and attributed progress', () => {
    const expected: ExpectedReply = { outcome: 'unsupported', state: { taskId: 'source-task', focusPath: 'src/one.ts',
        candidates: 'none', taskCandidates: 'none', reportedProgressContains: 'I finished it' } };
    const selected = { snapshot: identity, taskId: 'source-task', focusPath: 'src/one.ts', userReportedProgress: 'I finished it and ran checks.' };
    assert(scoreRepositoryReply(expected, conversation(selected), [], identity).every(check => check.passed));
    const retained = scoreRepositoryReply(expected, conversation({ ...selected, taskCandidates: ['source-task', 'other-task'] }), [], identity);
    assert.equal(retained.find(check => check.name === 'task candidate lifecycle matches intent')?.passed, false);
    const competition: ExpectedReply = { outcome: 'clarification', state: { taskCandidates: 'multiple', taskCandidateIncludes: ['source-task', 'other-task'] } };
    assert(scoreRepositoryReply(competition, conversation({ taskCandidates: ['source-task', 'other-task'] }), [], identity).every(check => check.passed));
});

test('eight follow-up development chains remain separate from the forty-case regression contract', () => {
    const suite: RepositoryFollowupSuite = { version: 1, purpose: 'repository-followup-development',
        description: 'Isolated follow-up contract fixture.', scenarios: Array.from({ length: 8 }, (_, index) => ({
            id: `followup-${index}`, group: 'context', split: 'development', description: 'Synthetic follow-up schema control.',
            turns: Array.from({ length: 4 }, () => ({ content: 'Which source file supports that?',
                expected: { outcome: 'supported', sourcePaths: ['src/one.ts'], evidence: [{ path: 'src/one.ts', quote: 'A source fixture.' }] } })),
        })) };
    const byPath = new Map<string, string[]>();
    for (const scenario of suite.scenarios) for (const turn of scenario.turns) for (const gold of turn.expected.evidence ?? [])
        byPath.set(gold.path, [...(byPath.get(gold.path) ?? []), gold.quote]);
    for (const scenario of suite.scenarios) for (const turn of scenario.turns) for (const path of turn.expected.sourcePaths ?? [])
        if (!byPath.has(path)) byPath.set(path, ['A source fixture.']);
    const documents: Document[] = [...byPath].map(([path, quotes]) => {
        const text = quotes.join('\n');
        return { text, sha256: sha256(text), sources: [{ path, commit: identity.commit, blob: 'c'.repeat(40) }] };
    });
    assert.equal(validateRepositoryFollowups(suite, documents).scenarios.length, 8);
    assert(suite.scenarios.every(scenario => scenario.turns.length >= 4));
    assert.throws(() => validateRepositorySuite(suite, documents), /Expected 24 questions/);
    assert.throws(() => validateRepositoryFollowups(suite, []), /Missing (gold evidence|required source path)/);
    const acceptance = structuredClone(suite);
    acceptance.scenarios[0]!.split = 'acceptance';
    assert.throws(() => validateRepositoryFollowups(acceptance, documents), /development-only/);
    const shortened = structuredClone(suite);
    shortened.scenarios[0]!.turns = shortened.scenarios[0]!.turns.slice(0, 3);
    assert.throws(() => validateRepositoryFollowups(shortened, documents), /four turns/);
    const invalid = structuredClone(suite);
    invalid.scenarios[0]!.turns[0]!.expected.state = { candidates: 'invented' } as unknown as NonNullable<ExpectedReply['state']>;
    assert.throws(() => validateRepositoryFollowups(invalid, documents), /Invalid state expectation/);
});
