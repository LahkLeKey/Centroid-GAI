import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import type { ChatMetrics, ChatModelMetadata, ChatReply, ChatTrainRequest, ChatTrainingJob } from '../../../shared/chat.ts';
import type { ChatQualityReport } from '../../../shared/chat-quality.ts';
import { FileChatStore } from './file-store.ts';
import { ChatService, ChatServiceError, fingerprintBytes } from './service.ts';
import { evaluateTrainingValidation } from './training-quality.ts';
import type { ChatWorkers, ChatWorkerTask } from './workers.ts';

const request = (): ChatTrainRequest => ({ name: 'reviewed', examples: [
    { id: 'training-case', messages: [{ role: 'user', content: 'training question' }], answer: 'training answer' },
], validation: { version: 1, cases: [
    { id: 'answer-case', messages: [{ role: 'user', content: 'Which signal is ready?' }], expected: 'answer',
        acceptedAnswers: ['The silver signal is ready.'], evidence: [{ id: 'held-document', excerpt: 'The silver signal is ready.' }] },
    { id: 'abstain-case', messages: [{ role: 'user', content: 'What is the measured temperature?' }], expected: 'abstain',
        acceptedAnswers: ['I do not have supporting evidence.'] },
] } });
const metadata: ChatModelMetadata = { engineKind: 'neural-centroid-chat', formatVersion: 2, protocolVersion: 2,
    tokenizerVersion: 1, config: {}, vocabularySize: 64, parameterCount: 256 };
const metrics: ChatMetrics = { tokens: 12, unknownTokens: 0, crossEntropy: 0, perplexity: 1, accuracy: 1 };
const reply = (content: string): ChatReply => ({ content, finishReason: 'eos', generatedTokens: 8, promptTokens: 20,
    droppedMessages: 0, evidenceTokens: 8, droppedEvidence: 0, unknownTokens: 0 });
const candidate = Buffer.from('trained candidate artifact');

async function fixture(validate: (task: Extract<ChatWorkerTask, { kind: 'validate' }>) => Promise<ChatQualityReport>) {
    const directory = await mkdtemp(join(tmpdir(), 'cgai-training-gate-'));
    const path = join(directory, 'store.json');
    let store = await FileChatStore.open(path);
    const oldPayload = Buffer.from('previous published artifact');
    const oldModel = await store.publishModel('reviewed', { payload: oldPayload, checksumSha256: fingerprintBytes(oldPayload),
        metadata, createdAt: '2026-01-01T00:00:00.000Z', provenance: null });
    const terminal = Promise.withResolvers<ChatTrainingJob>();
    const finished = new Map<string, ChatTrainingJob>();
    const waiting = new Map<string, ReturnType<typeof Promise.withResolvers<ChatTrainingJob>>>();
    const save = store.saveJob.bind(store);
    store.saveJob = async job => {
        await save(job);
        if (['complete', 'rejected', 'error'].includes(job.status)) {
            finished.set(job.id, job);
            waiting.get(job.id)?.resolve(job);
            terminal.resolve(job);
        }
    };
    const tasks: ChatWorkerTask[] = [];
    const workers: ChatWorkers = { async run<T>(task: ChatWorkerTask): Promise<T> {
        tasks.push(task);
        if (task.kind === 'train') return { payload: candidate, metadata, before: metrics, after: metrics } as T;
        if (task.kind === 'validate') return await validate(task) as T;
        throw new Error('unexpected worker operation');
    }, close() {} };
    const service = new ChatService(store, workers, 'owner');
    return { get store() { return store; }, oldModel, service, tasks, terminal: terminal.promise,
        waitJob(id: string): Promise<ChatTrainingJob> {
            const done = finished.get(id);
            if (done) return Promise.resolve(done);
            const waiter = waiting.get(id) ?? Promise.withResolvers<ChatTrainingJob>();
            waiting.set(id, waiter);
            return waiter.promise;
        },
        async reopen() {
            await service.close();
            await store.close();
            store = await FileChatStore.open(path);
            return store;
        }, async close() {
            await service.close();
            await store.close();
            assert.equal(dirname(resolve(directory)), resolve(tmpdir()));
            await rm(directory, { recursive: true, force: true });
        } };
}

