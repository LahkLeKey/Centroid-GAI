import assert from 'node:assert/strict';
import test from 'node:test';
import { readFileSync } from 'node:fs';
import type { Conversation } from '../../../shared/chat.ts';
import type { Document } from './codebase-data.ts';
import { sha256 } from './codebase-data.ts';
import { passageId } from '../knowledge/retrieval.ts';
import { latencyPercentiles, scoreRepositoryReply, validRepositorySource, validateRepositorySuite } from './repository-chat-metrics.ts';
import type { RepositoryScenarioSuite } from './repository-chat-metrics.ts';

const manifestSha256 = 'f'.repeat(64);
const commit = 'a'.repeat(40);
const blob = 'b'.repeat(40);
const text = 'A reviewed fact.\nRun bun run typecheck to verify.';
const document: Document = { text, sha256: sha256(text), sources: [{ path: 'docs/facts.md', commit, blob }] };
const source = { id: passageId(document.sha256, 1, 2), path: 'docs/facts.md', excerpt: text,
    commit, blob, documentSha256: document.sha256, passageSha256: sha256(text),
    manifestSha256, coordinateSystem: 'snapshot-normalized-lines', startLine: 1, endLine: 2 };

test('repository source assertions reject fabricated spans and every provenance mismatch', () => {
    assert.equal(validRepositorySource(source, [document], manifestSha256), true);
    for (const update of [{ excerpt: 'fabricated' }, { commit: 'c'.repeat(40) }, { blob: 'c'.repeat(40) },
        { documentSha256: 'c'.repeat(64) }, { passageSha256: 'c'.repeat(64) }, { manifestSha256: 'c'.repeat(64) },
        { coordinateSystem: 'git-lines' }, { startLine: 0 }, { endLine: 3 }, { id: 'invented' }, { path: 'secret.txt' }])
        assert.equal(validRepositorySource({ ...source, ...update }, [document], manifestSha256), false, JSON.stringify(update));
});

test('reply scoring distinguishes valid citations from unsupported or invented actionable claims', () => {
    const conversation = { scope: 'repository', repositorySnapshot: { commit, manifestSha256 }, messages: [{ role: 'assistant',
        status: 'complete', finishReason: 'sources', content: 'A reviewed fact.', sources: [source], research: { queries: 0, fetched: 0 },
        action: { taskId: 'next', verification: 'proposed', paths: ['docs/facts.md'], checks: [{ command: 'bun run typecheck', cwd: 'persistence' }] },
    }] } as unknown as Conversation;
    const expected = { outcome: 'action' as const, evidence: [{ path: 'docs/facts.md', quote: 'A reviewed fact.' }], task: 'next' };
    assert(scoreRepositoryReply(expected, conversation, [document], { commit, manifestSha256 }).every(item => item.passed));
    const invented = structuredClone(conversation) as Conversation & { messages: [{ action: { paths: string[]; checks: { command: string; cwd: string }[] } }] };
    invented.messages[0].action.paths = ['src/nonexistent.c'];
    invented.messages[0].action.checks = [{ command: 'bun run imaginary', cwd: '.' }];
    const failed = scoreRepositoryReply(expected, invented, [document], { commit, manifestSha256 }).filter(item => !item.passed);
    assert.deepEqual(failed.map(item => item.name), ['action references existing paths', 'action commands appear in reviewed snapshot']);
    assert.equal(scoreRepositoryReply({ outcome: 'unsupported' }, conversation, [document], { commit, manifestSha256 })
        .find(item => item.name === 'abstains or clarifies')?.passed, false);
    const quoted = structuredClone(conversation) as typeof invented;
    quoted.messages[0].action.checks = [{ command: '"C:/Python314/python.exe" tools/check.py', cwd: '.' }];
    const registryText = JSON.stringify({ version: 1, tasks: [{ id: 'next', checks: quoted.messages[0].action.checks }] });
    const registry = { text: registryText, sha256: sha256(registryText), sources: [{ path: 'docs/codebase-tasks.json', commit, blob }] };
    assert(scoreRepositoryReply(expected, quoted, [document, registry], { commit, manifestSha256 }).every(item => item.passed),
        'JSON escaping must not hide the exact decoded reviewed command');
});

test('the 40-scenario fixture freezes all denominators and rejects stale gold setup', () => {
    const suite = JSON.parse(readFileSync(new URL('../../../../examples/chat/repository-scenarios-v1.json', import.meta.url), 'utf8')) as RepositoryScenarioSuite;
    const byPath = new Map<string, string[]>();
    for (const scenario of suite.scenarios) for (const turn of scenario.turns) for (const gold of turn.expected.evidence ?? [])
        byPath.set(gold.path, [...(byPath.get(gold.path) ?? []), gold.quote]);
    const documents = [...byPath].map(([path, quotes]) => {
        const text = quotes.join('\n');
        return { text, sha256: sha256(text), sources: [{ path, commit, blob }] };
    });
    assert.equal(validateRepositorySuite(suite, documents).scenarios.length, 40);
    assert.throws(() => validateRepositorySuite(suite, []), /Missing gold evidence/);
    const missing = structuredClone(suite);
    missing.scenarios.pop();
    assert.throws(() => validateRepositorySuite(missing, documents), /Expected 4 http/);
    const duplicate = structuredClone(suite);
    duplicate.scenarios[1]!.id = duplicate.scenarios[0]!.id;
    assert.throws(() => validateRepositorySuite(duplicate, documents), /duplicate/);
});

test('latency percentiles report empty observations as unavailable', () => {
    assert.deepEqual(latencyPercentiles([]), { samples: 0, p50Ms: null, p95Ms: null, maxMs: null });
    assert.deepEqual(latencyPercentiles([9, 1, 5, 4]), { samples: 4, p50Ms: 4, p95Ms: 9, maxMs: 9 });
});
