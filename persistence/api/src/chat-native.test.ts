import assert from 'node:assert/strict';
import test from 'node:test';
import { evaluateChatModel, inspectChatModel, replyChatModel, trainChatModel } from './chat-native.ts';
import { ProcessChatWorkers } from './chat/workers.ts';

const examples = [
    { messages: [{ role: 'user' as const, content: 'red' }], answer: 'warm' },
    { messages: [{ role: 'user' as const, content: 'blue' }], answer: 'cold' },
];
const config = { embeddingDimensions: 4, hiddenDimensions: 8, centroidCount: 8, promptWindow: 4, responseWindow: 1 };

test('chat options reject JSON coercions before reaching native numeric arrays', () => {
    for (const value of [null, [], 'settings', 4, true]) {
        assert.throws(() => trainChatModel(examples, value as never, { epochs: 1 }), /config must be an object/);
        assert.throws(() => trainChatModel(examples, config, value as never), /training must be an object/);
        assert.throws(() => replyChatModel(Buffer.alloc(0), examples[0]!.messages, value as never), /options must be an object/);
    }
    for (const value of ['4', null, true, [], Infinity, NaN]) {
        assert.throws(() => trainChatModel(examples, { ...config, embeddingDimensions: value as never }, { epochs: 1 }), /embeddingDimensions must be a finite number/);
        assert.throws(() => trainChatModel(examples, { ...config, evidenceWindow: value as never }, { epochs: 1 }), /evidenceWindow must be a finite number/);
        assert.throws(() => trainChatModel(examples, config, { epochs: value as never }), /epochs must be a finite number/);
        assert.throws(() => replyChatModel(Buffer.alloc(0), examples[0]!.messages, { temperature: value as never }), /temperature must be a finite number/);
    }
    assert.throws(() => trainChatModel(examples, { ...config, seed: null as never }, { epochs: 1, seed: '42' }), /seed must be an unsigned decimal string/);
    const trained = trainChatModel(examples, config, { epochs: 1 });
    assert.equal(replyChatModel(trained.payload, examples[0]!.messages, { maxTokens: 0 }).generatedTokens, 0);
});

test('new artifacts retain ordered evidence and disclose unknown words and whole evidence drops', () => {
    const trained = trainChatModel(examples, { ...config, promptWindow: 24, evidenceWindow: 12 }, { epochs: 1 });
    assert.equal(trained.metadata.formatVersion, 2);
    assert.equal(trained.metadata.protocolVersion, 2);
    assert.equal(trained.metadata.tokenizerVersion, 1);
    assert.equal(trained.metadata.config.evidenceWindow, 12);
    const retained = replyChatModel(trained.payload, [
        { role: 'evidence', content: 'red red red red' },
        { role: 'evidence', content: 'blue blue blue blue' },
        { role: 'evidence', content: 'unseen' },
        ...examples[0]!.messages,
    ], { maxTokens: 0 });
    assert.equal(retained.evidenceTokens, 12);
    assert.equal(retained.droppedEvidence, 1);
    assert.equal(retained.droppedMessages, 1);
    assert.equal(retained.unknownTokens, 0);
    const unknown = replyChatModel(trained.payload, [
        { role: 'evidence', content: 'NovelLibrary42' }, ...examples[0]!.messages,
    ], { maxTokens: 0 });
    assert.equal(unknown.evidenceTokens, 3);
    assert.equal(unknown.droppedEvidence, 0);
    assert.equal(unknown.unknownTokens, 1);
    for (const words of [11, 24]) {
        const oversized = [{ role: 'evidence' as const, content: 'red '.repeat(words) }, ...examples[0]!.messages];
        const reply = replyChatModel(trained.payload, oversized, { maxTokens: 0 });
        assert.equal(reply.evidenceTokens, 0);
        assert.equal(reply.droppedEvidence, 1);
        assert.throws(() => trainChatModel([{ messages: oversized, answer: 'warm' }],
            { ...config, promptWindow: 24, evidenceWindow: 12 }, { epochs: 1 }), /evidence exceeds/);
    }
    for (const evidenceWindow of [-1, 1.5, 21, 257]) {
        assert.throws(() => trainChatModel(examples, { ...config, promptWindow: 24, evidenceWindow }, { epochs: 1 }));
    }
});

test('legacy artifacts keep their version and smaller evidence budget after the addon upgrade', () => {
    const trained = trainChatModel(examples, { ...config, promptWindow: 24, evidenceWindow: 12 }, { epochs: 1 });
    // Version one has no evidence-budget field; weights and all preceding header fields match.
    const legacy = Buffer.concat([trained.payload.subarray(0, 104), trained.payload.subarray(112)]);
    legacy.writeBigUInt64LE(1n, 8); legacy.writeBigUInt64LE(1n, 16);
    const metadata = inspectChatModel(legacy);
    assert.equal(metadata.formatVersion, 1);
    assert.equal(metadata.protocolVersion, 1);
    const reply = replyChatModel(legacy, [
        { role: 'evidence', content: 'red red red red' },
        { role: 'evidence', content: 'blue blue blue blue' },
        ...examples[0]!.messages,
    ], { maxTokens: 0 });
    assert.equal(reply.evidenceTokens, 6);
    assert.equal(reply.droppedEvidence, 1);
});