test('missing, leaked, and unsupported validation is rejected before any training job or worker call', async () => {
    const context = await fixture(async () => { throw new Error('validation should not run'); });
    try {
        const valid = request();
        const invalid = [
            { ...valid, validation: undefined },
            { ...valid, validation: { version: 1, cases: [] } },
            { ...valid, validation: { ...valid.validation, cases: [{ ...valid.validation.cases[0], id: 'training-case' }, valid.validation.cases[1]] } },
            { ...valid, validation: { ...valid.validation, cases: [{ ...valid.validation.cases[0], acceptedAnswers: ['The silver signal is not ready.'] }, valid.validation.cases[1]] } },
        ];
        for (const value of invalid) await assert.rejects(context.service.train(value as unknown as ChatTrainRequest),
            (error: unknown) => error instanceof ChatServiceError && error.status === 400);
        assert.equal(context.tasks.length, 0);
        assert.deepEqual(await context.store.listJobs(), []);
        assert.deepEqual(await context.store.findModel('reviewed'), context.oldModel);
    } finally { await context.close(); }
});

test('a candidate publishes only after successful validation and its report survives reopening', { timeout: 10000 }, async () => {
    const entered = Promise.withResolvers<void>(), release = Promise.withResolvers<void>();
    const context = await fixture(async task => {
        entered.resolve();
        await release.promise;
        return evaluateTrainingValidation(task.payload, task.validation, (_payload, messages) => reply(messages.at(-1)!.content.includes('temperature') ?
            'I do not have supporting evidence.' : 'The silver signal is ready.'));
    });
    try {
        const queued = await context.service.train(request());
        assert.equal(queued.status, 'queued');
        await entered.promise;
        assert.deepEqual(await context.store.findModel('reviewed'), context.oldModel);
        release.resolve();
        const done = await context.terminal;
        assert.equal(done.status, 'complete');
        assert.equal(done.quality?.passed, true);
        assert.deepEqual(context.tasks.map(task => task.kind), ['train', 'validate']);
        const checksum = fingerprintBytes(candidate);
        assert.equal(done.candidateChecksum, checksum);
        assert.equal((await context.store.findModel('reviewed'))!.checksumSha256, checksum);
        assert.equal((await context.store.getArtifact(checksum))!.provenance!.quality, undefined);
        assert.equal((await context.store.getArtifact(checksum))!.provenance!.createdByJobId, queued.id);
        const reopened = await context.reopen();
        assert.deepEqual(await reopened.getJob(queued.id), done);
        assert.equal((await reopened.findModel('reviewed'))!.checksumSha256, checksum);
        assert.ok(await reopened.getArtifact(context.oldModel.checksumSha256), 'old checkpoint remains available');
    } finally { release.resolve(); await context.close(); }
});

test('a rejected candidate and measurements persist without changing the existing model head', { timeout: 10000 }, async () => {
    const context = await fixture(async task => evaluateTrainingValidation(task.payload, task.validation, () => reply('unsupported invented answer')));
    try {
        const queued = await context.service.train(request());
        const done = await context.terminal;
        assert.equal(done.status, 'rejected');
        assert.equal(done.quality?.passed, false);
        assert.equal(done.quality?.metrics.supportRate, 0);
        assert.ok(done.quality!.failures.length > 0);
        assert.match(done.error!, /quality gate/);
        assert.equal(done.model, undefined);
        assert.deepEqual(await context.store.findModel('reviewed'), context.oldModel);
        const checksum = fingerprintBytes(candidate);
        assert.equal(done.candidateChecksum, checksum);
        assert.deepEqual((await context.store.getArtifact(checksum))!.payload, candidate);
        const reopened = await context.reopen();
        assert.deepEqual(await reopened.getJob(queued.id), done);
        assert.deepEqual(await reopened.findModel('reviewed'), context.oldModel);
        assert.equal((await reopened.getArtifact(checksum))!.provenance!.quality, undefined);
    } finally { await context.close(); }
});

