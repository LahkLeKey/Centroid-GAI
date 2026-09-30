import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import type { ChatSendRequest, Conversation } from '../../../shared/chat.ts';
import type { RepositoryAnswer } from '../../../shared/repository.ts';
import { validRepositorySource } from '../evaluation/repository-chat-metrics.ts';
import { FileChatStore } from './file-store.ts';
import { RepositoryService } from './repository.ts';
import { ChatService, ChatServiceError } from './service.ts';
import { validRepositoryResearch } from './repository-investigation.ts';
import { writeRepositoryFixture, writeSnapshotDocuments } from './repository-fixture.ts';
import type { ChatWorkers } from './workers.ts';

const workers: ChatWorkers = { async run() { throw new Error('Software research must stay in repository sources'); }, close() {} };
const hasStatus = (status: number) => (error: unknown) => error instanceof ChatServiceError && error.status === status;
async function setup() {
    const directory = await mkdtemp(join(tmpdir(), 'cgai-investigation-'));
    const original = writeRepositoryFixture(join(directory, 'original'));
    const files = { ...original.files,
        'src/amber.ts': `// ${original.files['src/amber.ts']}\nimport { SAPPHIRE } from './sapphire.ts';\nexport function AmberExact() { return SAPPHIRE; }\n`,
        'src/sapphire.ts': `/* ${original.files['src/sapphire.ts']} */\nexport const SAPPHIRE = 1;\n`,
        'src/consumer.ts': "import { AmberExact } from './amber.ts';\nexport const value = AmberExact();\n",
        'tests/amber.test.ts': "import { AmberExact } from '../src/amber.ts';\n// AmberExact is an assertion subject, not proof a test passed.\n",
        'docs/symbol.md': 'AmberExactExtra is a different identifier.\n',
    };
    const fixture = writeSnapshotDocuments(join(directory, 'snapshot'), original.identity.commit, files);
    const second = writeSnapshotDocuments(join(directory, 'second'), 'b'.repeat(40), files);
    const path = join(directory, 'chat.json');
    const store = await FileChatStore.open(path);
    const repository = new RepositoryService(fixture.directory, [second.directory]);
    const service = new ChatService(store, workers, 'owner', undefined, repository);
    return { directory, fixture, second, path, store, repository, service };
}
function request(conversation: Conversation, content: string, options: Partial<ChatSendRequest> = {}): ChatSendRequest {
    return { requestId: `turn-${conversation.revision}`, revision: conversation.revision, content, ...options };
}
async function send(service: ChatService, conversation: Conversation, content: string, options: Partial<ChatSendRequest> = {}) {
    const result = await service.send(conversation.id, request(conversation, content, options));
    assert.equal(result.conversation.messages.at(-1)?.status, 'complete');
    assert.equal(result.conversation.messages.at(-1)?.research?.queries, 0);
    assert.deepEqual(result.conversation.messages.at(-1)?.memoryIds, []);
    return result.conversation;
}

test('impact investigations cite imports, dependent tests and reviewed check proposals with exact provenance', async () => {
    const fixture = await setup();
    try {
        const initial = await fixture.service.create(undefined, 'Impact', { scope: 'repository' });
        const conversation = await send(fixture.service, initial, 'Research this change.', { repositoryResearch: { kind: 'impact', target: 'src/amber.ts' } });
        const message = conversation.messages.at(-1)!;
        const investigation = message.investigation!;
        assert.equal(investigation.kind, 'impact');
        assert.deepEqual(investigation.snapshot, fixture.fixture.identity);
        assert(investigation.findings.some(finding => finding.kind === 'dependency' && finding.path === 'src/amber.ts' && finding.relatedPath === 'src/sapphire.ts'));
        assert(investigation.findings.some(finding => finding.kind === 'dependent' && finding.path === 'tests/amber.test.ts' && finding.relatedPath === 'src/amber.ts'));
        assert(investigation.relatedTasks.some(task => task.taskId === 'source-followups' && task.checks[0]?.command === 'node --test amber.test.ts'));
        for (const finding of investigation.findings) assert(message.sources?.some(source => source.id === finding.sourceId && source.path === finding.path));
        for (const source of message.sources ?? []) assert(validRepositorySource(source, fixture.fixture.documents, fixture.fixture.identity.manifestSha256));
        assert.match(message.content, /proposed, not run/);
        assert.equal(message.action, undefined);
        assert.deepEqual(conversation.repositoryState?.investigation, { kind: 'impact', target: 'src/amber.ts' });
    } finally { await fixture.service.close(); await fixture.store.close(); await rm(fixture.directory, { recursive: true, force: true }); }
});

