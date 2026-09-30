import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { FileChatStore } from './file-store.ts';
import { RepositoryService } from './repository.ts';
import { ChatService } from './service.ts';
import { writeRepositoryFixture, writeSnapshotDocuments } from './repository-fixture.ts';
import { fileReferences, matchingTasks, referenceQuestion } from './repository-context.ts';
import type { Conversation } from '../../../shared/chat.ts';
import type { ChatWorkers } from './workers.ts';

const workers: ChatWorkers = { async run() { throw new Error('Repository references must stay offline'); }, close() {} };
async function setup() {
    const directory = await mkdtemp(join(tmpdir(), 'cgai-followup-'));
    const original = writeRepositoryFixture(join(directory, 'original'));
    const registry = JSON.parse(original.files['docs/codebase-tasks.json']!);
    registry.tasks[1].paths = ['src/amber.ts', 'src/sapphire.ts'];
    const fixture = writeSnapshotDocuments(join(directory, 'snapshot'), original.identity.commit,
        { ...original.files, 'docs/codebase-tasks.json': JSON.stringify(registry, null, 2) });
    const store = await FileChatStore.open(join(directory, 'chat.json'));
    const repository = new RepositoryService(fixture.directory);
    const service = new ChatService(store, workers, 'owner', undefined, repository);
    const conversation = await service.create(undefined, 'References', { scope: 'repository' });
    return { directory, fixture, store, repository, service, conversation };
}
async function send(service: ChatService, conversation: Conversation, content: string) {
    const result = await service.send(conversation.id, { requestId: `turn-${conversation.revision}`, revision: conversation.revision, content });
    assert.equal(result.conversation.messages.at(-1)?.status, 'complete');
    assert.equal(result.conversation.messages.at(-1)?.research?.queries, 0);
    return result.conversation;
}

test('reference grammar accepts check paraphrases without treating an explicit new subject as the active task', () => {
    for (const phrase of ['Which checks are still needed?', 'Which one of those files should I edit?',
        'What prerequisites remain?', 'Which module did you mean?', 'Which files belong to that?',
        'Which function implements it?']) assert(referenceQuestion(phrase), phrase);
    for (const phrase of ['Does it use CUDA?', 'Which checks cover the tokenizer?', 'What does that network socket do?'])
        assert.equal(referenceQuestion(phrase), false, phrase);
    assert.deepEqual(fileReferences('Use amber.ts or src/sapphire.ts; unknown.ts?', ['src/amber.ts', 'test/amber.ts', 'src/sapphire.ts']),
        { found: ['src/amber.ts', 'test/amber.ts', 'src/sapphire.ts'], missing: ['unknown.ts'] });
    const tasks = [{ id: 'amber-task', title: 'Amber Amber Amber', keywords: ['amber', 'amber', 'calibration'], priority: 1 },
        { id: 'sapphire-task', title: 'Sapphire', keywords: ['sapphire'], priority: 2 }];
    assert.deepEqual(matchingTasks(tasks, 'amber-task calibration amber OR sapphire-task').map(task => task.id), ['amber-task', 'sapphire-task']);
});

test('file ambiguity persists through restart and an unknown choice, then a chosen path retains reviewed checks', async () => {
    const fixture = await setup();
    let { service, store, conversation } = fixture;
    try {
        conversation = await send(service, conversation, 'What is the next step?');
        conversation = await send(service, conversation, 'Which one of those files should I edit?');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'clarification');
        assert.deepEqual(conversation.repositoryState?.topicCandidates, ['src/amber.ts', 'src/sapphire.ts']);
        assert.equal(conversation.repositoryState?.taskId, 'source-followups');
        await service.close(); await store.close();
        store = await FileChatStore.open(join(fixture.directory, 'chat.json'));
        service = new ChatService(store, workers, 'owner', undefined, new RepositoryService(fixture.fixture.directory));
        await service.recover();
        conversation = await service.conversation(conversation.id);
        conversation = await send(service, conversation, 'Which one did you mean?');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'clarification');
        const candidates = conversation.repositoryState?.topicCandidates;
        conversation = await send(service, conversation, 'Use src/invented.ts');
        assert.deepEqual(conversation.repositoryState?.topicCandidates, candidates);
        conversation = await send(service, conversation, 'Choose src/amber.ts.');
        assert.equal(conversation.repositoryState?.focusPath, 'src/amber.ts');
        assert.equal(conversation.repositoryState?.topicCandidates, undefined);
        assert(conversation.messages.at(-1)?.sources?.some(source => source.path === 'src/amber.ts'));
        conversation = await send(service, conversation, 'I finished it and tests passed.');
        conversation = await send(service, conversation, 'Which checks are still needed?');
        assert.equal(conversation.messages.at(-1)?.action?.taskId, 'source-followups');
        assert.equal(conversation.messages.at(-1)?.action?.verification, 'proposed');
        assert.equal(conversation.repositoryState?.focusPath, 'src/amber.ts');
    } finally { await service.close(); await store.close(); await rm(fixture.directory, { recursive: true, force: true }); }
});

