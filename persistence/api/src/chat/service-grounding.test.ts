import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import type { ChatReply } from '../../../shared/chat.ts';
import type { ChatWorkers, ChatWorkerTask } from './workers.ts';
import { ChatService, fingerprintBytes } from './service.ts';
import { FileChatStore } from './file-store.ts';
import { ResearchService } from './research.ts';
import { MemoryService } from './memory.ts';

const quote = 'Automatic training is not enabled.';
const reply: ChatReply = { content: 'automatic training is not enabled .', finishReason: 'eos', generatedTokens: 6,
    promptTokens: 15, unknownTokens: 0, droppedMessages: 0, evidenceTokens: 8, droppedEvidence: 0 };
async function fixture(output: ChatReply = reply, protocolVersion = 2, hasSources = true) {
    const directory = await mkdtemp(join(tmpdir(), 'cgai-grounding-'));
    const store = await FileChatStore.open(join(directory, 'store.json'));
    const payload = Buffer.from('fixture artifact');
    await store.publishModel('fixture', { payload, checksumSha256: fingerprintBytes(payload), createdAt: new Date().toISOString(),
        provenance: null, metadata: { engineKind: 'neural-centroid-chat', formatVersion: protocolVersion, protocolVersion,
            tokenizerVersion: 1, config: { promptWindow: 160, evidenceWindow: 80 }, vocabularySize: 64, parameterCount: 1000 } });
    const tasks: ChatWorkerTask[] = [], queries: string[] = [];
    const workers: ChatWorkers = { async run<T>(task: ChatWorkerTask) { tasks.push(task); return output as T; }, close() {} };
    const research = new ResearchService(new MemoryService(store, 'owner'), { name: 'fixture', async search(query) {
        queries.push(query); return hasSources ? [{ url: 'https://example.com/training', title: 'Training' }] : [];
    } }, undefined, async url => ({ url, text: quote, contentType: 'text/plain' }));
    const service = new ChatService(store, workers, 'owner', research);
    return { service, store, tasks, queries, async close() {
        await service.close(); await store.close();
        assert.equal(dirname(resolve(directory)), resolve(tmpdir()));
        await rm(directory, { recursive: true, force: true });
    } };
}
const request = { requestId: 'one', revision: 0, content: 'Is automatic training enabled?', answerMode: 'neural' as const };

test('neural service supplies current evidence and persists only a matched original quotation', async () => {
    const context = await fixture();
    try {
        const conversation = await context.service.create('fixture');
        const result = await context.service.send(conversation.id, request);
        const answer = result.conversation.messages.at(-1)!;
        assert.equal(answer.grounding?.status, 'quoted');
        assert.equal(answer.sources?.length, 1);
        assert.equal(answer.grounding?.claims[0]?.quote, quote);
        const task = context.tasks[0]!;
        assert.equal(task.kind, 'reply');
        if (task.kind === 'reply') assert.deepEqual(task.messages, [
            { role: 'evidence', content: quote }, { role: 'user', content: request.content },
        ]);
        assert.deepEqual(context.queries, [request.content]);
        assert.deepEqual(await context.service.send(conversation.id, request), result);
        assert.equal(context.tasks.length, 1);
        assert.deepEqual(await context.store.getConversation(conversation.id), result.conversation);
    } finally { await context.close(); }
});

test('unsupported neural claims and unretained evidence fall back without persisting generated misinformation', async () => {
    for (const output of [{ ...reply, content: 'automatic training is enabled .' }, { ...reply, unknownTokens: 1 },
        { ...reply, droppedEvidence: 1 }, { ...reply, finishReason: 'repetition' as const }]) {
        const context = await fixture(output);
        try {
            const conversation = await context.service.create('fixture');
            const answer = (await context.service.send(conversation.id, request)).conversation.messages.at(-1)!;
            assert.equal(answer.grounding?.status, 'fallback');
            assert.equal(answer.finishReason, 'sources');
            assert.equal(answer.research?.mode, 'sources');
            assert.ok(answer.content.includes(quote));
            assert.ok(!answer.content.includes('automatic training is enabled .'));
            assert.deepEqual(answer.grounding?.claims, []);
        } finally { await context.close(); }
    }
});

test('missing evidence abstains before generation; legacy models return sources until retrained', async () => {
    for (const [protocol, hasSources] of [[2, false], [1, true]] as const) {
        const context = await fixture(reply, protocol, hasSources);
        try {
            const conversation = await context.service.create('fixture');
            const answer = (await context.service.send(conversation.id, request)).conversation.messages.at(-1)!;
            assert.equal(answer.finishReason, hasSources ? 'sources' : 'abstained');
            assert.equal(context.tasks.length, 0);
        } finally { await context.close(); }
    }
});

test('default source mode and offline neural requests honor research choices without worker calls', async () => {
    const context = await fixture();
    try {
        const first = await context.service.create('fixture');
        const { answerMode: _mode, ...sourceRequest } = request;
        const answer = (await context.service.send(first.id, sourceRequest)).conversation.messages.at(-1)!;
        assert.equal(answer.finishReason, 'sources');
        const second = await context.service.create('fixture');
        const offline = (await context.service.send(second.id, { ...request, autoSearch: false })).conversation.messages.at(-1)!;
        assert.equal(offline.finishReason, 'abstained');
        assert.equal(context.tasks.length, 0);
        assert.equal(context.queries.length, 1);
    } finally { await context.close(); }
});