test('investigation topic survives store restart, isolates sessions, and clears on task selection or snapshot switch', async () => {
    const fixture = await setup();
    let { service, store } = fixture;
    try {
        let conversation = await service.create(undefined, 'Research', { scope: 'repository' });
        const other = await service.create(undefined, 'Independent', { scope: 'repository' });
        conversation = await send(service, conversation, 'Research the impact of changing src/amber.ts');
        await service.close(); await store.close();
        store = await FileChatStore.open(fixture.path);
        service = new ChatService(store, workers, 'owner', undefined, new RepositoryService(fixture.fixture.directory, [fixture.second.directory]));
        await service.recover();
        conversation = await service.conversation(conversation.id);
        conversation = await send(service, conversation, 'Which tests cover that?');
        assert.equal(conversation.messages.at(-1)?.investigation?.target, 'src/amber.ts');
        conversation = await send(service, conversation, 'What depends on that?');
        assert.equal(conversation.messages.at(-1)?.investigation?.target, 'src/amber.ts');
        const isolated = await send(service, other, 'Which tests cover that?');
        assert.equal(isolated.messages.at(-1)?.investigation, undefined);
        assert.equal(isolated.messages.at(-1)?.finishReason, 'clarification');
        conversation = await send(service, conversation, 'Choose artifact-checks.');
        assert.equal(conversation.repositoryState?.investigation, undefined);
        assert.equal(conversation.messages.at(-1)?.action?.taskId, 'artifact-checks');
        conversation = await send(service, conversation, 'Find references to AmberExact');
        assert.equal(conversation.messages.at(-1)?.investigation?.kind, 'symbol');
        const switched = await service.switchRepository(conversation.id, { revision: conversation.revision, snapshot: fixture.second.identity });
        assert.equal(switched.repositoryState?.investigation, undefined);
        assert.deepEqual(switched.messages, conversation.messages);
    } finally { await service.close(); await store.close(); await rm(fixture.directory, { recursive: true, force: true }); }
});

test('symbol research uses exact identifiers, fails on absent targets, and resets on new factual topics', async () => {
    const fixture = await setup();
    try {
        let conversation = await fixture.service.create(undefined, 'Symbols', { scope: 'repository' });
        conversation = await send(fixture.service, conversation, 'Find references to AmberExact');
        assert(conversation.messages.at(-1)?.investigation?.findings.some(finding => finding.path === 'src/amber.ts'));
        assert(!conversation.messages.at(-1)?.sources?.some(source => source.path === 'docs/symbol.md'));
        conversation = await send(fixture.service, conversation, 'Where is it referenced?');
        assert.equal(conversation.messages.at(-1)?.investigation?.target, 'AmberExact');
        conversation = await send(fixture.service, conversation, 'Did you run those tests?');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'abstained');
        assert.equal(conversation.messages.at(-1)?.investigation, undefined);
        conversation = await send(fixture.service, conversation, 'Continue this investigation');
        assert.equal(conversation.messages.at(-1)?.investigation?.target, 'AmberExact');
        conversation = await send(fixture.service, conversation, 'Continue this investigation about ZXQPL');
        assert.equal(conversation.repositoryState?.investigation, undefined);
        assert.equal(conversation.messages.at(-1)?.investigation, undefined);
        conversation = await send(fixture.service, conversation, 'Continue this investigation.');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'clarification');
        conversation = await send(fixture.service, conversation, 'Find references to MissingSymbol');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'abstained');
        assert.equal(conversation.repositoryState?.investigation, undefined);
        conversation = await send(fixture.service, conversation, 'Research src/missing.ts');
        assert.equal(conversation.messages.at(-1)?.finishReason, 'clarification');
        assert.equal(conversation.repositoryState?.investigation, undefined);
        conversation = await send(fixture.service, conversation, 'Find references to AmberExact');
        conversation = await send(fixture.service, conversation, 'What byte order do Sapphire artifacts use?');
        assert.equal(conversation.repositoryState?.investigation, undefined);
        assert.equal(conversation.messages.at(-1)?.investigation, undefined);
    } finally { await fixture.service.close(); await fixture.store.close(); await rm(fixture.directory, { recursive: true, force: true }); }
});

