import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { ChatWorkerError, ProcessChatWorkers, type ChatWorkerTask } from './workers.ts';
import type { ChatTrainingValidation } from '../../../shared/chat-quality.ts';

const inspect = (label: string): ChatWorkerTask => ({ kind: 'inspect', payload: Buffer.from(label) });
const train = (label: string): ChatWorkerTask => ({ kind: 'train', examples: [{ messages: [], answer: label }] });
const validate = (label: string): ChatWorkerTask => ({ kind: 'validate', payload: Buffer.from(label), validation: { version: 1, cases: [] } as ChatTrainingValidation });
async function fixture(timeoutMs = 5000) {
    const directory = await mkdtemp(join(tmpdir(), 'cgai-workers-'));
    const workerPath = join(directory, 'worker.cjs'), logPath = join(directory, 'started.jsonl');
    await writeFile(logPath, '');
    await writeFile(workerPath, `
const fs = require('node:fs');
process.on('SIGTERM', () => {});
process.once('message', (task) => {
    const label = task.kind === 'train' ? task.examples[0].answer : Buffer.from(task.payload).toString();
    fs.appendFileSync(${JSON.stringify(logPath)}, JSON.stringify({ label, pid: process.pid }) + '\\n');
    if (label === 'exit') process.exit(7);
    else if (label === 'invalid') process.send(null);
    else if (label === 'failure') process.send({ ok: false, error: 'fixture operation failed' });
    else if (!label.startsWith('hold')) process.send({ ok: true, value: { label, pid: process.pid } });
    setInterval(() => {}, 1000);
});
`);
    const workers = new ProcessChatWorkers({ workerPath, timeoutMs, trainTimeoutMs: timeoutMs });
    const started = async (): Promise<{ label: string; pid: number }[]> => (await readFile(logPath, 'utf8')).trim().split('\n').filter(Boolean).map((line) => JSON.parse(line));
    return { workers, started, async waitForStarts(count: number) {
        const deadline = Date.now() + 4000;
        while ((await started()).length < count) {
            if (Date.now() > deadline) throw new Error('worker fixture did not start');
            await new Promise((resolve) => setTimeout(resolve, 10));
        }
    }, async close() { await workers.close(); await rm(directory, { recursive: true, force: true }); } };
}

test('worker lanes isolate training, cancel queued work, and release slots only after process exit', { timeout: 15000 }, async () => {
    const context = await fixture();
    const firstController = new AbortController();
    const first = assert.rejects(context.workers.run(inspect('hold-first'), firstController.signal), /cancelled/);
    const second = assert.rejects(context.workers.run(inspect('hold-second')), /cancelled/);
    const training = assert.rejects(context.workers.run(train('hold-training')), /cancelled/);
    try {
        await context.waitForStarts(3);
        const queuedController = new AbortController();
        const queued = assert.rejects(context.workers.run(inspect('never-start'), queuedController.signal), /cancelled/);
        queuedController.abort(); await queued;
        const fast = context.workers.run<{ label: string; pid: number }>(inspect('fast'));
        assert.equal((await context.started()).length, 3);
        firstController.abort(); await first;
        const firstPid = (await context.started()).find((entry) => entry.label === 'hold-first')!.pid;
        assert.throws(() => process.kill(firstPid, 0), /ESRCH/);
        assert.equal((await fast).label, 'fast');
        assert.ok(!(await context.started()).some((entry) => entry.label === 'never-start'));
        await context.workers.close(); await Promise.all([second, training]);
        for (const entry of await context.started()) assert.throws(() => process.kill(entry.pid, 0), /ESRCH/);
        await assert.rejects(context.workers.run(inspect('late')), (error: unknown) => error instanceof ChatWorkerError && error.status === 503);
    } finally { await context.close(); await Promise.allSettled([first, second, training]); }
});

test('worker failures and malformed IPC are contained, and timeouts terminate native work', { timeout: 15000 }, async () => {
    const context = await fixture(500);
    try {
        await assert.rejects(context.workers.run(inspect('failure')), /fixture operation failed/);
        await assert.rejects(context.workers.run(inspect('invalid')), (error: unknown) => error instanceof ChatWorkerError && error.status === 500 && /invalid/.test(error.message));
        await assert.rejects(context.workers.run(inspect('exit')), /exited \(7\)/);
        await assert.rejects(context.workers.run(inspect('hold-timeout')), (error: unknown) => error instanceof ChatWorkerError && error.status === 504);
        const pid = (await context.started()).find((entry) => entry.label === 'hold-timeout')!.pid;
        assert.throws(() => process.kill(pid, 0), /ESRCH/);
        assert.equal((await context.workers.run<{ label: string }>(inspect('after-timeout'))).label, 'after-timeout');
    } finally { await context.close(); }
});

test('candidate validation shares the bounded training lane while replies remain available', { timeout: 15000 }, async () => {
    const context = await fixture();
    const controller = new AbortController();
    const validation = assert.rejects(context.workers.run(validate('hold-validation'), controller.signal), /cancelled/);
    try {
        await context.waitForStarts(1);
        const training = context.workers.run<{ label: string }>(train('after-validation'));
        assert.equal((await context.workers.run<{ label: string }>(inspect('reply-during-validation'))).label, 'reply-during-validation');
        assert.ok(!(await context.started()).some(entry => entry.label === 'after-validation'));
        controller.abort();
        await validation;
        assert.equal((await training).label, 'after-validation');
    } finally { await context.close(); await validation; }
});

test('queue capacity and shutdown settle every pending operation without starting it', { timeout: 15000 }, async () => {
    const context = await fixture();
    const first = assert.rejects(context.workers.run(inspect('hold-first')), /cancelled/);
    const second = assert.rejects(context.workers.run(inspect('hold-second')), /cancelled/);
    const queued: Promise<void>[] = [];
    try {
        await context.waitForStarts(2);
        for (let index = 0; index < 16; index++) queued.push(assert.rejects(context.workers.run(inspect(`queued-${index}`)), /shutting down/));
        await assert.rejects(context.workers.run(inspect('overflow')), (error: unknown) => error instanceof ChatWorkerError && error.status === 429);
        await context.workers.close();
        await Promise.all([first, second, ...queued]);
        assert.equal((await context.started()).length, 2);
    } finally { await context.close(); await Promise.allSettled([first, second, ...queued]); }
});
