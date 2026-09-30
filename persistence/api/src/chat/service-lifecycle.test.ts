import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { FileChatStore } from './file-store.ts';
import { ChatService, ChatServiceError, fingerprintBytes } from './service.ts';
import type { ChatReply, ChatSendRequest } from '../../../shared/chat.ts';
import type { ChatWorkers, ChatWorkerTask } from './workers.ts';

const reply: ChatReply = { content: 'reply', finishReason: 'eos', generatedTokens: 1, promptTokens: 2, droppedMessages: 0, unknownTokens: 0 };
const training = { name: 'test', examples: [{ messages: [{ role: 'user' as const, content: 'question' }], answer: 'answer' }] };
const immediate = () => new Promise<void>((resolve) => setImmediate(resolve));
async function fixture(run: (task: ChatWorkerTask, signal?: AbortSignal) => Promise<unknown>) {
    const directory = await mkdtemp(join(tmpdir(), 'cgai-lifecycle-'));
    const store = await FileChatStore.open(join(directory, 'store.json'));
    const payload = Buffer.from('test model');
    await store.publishModel('test', { payload, checksumSha256: fingerprintBytes(payload), provenance: null,
        createdAt: new Date().toISOString(), metadata: { engineKind: 'neural-centroid-chat', formatVersion: 1,
            protocolVersion: 1, tokenizerVersion: 1, config: {}, vocabularySize: 8, parameterCount: 100 } });
    const workers: ChatWorkers = { run: async <T>(task: ChatWorkerTask, signal?: AbortSignal) => await run(task, signal) as T, close() {} };
    const service = new ChatService(store, workers, 'owner');
    return { store, service, workers, async close() { await service.close(); await store.close(); await rm(directory, { recursive: true, force: true }); } };
}

test('simultaneous retries share one execution and cancellation wins over a resolving worker', async () => {
    const started = Promise.withResolvers<void>();
    let calls = 0;
    const context = await fixture(async (_task, signal) => {
        ++calls; started.resolve();
        return new Promise<ChatReply>((resolve) => signal!.addEventListener('abort', () => resolve(reply), { once: true }));
    });
    try {
        const conversation = await context.service.create('test');
        const input = { requestId: 'same', revision: 0, content: 'question' };
        const responses = Array.from({ length: 8 }, () => context.service.send(conversation.id, input));
        await started.promise;
        await assert.rejects(context.service.send(conversation.id, { ...input, content: 'different' }), /different input/);
        const cancelled = await context.service.cancel(conversation.id, input.requestId);
        assert.equal(cancelled.messages.length, 2);
        assert.equal(cancelled.messages[1]!.status, 'cancelled');
        assert.equal(cancelled.messages[1]!.content, '');
        for (const response of await Promise.all(responses)) assert.deepEqual(response.conversation, cancelled);
        assert.equal(calls, 1);
    } finally { await context.close(); }
});

test('shutdown waits for message admission and prevents work after a delayed store read', async () => {
    let calls = 0;
    const context = await fixture(async () => { ++calls; return reply; });
    const entered = Promise.withResolvers<void>(), release = Promise.withResolvers<void>();
    try {
        const conversation = await context.service.create('test');
        const original = context.store.getConversation.bind(context.store);
        context.store.getConversation = async (id) => { entered.resolve(); await release.promise; return original(id); };
        const rejected = assert.rejects(context.service.send(conversation.id, { requestId: 'shutdown', revision: 0, content: 'question' }), /shutting down/);
        await entered.promise;
        let closed = false;
        const closing = context.service.close().then(() => { closed = true; });
        await immediate(); assert.equal(closed, false);
        release.resolve(); await rejected; await closing;
        assert.equal(calls, 0);
        assert.equal((await original(conversation.id))!.messages.length, 0);
        await assert.rejects(context.service.create('test'), /shutting down/);
    } finally { release.resolve(); await context.close(); }
});

test('a retry with a delayed old snapshot returns the already committed response', async () => {
    let calls = 0, reads = 0;
    const context = await fixture(async () => { ++calls; return reply; });
    const entered = Promise.withResolvers<void>(), release = Promise.withResolvers<void>();
    try {
        const conversation = await context.service.create('test');
        const original = context.store.getConversation.bind(context.store);
        context.store.getConversation = async (id) => {
            const delayed = ++reads === 2;
            const snapshot = await original(id);
            if (delayed) { entered.resolve(); await release.promise; }
            return snapshot;
        };
        const input = { requestId: 'same', revision: 0, content: 'question' };
        const first = context.service.send(conversation.id, input);
        const second = context.service.send(conversation.id, input);
        await entered.promise;
        const completed = await first;
        release.resolve();
        assert.deepEqual(await second, completed);
        assert.equal(calls, 1);
    } finally { release.resolve(); await context.close(); }
});

