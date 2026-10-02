import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import type { MemoryDocument } from '../../../shared/research.ts';
import { sha256 } from '../evaluation/codebase-data.ts';
import { MemoryService } from './memory.ts';
import { ResearchService, relevantExcerpt } from './research.ts';

function memory(clock = Date.now) {
    let document: MemoryDocument | null = null;
    return new MemoryService({ async getMemory() { return structuredClone(document); },
        async saveMemory(value, revision) { if ((document?.revision ?? 0) !== revision) return false;
            document = structuredClone(value); return true; } }, 'owner', clock);
}
const input = { requestId: 'research', revision: 0, content: 'private centroid question',
    publicQuery: 'public centroid facts', rememberSources: true };
const { publicQuery: _query, ...cachedInput } = input;
const hit = { url: 'https://example.com/source', title: 'Original source' };
const page = { url: 'https://example.com/canonical', contentType: 'text/plain', text: 'Public centroid facts are source excerpts.' };

test('public excerpt selection preserves complete qualifiers rather than clipping at a byte window', () => {
    const qualified = `Centroid training ${'requires reviewed data '.repeat(85)}and is not automatic.`;
    assert.equal(relevantExcerpt(qualified, 'centroid training'), null);
    const text = 'Unrelated introduction. Centroid training is not automatic. It requires reviewed examples.';
    const excerpt = relevantExcerpt(text, 'centroid training');
    assert.ok(excerpt?.includes('Centroid training is not automatic.'));
    assert.ok(text.includes(excerpt!));
});

test('explicit volatile public queries use short freshness even when the private cache key is neutral', async () => {
    const time = Date.parse('2026-10-01T12:00:00Z');
    const records = memory(() => time);
    const research = new ResearchService(records, { name: 'fixture', async search() { return [hit]; } }, undefined,
        async () => ({ ...page, text: 'The latest centroid price is 10 credits.' }), 100, () => time);
    await research.answer({ ...input, content: 'check this topic', publicQuery: 'latest centroid price' }, 'conversation', new AbortController().signal);
    const saved = (await records.read()).records[0]!;
    assert.equal(saved.queryKey, 'check this topic');
    assert.equal(saved.freshnessPolicy, 'volatile-v1');
    assert.equal(Date.parse(saved.expiresAt!), time + 300000);
});

test('an unsupported hello world automatically researches only the current message, remembers sources on request, then uses cache', async () => {
    const records = memory();
    await records.remember('Private deployment password: do-not-export', 'private-conversation');
    const queries: string[] = [];
    let fetches = 0;
    const research = new ResearchService(records, { name: 'fixture', async search(query) {
        queries.push(query); return [hit];
    } }, undefined, async () => { fetches++; return { ...page, text: 'Hello world is a traditional introductory programming example.' }; });
    const request = { requestId: 'hello', revision: 0, content: '  hello\n world  ', rememberSources: true,
        applicability: 'private deployment context' };
    const answer = await research.answer(request, 'conversation', new AbortController().signal);
    assert.deepEqual(queries, ['hello world']);
    assert.equal(answer.research.status, 'searched');
    assert.match(answer.research.reason, /automatic research/);
    assert.equal(answer.research.queries, 1);
    assert.equal(answer.research.fetched, 1);
    assert.equal(answer.sources.length, 1);
    assert.equal(answer.memoryIds.length, 1);
    assert.match(answer.content, /introductory programming/);
    const cached = await research.answer({ ...request, requestId: 'again', content: 'HELLO WORLD' }, 'next', new AbortController().signal);
    assert.equal(cached.research.status, 'memory');
    assert.equal(cached.research.queries, 0);
    assert.deepEqual(cached.sources, answer.sources);
    assert.deepEqual(cached.memoryIds, answer.memoryIds);
    assert.equal(queries.length, 1);
    assert.equal(fetches, 1);
});

test('single-term messages can research without silently enabling source memory', async () => {
    const records = memory();
    const research = new ResearchService(records, { name: 'fixture', async search(query) {
        assert.equal(query, 'hello'); return [hit];
    } }, undefined, async () => ({ ...page, text: 'Hello is a common greeting.' }));
    const answer = await research.answer({ requestId: 'short', revision: 0, content: 'hello' }, 'conversation', new AbortController().signal);
    assert.equal(answer.research.status, 'searched');
    assert.equal(answer.sources.length, 1);
    assert.deepEqual(answer.memoryIds, []);
    assert.deepEqual((await records.read()).records, []);
});