test('explicit competing tasks override active context and unmet prerequisites never silently change the selected task', async () => {
    const { directory, store, service, conversation: initial } = await setup();
    try {
        let conversation = await send(service, initial, 'What next?');
        conversation = await send(service, conversation, 'Should I work on source-followups source context or artifact-checks?');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'clarification');
        assert.deepEqual(conversation.repositoryState?.taskCandidates, ['source-followups', 'artifact-checks']);
        assert.equal(conversation.repositoryState?.taskId, undefined);
        conversation = await send(service, conversation, 'Which files belong to that?');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'clarification');
        conversation = await send(service, conversation, 'Choose artifact-checks.');
        assert.equal(conversation.messages.at(-1)?.action?.taskId, 'artifact-checks');
        assert.equal(conversation.repositoryState?.taskCandidates, undefined);
        assert.match(conversation.messages.at(-1)!.content, /unverified prerequisites/);
        conversation = await send(service, conversation, 'What is the next action for artifact-checks?');
        assert.equal(conversation.messages.at(-1)?.action?.taskId, 'artifact-checks');
        conversation = await send(service, conversation, 'What is the next action?');
        assert.equal(conversation.messages.at(-1)?.action?.taskId, 'source-followups');
    } finally { await service.close(); await store.close(); await rm(directory, { recursive: true, force: true }); }
});

test('multiple explicit file subjects clear focus and a failed new-topic lookup cannot reuse the old task', async () => {
    const { directory, store, service, conversation: initial } = await setup();
    try {
        let conversation = await send(service, initial, 'What next?');
        conversation = await send(service, conversation, 'Choose src/amber.ts');
        conversation = await send(service, conversation, 'Compare src/amber.ts and src/sapphire.ts');
        assert.equal(conversation.repositoryState?.focusPath, undefined);
        assert.equal(conversation.repositoryState?.topicCandidates?.length, 2);
        conversation = await send(service, conversation, 'Which one should I edit?');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'clarification');
        conversation = await send(service, conversation, 'How does the ZXQ nebula protocol work?');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'abstained');
        assert.equal(conversation.repositoryState?.taskId, undefined);
        assert.equal(conversation.repositoryState?.topicCandidates, undefined);
        conversation = await send(service, conversation, 'Which checks are needed?');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'clarification');
    } finally { await service.close(); await store.close(); await rm(directory, { recursive: true, force: true }); }
});

test('a shared source path requires a task choice before suggesting reviewed check commands', async () => {
    const { directory, store, service, conversation: initial } = await setup();
    try {
        let conversation = await send(service, initial, 'Which checks cover src/sapphire.ts?');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'clarification');
        assert.deepEqual(conversation.repositoryState?.taskCandidates, ['source-followups', 'artifact-checks']);
        assert.equal(conversation.repositoryState?.focusPath, 'src/sapphire.ts');
        conversation = await send(service, conversation, 'Choose artifact-checks.');
        assert.equal(conversation.messages.at(-1)?.action?.taskId, 'artifact-checks');
        assert.equal(conversation.repositoryState?.focusPath, 'src/sapphire.ts');
        assert(conversation.messages.at(-1)?.sources?.some(source => source.path === 'src/sapphire.ts'));

        let separate = await service.create(undefined, 'Select file before checks', { scope: 'repository' });
        separate = await send(service, separate, 'Choose src/sapphire.ts');
        separate = await send(service, separate, 'Which checks should I run?');
        assert.equal(separate.messages.at(-1)?.finishReason, 'clarification');
        assert.equal(separate.repositoryState?.focusPath, 'src/sapphire.ts');
        separate = await send(service, separate, 'Choose artifact-checks.');
        assert.equal(separate.repositoryState?.focusPath, 'src/sapphire.ts');
    } finally { await service.close(); await store.close(); await rm(directory, { recursive: true, force: true }); }
});

test('an unknown explicit source subject without pending choices clears the previous task', async () => {
    const { directory, store, service, conversation: initial } = await setup();
    try {
        let conversation = await send(service, initial, 'What next?');
        conversation = await send(service, conversation, 'Which checks cover src/invented.ts?');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'clarification');
        assert.equal(conversation.repositoryState?.taskId, undefined);
        conversation = await send(service, conversation, 'Which checks should I run?');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'clarification');
        assert.equal(conversation.messages.at(-1)?.action, undefined);
    } finally { await service.close(); await store.close(); await rm(directory, { recursive: true, force: true }); }
});

test('explicit verified tasks disclose snapshot status and generic next skips them', async () => {
    const { directory, store, service, conversation: initial } = await setup();
    try {
        let conversation = await send(service, initial, 'What is the next action for foundation?');
        assert.equal(conversation.repositoryState?.taskId, 'foundation');
        assert.match(conversation.messages.at(-1)!.content, /already verified in this snapshot/);
        assert.doesNotMatch(conversation.messages.at(-1)!.content, /Proposed next action:/);
        conversation = await send(service, conversation, 'What is the next action?');
        assert.equal(conversation.messages.at(-1)?.action?.taskId, 'source-followups');
    } finally { await service.close(); await store.close(); await rm(directory, { recursive: true, force: true }); }
});
