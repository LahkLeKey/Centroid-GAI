/** Offline lexical passage retrieval over verified, commit-pinned snapshot documents. */
import { loadSnapshot, sha256 } from '../evaluation/codebase-data.ts';
import type { Document } from '../evaluation/codebase-data.ts';

export const retrievalPolicy = {
    version: 1, chunkLines: 24, overlapLines: 8, k1: 1.2, b: 0.75,
    pathWeight: 2, minimumCoverage: 0.5, defaultLimit: 5,
} as const;
const stopWords = new Set(('a an and are as at be by can do does for from how i in into is it of on or ' +
    's that the their this to was what when where which who why with would you').split(' '));

/** Keep complete identifiers plus camel/snake-case components; use no model or query-specific synonyms. */
export function retrievalTerms(text: string): string[] {
    const words = text.normalize('NFC').match(/[\p{L}\p{N}_]+/gu) ?? [];
    return words.flatMap(word => {
        const parts = word.replace(/([a-z])([A-Z])/g, '$1 $2').toLowerCase().split(/[_\s]+/);
        const full = word.toLowerCase();
        return parts.length > 1 ? [full, ...parts] : parts;
    }).filter(word => word.length > 1 && !stopWords.has(word));
}

export interface Citation {
    path: string;
    blob: string;
    commit: string;
    documentSha256: string;
    // These coordinates refer to normalized JSONL text, NOT raw Git blob line numbers.
    coordinateSystem: 'snapshot-normalized-lines';
    startLine: number;
    endLine: number;
}
export interface Passage {
    id: string;
    text: string;
    textSha256: string;
    citations: Citation[];
}
export interface Hit extends Passage { score: number; coverage: number; matchedTerms: string[] }
export interface RetrievalResult {
    question: string;
    status: 'evidence' | 'abstained';
    reason: string;
    hits: Hit[];
}

/** Citation IDs depend only on snapshot text and coordinates, independent of input record ordering. */
export function passageId(documentSha256: string, startLine: number, endLine: number): string {
    return `${documentSha256}:${startLine}-${endLine}`;
}

/** Preserve exact normalized lines, including all provenance aliases of deduplicated documents. */
function passages(documents: Document[]): Passage[] {
    return documents.flatMap(document => {
        const lines = document.text.split('\n');
        const result: Passage[] = [];
        for (let start = 0; start < lines.length; start += retrievalPolicy.chunkLines - retrievalPolicy.overlapLines) {
            const end = Math.min(start + retrievalPolicy.chunkLines, lines.length);
            const text = lines.slice(start, end).join('\n');
            result.push({ id: passageId(document.sha256, start + 1, end), text, textSha256: sha256(text),
                citations: [...document.sources].sort((a, b) => compare(a.path, b.path)).map(source => ({
                    path: source.path, blob: source.blob, commit: source.commit, documentSha256: document.sha256,
                    coordinateSystem: 'snapshot-normalized-lines', startLine: start + 1, endLine: end,
                })) });
            if (end === lines.length) break;
        }
        return result;
    }).sort((a, b) => compare(a.id, b.id));
}

function compare(left: string, right: string) { return left < right ? -1 : left > right ? 1 : 0; }