test('autoSearch false keeps unsupported messages offline and explicit publicQuery still requests research', async () => {
    const records = memory();
    let calls = 0;
    const research = new ResearchService(records, { name: 'fixture', async search(query) {
        calls++; assert.equal(query, input.publicQuery); return [hit];
    } }, undefined, async () => page);
    const offline = { requestId: 'offline', revision: 0, content: 'hello world', autoSearch: false };
    const answer = await research.answer(offline, 'conversation', new AbortController().signal);
    assert.equal(answer.research.status, 'none');
    assert.equal(answer.research.queries, 0);
    assert.match(answer.research.reason, /autoSearch is false/);
    assert.equal(calls, 0);
    assert.equal((await records.read()).researchQuota, undefined);
    const explicit = await research.answer({ ...offline, content: '?', publicQuery: input.publicQuery }, 'conversation', new AbortController().signal);
    assert.equal(explicit.research.status, 'searched');
    assert.match(explicit.research.reason, /explicit public query/);
    assert.equal(explicit.sources.length, 1);
    assert.equal(calls, 1);
});

test('explicit publicQuery refreshes previously remembered evidence rather than using cache', async () => {
    const records = memory();
    let calls = 0;
    const research = new ResearchService(records, { name: 'fixture', async search() {
        calls++; return [hit];
    } }, undefined, async () => page);
    await research.answer(input, 'conversation', new AbortController().signal);
    const refreshed = await research.answer(input, 'conversation', new AbortController().signal);
    assert.equal(refreshed.research.status, 'searched');
    assert.match(refreshed.research.reason, /explicit public query/);
    assert.equal(calls, 2);
});

test('automatic research refuses long or credential-shaped messages without truncation or network calls', async () => {
    const records = memory();
    let calls = 0;
    const research = new ResearchService(records, { name: 'fixture', async search() { calls++; return [hit]; } });
    for (const content of ['x'.repeat(513), 'é'.repeat(257), 'my password is very-private',
        'api_key=super-secret', 'Authorization: Bearer abcdefghijklmnop', 'Please explain sk-abcdefghijklmnopqrstuv',
        '-----BEGIN RSA PRIVATE KEY-----', 'postgresql://person:private@database.example/table']) {
        const answer = await research.answer({ requestId: 'private', revision: 0, content }, 'conversation', new AbortController().signal);
        assert.equal(answer.research.mode, 'clarification', content);
        assert.equal(answer.research.queries, 0);
        assert.match(answer.content, /publicQuery/);
    }
    assert.equal(calls, 0);
    assert.equal((await records.read()).researchQuota, undefined);
});

test('an explicit public query exports only its value even when the message contains credentials', async () => {
    const records = memory();
    const research = new ResearchService(records, { name: 'fixture', async search(query) {
        assert.equal(query, input.publicQuery); return [hit];
    } }, undefined, async () => page);
    const answer = await research.answer({ ...input, content: 'my password is private; explain centroid facts' }, 'conversation', new AbortController().signal);
    assert.equal(answer.research.status, 'searched');
    assert.equal(answer.sources.length, 1);
});

test('automatic routing reports disabled providers, failures, and exhausted quotas with actual attempt counts', async () => {
    const request = { requestId: 'hello', revision: 0, content: 'hello world' };
    const disabledMemory = memory();
    const disabled = await new ResearchService(disabledMemory).answer(request, 'conversation', new AbortController().signal);
    assert.equal(disabled.research.status, 'disabled');
    assert.equal(disabled.research.provider, null);
    assert.equal(disabled.research.queries, 0);
    assert.match(disabled.research.reason, /automatic research.*no provider/);
    assert.equal((await disabledMemory.read()).researchQuota, undefined);
    const failed = await new ResearchService(memory(), { name: 'broken', async search() {
        throw new Error('fixture provider failed');
    } }).answer(request, 'conversation', new AbortController().signal);
    assert.equal(failed.research.status, 'failed');
    assert.equal(failed.research.queries, 1);
    assert.equal(failed.research.fetched, 0);
    assert.match(failed.research.reason, /automatic research.*fixture provider failed/);
    const limitedMemory = memory();
    let calls = 0;
    const limited = new ResearchService(limitedMemory, { name: 'limited', async search() { calls++; return []; } }, undefined, undefined, 1);
    const first = await limited.answer(request, 'conversation', new AbortController().signal);
    assert.equal(first.research.status, 'searched');
    assert.equal(first.research.mode, 'abstained');
    const second = await limited.answer(request, 'conversation', new AbortController().signal);
    assert.equal(second.research.status, 'unavailable');
    assert.equal(second.research.queries, 0);
    assert.match(second.research.reason, /automatic research.*quota exhausted/);
    assert.equal(calls, 1);
});

test('messages with no retrieval terms request clarification before trying automatic research', async () => {
    let calls = 0;
    const research = new ResearchService(memory(), { name: 'fixture', async search() { calls++; return [hit]; } });
    for (const content of ['?', '   ', 'what is it']) {
        const answer = await research.answer({ requestId: 'ambiguous', revision: 0, content }, 'conversation', new AbortController().signal);
        assert.equal(answer.research.mode, 'clarification');
        assert.equal(answer.research.queries, 0);
    }
    assert.equal(calls, 0);
});

