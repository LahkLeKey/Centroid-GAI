import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import type { ChatSendRequest, Conversation } from '../../../shared/chat.ts';
import type { RepositoryAnswer, RepositorySnapshotIdentity } from '../../../shared/repository.ts';
import { FileChatStore } from './file-store.ts';
import { MemoryService } from './memory.ts';
import { RepositoryService } from './repository.ts';
import { writeRepositoryFixture } from './repository-fixture.ts';
import { ResearchService } from './research.ts';
import { ChatService, ChatServiceError } from './service.ts';
import type { ChatWorkers, ChatWorkerTask } from './workers.ts';

class UnusedWorkers implements ChatWorkers {
    calls = 0;
    async run<T>(_task: ChatWorkerTask): Promise<T> { ++this.calls; throw new Error('repository chat must not invoke neural workers'); }
    close() {}
}
async function setup() {
    const directory = await mkdtemp(join(tmpdir(), 'cgai-repository-service-'));
    const first = writeRepositoryFixture(join(directory, 'a'));
    const second = writeRepositoryFixture(join(directory, 'b'), 'b'.repeat(40), true);
    const path = join(directory, 'chat.json');
    const store = await FileChatStore.open(path);
    const workers = new UnusedWorkers();
    const repository = new RepositoryService(first.directory, [second.directory]);
    return { directory, first, second, path, store, workers, repository };
}
const request = (conversation: Conversation, content: string, requestId = 'request'): ChatSendRequest => ({
    requestId, revision: conversation.revision, content, autoSearch: true, rememberSources: true,
});
function hasStatus(status: number) { return (error: unknown) => error instanceof ChatServiceError && error.status === status; }

test('repository service persists task context across restart and isolates public memory, owners and sessions', async () => {
    const fixture = await setup();
    let store = fixture.store;
    let calls = 0;
    const memory = new MemoryService(store, 'owner');
    const research = new ResearchService(memory, { name: 'must-not-run', async search() { ++calls; throw new Error('no public research'); } });
    await memory.sources('what is the next step?', '', [{ id: 'public', path: 'https://example.com', excerpt: 'Deploy invented secret configuration.',
        contentHash: 'a'.repeat(64), fetchedAt: new Date().toISOString() }], 'public-session', new AbortController().signal);
    const originalMemory = await memory.read();
    let service = new ChatService(store, fixture.workers, 'owner', research, fixture.repository);
    try {
        const created = await service.create(undefined, 'Codebase', { scope: 'repository' });
        assert.equal(created.modelName, null);
        assert.deepEqual(created.repositorySnapshot, fixture.first.identity);
        const other = await service.create(undefined, 'Other', { scope: 'repository' });
        const sent = await service.send(created.id, request(created, 'What is the next step?'));
        assert.equal(sent.conversation.repositoryState?.taskId, 'source-followups');
        assert.equal(sent.conversation.messages.at(-1)?.action?.taskId, 'source-followups');
        assert.equal(sent.conversation.messages.at(-1)?.research?.queries, 0);
        assert.deepEqual(sent.conversation.messages.at(-1)?.memoryIds, []);
        assert.deepEqual(await memory.read(), originalMemory);
        assert.deepEqual(await service.send(created.id, request(created, 'What is the next step?')), sent);
        const second = await service.send(other.id, request(other, 'Which files?'));
        assert.equal(second.conversation.messages.at(-1)?.finishReason, 'clarification');
        const alien = new ChatService(store, fixture.workers, 'different-owner', undefined, fixture.repository);
        await assert.rejects(alien.conversation(created.id), hasStatus(404));
        await service.close(); await store.close();
        store = await FileChatStore.open(fixture.path);
        service = new ChatService(store, fixture.workers, 'owner', undefined,
            new RepositoryService(fixture.first.directory, [fixture.second.directory]));
        await service.recover();
        const resumed = await service.conversation(created.id);
        assert.deepEqual(resumed.repositoryState, sent.conversation.repositoryState);
        assert.deepEqual(resumed.messages, sent.conversation.messages);
        const continued = await service.send(created.id, request(resumed, 'How should I verify that change?', 'followup'));
        assert.equal(continued.conversation.messages.at(-1)?.action?.taskId, 'source-followups');
        assert.match(continued.conversation.messages.at(-1)!.content, /node --test amber.test.ts/);
        assert.match(continued.conversation.messages.at(-1)!.content, /not run/);
        assert.equal(fixture.workers.calls, 0); assert.equal(calls, 0);
    } finally { await service.close(); await store.close(); await rm(fixture.directory, { recursive: true, force: true }); }
});

