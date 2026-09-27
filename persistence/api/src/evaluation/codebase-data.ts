/** Verify Git snapshot provenance before constructing repository prediction probes. */
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import type { Probe } from './metrics.ts';

export const sha256 = (data: string | Uint8Array) => createHash('sha256').update(data).digest('hex');
export interface Document {
    sha256: string;
    text: string;
    sources: { path: string; blob: string; commit: string }[];
}
interface Manifest {
    schema_version: number;
    source: { commit: string };
    documents: { train: number; validation: number };
    files: Record<string, string>;
    entries: { path: string; oid: string }[];
    rejected: { path: string; reason: string }[];
}

/** Match tokenizer.c in the default C locale: ASCII case folding and UTF-8 word bytes. */
export function cLocaleTokens(text: string): string[] {
    if (text.includes('\0')) throw new Error('NUL is not valid corpus text');
    return (text.match(/[A-Za-z0-9'\u0080-\uffff]+|[^ \t\r\n\v\f]/g) ?? [])
        .map(token => token.replace(/[A-Z]/g, letter => letter.toLowerCase()));
}

/** Check files, hashes, provenance, counts, and split separation; retrieval may allow empty validation. */
export function loadSnapshot(directory: string, { allowEmptyValidation = false } = {}) {
    const manifestBytes = readFileSync(resolve(directory, 'manifest.json'));
    const manifest = JSON.parse(manifestBytes.toString('utf8')) as Manifest;
    const required = ['SOURCE_LICENSE.txt', 'train.jsonl', 'train.txt', 'validation.jsonl', 'validation.txt'];
    if (manifest.schema_version !== 1 || JSON.stringify(Object.keys(manifest.files).sort()) !== JSON.stringify(required)) {
        throw new Error('Invalid snapshot file inventory');
    }
    const objectId = /^(?:[a-f0-9]{40}|[a-f0-9]{64})$/;
    if (!objectId.test(manifest.source.commit) || manifest.entries.some(entry =>
        typeof entry.path !== 'string' || !entry.path || !objectId.test(entry.oid)) ||
        new Set(manifest.entries.map(entry => entry.path)).size !== manifest.entries.length) {
        throw new Error('Invalid snapshot provenance inventory');
    }
    const files = new Map(required.map(name => {
        const bytes = readFileSync(resolve(directory, name));
        if (sha256(bytes) !== manifest.files[name]) throw new Error(`Snapshot checksum mismatch: ${name}`);
        return [name, bytes.toString('utf8')];
    }));
    const seen = new Set<string>();
    const entries = new Map(manifest.entries.map(entry => [entry.path, entry.oid]));
    function documents(split: 'train' | 'validation') {
        const records = files.get(`${split}.jsonl`)!.trim().split('\n').filter(Boolean)
            .map(line => JSON.parse(line) as Document);
        if ((!records.length && !(split === 'validation' && allowEmptyValidation)) ||
            records.length !== manifest.documents[split]) throw new Error(`Invalid ${split} document count`);
        for (const record of records) {
            if (sha256(record.text) !== record.sha256) throw new Error('Document checksum mismatch');
            if (seen.has(record.sha256)) throw new Error('Duplicate document or train/validation leakage');
            seen.add(record.sha256);
            if (!record.sources.length || record.sources.some(source => source.commit !== manifest.source.commit ||
                !entries.has(source.path) || !objectId.test(source.blob) ||
                entries.get(source.path) !== source.blob)) throw new Error('Document provenance mismatch');
        }
        if (records.map(record => `${record.text}\n\n`).join('') !== files.get(`${split}.txt`)) {
            throw new Error(`Corpus/record mismatch: ${split}`);
        }
        return records;
    }
    return { manifest, manifestSha256: sha256(manifestBytes), train: documents('train'),
        validation: documents('validation'), text: files.get('train.txt')! };
}

/** Evenly sample word targets per document; punctuation never pads accuracy. No cross-file prompts. */
export function documentProbes(documents: Document[], slice: 'recall' | 'challenge', contextWindow = 3) {
    return documents.flatMap(document => {
        const tokens = cLocaleTokens(document.text);
        const positions = tokens.flatMap((token, index) => index >= contextWindow && /[a-z0-9\u0080-\uffff]/.test(token) ? [index] : []);
        const count = Math.min(8, positions.length);
        return Array.from({ length: count }, (_, sample) => {
            const position = positions[Math.floor(sample * positions.length / count)]!;
            return {
                id: `${slice}:${document.sha256}:${position}`, domain: document.sources[0]!.path,
                slice, prompt: tokens.slice(position - contextWindow, position).join(' '), expected: tokens[position]!,
                tokenOffset: position, sources: document.sources,
            } satisfies Probe & { tokenOffset: number; sources: Document['sources'] };
        });
    });
}