test('public research sends only the approved query and bounds attempts with provenance-preserving deduplication', async () => {
    const records = memory();
    const fetched: string[] = [];
    const research = new ResearchService(records, { name: 'fixture', async search(query) {
        assert.equal(query, input.publicQuery);
        return [hit, { ...hit, url: `${hit.url}#duplicate` }, ...Array.from({ length: 7 }, (_, i) => ({ ...hit, url: `${hit.url}/${i}` }))];
    } }, undefined, async (url) => { fetched.push(url); return page; });
    const answer = await research.answer(input, 'conversation', new AbortController().signal);
    assert.equal(fetched.length, 4);
    assert.equal(answer.research.queries, 1);
    assert.equal(answer.sources.length, 1);
    assert.equal(answer.sources[0]!.path, hit.url);
    assert.equal(answer.sources[0]!.url, page.url);
    assert.match(answer.sources[0]!.contentHash!, /^[a-f0-9]{64}$/);
    assert.equal(answer.memoryIds.length, 1);
    const cached = await research.answer(cachedInput, 'next', new AbortController().signal);
    assert.equal(cached.research.status, 'memory');
    assert.deepEqual(cached.sources, answer.sources);
    assert.equal(fetched.length, 4);
});

test('successful source results survive disabled memory with truthful save status', async () => {
    const records = memory();
    await records.settings(false, 30);
    const research = new ResearchService(records, { name: 'fixture', async search() { return [hit]; } }, undefined, async () => page);
    const answer = await research.answer(input, 'conversation', new AbortController().signal);
    assert.equal(answer.research.status, 'searched');
    assert.equal(answer.sources.length, 1);
    assert.equal(answer.memoryIds.length, 0);
    assert.match(answer.research.reason, /could not be saved/);
    assert.deepEqual((await records.read()).records, []);
});

test('explicit public research cannot mix private repository passages into its source memory', async (context) => {
    const directory = await mkdtemp(join(tmpdir(), 'cgai-public-research-'));
    context.after(async () => {
        assert.equal(dirname(resolve(directory)), resolve(tmpdir()));
        await rm(directory, { recursive: true, force: true });
    });
    const text = 'Private centroid question: private deployment identifiers belong only to this repository.';
    const commit = 'a'.repeat(40), blob = 'b'.repeat(40), path = 'private/deployment.ts';
    const document = { text, sha256: sha256(text), sources: [{ path, commit, blob }] };
    const files = { 'train.txt': `${text}\n\n`, 'train.jsonl': `${JSON.stringify(document)}\n`,
        'validation.txt': '', 'validation.jsonl': '', 'SOURCE_LICENSE.txt': 'MIT\n' };
    for (const [name, content] of Object.entries(files)) await writeFile(join(directory, name), content);
    await writeFile(join(directory, 'manifest.json'), JSON.stringify({ schema_version: 1,
        source: { commit }, documents: { train: 1, validation: 0 }, entries: [{ path, oid: blob }],
        files: Object.fromEntries(Object.entries(files).map(([name, content]) => [name, sha256(content)])), rejected: [] }));
    const records = memory();
    const research = new ResearchService(records, { name: 'fixture', async search(query) {
        assert.equal(query, input.publicQuery); return [hit];
    } }, directory, async () => page);
    const local = await research.answer(cachedInput, 'local', new AbortController().signal);
    assert.equal(local.sources[0]!.path, path);
    const publicResult = await research.answer(input, 'public', new AbortController().signal);
    assert.deepEqual(publicResult.sources.map((source) => source.path), [hit.url]);
    assert.deepEqual((await records.read()).records.flatMap((record) => record.sources.map((source) => source.path)), [hit.url]);
});

test('pre-aborted research neither spends quota nor returns cached evidence', async () => {
    const records = memory();
    let calls = 0;
    const research = new ResearchService(records, { name: 'fixture', async search() { calls++; return [hit]; } }, undefined, async () => page);
    await assert.rejects(research.answer(input, 'conversation', AbortSignal.abort(new Error('cancelled'))), /cancelled/);
    assert.equal(calls, 0);
    assert.equal((await records.read()).researchQuota, undefined);
    await research.answer(input, 'conversation', new AbortController().signal);
    await assert.rejects(research.answer(cachedInput, 'conversation', AbortSignal.abort(new Error('cancelled'))), /cancelled/);
});

test('cancellation settles even when a provider ignores abort and never resolves', async () => {
    const records = memory();
    const controller = new AbortController();
    const research = new ResearchService(records, { name: 'fixture', async search() {
        controller.abort(new Error('cancelled provider')); return new Promise(() => {});
    } });
    await assert.rejects(research.answer(input, 'conversation', controller.signal), /cancelled provider/);
    assert.deepEqual((await records.read()).records, []);
});

test('cancellation after a fetch resolves prevents admitting the source to memory', async () => {
    const records = memory();
    const controller = new AbortController();
    const research = new ResearchService(records, { name: 'fixture', async search() { return [hit]; } }, undefined,
        async () => { controller.abort(new Error('cancelled fetch')); return page; });
    await assert.rejects(research.answer(input, 'conversation', controller.signal), /cancelled fetch/);
    assert.deepEqual((await records.read()).records, []);
});
