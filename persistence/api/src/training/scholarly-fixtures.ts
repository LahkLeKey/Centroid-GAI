/** Synthetic software-test sources; these are not research evidence or training releases. */
import { createHash } from 'node:crypto';
import { parseCrossrefRecord, parseEuropePmcRecord } from './scholarly-provider.ts';
import { parseScholarlyXml } from './scholarly-xml.ts';
import type { CrossrefRecord, ScholarlyCollector, ScholarlyPaper, ScholarlySnapshot } from './scholarly-types.ts';

const escape = (value: string) => value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;');
export function scholarlyFixtureSnapshot(url: string, text: string, contentType = 'application/json'): ScholarlySnapshot {
    return { url, text, contentType, sha256: createHash('sha256').update(text).digest('hex'), fetchedAt: '2026-01-01T00:00:00.000Z' };
}
export function scholarlyFixtureCrossref(doi: string, options: { title?: string; year?: number; authors?: readonly string[];
    references?: readonly string[]; type?: string; retracted?: boolean } = {}): CrossrefRecord {
    const text = JSON.stringify({ status: 'ok', message: { DOI: doi, title: [options.title ?? `Bibliographic reference ${doi}`],
        type: options.type ?? 'journal-article', published: { 'date-parts': [[options.year ?? 2024]] },
        author: (options.authors ?? ['Independent Reference Author']).map(name => ({ name })),
        reference: (options.references ?? []).map(DOI => ({ DOI })), ...(options.retracted ? { 'update-to': [{ type: 'retraction' }] } : {}) } });
    return parseCrossrefRecord(scholarlyFixtureSnapshot(`https://api.crossref.org/works/${encodeURIComponent(doi)}`, text));
}
export function scholarlyFixturePaper(index: number, options: { quote?: string; secondQuote?: string; doi?: string;
    author?: string; title?: string; license?: string; publicationTypes?: readonly string[]; references?: readonly string[] } = {}): ScholarlyPaper {
    const id = `PMC${index + 1}`, doi = options.doi ?? `10.1234/synthetic-work-${index}`, title = options.title ?? `Synthetic research bibliography ${index}`;
    const author = options.author ?? `Researcher ${index}`, refs = options.references ?? ['10.9999/reference-a', '10.9999/reference-b'];
    const quote = options.quote ?? `The experiment number ${index} uses a controlled synthetic software fixture to test complete source attribution and reproducible evidence boundaries.`;
    const text = `<article xmlns:xlink="http://www.w3.org/1999/xlink"><front><article-meta><article-id pub-id-type="pmc">${id}</article-id>` +
        `<article-id pub-id-type="doi">${escape(doi)}</article-id><title-group><article-title>${escape(title)}</article-title></title-group>` +
        `<contrib-group><contrib contrib-type="author"><collab>${escape(author)}</collab></contrib></contrib-group>` +
        `<pub-date pub-type="epub"><year>2024</year></pub-date><permissions><license xlink:href="${escape(options.license ?? 'https://creativecommons.org/licenses/by/4.0/')}">Creative Commons Attribution</license></permissions>` +
        `</article-meta></front><body><p>${escape(quote)}</p>${options.secondQuote ? `<p>${escape(options.secondQuote)}</p>` : ''}</body>` +
        `<back><ref-list>${refs.map(reference => `<ref><element-citation><pub-id pub-id-type="doi">${escape(reference)}</pub-id></element-citation></ref>`).join('')}</ref-list></back></article>`;
    const parsed = parseScholarlyXml(text), publicationTypes = options.publicationTypes ?? ['JournalArticle'];
    const metadata = scholarlyFixtureSnapshot('https://www.ebi.ac.uk/europepmc/webservices/rest/search?query=synthetic&format=json&resultType=core',
        JSON.stringify({ resultList: { result: [{ pmcid: id, doi, title, pubYear: '2024', isOpenAccess: 'Y', inPMC: 'Y',
            authorString: author, pubTypeList: { pubType: publicationTypes } }] } }));
    parseEuropePmcRecord(metadata, id);
    return { ...parsed, id, publicationTypes, metadata,
        fullText: scholarlyFixtureSnapshot(`https://www.ebi.ac.uk/europepmc/webservices/rest/${id}/fullTextXML`, text, 'application/xml') };
}
export function scholarlyFixtureCollector(papers: readonly ScholarlyPaper[]): Pick<ScholarlyCollector, 'crossref'> {
    const records = new Map<string, CrossrefRecord>();
    for (const paper of papers) {
        records.set(paper.doi, scholarlyFixtureCrossref(paper.doi, { title: paper.title, year: paper.year, authors: paper.authors,
            references: paper.references.map(reference => reference.doi) }));
        for (const reference of paper.references) if (!records.has(reference.doi)) records.set(reference.doi,
            scholarlyFixtureCrossref(reference.doi, reference.title ? { title: reference.title } : {}));
    }
    return { async crossref(doi, signal) { signal.throwIfAborted(); const record = records.get(doi);
        if (!record) throw new Error('Synthetic registry record missing'); return record; } };
}
