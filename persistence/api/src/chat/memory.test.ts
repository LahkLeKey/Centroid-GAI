import assert from 'node:assert/strict';
import test from 'node:test';
import type { ChatSource } from '../../../shared/chat.ts';
import type { MemoryDocument } from '../../../shared/research.ts';
import { MemoryService } from './memory.ts';

const epoch = Date.parse('2026-09-29T12:00:00Z');
const source: ChatSource = { id: 'source', path: 'https://example.com/source', url: 'https://example.com/source',
    excerpt: 'Centroid facts from the original passage.', contentHash: 'a'.repeat(64), fetchedAt: new Date(epoch).toISOString() };
function storage() {
    const documents = new Map<string, MemoryDocument>();
    return { documents, async getMemory(owner: string) { return structuredClone(documents.get(owner) ?? null); },
        async saveMemory(value: MemoryDocument, revision: number) {
            if ((documents.get(value.ownerId)?.revision ?? 0) !== revision) return false;
            documents.set(value.ownerId, structuredClone(value)); return true;
        } };
}

test('source memory retains original provenance, isolates owners and rejects expired or disputed reuse', async () => {
    const store = storage();
    let now = epoch;
    const first = new MemoryService(store, 'first', () => now), second = new MemoryService(store, 'second', () => now);
    const [saved] = await first.sources('centroid question', 'version-one', [source, source], 'conversation');
    assert.ok(saved);
    assert.equal((await first.read()).records.length, 1);
    assert.deepEqual((await first.retrieve('centroid question', 'version-one'))[0]!.sources, [source]);
    assert.deepEqual(await first.retrieve('centroid question', 'version-two'), []);
    assert.deepEqual(await second.retrieve('centroid question', 'version-one'), []);
    await assert.rejects(second.dispute(saved.id), /not found/);
    now += 86400000;
    assert.deepEqual(await first.retrieve('centroid question', 'version-one'), []);
    assert.equal((await first.read()).records[0]!.state, 'stale');
    await first.dispute(saved.id);
    assert.equal((await first.read()).records[0]!.state, 'disputed');
});

test('invalid expiry never becomes permanent supported evidence and retention physically prunes records', async () => {
    const store = storage();
    let now = epoch;
    const memory = new MemoryService(store, 'owner', () => now);
    await memory.sources('centroid question', '', [source], 'conversation');
    const original = store.documents.get('owner')!;
    store.documents.set('owner', { ...original, records: [
        { ...original.records[0]!, expiresAt: 'not-a-date' },
        { ...original.records[0]!, id: 'no-expiry', expiresAt: null },
        { ...original.records[0]!, id: 'foreign', ownerId: 'other' },
    ] });
    assert.deepEqual(await memory.retrieve('centroid question', ''), []);
    assert.equal((await memory.read()).records.length, 2);
    now += 31 * 86400000;
    await memory.prune();
    assert.deepEqual(store.documents.get('owner')!.records, []);
    store.documents.set('owner', { ...original, ownerId: 'other' });
    await assert.rejects(memory.read(), /owner mismatch/);
});

test('disabled memory, forgetting and quota reservations remain owner scoped', async () => {
    const store = storage();
    const memory = new MemoryService(store, 'owner', () => epoch);
    await memory.remember('my preferred version is one', 'first');
    await memory.remember('my preferred editor is two', 'second');
    await memory.forget(undefined, 'first');
    assert.equal((await memory.read()).records[0]!.conversationId, 'second');
    await memory.settings(false, 30);
    await assert.rejects(memory.sources('question', '', [source], 'first'), /disabled/);
    await assert.rejects(memory.remember('new statement', null), /disabled/);
    const cancelled = AbortSignal.abort(new Error('cancelled'));
    await assert.rejects(memory.reserveQuery('2026-09-29', 1, cancelled), /cancelled/);
    assert.equal((await memory.read()).researchQuota, undefined);
    assert.equal(await memory.reserveQuery('2026-09-29', 1), true);
    assert.equal(await memory.reserveQuery('2026-09-29', 1), false);
    assert.equal(await new MemoryService(store, 'other').reserveQuery('2026-09-29', 1), true);
    assert.equal(await memory.reserveQuery('2026-09-30', 1), true);
    await assert.rejects(memory.reserveQuery('2026-09-30', NaN), /invalid research quota/);
    await memory.forget();
    assert.deepEqual((await memory.read()).records, []);
});

test('cancellation during a storage read prevents source admission and quota writes', async () => {
    const controller = new AbortController();
    let writes = 0;
    const memory = new MemoryService({ async getMemory() { controller.abort(new Error('cancelled')); return null; },
        async saveMemory() { writes++; return true; } }, 'owner', () => epoch);
    await assert.rejects(memory.sources('centroid question', '', [source], 'conversation', controller.signal), /cancelled/);
    assert.equal(writes, 0);
});