test('research request validation runs before writes and research options participate in retry identity', async () => {
    const fixture = await setup();
    try {
        const initial = await fixture.service.create(undefined, 'Validation', { scope: 'repository' });
        for (const invalid of [null, [], {}, { kind: 'symbol', target: 'a();' }, { kind: 'impact', target: '../outside.ts' },
            { kind: 'impact', target: 'C:/outside.ts' }, { kind: 'impact', target: 'src/amber.ts', execute: true }]) {
            assert.equal(validRepositoryResearch(invalid), false);
            await assert.rejects(fixture.service.send(initial.id, request(initial, 'Inspect', { repositoryResearch: invalid } as unknown as Partial<ChatSendRequest>)), hasStatus(400));
        }
        assert.equal((await fixture.service.conversation(initial.id)).revision, 0);
        const input = request(initial, 'Inspect', { repositoryResearch: { kind: 'impact', target: 'amber.ts' } });
        const first = await fixture.service.send(initial.id, input);
        assert.equal(first.conversation.messages.at(-1)?.investigation?.target, 'src/amber.ts');
        assert.deepEqual(await fixture.service.send(initial.id, input), first);
        await assert.rejects(fixture.service.send(initial.id, { ...input, repositoryResearch: { kind: 'impact', target: 'src/sapphire.ts' } }), hasStatus(409));
        const publicConversation = await fixture.service.create(undefined, 'Public');
        await assert.rejects(fixture.service.send(publicConversation.id, request(publicConversation, 'Inspect', {
            repositoryResearch: { kind: 'symbol', target: 'AmberExact' },
        })), hasStatus(400));
        assert.equal((await fixture.service.conversation(publicConversation.id)).revision, 0);
    } finally { await fixture.service.close(); await fixture.store.close(); await rm(fixture.directory, { recursive: true, force: true }); }
});

test('cancelled investigations leave completed research context and transcript metadata unchanged', async () => {
    const fixture = await setup();
    try {
        const initial = await fixture.service.create(undefined, 'Cancellation', { scope: 'repository' });
        const conversation = await send(fixture.service, initial, 'Find references to AmberExact');
        const before = structuredClone(conversation.repositoryState);
        const input = request(conversation, 'Research src/sapphire.ts');
        const late = await fixture.repository.answer(input, conversation, new AbortController().signal);
        let release!: (answer: RepositoryAnswer) => void;
        let entered!: () => void;
        const ready = new Promise<void>(resolve => { entered = resolve; });
        fixture.repository.answer = async () => { entered(); return new Promise<RepositoryAnswer>(resolve => { release = resolve; }); };
        const pending = fixture.service.send(conversation.id, input);
        await ready;
        const cancellation = fixture.service.cancel(conversation.id, input.requestId);
        await new Promise<void>(resolve => setImmediate(resolve));
        release(late);
        const cancelled = await cancellation;
        await pending;
        assert.equal(cancelled.messages.at(-1)?.status, 'cancelled');
        assert.equal(cancelled.messages.at(-1)?.investigation, undefined);
        assert.deepEqual(cancelled.repositoryState, before);
    } finally { await fixture.service.close(); await fixture.store.close(); await rm(fixture.directory, { recursive: true, force: true }); }
});
