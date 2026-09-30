/** Repository chat's lexical policy adds document structure without changing the standalone baseline. */
import type { Document } from '../evaluation/codebase-data.ts';
import { sha256 } from '../evaluation/codebase-data.ts';
import { createRetriever, passageId, retrievalTerms, type Hit, type Passage } from '../knowledge/retrieval.ts';

const queryBoilerplate = new Set(('please tell explain currently during across before after using use used implemented implement ' +
    'implements implementation function functions defined definition located location does should would could need enabled enable').split(' '));
/** Small symmetric inflection normalization; no query-specific synonyms or gold-answer lookup. */
export function repositoryTerms(text: string): string[] {
    return retrievalTerms(text).filter(term => !queryBoilerplate.has(term)).map(term => {
        if (term.length > 5 && term.endsWith('ies')) return term.slice(0, -3) + 'y';
        if (term.length > 4 && term.endsWith('s') && !term.endsWith('ss')) return term.slice(0, -1);
        return term;
    });
}

function span(document: Document, start: number, end: number): Passage {
    const text = document.text.split('\n').slice(start - 1, end).join('\n');
    return { id: passageId(document.sha256, start, end), text, textSha256: sha256(text), citations: document.sources.map(source => ({
        ...source, documentSha256: document.sha256, startLine: start, endLine: end, coordinateSystem: 'snapshot-normalized-lines',
    })) };
}
function expand(hit: Passage, documents: readonly Document[]): Passage {
    const citation = hit.citations[0]!;
    const document = documents.find(document => document.sha256 === citation.documentSha256)!;
    const lines = document.text.split('\n');
    // Preserve nearby declarations and documentation around a matching code line.
    let start = Math.max(1, citation.startLine - 12), end = Math.min(lines.length, citation.endLine + 12);
    if (/\.(md|ya?ml|json)$/.test(citation.path) && lines.length <= 160 && Buffer.byteLength(document.text) <= 16384) {
        start = 1; end = lines.length;
    }
    const expanded = span(document, start, end);
    return Buffer.byteLength(expanded.text) <= 16384 ? expanded : hit;
}

/** Parameter/field documentation must describe the queried relationship, not merely nearby settings. */
function declarationSupport(passage: Passage, query: ReadonlySet<string>): number {
    const units: string[] = [];
    let current = '';
    for (const line of passage.text.split('\n')) {
        if (/@(?:param|return|brief)\b|\/\*\*</.test(line)) {
            if (current) units.push(current);
            current = line;
        } else if (current && /^\s*\*(?!\/)/.test(line)) current += '\n' + line;
        else if (current) { units.push(current); current = ''; }
    }
    if (current) units.push(current);
    return units.reduce((best, unit) => Math.max(best,
        [...new Set(repositoryTerms(unit))].filter(term => query.has(term)).length), 0);
}

/** Keep the strongest supported declaration per document before using the two priority slots. */
function declarationCandidates(hits: readonly Hit[], query: ReadonlySet<string>): Passage[] {
    const ranked = hits.map(hit => ({ hit, support: declarationSupport(hit, query) }))
        .sort((left, right) => right.support - left.support || right.hit.score - left.hit.score || left.hit.id.localeCompare(right.hit.id));
    const selected: Passage[] = [];
    for (const { hit } of ranked) {
        if (selected.some(previous => previous.citations.some(other => hit.citations.some(citation =>
            citation.documentSha256 === other.documentSha256)))) continue;
        selected.push(hit);
        if (selected.length === 2) break;
    }
    return selected;
}

/** Admission and context policy stay in RepositoryService; this component only returns attributable passages. */
export function createRepositoryRetriever(documents: Document[]) {
    const options = { terms: repositoryTerms, minimumCoverage: 0.35, pathWeight: 4 };
    const index = createRetriever(documents, options);
    const vocabulary = new Set(documents.flatMap(document => repositoryTerms(document.text)));
    const basenames = new Map(documents.map(document => [document.sha256,
        new Set(document.sources.flatMap(source => repositoryTerms(source.path.split('/').at(-1)!.replace(/\.[^.]+$/, ''))))]));
    const pathFrequencies = new Map<string, number>();
    for (const terms of basenames.values()) for (const term of terms) pathFrequencies.set(term, (pathFrequencies.get(term) ?? 0) + 1);
    const headers = documents.filter(document => document.sources.some(source => /^include\/.*\.h$/.test(source.path)));
    const headerIndex = headers.length ? createRetriever(headers, options) : undefined;
    return {
        search(question: string): Passage[] {
            const acronyms = question.match(/\b[A-Z][A-Z0-9_]{1,}\b/g) ?? [];
            if (acronyms.some(word => !vocabulary.has(word.toLowerCase()))) return [];
            const terms = new Set(repositoryTerms(question));
            const hits: Passage[] = [];
            const overview = /\b(codebase|project|repository)\b/i.test(question) && /\b(support|purpose|overview|currently|capabilities)\b/i.test(question);
            if (overview) {
                const brief = documents.find(document => document.sources.some(source => source.path === 'docs/codebase-brief.md'));
                if (brief) hits.push(span(brief, 1, Math.min(32, brief.text.split('\n').length)));
            }
            const location = /\b(where|which (?:file|module|function)|implemented|defined|located)\b/i.test(question);
            const configuration = /\b(start|startup|boot|deploy|configuration|compose|docker)\b/i.test(question);
            if (location || configuration) {
                const anchors = documents.filter(document => [...basenames.get(document.sha256)!].some(term =>
                    terms.has(term) && term.length > 3 && pathFrequencies.get(term)! <= 8));
                const relevant = anchors.filter(document => document.sources.some(source => location ? /\.(c|h|ts|py)$/.test(source.path) : /\.(ya?ml|json)$/.test(source.path)));
                if (relevant.length) hits.push(...createRetriever(relevant, options).search(question, 2).hits);
            }
            if (/\bwhat\b.*\b(does|means?|do)\b/i.test(question) && headerIndex)
                hits.push(...declarationCandidates(headerIndex.search(question, 20).hits, terms));
            hits.push(...index.search(question, 10).hits);
            const expanded = hits.map(hit => expand(hit, documents));
            return expanded.filter((hit, index) => !expanded.slice(0, index).some(previous => previous.citations.some(other =>
                hit.citations.some(citation => citation.documentSha256 === other.documentSha256 &&
                    other.startLine <= citation.endLine && citation.startLine <= other.endLine)))).slice(0, 7);
        },
    };
}
