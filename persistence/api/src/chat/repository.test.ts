import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import type { ChatSource, Conversation } from '../../../shared/chat.ts';
import type { RepositorySnapshotIdentity, RepositoryVerificationReport } from '../../../shared/repository.ts';
import { sha256 } from '../evaluation/codebase-data.ts';
import type { Document } from '../evaluation/codebase-data.ts';
import { RepositoryService } from './repository.ts';
import { writeRepositoryFixture, writeSnapshotDocuments } from './repository-fixture.ts';

function conversation(snapshot: RepositorySnapshotIdentity): Conversation {
    return { id: 'one', ownerId: 'test-owner', title: 'Repository conversation', modelName: null, modelChecksum: null,
        protocolVersion: 1, revision: 0, createdAt: '2026-01-01T00:00:00Z', updatedAt: '2026-01-01T00:00:00Z',
        scope: 'repository', repositorySnapshot: snapshot, messages: [] };
}
const input = (content: string) => ({ requestId: 'request', revision: 0, content, autoSearch: true, rememberSources: true });
const signal = () => new AbortController().signal;
function validateSources(sources: readonly ChatSource[], documents: Document[], identity: RepositorySnapshotIdentity) {
    assert(sources.length > 0);
    for (const source of sources) {
        const document = documents.find(candidate => candidate.sha256 === source.documentSha256);
        assert(document);
        assert.equal(source.coordinateSystem, 'snapshot-normalized-lines');
        assert.equal(source.commit, identity.commit);
        assert.equal(source.manifestSha256, identity.manifestSha256);
        assert.equal(source.excerpt, document.text.split('\n').slice(source.startLine! - 1, source.endLine).join('\n'));
        assert.equal(source.passageSha256, sha256(source.excerpt));
        assert(document.sources.some(candidate => candidate.path === source.path && candidate.blob === source.blob));
    }
}