/** Deterministic BM25 baseline. Both snapshot splits are searchable; this does not train a model. */
export function createRetriever(documents: Document[], options: {
    terms?: (text: string) => string[]; minimumCoverage?: number; pathWeight?: number;
} = {}) {
    if (!documents.length) throw new Error('Need source documents');
    const termsFor = options.terms ?? retrievalTerms;
    const minimumCoverage = options.minimumCoverage ?? retrievalPolicy.minimumCoverage;
    const pathWeight = options.pathWeight ?? retrievalPolicy.pathWeight;
    const rows = passages(documents).map(passage => {
        const content = termsFor(passage.text);
        const path = [...new Set(passage.citations.flatMap(citation => termsFor(citation.path)))];
        const counts = new Map<string, number>();
        for (const term of content) counts.set(term, (counts.get(term) ?? 0) + 1);
        for (const term of path) counts.set(term, (counts.get(term) ?? 0) + pathWeight);
        return { passage, counts, length: content.length + path.length * pathWeight };
    });
    const frequencies = new Map<string, number>();
    for (const row of rows) for (const term of row.counts.keys()) frequencies.set(term, (frequencies.get(term) ?? 0) + 1);
    const averageLength = rows.reduce((sum, row) => sum + row.length, 0) / rows.length || 1;
    return {
        documents: documents.length, passages: rows.length,
        search(question: string, limit: number = retrievalPolicy.defaultLimit): RetrievalResult {
            if (!question.trim() || question.includes('\0') || Buffer.byteLength(question) > 16384) {
                throw new Error('Question must contain text without NUL and fit 16384 UTF-8 bytes');
            }
            if (!Number.isInteger(limit) || limit < 1 || limit > 20) throw new Error('Limit must be an integer from 1 to 20');
            const terms = [...new Set(termsFor(question))];
            const ranked: Hit[] = [];
            for (const row of rows) {
                const matchedTerms = terms.filter(term => row.counts.has(term));
                const coverage = terms.length ? matchedTerms.length / terms.length : 0;
                if (coverage < minimumCoverage || matchedTerms.length < Math.min(2, terms.length) || !terms.length) continue;
                const score = matchedTerms.reduce((sum, term) => {
                    const tf = row.counts.get(term)!;
                    const df = frequencies.get(term)!;
                    const idf = Math.log(1 + (rows.length - df + 0.5) / (df + 0.5));
                    return sum + idf * tf * (retrievalPolicy.k1 + 1) /
                        (tf + retrievalPolicy.k1 * (1 - retrievalPolicy.b + retrievalPolicy.b * row.length / averageLength));
                }, 0);
                ranked.push({ ...row.passage, score, coverage, matchedTerms });
            }
            ranked.sort((a, b) => b.score - a.score || compare(a.id, b.id));
            const hits: Hit[] = [];
            for (const hit of ranked) {
                const citation = hit.citations[0]!;
                if (hits.some(previous => previous.citations.some(other =>
                    other.documentSha256 === citation.documentSha256 &&
                    other.startLine <= citation.endLine && citation.startLine <= other.endLine))) continue;
                hits.push(hit);
                if (hits.length === limit) break;
            }
            return { question, status: hits.length ? 'evidence' : 'abstained',
                reason: hits.length ? 'Candidate source excerpts; lexical relevance is not proof that they answer the question.' :
                    'No passage met the lexical coverage policy in this snapshot.', hits };
        },
    };
}

/** Verify a quote and every source alias against the loaded snapshot, not the mutable checkout. */
export function validPassage(passage: Passage, documents: Document[]): boolean {
    if (!passage.citations.length || sha256(passage.text) !== passage.textSha256) return false;
    return passage.citations.every(citation => {
        const document = documents.find(item => item.sha256 === citation.documentSha256);
        if (!document || citation.coordinateSystem !== 'snapshot-normalized-lines' ||
            !Number.isInteger(citation.startLine) || !Number.isInteger(citation.endLine) ||
            citation.startLine < 1 || citation.endLine < citation.startLine ||
            passage.id !== passageId(document.sha256, citation.startLine, citation.endLine)) return false;
        const lines = document.text.split('\n');
        return citation.endLine <= lines.length &&
            lines.slice(citation.startLine - 1, citation.endLine).join('\n') === passage.text &&
            document.sources.some(source => source.path === citation.path && source.blob === citation.blob && source.commit === citation.commit);
    });
}

/** Load checksummed immutable records; source indexing does not require a held-out training split. */
export function loadRepositoryIndex(directory: string) {
    const snapshot = loadSnapshot(directory, { allowEmptyValidation: true });
    const documents = [...snapshot.train, ...snapshot.validation];
    return { snapshot, documents, retriever: createRetriever(documents) };
}