test('the same artifact can fail suite A and pass suite B while each job keeps its own approval report', { timeout: 10000 }, async () => {
    const context = await fixture(async task => evaluateTrainingValidation(task.payload, task.validation, (_payload, messages) =>
        reply(messages.at(-1)!.content.includes('temperature') ? 'I do not have supporting evidence.' : 'The silver signal is ready.')));
    try {
        const firstRequest = request();
        const firstSuite = { ...firstRequest.validation, cases: [{ ...firstRequest.validation.cases[0]!,
            acceptedAnswers: ['The orange signal is ready.'], evidence: [{ id: 'orange-doc', excerpt: 'The orange signal is ready.' }] }, firstRequest.validation.cases[1]!] };
        const first = await context.service.train({ ...firstRequest, validation: firstSuite });
        const rejected = await context.waitJob(first.id);
        assert.equal(rejected.status, 'rejected');
        assert.equal(rejected.quality!.passed, false);
        const second = await context.service.train(request());
        const accepted = await context.waitJob(second.id);
        assert.equal(accepted.status, 'complete');
        assert.equal(accepted.quality!.passed, true);
        assert.equal(accepted.candidateChecksum, rejected.candidateChecksum);
        assert.notEqual(accepted.quality!.suiteSha256, rejected.quality!.suiteSha256);
        const model = (await context.store.findModel('reviewed'))!;
        assert.equal(model.checksumSha256, accepted.candidateChecksum);
        assert.equal(model.provenance!.createdByJobId, first.id, 'immutable provenance describes artifact creation, not later approval');
        assert.equal(model.provenance!.quality, undefined);
        const reopened = await context.reopen();
        assert.equal((await reopened.getJob(first.id))!.quality!.passed, false);
        assert.equal((await reopened.getJob(second.id))!.quality!.passed, true);
        assert.deepEqual((await reopened.getJob(second.id))!.model, model);
    } finally { await context.close(); }
});

test('validation errors retain candidate bytes and identify them without replacing the model head', { timeout: 10000 }, async () => {
    const context = await fixture(async () => { throw new Error('validation worker timed out'); });
    try {
        const queued = await context.service.train(request());
        const failed = await context.waitJob(queued.id);
        assert.equal(failed.status, 'error');
        assert.match(failed.error!, /validation worker timed out/);
        assert.equal(failed.candidateChecksum, fingerprintBytes(candidate));
        assert.equal(failed.quality, undefined);
        assert.deepEqual((await context.store.getArtifact(failed.candidateChecksum!))!.payload, candidate);
        assert.deepEqual(await context.store.findModel('reviewed'), context.oldModel);
        const reopened = await context.reopen();
        assert.deepEqual(await reopened.getJob(queued.id), failed);
        assert.ok(await reopened.getArtifact(failed.candidateChecksum!));
    } finally { await context.close(); }
});

test('failure to record completion preserves the successful publication and measured quality in the error job', { timeout: 10000 }, async () => {
    const context = await fixture(async task => evaluateTrainingValidation(task.payload, task.validation, (_payload, messages) =>
        reply(messages.at(-1)!.content.includes('temperature') ? 'I do not have supporting evidence.' : 'The silver signal is ready.')));
    try {
        const save = context.store.saveJob.bind(context.store);
        let injected = false;
        context.store.saveJob = async job => {
            if (job.status === 'complete' && !injected) { injected = true; throw new Error('completion write unavailable'); }
            await save(job);
        };
        const queued = await context.service.train(request());
        const failed = await context.waitJob(queued.id);
        assert.equal(injected, true);
        assert.equal(failed.status, 'error');
        assert.equal(failed.quality!.passed, true);
        assert.deepEqual(failed.metrics, { before: metrics, after: metrics });
        assert.match(failed.error!, /model was published.*recording the completed job failed.*completion write unavailable/i);
        const model = (await context.store.findModel('reviewed'))!;
        assert.equal(model.checksumSha256, fingerprintBytes(candidate));
        assert.deepEqual(failed.model, model);
        const reopened = await context.reopen();
        assert.deepEqual(await reopened.getJob(queued.id), failed);
        assert.deepEqual(await reopened.findModel('reviewed'), model);
    } finally { await context.close(); }
});
