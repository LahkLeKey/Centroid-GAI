import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import type { Conversation } from '../../../shared/chat.ts';
import type { RepositoryInvestigation } from '../../../shared/repository.ts';
import type { Document } from './codebase-data.ts';
import { sha256 } from './codebase-data.ts';
import { passageId } from '../knowledge/retrieval.ts';
import { scoreRepositoryReply, validateRepositoryFollowups, validateRepositoryResearch, validateRepositorySuite } from './repository-chat-metrics.ts';
import type { ExpectedReply, RepositoryResearchSuite } from './repository-chat-metrics.ts';

const identity = { repository: 'test', commit: 'a'.repeat(40), manifestSha256: 'b'.repeat(64) };
const task = { id: 'review-service', title: 'Review service', paths: ['src/service.ts'], checks: [{ command: 'bun run typecheck', cwd: 'persistence' }] };
function document(path: string, text: string): Document {
    return { text, sha256: sha256(text), sources: [{ path, commit: identity.commit, blob: 'c'.repeat(40) }] };
}
const documents = [document('src/service.ts', "import { core } from './core.ts';\nexport const service = core;"),
    document('src/core.ts', 'export const core = 1;'), document('docs/codebase-tasks.json', JSON.stringify({ version: 1, tasks: [task] }, null, 2))];
const sources = documents.map(doc => ({ id: passageId(doc.sha256, 1, doc.text.split('\n').length), path: doc.sources[0]!.path,
    excerpt: doc.text, commit: identity.commit, blob: 'c'.repeat(40), documentSha256: doc.sha256, passageSha256: sha256(doc.text),
    manifestSha256: identity.manifestSha256, coordinateSystem: 'snapshot-normalized-lines' as const, startLine: 1, endLine: doc.text.split('\n').length }));
const investigation: RepositoryInvestigation = { kind: 'impact', target: 'src/service.ts', snapshot: identity,
    findings: [{ kind: 'target', path: 'src/service.ts', sourceId: sources[0]!.id },
        { kind: 'dependency', path: 'src/service.ts', relatedPath: 'src/core.ts', sourceId: sources[0]!.id }],
    relatedTasks: [{ taskId: task.id, title: task.title, checks: task.checks, sourceId: sources[2]!.id }],
    limitations: ['Static imports only. Proposed checks have not been run.'], truncated: false };
const expected: ExpectedReply = { outcome: 'supported', investigation: { kind: 'impact', target: 'src/service.ts',
    findings: [{ kind: 'dependency', path: 'src/service.ts', relatedPath: 'src/core.ts', quote: "import { core }" }], relatedTasks: [task.id] },
    state: { investigation: { kind: 'impact', target: 'src/service.ts' } } };
type Mutable<T> = { -readonly [Key in keyof T]: Mutable<T[Key]> };
function conversation(): Mutable<Conversation> {
    return structuredClone({ scope: 'repository', repositorySnapshot: identity,
        repositoryState: { snapshot: identity, investigation: { kind: 'impact', target: 'src/service.ts' } },
        messages: [{ role: 'assistant', status: 'complete', finishReason: 'sources', content: 'Static evidence and proposed checks.', sources,
            research: { queries: 0, fetched: 0 }, investigation }] }) as unknown as Mutable<Conversation>;
}
function failures(value: Conversation, gold = expected) {
    return scoreRepositoryReply(gold, value, documents, identity).filter(check => !check.passed).map(check => check.name);
}

test('research scores bind exact relationship tuples to valid source evidence and the pinned identity', () => {
    assert.deepEqual(failures(conversation()), []);
    const otherSource = conversation();
    otherSource.messages[0]!.investigation!.findings[1]!.sourceId = sources[1]!.id;
    assert(failures(otherSource).includes('all investigation findings bind existing paths to cited evidence'));
    const wrongRelation = conversation();
    wrongRelation.messages[0]!.investigation!.findings[1]!.relatedPath = 'src/service.ts';
    assert(failures(wrongRelation).some(name => name.startsWith('investigation finding: dependency')));
    const missingPath = conversation();
    missingPath.messages[0]!.investigation!.findings[1]!.relatedPath = 'src/invented.ts';
    assert(failures(missingPath).includes('all investigation findings bind existing paths to cited evidence'));
    const stale = conversation();
    stale.messages[0]!.investigation!.snapshot = { ...identity, manifestSha256: 'd'.repeat(64) };
    assert(failures(stale).includes('investigation uses the conversation snapshot'));
});