test('repository switch uses CAS, resets context and preserves old citation identities', async () => {
    const fixture = await setup();
    const service = new ChatService(fixture.store, fixture.workers, 'owner', undefined, fixture.repository);
    try {
        const created = await service.create(undefined, 'Codebase', { scope: 'repository', snapshot: fixture.first.identity });
        const sent = await service.send(created.id, request(created, 'What next?'));
        const oldMessages = structuredClone(sent.conversation.messages);
        const switched = await service.switchRepository(created.id, { revision: sent.conversation.revision, snapshot: fixture.second.identity });
        assert.equal(switched.revision, sent.conversation.revision + 1);
        assert.deepEqual(switched.repositoryState, { snapshot: fixture.second.identity });
        assert.deepEqual(switched.messages, oldMessages);
        assert.equal(switched.messages.at(-1)?.sources?.[0]?.commit, fixture.first.identity.commit);
        assert.deepEqual(switched.repositoryTransitions?.[0]?.from, fixture.first.identity);
        assert.deepEqual(switched.repositoryTransitions?.[0]?.to, fixture.second.identity);
        await assert.rejects(service.switchRepository(created.id, { revision: sent.conversation.revision, snapshot: fixture.first.identity }), hasStatus(409));
        const newAnswer = await service.send(created.id, request(switched, 'What is the next step?', 'after-switch'));
        assert.equal(newAnswer.conversation.messages.at(-1)?.action?.taskId, 'artifact-checks');
        assert(newAnswer.conversation.messages.at(-1)?.sources?.every(source => source.commit === fixture.second.identity.commit));
        await assert.rejects(service.switchRepository(created.id, { revision: newAnswer.conversation.revision,
            snapshot: { ...fixture.first.identity, commit: 'f'.repeat(40) } }), hasStatus(404));
        assert.deepEqual((await service.conversation(created.id)).repositorySnapshot, fixture.second.identity);
    } finally { await service.close(); await fixture.store.close(); await rm(fixture.directory, { recursive: true, force: true }); }
});

test('repository admission rejects scope mixing and unavailable or malformed snapshot identities before writing', async () => {
    const fixture = await setup();
    const service = new ChatService(fixture.store, fixture.workers, 'owner', undefined, fixture.repository);
    try {
        await assert.rejects(service.create('a-model', 'Bad', { scope: 'repository' }), /modelName/);
        await assert.rejects(service.create(undefined, 'Bad', { snapshot: fixture.first.identity }), /requires repository scope/);
        await assert.rejects(service.create(undefined, 'Bad', { scope: 'repository', snapshot: null as unknown as RepositorySnapshotIdentity }), hasStatus(400));
        const absent = new ChatService(fixture.store, fixture.workers, 'owner', undefined, new RepositoryService());
        assert.equal(absent.repositoryKnowledge().ready, false);
        await assert.rejects(absent.create(undefined, 'Bad', { scope: 'repository' }), hasStatus(503));
        assert.equal((await absent.create()).scope, 'public');
        const created = await service.create(undefined, 'Codebase', { scope: 'repository' });
        await assert.rejects(service.send(created.id, { ...request(created, 'Amber source'), answerMode: 'neural' }), hasStatus(400));
        await assert.rejects(service.send(created.id, { ...request(created, 'Amber source'), publicQuery: 'search privately' }), hasStatus(400));
        assert.equal((await service.conversation(created.id)).revision, 0);
        const { repositorySnapshot: _pin, ...broken } = created;
        await fixture.store.createConversation({ ...broken, id: 'missing-pin' });
        await assert.rejects(service.send('missing-pin', request(created, 'Amber source')), hasStatus(503));
        assert.equal((await service.conversation('missing-pin')).revision, 0);
    } finally { await service.close(); await fixture.store.close(); await rm(fixture.directory, { recursive: true, force: true }); }
});

test('repository cancellation keeps durable topic state unchanged even if answer resolves after abort', async () => {
    const fixture = await setup();
    const service = new ChatService(fixture.store, fixture.workers, 'owner', undefined, fixture.repository);
    try {
        const created = await service.create(undefined, 'Codebase', { scope: 'repository' });
        const sent = await service.send(created.id, request(created, 'What next?'));
        const before = structuredClone(sent.conversation.repositoryState);
        const original = fixture.repository.answer.bind(fixture.repository);
        let release!: (answer: RepositoryAnswer) => void;
        let entered!: () => void;
        const ready = new Promise<void>(resolve => { entered = resolve; });
        const late = await original(request(sent.conversation, 'Sapphire artifact byte order'), sent.conversation, new AbortController().signal);
        fixture.repository.answer = async () => { entered(); return new Promise<RepositoryAnswer>(resolve => { release = resolve; }); };
        const pending = service.send(created.id, request(sent.conversation, 'Sapphire artifact byte order', 'cancelled'));
        await ready;
        await assert.rejects(service.switchRepository(created.id, { revision: sent.conversation.revision + 1, snapshot: fixture.second.identity }), hasStatus(409));
        const cancelled = service.cancel(created.id, 'cancelled');
        await new Promise<void>(resolve => setImmediate(resolve));
        release(late);
        const completed = await cancelled;
        await pending;
        assert.equal(completed.messages.at(-1)?.status, 'cancelled');
        assert.equal(completed.messages.at(-1)?.repositoryState, undefined);
        assert.deepEqual(completed.repositoryState, before);
        fixture.repository.answer = original;
        const resumed = await service.send(created.id, request(completed, 'Which files?', 'resume'));
        assert.equal(resumed.conversation.messages.at(-1)?.action?.taskId, 'source-followups');
        assert.equal(fixture.workers.calls, 0);
    } finally { await service.close(); await fixture.store.close(); await rm(fixture.directory, { recursive: true, force: true }); }
});