test('default models allocate prompt and evidence context within the native total limit', () => {
    const { metadata } = trainChatModel(examples, {}, { epochs: 1 });
    assert.equal(metadata.config.promptWindow, 160);
    assert.equal(metadata.config.responseWindow, 32);
    assert.equal(metadata.config.evidenceWindow, 80);
});

test('chat trains independent answers, measures held-out targets, and rejects incompatible artifacts', () => {
    const trained = trainChatModel(examples, config, { epochs: 400 });
    assert.ok(trained.after.crossEntropy < trained.before.crossEntropy / 2);
    assert.equal(inspectChatModel(trained.payload).engineKind, 'neural-centroid-chat');
    // Correctness gate: optimizer lowers supervised loss. Exact answer quality is reported by
    // the separate fixed evaluation suite, and is not inferred from this tiny fixture.
    assert.ok(trained.after.accuracy >= 0.75);
    assert.deepEqual(replyChatModel(trained.payload, examples[0]!.messages), replyChatModel(Buffer.from(trained.payload), examples[0]!.messages));
    assert.equal(replyChatModel(trained.payload, examples[0]!.messages).finishReason, 'eos');
    assert.equal(evaluateChatModel(trained.payload, [{ messages: examples[0]!.messages, answer: 'unseen' }]).unknownTokens, 1);
    assert.throws(() => inspectChatModel(Buffer.concat([trained.payload, Buffer.from([0])])));
    for (const offset of [0, 8, 16, 24, 80, 88, 96]) {
        const broken = Buffer.from(trained.payload); broken.fill(255, offset, offset + 8);
        assert.throws(() => inspectChatModel(broken));
    }
    for (const size of [0, 8, 104, trained.payload.length - 1]) assert.throws(() => inspectChatModel(trained.payload.subarray(0, size)));
    const nonfinite = Buffer.from(trained.payload);
    nonfinite.writeDoubleLE(Infinity, nonfinite.length - 8);
    assert.throws(() => inspectChatModel(nonfinite));
    assert.throws(() => replyChatModel(trained.payload, [{ role: 'user', content: 'red '.repeat(20) }]));
    assert.throws(() => replyChatModel(trained.payload, [{ role: 'assistant', content: 'warm' }]));
    assert.throws(() => replyChatModel(trained.payload, examples[0]!.messages, { maxTokens: -1 }));
    assert.equal(replyChatModel(trained.payload, examples[0]!.messages, { maxTokens: 0 }).finishReason, 'length');
});

test('bounded queue rejects overload, cancels queued work and settles shutdown', async () => {
    const workers = new ProcessChatWorkers();
    const tasks = Array.from({ length: 18 }, () => workers.run({ kind: 'inspect', payload: Buffer.alloc(8) }));
    const settled = Promise.allSettled(tasks);
    await assert.rejects(workers.run({ kind: 'inspect', payload: Buffer.alloc(8) }), /queue is full/);
    workers.close();
    assert.equal((await settled).filter((result) => result.status === 'rejected').length, 18);
    const crashed = new ProcessChatWorkers({ workerPath: '/nonexistent/cgai-worker.ts' });
    try { await assert.rejects(crashed.run({ kind: 'inspect', payload: Buffer.alloc(8) }), /exited/); }
    finally { crashed.close(); }
});

test('process workers complete, fail, cancel, time out and close', async () => {
    const workers = new ProcessChatWorkers({ timeoutMs: 5000 });
    try {
        const trained = trainChatModel(examples, config, { epochs: 1 });
        const result = await workers.run<{ engineKind: string }>({ kind: 'inspect', payload: trained.payload });
        assert.equal(result.engineKind, 'neural-centroid-chat');
        await assert.rejects(workers.run({ kind: 'inspect', payload: Buffer.alloc(8) }));
        const abort = new AbortController();
        const long = workers.run({ kind: 'train', examples, config, training: { epochs: 10000 } }, abort.signal);
        abort.abort(); await assert.rejects(long, /cancelled/);
    } finally { workers.close(); }
    await assert.rejects(workers.run({ kind: 'inspect', payload: Buffer.alloc(8) }), /shutting down/);
    const timed = new ProcessChatWorkers({ timeoutMs: 1 });
    try { await assert.rejects(timed.run({ kind: 'inspect', payload: Buffer.alloc(8) }), /timed out/); }
    finally { timed.close(); }
});
