/** Categorize and globally deduplicate normalized snapshot text before native training. */
import { cLocaleTokens, sha256 } from '../evaluation/codebase-data.ts';
import type { Document } from '../evaluation/codebase-data.ts';
import { compareIds } from './spatial.ts';

export const corpusPolicy = { version: 1, chunkLines: 32, maxShardBytes: 128 * 1024,
    deduplication: 'exact-normalized-document-and-chunk-sha256', category: 'source-path-v1' } as const;
export interface ChunkSource { documentSha256: string; path: string; blob: string; commit: string; startLine: number; endLine: number }
export interface SourceChunk { sha256: string; text: string; category: string; categories: string[]; sources: ChunkSource[] }
export interface CorpusShard { id: string; category: string; chunks: SourceChunk[]; text: string }

/** Categories describe repository responsibilities, not semantic clusters in the embedding space. */
export function sourceCategory(path: string): string {
    if (/\.(md|rst)$/i.test(path)) return 'documentation';
    if (path.startsWith('include/')) return 'public-api';
    if (path.startsWith('src/')) return 'native-core';
    if (path.startsWith('tests/')) return 'native-tests';
    if (path.startsWith('persistence/api/native/')) return 'native-bridge';
    if (path.startsWith('persistence/api/') || path.startsWith('persistence/shared/')) return 'service';
    if (path.startsWith('persistence/db/')) return 'database';
    // Preserve category identity when reading snapshots from before the frontend was removed.
    if (path.startsWith('persistence/web/')) return 'web';
    if (path.startsWith('tools/')) return 'tooling';
    return 'build';
}

/** Train each exact chunk once globally, retaining aliases and cross-category membership as provenance. */
export function prepareCodebase(documents: Document[]) {
    const chunks = new Map<string, SourceChunk>();
    let inputChunks = 0;
    let inputBytes = 0;
    for (const document of [...documents].sort((a, b) => compareIds(a.sha256, b.sha256))) {
        if (sha256(document.text) !== document.sha256 || !document.sources.length) throw new Error('Invalid source document');
        const lines = document.text.split('\n');
        for (let start = 0; start < lines.length; start += corpusPolicy.chunkLines) {
            const end = Math.min(start + corpusPolicy.chunkLines, lines.length);
            const text = lines.slice(start, end).join('\n');
            if (!cLocaleTokens(text).length) continue;
            inputChunks += document.sources.length;
            inputBytes += Buffer.byteLength(text) * document.sources.length;
            const hash = sha256(text);
            const chunk = chunks.get(hash) ?? { sha256: hash, text, category: '', categories: [], sources: [] };
            if (chunk.text !== text) throw new Error('Chunk hash collision');
            for (const source of document.sources) {
                const reference = { ...source, documentSha256: document.sha256, startLine: start + 1, endLine: end };
                if (!chunk.sources.some(item => JSON.stringify(item) === JSON.stringify(reference))) chunk.sources.push(reference);
            }
            chunks.set(hash, chunk);
        }
    }
    for (const chunk of chunks.values()) {
        chunk.sources.sort((a, b) => compareIds(a.path, b.path) || compareIds(a.documentSha256, b.documentSha256) || a.startLine - b.startLine);
        chunk.categories = [...new Set(chunk.sources.map(source => sourceCategory(source.path)))].sort(compareIds);
        // One owner avoids training cross-category duplicates twice. All categories remain recorded.
        chunk.category = chunk.categories[0]!;
    }
    const sorted = [...chunks.values()].sort((a, b) => compareIds(a.category, b.category) || compareIds(a.sha256, b.sha256));
    if (!sorted.length) throw new Error('No trainable chunks');
    const shards: CorpusShard[] = [];
    for (const category of [...new Set(sorted.map(chunk => chunk.category))]) {
        let members: SourceChunk[] = [];
        let bytes = 0;
        let number = 0;
        function flush() {
            if (!members.length) return;
            shards.push({ id: `${category}-${String(number++).padStart(3, '0')}`, category, chunks: members,
                text: members.map(chunk => chunk.text).join('\n\n') });
            members = [];
            bytes = 0;
        }
        for (const chunk of sorted.filter(item => item.category === category)) {
            const size = Buffer.byteLength(chunk.text);
            if (size > corpusPolicy.maxShardBytes) throw new Error('A source chunk exceeds the shard byte budget');
            if (bytes + size + (members.length ? 2 : 0) > corpusPolicy.maxShardBytes) flush();
            bytes += size + (members.length ? 2 : 0);
            members.push(chunk);
        }
        flush();
    }
    return { shards, statistics: { documents: documents.length, sourceFiles: new Set(documents.flatMap(doc => doc.sources.map(source => source.path))).size,
        inputChunks, uniqueChunks: sorted.length, duplicatesRemoved: inputChunks - sorted.length, inputBytes,
        uniqueBytes: sorted.reduce((sum, chunk) => sum + Buffer.byteLength(chunk.text), 0) } };
}
