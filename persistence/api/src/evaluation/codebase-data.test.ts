import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { inspectNativeContents, inspectNativeModel, trainNativeModel } from '../native.ts';
import { cLocaleTokens, documentProbes, loadSnapshot, sha256 } from './codebase-data.ts';

function fixture(trainText = 'int training_value = 42;', validationText = 'int heldout_value = 17;') {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-codebase-'));
    const record = (text: string, path: string) => ({ text, sha256: sha256(text),
        sources: [{ path, blob: sha256(path), commit: 'a'.repeat(40) }] });
    const train = record(trainText, 'train.c');
    const validation = record(validationText, 'validation.c');
    const content = { 'train.txt': `${trainText}\n\n`, 'validation.txt': `${validationText}\n\n`,
        'train.jsonl': JSON.stringify(train) + '\n', 'validation.jsonl': JSON.stringify(validation) + '\n',
        'SOURCE_LICENSE.txt': 'MIT\n' };
    for (const [name, text] of Object.entries(content)) writeFileSync(join(directory, name), text);
    const manifest = { schema_version: 1, source: { commit: 'a'.repeat(40) }, documents: { train: 1, validation: 1 },
        entries: [train, validation].map(row => ({ path: row.sources[0]!.path, oid: row.sources[0]!.blob })),
        files: Object.fromEntries(Object.entries(content).map(([name, text]) => [name, sha256(text)])), rejected: [] };
    writeFileSync(join(directory, 'manifest.json'), JSON.stringify(manifest));
    return { directory, manifest };
}

test('code probe tokenizer matches native token spellings and training counts', () => {
    const text = "int foo_bar=42; // CAFÉ\u00a0ABC don't 😀.\r\n";
    const payload = trainNativeModel(text);
    const tokens = cLocaleTokens(text);
    assert.equal(inspectNativeModel(payload).examplesSeen, BigInt(tokens.length + 1));
    const actual = inspectNativeContents(payload, 'vocabulary', 3, 100).items.map(item => item.token).sort();
    assert.deepEqual(actual, [...new Set(tokens)].sort());
});

test('snapshot verifies provenance, corpus consistency and document separation', () => {
    const { directory, manifest } = fixture();
    try {
        const snapshot = loadSnapshot(directory);
        assert.equal(snapshot.train.length, 1);
        const probes = documentProbes(snapshot.train, 'recall');
        assert(probes.length > 0);
        assert(probes.every(probe => probe.sources[0]!.path === 'train.c' && probe.tokenOffset >= 3));
        assert(probes.every(probe => /[a-z0-9]/.test(probe.expected)));
        writeFileSync(join(directory, 'train.txt'), 'changed');
        assert.throws(() => loadSnapshot(directory), /checksum/);
        manifest.files['train.txt'] = sha256('changed');
        writeFileSync(join(directory, 'manifest.json'), JSON.stringify(manifest));
        assert.throws(() => loadSnapshot(directory), /Corpus\/record mismatch/);
    } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('even a consistently rehashed snapshot cannot hide exact split leakage', () => {
    const { directory } = fixture('int duplicate = 1;', 'int duplicate = 1;');
    try { assert.throws(() => loadSnapshot(directory), /leakage/); }
    finally { rmSync(directory, { recursive: true, force: true }); }
});

test('provenance mismatch and empty held-out splits fail before training', () => {
    const { directory, manifest } = fixture();
    try {
        const path = join(directory, 'validation.jsonl');
        const record = JSON.parse(readFileSync(path, 'utf8'));
        record.sources[0].commit = 'b'.repeat(40);
        const bytes = JSON.stringify(record) + '\n';
        writeFileSync(path, bytes);
        manifest.files['validation.jsonl'] = sha256(bytes);
        writeFileSync(join(directory, 'manifest.json'), JSON.stringify(manifest));
        assert.throws(() => loadSnapshot(directory), /provenance/);
        writeFileSync(path, '');
        manifest.files['validation.jsonl'] = sha256('');
        manifest.documents.validation = 0;
        writeFileSync(join(directory, 'manifest.json'), JSON.stringify(manifest));
        assert.throws(() => loadSnapshot(directory), /validation document count/);
    } finally { rmSync(directory, { recursive: true, force: true }); }
});