test('shutdown records interrupted training admission before storage is released', async () => {
    let calls = 0;
    const context = await fixture(async () => { ++calls; throw new Error('unexpected worker'); });
    const entered = Promise.withResolvers<void>(), release = Promise.withResolvers<void>();
    try {
        const original = context.store.saveJob.bind(context.store);
        context.store.saveJob = async (job) => {
            if (job.status === 'queued') { entered.resolve(); await release.promise; }
            await original(job);
        };
        const rejected = assert.rejects(context.service.train(training), /shutting down/);
        await entered.promise;
        let closed = false;
        const closing = context.service.close().then(() => { closed = true; });
        await immediate(); assert.equal(closed, false);
        release.resolve(); await rejected; await closing;
        assert.equal(calls, 0);
        const jobs = await context.store.listJobs();
        assert.equal(jobs.length, 1);
        assert.equal(jobs[0]!.status, 'error');
        assert.match(jobs[0]!.error!, /shutting down/);
    } finally { release.resolve(); await context.close(); }
});

test('concurrent training admission obeys the queue bound before durable writes finish', async () => {
    const context = await fixture(async () => { throw new Error('fixture training failure'); });
    const admitted = Promise.withResolvers<void>(), release = Promise.withResolvers<void>();
    let writes = 0;
    try {
        const original = context.store.saveJob.bind(context.store);
        context.store.saveJob = async (job) => {
            if (job.status === 'queued') { if (++writes === 16) admitted.resolve(); await release.promise; }
            await original(job);
        };
        const waiting = Array.from({ length: 16 }, (_, index) => context.service.train({ ...training, name: `test-${index}` }));
        await admitted.promise;
        await assert.rejects(context.service.train(training), (error: unknown) => error instanceof ChatServiceError && error.status === 429);
        assert.equal(writes, 16);
        release.resolve(); await Promise.all(waiting);
        await context.service.close();
        assert.ok((await context.store.listJobs()).every((job) => job.status === 'error'));
    } finally { release.resolve(); await context.close(); }
});

test('recovery changes only its owner and leaves locally running requests alone', async () => {
    const entered = Promise.withResolvers<void>(), release = Promise.withResolvers<ChatReply>();
    const context = await fixture(async () => { entered.resolve(); return release.promise; });
    try {
        const mine = await context.service.create('test');
        const other = new ChatService(context.store, context.workers, 'other');
        const theirs = await other.create('test');
        for (const conversation of [mine, theirs]) {
            await context.store.saveConversation({ ...conversation, revision: 1, messages: [{ id: 'pending', sequence: 0,
                requestId: 'interrupted', createdAt: new Date().toISOString(), role: 'user', content: 'question', status: 'pending' }] }, 0);
        }
        await context.service.recover();
        assert.equal((await context.service.conversation(mine.id)).messages[0]!.status, 'error');
        assert.equal((await other.conversation(theirs.id)).messages[0]!.status, 'pending');
        const active = await context.service.create('test');
        const response = context.service.send(active.id, { requestId: 'live', revision: 0, content: 'question' });
        await entered.promise;
        await context.service.recover();
        assert.equal((await context.service.conversation(active.id)).messages[0]!.status, 'pending');
        release.resolve(reply);
        assert.equal((await response).conversation.messages[1]!.status, 'complete');
    } finally { release.resolve(reply); await context.close(); }
});

test('research consent fields reject non-booleans before admitting a message', async () => {
    let calls = 0;
    const context = await fixture(async () => { ++calls; return reply; });
    try {
        const conversation = await context.service.create('test');
        for (const option of ['autoSearch', 'rememberSources']) {
            const input = { requestId: option, revision: 0, content: 'question', [option]: 'false' } as unknown as ChatSendRequest;
            await assert.rejects(context.service.send(conversation.id, input), /must be a boolean/);
        }
        assert.equal(calls, 0);
        assert.equal((await context.service.conversation(conversation.id)).revision, 0);
    } finally { await context.close(); }
});

test('invalid training setting shapes and numeric coercions fail before a job is persisted', async () => {
    let calls = 0;
    const context = await fixture(async () => { ++calls; throw new Error('unexpected worker'); });
    const badRequest = (error: unknown) => error instanceof ChatServiceError && error.status === 400;
    try {
        for (const field of ['config', 'training']) {
            for (const value of [null, false, true, 0, 4, '', 'settings', []])
                await assert.rejects(context.service.train({ ...training, [field]: value } as never), badRequest);
        }
        for (const value of [null, false, '4', [], Infinity, NaN]) {
            for (const field of ['embeddingDimensions', 'hiddenDimensions', 'centroidCount', 'promptWindow', 'responseWindow', 'routingTemperature'])
                await assert.rejects(context.service.train({ ...training, config: { [field]: value } } as never), badRequest);
            for (const field of ['epochs', 'learningRate'])
                await assert.rejects(context.service.train({ ...training, training: { [field]: value } } as never), badRequest);
        }
        for (const value of [null, 42, '-1', '18446744073709551616']) {
            await assert.rejects(context.service.train({ ...training, config: { seed: value as never }, training: { seed: '42' } }), badRequest);
            await assert.rejects(context.service.train({ ...training, training: { seed: value as never } }), badRequest);
        }
        assert.equal(calls, 0);
        assert.equal((await context.store.listJobs()).length, 0);
    } finally { await context.close(); }
});