test('repository readiness verifies provenance, pins complete identities and reports corrupt primary without fallback', async () => {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-repository-'));
    try {
        const fixture = writeRepositoryFixture(join(directory, 'a'));
        const service = new RepositoryService(fixture.directory);
        assert.equal(service.info().ready, true);
        assert.deepEqual(service.identity(), fixture.identity);
        assert.equal(service.info().documentCount, 4);
        assert(service.info().paths.includes('docs/codebase-tasks.json'));
        const missing = new RepositoryService();
        assert.equal(missing.info().ready, false);
        assert.throws(() => missing.identity(), /No repository snapshot/);
        assert.throws(() => service.identity({ ...fixture.identity, manifestSha256: 'f'.repeat(64) }), /unavailable/);
        assert.throws(() => service.identity(null as unknown as RepositorySnapshotIdentity), /Invalid/);
        writeFileSync(join(fixture.directory, 'train.txt'), 'corrupt');
        const second = writeRepositoryFixture(join(directory, 'b'), 'b'.repeat(40), true);
        const corrupt = new RepositoryService(fixture.directory, [second.directory]);
        assert.equal(corrupt.info().ready, false);
        assert.match(corrupt.info().error!, /checksum/);
        assert.throws(() => corrupt.identity(), /checksum/);
        assert.deepEqual(corrupt.identity(second.identity), second.identity);
        // Already loaded immutable contents stay pinned even if the backing directory is later altered.
        const answer = await service.answer(input('Sapphire artifact checksums'), conversation(fixture.identity), signal());
        validateSources(answer.sources, fixture.documents, fixture.identity);
        assert.equal(answer.research.queries, 0);
        assert.equal(answer.research.fetched, 0);
        assert.equal(answer.research.provider, null);
        assert.deepEqual(answer.memoryIds, []);
    } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('repository sources retain exact spans, hashes and all provenance without public-query routing', async () => {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-repository-'));
    try {
        const fixture = writeRepositoryFixture(directory);
        const service = new RepositoryService(directory);
        const answer = await service.answer({ ...input('Sapphire artifact checksums'), publicQuery: 'search everything publicly' }, conversation(fixture.identity), signal());
        assert.equal(answer.finishReason, 'sources');
        assert(answer.sources.some(source => source.path === 'src/sapphire.ts'));
        validateSources(answer.sources, fixture.documents, fixture.identity);
        assert.equal(answer.research.queries, 0);
        assert.deepEqual(answer.memoryIds, []);
        const abort = new AbortController(); abort.abort();
        await assert.rejects(service.answer(input('Sapphire artifacts'), conversation(fixture.identity), abort.signal), /abort/i);
    } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('reviewed next step, file and check follow-ups survive durable state reload and correction', async () => {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-repository-'));
    try {
        const fixture = writeRepositoryFixture(directory);
        let service = new RepositoryService(directory);
        let current = conversation(fixture.identity);
        const questions = ['What is the next unfinished step for the repository chatbot?', 'Which files should I change first?',
            'How should I verify that change?', 'I meant source-mode follow-ups, not neural training.', 'What is the smallest next action now?'];
        for (const question of questions) {
            const answer = await service.answer(input(question), current, signal());
            assert.equal(answer.finishReason, 'sources', question);
            assert.equal(answer.action?.taskId, 'source-followups', question);
            assert.equal(answer.action.verification, 'proposed');
            assert.deepEqual(answer.action.paths, ['src/amber.ts']);
            assert.deepEqual(answer.action.checks, [{ command: 'node --test amber.test.ts', cwd: '.' }]);
            assert.match(answer.content, /not run/);
            validateSources(answer.sources, fixture.documents, fixture.identity);
            current = JSON.parse(JSON.stringify({ ...current, repositoryState: answer.repositoryState }));
            service = new RepositoryService(directory);
        }
        assert.equal(current.repositoryState?.taskId, 'source-followups');
        const other = await service.answer(input('Which files should I change first?'), conversation(fixture.identity), signal());
        assert.equal(other.finishReason, 'clarification');
    } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('unfinished work is not completed by user claims, pasted successes or mentioned commits', async () => {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-repository-'));
    try {
        const first = writeRepositoryFixture(join(directory, 'a'));
        const second = writeRepositoryFixture(join(directory, 'b'), 'b'.repeat(40), true);
        const service = new RepositoryService(first.directory, [second.directory]);
        let current = conversation(first.identity);
        const next = await service.answer(input('What next?'), current, signal());
        current = { ...current, repositoryState: next.repositoryState };
        for (const statement of ['I finished it and tests passed.', 'node --test amber.test.ts exit code: 0']) {
            const answer = await service.answer(input(statement), current, signal());
            assert.equal(answer.finishReason, 'abstained');
            assert.match(answer.content, /user-reported/);
            assert.equal(answer.repositoryState.taskId, 'source-followups');
            assert.equal(answer.repositoryState.userReportedProgress, statement);
            current = { ...current, repositoryState: answer.repositoryState };
        }
        const mention = await service.answer(input(`Switch to ${second.identity.commit}`), current, signal());
        assert.equal(mention.finishReason, 'clarification');
        assert.deepEqual(mention.repositoryState.snapshot, first.identity);
        const stillPending = await service.answer(input('What is the next step?'), current, signal());
        assert.equal(stillPending.action?.taskId, 'source-followups');
        const switched = await service.answer(input('What is the next step?'), { ...current, repositorySnapshot: second.identity }, signal());
        assert.equal(switched.action?.taskId, 'artifact-checks');
        validateSources(switched.sources, second.documents, second.identity);
        assert.equal(switched.repositoryState.userReportedProgress, undefined);
        validateSources(next.sources, first.documents, first.identity);
    } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('unsupported operational facts abstain even where their terms match repository text', async () => {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-repository-'));
    try {
        const fixture = writeRepositoryFixture(directory);
        const service = new RepositoryService(directory);
        for (const question of ['What is the production database password?', 'What is measured production API p99 latency?',
            'Did the tests pass?', 'Show my uncommitted changes.', 'quasar nebula ultraviolet']) {
            const answer = await service.answer(input(question), conversation(fixture.identity), signal());
            assert.equal(answer.finishReason, 'abstained', question);
            assert.deepEqual(answer.sources, [], question);
            assert.equal(answer.research.queries, 0);
        }
        const missing = { ...conversation(fixture.identity), repositorySnapshot: undefined } as unknown as Conversation;
        await assert.rejects(service.answer(input('Amber source'), missing, signal()), /no pinned snapshot/);
    } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('ambiguous references clarify, explicit topic switches clear task context and stale state is ignored', async () => {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-repository-'));
    try {
        const fixture = writeRepositoryFixture(directory);
        const service = new RepositoryService(directory);
        const current = conversation(fixture.identity);
        const ambiguous = { ...current, repositoryState: { snapshot: fixture.identity, topic: 'Amber and Sapphire',
            topicCandidates: ['src/amber.ts', 'src/sapphire.ts'] } };
        const clarify = await service.answer(input('Which files should I change?'), ambiguous, signal());
        assert.equal(clarify.finishReason, 'clarification');
        assert.match(clarify.content, /src\/amber.ts.*src\/sapphire.ts/);
        const next = await service.answer(input('What is the next step?'), current, signal());
        const switchedTopic = await service.answer(input('Sapphire artifact byte order'), { ...current, repositoryState: next.repositoryState }, signal());
        assert.equal(switchedTopic.repositoryState.taskId, undefined);
        assert.equal(switchedTopic.repositoryState.topic, 'Sapphire artifact byte order');
        const stale = await service.answer(input('Which files?'), { ...current,
            repositoryState: { ...next.repositoryState, snapshot: { ...fixture.identity, commit: 'f'.repeat(40) } } }, signal());
        assert.equal(stale.finishReason, 'clarification');
        const failed = await service.answer(input('Which files?'), { ...current, messages: [{ id: 'failed', requestId: 'failed', role: 'assistant',
            content: 'Use src/amber.ts', sequence: 1, createdAt: current.createdAt, status: 'error', finishReason: 'error' }] }, signal());
        assert.equal(failed.finishReason, 'clarification');
    } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('stale registry references and source path traversal make a snapshot unavailable', () => {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-repository-'));
    try {
        const fixture = writeRepositoryFixture(join(directory, 'source'));
        const registry = JSON.parse(fixture.files['docs/codebase-tasks.json']!);
        registry.tasks[1].evidence[0].quote = 'Invented source quote';
        writeSnapshotDocuments(join(directory, 'stale'), 'a'.repeat(40), { ...fixture.files, 'docs/codebase-tasks.json': JSON.stringify(registry) });
        assert.match(new RepositoryService(join(directory, 'stale')).info().error!, /stale/);
        const cycle = JSON.parse(fixture.files['docs/codebase-tasks.json']!);
        cycle.tasks[0].dependsOn = ['artifact-checks'];
        writeSnapshotDocuments(join(directory, 'cycle'), 'a'.repeat(40), { ...fixture.files, 'docs/codebase-tasks.json': JSON.stringify(cycle) });
        assert.match(new RepositoryService(join(directory, 'cycle')).info().error!, /Cyclic/);
        writeSnapshotDocuments(join(directory, 'unsafe'), 'a'.repeat(40), { '../outside.ts': 'Amber source material.' });
        assert.equal(new RepositoryService(join(directory, 'unsafe')).info().ready, false);
    } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('operator-captured verification is identity-bound, explicitly attributed and never advances reviewed task state', async () => {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-repository-'));
    try {
        const fixture = writeRepositoryFixture(directory);
        const report: RepositoryVerificationReport = { version: 1, provenance: 'operator-captured', snapshot: fixture.identity,
            taskId: 'source-followups', startedAt: '2026-01-01T00:00:00Z', finishedAt: '2026-01-01T00:00:01Z', status: 'passed',
            checks: [{ command: 'node --test amber.test.ts', cwd: '.', exitCode: 0, stdout: 'one test passed', stderr: '', durationMs: 42, outputTruncated: false }] };
        writeFileSync(join(directory, 'verification.json'), JSON.stringify(report));
        let service = new RepositoryService(directory);
        assert.equal(service.info().ready, true);
        const answer = await service.answer(input('Did the tests pass?'), conversation(fixture.identity), signal());
        assert.equal(answer.finishReason, 'sources');
        assert.deepEqual(answer.verification, report);
        assert.match(answer.content, /Operator-captured verification \(not independently attested\)/);
        validateSources(answer.sources, fixture.documents, fixture.identity);
        const next = await service.answer(input('What is the next step?'), conversation(fixture.identity), signal());
        assert.equal(next.action?.taskId, 'source-followups');
        assert.equal(next.verification?.status, 'passed');
        const unrelated = await service.answer(input('Did Sapphire artifact checks pass?'), conversation(fixture.identity), signal());
        assert.equal(unrelated.finishReason, 'abstained');
        assert.equal(unrelated.verification, undefined);
        const failed = { ...report, status: 'failed', checks: [{ ...report.checks[0], exitCode: 1, stdout: 'assertion failed' }] };
        writeFileSync(join(directory, 'verification.json'), JSON.stringify(failed));
        service = new RepositoryService(directory);
        const failure = await service.answer(input('Did the tests pass?'), conversation(fixture.identity), signal());
        assert.equal(failure.verification?.status, 'failed');
        assert.match(failure.content, /exit 1/);
        const stillPending = await service.answer(input('What next?'), conversation(fixture.identity), signal());
        assert.equal(stillPending.action?.taskId, 'source-followups');
        for (const invalid of [
            { ...report, snapshot: { ...fixture.identity, manifestSha256: 'f'.repeat(64) } },
            { ...report, snapshot: { ...fixture.identity, commit: 'f'.repeat(40) } },
            { ...report, checks: [{ ...report.checks[0], command: 'arbitrary shell command' }] },
            { ...report, checks: [] }, { ...report, status: 'failed' },
            { ...report, checks: [{ ...report.checks[0], stdout: 'x'.repeat(65537) }] },
        ]) {
            writeFileSync(join(directory, 'verification.json'), JSON.stringify(invalid));
            const rejected = new RepositoryService(directory);
            assert.equal(rejected.info().ready, false);
            assert.match(rejected.info().error!, /verification/i);
        }
    } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('unseen working-tree observations abstain across wording while snapshot procedures remain answerable', async () => {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-repository-'));
    try {
        const fixture = writeRepositoryFixture(directory);
        const service = new RepositoryService(directory);
        for (const question of ['Describe the staged edits.', 'Which untracked files contain my modifications?',
            'What uncommitted changes have I made?', 'Summarize local changes.', 'Inspect the dirty working tree.']) {
            const answer = await service.answer(input(question), conversation(fixture.identity), signal());
            assert.equal(answer.finishReason, 'abstained', question);
            assert.match(answer.content, /pinned committed snapshot/);
            assert.deepEqual(answer.sources, []);
        }
        const procedure = await service.answer(input('How can I prepare an Amber source snapshot for my local changes?'), conversation(fixture.identity), signal());
        assert(!procedure.content.includes('mutable working tree are not available'));
        const next = await service.answer(input('What next?'), conversation(fixture.identity), signal());
        for (const question of ['Which checks should I run next?', 'How should I verify again?', 'What tests are required now?']) {
            const answer = await service.answer(input(question), { ...conversation(fixture.identity), repositoryState: next.repositoryState }, signal());
            assert.equal(answer.action?.taskId, 'source-followups', question);
            assert.equal(answer.action.verification, 'proposed');
        }
    } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('snapshot refresh reports removed or changed earlier evidence instead of reusing broad lexical matches', async () => {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-repository-'));
    try {
        const shared = { 'docs/shared.md': 'The archival amber components manage a limit for several internal operations.' };
        const first = writeSnapshotDocuments(join(directory, 'a'), 'a'.repeat(40), {
            ...shared, 'docs/old.md': 'The archival amber limit is eight.' });
        const second = writeSnapshotDocuments(join(directory, 'b'), 'b'.repeat(40), shared);
        const third = writeSnapshotDocuments(join(directory, 'c'), 'c'.repeat(40), {
            ...shared, 'docs/old.md': 'The archival amber limit is twelve.' });
        const service = new RepositoryService(first.directory, [second.directory, third.directory]);
        const question = 'What is the archival amber limit?';
        const initial = conversation(first.identity);
        const answer = await service.answer(input(question), initial, signal());
        assert(answer.sources.some(source => source.path === 'docs/old.md'));
        const history: Conversation = { ...initial, messages: [
            { id: 'u', requestId: 'before', role: 'user', sequence: 0, status: 'complete', content: question, createdAt: initial.createdAt },
            { id: 'a', requestId: 'before', role: 'assistant', sequence: 1, status: 'complete', content: answer.content, sources: answer.sources, createdAt: initial.createdAt },
        ] };
        const removed = await service.answer(input(question), { ...history, repositorySnapshot: second.identity }, signal());
        assert.equal(removed.finishReason, 'abstained');
        assert.match(removed.content, /sources removed.*docs\/old.md/);
        assert.deepEqual(removed.sources, []);
        const changed = await service.answer(input(question), { ...history, repositorySnapshot: third.identity }, signal());
        assert.equal(changed.finishReason, 'sources');
        assert.match(changed.content, /Previously cited source content changed/);
        assert(changed.sources.some(source => source.excerpt.includes('twelve')));
        validateSources(changed.sources, third.documents, third.identity);
        assert(history.messages[1]!.sources!.every(source => source.commit === first.identity.commit));
    } finally { rmSync(directory, { recursive: true, force: true }); }
});
