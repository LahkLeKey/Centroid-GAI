/** One bounded native operation per process; termination also releases native allocations. */
import { inspectChatModel, replyChatModel, trainChatModel } from '../chat-native.ts';
import type { ChatWorkerTask } from './workers.ts';

process.once('message', (task: ChatWorkerTask) => {
    try {
        let value: unknown;
        if (task.kind === 'inspect') value = inspectChatModel(Buffer.from(task.payload));
        else if (task.kind === 'reply') value = replyChatModel(Buffer.from(task.payload), task.messages, task.options);
        else value = trainChatModel(task.examples, task.config, task.training);
        process.send?.({ ok: true, value }, undefined, {}, () => process.exit(0));
    } catch (error) {
        process.send?.({ ok: false, error: error instanceof Error ? error.message : 'native operation failed' }, undefined, {}, () => process.exit(1));
    }
});
