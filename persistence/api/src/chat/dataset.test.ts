import assert from 'node:assert/strict';
import test from 'node:test';
import { readFileSync } from 'node:fs';
import { validateDataset } from './dataset.ts';

const fixture = () => JSON.parse(readFileSync(new URL('../../../../examples/chat/dialogues-v1.json', import.meta.url), 'utf8'));
test('dialogue fixture has separate source/family splits and stable identities', () => {
    const first = validateDataset(fixture());
    assert.deepEqual(first.hashes, validateDataset(fixture()).hashes);
    for (const kind of ['family', 'sources', 'messages', 'id'] as const) {
        const value = fixture();
        value.test[0][kind] = value.train[0][kind];
        assert.throws(() => validateDataset(value));
    }
});
