import assert from 'node:assert/strict';
import test from 'node:test';
import { randomUUID } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import type { ChatTrainingJob, Conversation, ChatSendResponse, ChatTrainRequest } from '../../shared/chat.ts';

const base = process.env.CGAI_API_URL;
async function call<T>(path: string, method = 'GET', body?: unknown, expected = 200): Promise<T> {
    const response = await fetch(`${base}/api/v1/${path}`, { method, headers: {
        'content-type': 'application/json', authorization: `Bearer ${process.env.CGAI_CHAT_API_TOKEN ?? ''}` },
        ...(body === undefined ? {} : { body: JSON.stringify(body) }), signal: AbortSignal.timeout(30000) });
    const value = await response.json();
    assert.equal(response.status, expected, JSON.stringify(value));
    return value as T;
}
test('cold-start HTTP conversation researches hello world without a trained model', { skip: !base }, async () => {
    const conversation = await call<Conversation>('conversations', 'POST', {}, 201);
    assert.equal(conversation.modelName, null);
    assert.equal(conversation.modelChecksum, null);
    const request = { requestId: randomUUID(), revision: 0, content: 'hello world', rememberSources: true };
    try {
        await call(`conversations/${conversation.id}/messages`, 'POST', { ...request, answerMode: 'neural' }, 400);
        assert.deepEqual(await call(`conversations/${conversation.id}`), conversation);
        const response = await call<ChatSendResponse>(`conversations/${conversation.id}/messages`, 'POST', request);
        const answer = response.conversation.messages[1]!;
        assert.equal(response.conversation.revision, 2);
        assert.equal(answer.status, 'complete');
        assert.equal(answer.usage?.generatedTokens, 0);
        if (process.env.CGAI_E2E_RESEARCH_LIVE === '1') {
            assert.equal(answer.research?.status, 'searched', answer.research?.reason);
            assert.equal(answer.research.queries, 1);
            assert.ok(answer.sources!.length > 0);
            assert.ok(answer.memoryIds!.length > 0);
            const cached = await call<ChatSendResponse>(`conversations/${conversation.id}/messages`, 'POST', {
                ...request, requestId: randomUUID(), revision: 2,
            });
            assert.equal(cached.conversation.messages[3]?.research?.status, 'memory');
            assert.deepEqual(cached.conversation.messages[3]?.sources, answer.sources);
        } else {
            assert.equal(answer.research?.status, 'disabled');
            assert.equal(answer.research.queries, 0);
        }
        assert.deepEqual(await call(`conversations/${conversation.id}/messages`, 'POST', { ...request, autoSearch: true }),
            { conversation: await call(`conversations/${conversation.id}`), requestId: request.requestId });
    } finally {
        const current = await call<Conversation>(`conversations/${conversation.id}`);
        await call(`conversations/${conversation.id}`, 'DELETE', { revision: current.revision, forgetMemory: true });
    }
});
test('neural HTTP lifecycle with real PostgreSQL and bounded workers', { skip: !base }, async () => {
    const name = `chat-${randomUUID()}`;
    const fixture = JSON.parse(readFileSync(new URL('../../../examples/api/chat-train.json', import.meta.url), 'utf8')) as ChatTrainRequest;
    const job = await call<ChatTrainingJob>('chat-models/train', 'POST', { ...fixture, name }, 202);
    let done = job;
    for (let attempts = 0; attempts < 200 && ['queued', 'running'].includes(done.status); attempts++) {
        await call('health');
        await new Promise((resolve) => setTimeout(resolve, 50));
        done = await call<ChatTrainingJob>(`chat-jobs/${job.id}`);
    }
    assert.equal(done.status, 'complete', done.error);
    const conversation = await call<Conversation>('conversations', 'POST', { modelName: name }, 201);
    const isolated = await call<Conversation>('conversations', 'POST', { modelName: name }, 201);
    const request = { requestId: randomUUID(), revision: 0, content: 'hello', answerMode: 'neural' };
    const sent = await call<ChatSendResponse>(`conversations/${conversation.id}/messages`, 'POST', request);
    assert.equal(sent.conversation.revision, 2);
    assert.equal(sent.conversation.messages[1]!.status, 'complete');
    assert.equal(sent.conversation.messages[1]!.content, 'hello there');
    assert.equal(sent.conversation.messages[1]!.finishReason, 'eos');
    assert.deepEqual(await call(`conversations/${conversation.id}/messages`, 'POST', request), sent);
    await call(`conversations/${conversation.id}/messages`, 'POST', { ...request, requestId: randomUUID() }, 409);
    assert.equal((await call<Conversation>(`conversations/${isolated.id}`)).messages.length, 0);
    const project = process.env.CGAI_E2E_PROJECT;
    if (project && /^centroid-gai-e2e-\d+$/.test(project)) {
        const restarted = spawnSync('docker', ['compose', '-p', project, 'restart', 'api'], { cwd: new URL('../../..', import.meta.url), encoding: 'utf8' });
        assert.equal(restarted.status, 0, restarted.stderr);
        let loaded: Conversation | undefined;
        for (let attempt = 0; attempt < 100; attempt++) {
            try { loaded = await call<Conversation>(`conversations/${conversation.id}`); break; }
            catch { await new Promise((resolve) => setTimeout(resolve, 100)); }
        }
        assert.deepEqual(loaded, sent.conversation);
    }
    const help = await call<ChatSendResponse>(`conversations/${isolated.id}/messages`, 'POST', {
        requestId: randomUUID(), revision: 0, content: 'what can you do', answerMode: 'neural' });
    assert.equal(help.conversation.messages[1]!.content, 'i can return source excerpts');
    assert.equal(help.conversation.messages[1]!.finishReason, 'eos');
    const unsupported = await call<ChatSendResponse>(`conversations/${conversation.id}/messages`, 'POST', {
        requestId: randomUUID(), revision: 2, content: 'unsupported private topic', autoSearch: false });
    assert.equal(unsupported.conversation.messages.at(-1)!.research?.mode, 'abstained');
    const memory = await call<{ id: string }>('memory', 'POST', { content: 'prefer concise answers', conversationId: conversation.id }, 201);
    await call(`memory/${memory.id}`, 'PATCH', { content: 'prefer detailed answers' });
    await call(`conversations/${conversation.id}`, 'DELETE', { revision: 4, forgetMemory: true });
    await call(`conversations/${conversation.id}`, 'GET', undefined, 404);
    await call(`conversations/${isolated.id}`, 'DELETE', { revision: 2 });
});
