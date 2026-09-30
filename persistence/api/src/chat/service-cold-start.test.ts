import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { FileChatStore } from './file-store.ts';
import { ChatService, ChatServiceError } from './service.ts';
import { MemoryService } from './memory.ts';
import { ResearchService, type SearchProvider } from './research.ts';
import type { ChatWorkers } from './workers.ts';

async function fixture(provider?: SearchProvider) {
    const directory = await mkdtemp(join(tmpdir(), 'cgai-cold-start-'));
    const path = join(directory, 'store.json');
    const store = await FileChatStore.open(path);
    let workerCalls = 0;
    const workers: ChatWorkers = { async run() { ++workerCalls; throw new Error('cold start must not run neural workers'); }, close() {} };
    const research = new ResearchService(new MemoryService(store, 'owner'), provider, undefined,
        async (url) => ({ url, contentType: 'text/plain', text: 'Hello world is a simple first program used to demonstrate a language.' }));
    const service = new ChatService(store, workers, 'owner', research);
    let released: Promise<void> | undefined;
    const release = () => released ??= (async () => { await service.close(); await store.close(); })();
    return { path, store, service, workers, research, release, calls: () => workerCalls, async close() {
        await release();
        assert.equal(dirname(resolve(directory)), resolve(tmpdir()));
        await rm(directory, { recursive: true, force: true });
    } };
}

test('a fresh installation can research hello world without training or creating an artifact', async () => {
    const queries: string[] = [];
    const context = await fixture({ name: 'fixture', async search(query) {
        queries.push(query); return [{ url: 'https://example.com/hello', title: 'Hello world' }];
    } });
    try {
        const conversation = await context.service.create();
        assert.equal(conversation.modelName, null);
        assert.equal(conversation.modelChecksum, null);
        assert.deepEqual(await context.store.listModels(), []);
        const input = { requestId: 'hello', revision: 0, content: 'hello world', rememberSources: true };
        const response = await context.service.send(conversation.id, input);
        const assistant = response.conversation.messages[1]!;
        assert.equal(response.conversation.revision, 2);
        assert.equal(assistant.status, 'complete');
        assert.equal(assistant.finishReason, 'sources');
        assert.equal(assistant.research?.status, 'searched');
        assert.deepEqual(queries, ['hello world']);
        assert.equal(assistant.sources?.[0]?.url, 'https://example.com/hello');
        assert.equal(assistant.memoryIds?.length, 1);
        assert.deepEqual(await context.service.send(conversation.id, { ...input, autoSearch: true }), response);
        const repeated = await context.service.send(conversation.id, { ...input, requestId: 'repeat', revision: 2 });
        assert.equal(repeated.conversation.messages[3]?.research?.status, 'memory');
        assert.deepEqual(queries, ['hello world']);
        assert.equal(context.calls(), 0);
        assert.deepEqual(await context.store.listModels(), []);
        await context.release();
        const reopened = await FileChatStore.open(context.path);
        try { assert.deepEqual(await reopened.getConversation(conversation.id), repeated.conversation); }
        finally { await reopened.close(); }
    } finally { await context.close(); }
});

test('cold-start neural requests and invalid explicit model names fail before changing the transcript', async () => {
    const context = await fixture();
    const badRequest = (error: unknown) => error instanceof ChatServiceError && error.status === 400;
    try {
        for (const model of ['', null, false, [], {}]) await assert.rejects(context.service.create(model as never), badRequest);
        await assert.rejects(context.service.create('missing'), (error: unknown) => error instanceof ChatServiceError && error.status === 404);
        const conversation = await context.service.create();
        await assert.rejects(context.service.send(conversation.id, {
            requestId: 'neural', revision: 0, content: 'hello world', answerMode: 'neural',
        }), (error: unknown) => badRequest(error) && /trained modelName/.test((error as Error).message));
        assert.deepEqual(await context.service.conversation(conversation.id), conversation);
        const response = await context.service.send(conversation.id, { requestId: 'sources', revision: 0, content: 'hello world' });
        assert.equal(response.conversation.messages[1]?.research?.status, 'disabled');
        assert.equal(response.conversation.messages[1]?.finishReason, 'abstained');
        assert.equal(context.calls(), 0);
    } finally { await context.close(); }
});

test('a model-free conversation without a research service reports unavailability before admission', async () => {
    const context = await fixture();
    const service = new ChatService(context.store, context.workers, 'owner');
    try {
        const conversation = await service.create();
        await assert.rejects(service.send(conversation.id, { requestId: 'hello', revision: 0, content: 'hello world' }),
            (error: unknown) => error instanceof ChatServiceError && error.status === 503);
        assert.deepEqual(await service.conversation(conversation.id), conversation);
        assert.equal(context.calls(), 0);
    } finally { await service.close(); await context.close(); }
});