test('reviewed check proposals require exact command, cwd, title and cited task-record provenance', () => {
    for (const mutate of [
        (value: Mutable<RepositoryInvestigation>) => { value.relatedTasks[0]!.checks[0]!.command = 'bun run imaginary'; },
        (value: Mutable<RepositoryInvestigation>) => { value.relatedTasks[0]!.checks[0]!.cwd = '.'; },
        (value: Mutable<RepositoryInvestigation>) => { value.relatedTasks[0]!.title = 'Invented review'; },
        (value: Mutable<RepositoryInvestigation>) => { value.relatedTasks[0]!.sourceId = sources[0]!.id; },
    ]) {
        const value = conversation();
        mutate(value.messages[0]!.investigation!);
        assert(failures(value).includes('all investigation check proposals match cited reviewed task records'));
    }
    const executed = conversation();
    executed.messages[0]!.investigation!.limitations = ['All checks passed.'];
    assert(failures(executed).includes('investigation distinguishes check proposals from execution'));
    const unrelated = conversation();
    unrelated.messages[0]!.investigation!.target = 'src/core.ts';
    assert(failures(unrelated).includes('all investigation check proposals match cited reviewed task records'),
        'A valid reviewed task still cannot supply checks for an unrelated investigation subject');
});

test('research scores enforce bounded results, explicit limitations and clearing of previous focus', () => {
    const expanded = conversation();
    expanded.messages[0]!.investigation!.findings = Array.from({ length: 15 }, () => investigation.findings[0]!);
    assert(failures(expanded).includes('investigation respects finding and task bounds'));
    const missing = conversation();
    delete missing.messages[0]!.investigation;
    assert(failures(missing).includes('requested investigation is present'));
    const unresolved: ExpectedReply = { outcome: 'clarification', investigation: null, state: { investigation: null } };
    assert(failures(conversation(), unresolved).includes('reply does not reuse a previous investigation'));
    assert(failures(conversation(), unresolved).includes('persisted investigation matches current intent'));
    missing.messages[0]!.finishReason = 'clarification';
    delete missing.repositoryState!.investigation;
    assert.deepEqual(failures(missing, unresolved), []);
    const emptyLimits = conversation();
    emptyLimits.messages[0]!.investigation!.limitations = [];
    assert(failures(emptyLimits).includes('investigation declares static-analysis limitations and truncation'));
});

test('eight research development chains stay separate and reject stale relationship gold or malformed requests', () => {
    const suite = JSON.parse(readFileSync(new URL('../../../../examples/chat/repository-research-v1.json', import.meta.url), 'utf8')) as RepositoryResearchSuite;
    const byPath = new Map<string, string[]>();
    function include(path: string, quote = 'A source fixture.') { byPath.set(path, [...(byPath.get(path) ?? []), quote]); }
    for (const scenario of suite.scenarios) for (const turn of scenario.turns) {
        for (const evidence of turn.expected.evidence ?? []) include(evidence.path, evidence.quote);
        for (const path of turn.expected.sourcePaths ?? []) include(path);
        for (const finding of turn.expected.investigation?.findings ?? []) {
            include(finding.path, finding.quote);
            if (finding.relatedPath) include(finding.relatedPath);
        }
    }
    const goldDocuments = [...byPath].map(([path, quotes]) => document(path, quotes.join('\n')));
    assert.equal(validateRepositoryResearch(suite, goldDocuments).scenarios.length, 8);
    assert(suite.scenarios.every(scenario => scenario.turns.length >= 4));
    assert.throws(() => validateRepositorySuite(suite, goldDocuments), /Expected 24 questions/);
    assert.throws(() => validateRepositoryFollowups(suite, goldDocuments), /development-only follow-up/);
    assert.throws(() => validateRepositoryResearch(suite, []), /Missing investigation finding evidence/);
    const acceptance = structuredClone(suite);
    acceptance.scenarios[0]!.split = 'acceptance';
    assert.throws(() => validateRepositoryResearch(acceptance, goldDocuments), /development-only repository research/);
    const malformed = structuredClone(suite);
    malformed.scenarios[0]!.turns[0]!.repositoryResearch!.target = '';
    assert.throws(() => validateRepositoryResearch(malformed, goldDocuments), /Invalid investigation selection/);
    const invalidRelation = structuredClone(suite);
    invalidRelation.scenarios[0]!.turns[0]!.expected.investigation!.findings![1]!.relatedPath = 'src/invented.ts';
    assert.throws(() => validateRepositoryResearch(invalidRelation, goldDocuments), /Missing investigation relationship target/);
});
