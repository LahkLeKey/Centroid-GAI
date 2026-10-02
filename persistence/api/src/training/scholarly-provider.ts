import { createHash } from 'node:crypto';
import { mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { abortable, fetchPublic, publicUrl, type PublicFetchOptions, type PublicPage } from '../chat/public-fetch.ts';
import type { CrossrefRecord, ScholarlyCollector, ScholarlyPaper, ScholarlySnapshot } from './scholarly-types.ts';
import { normalizeScholarlyDoi, parseScholarlyXml, scholarlyAuthorsAgree } from './scholarly-xml.ts';

const hash = (value: string) => createHash('sha256').update(value).digest('hex');
const object = (value: unknown): value is Record<string, unknown> => !!value && typeof value === 'object' && !Array.isArray(value);
const string = (value: unknown, maximum = 4096): value is string => typeof value === 'string' && !!value.trim() && !value.includes('\0') && value.length <= maximum;
const normalized = (value: string) => value.normalize('NFC').replace(/\s+/gu, ' ').trim();
const doiPattern = /^10\.\d{4,9}\/[^\s<>#?]{1,230}$/;
const jsonType = /^application\/json(?:;|$)/i;
const xmlType = /^(?:application\/xml|text\/xml)(?:;|$)/i;
const validCursor = (value: unknown): value is string => typeof value === 'string' && /^[A-Za-z0-9+/_=-]{1,4096}$/.test(value);

/** Provider endpoints are fixed: fetched content cannot choose an acquisition destination. */
export function officialScholarlyUrl(value: string | URL): URL {
    const url = publicUrl(typeof value === 'string' ? value : value.href);
    if (url.protocol !== 'https:' || url.port || url.hash) throw new Error('scholarly endpoint must use official HTTPS');
    if (url.origin === 'https://www.ebi.ac.uk') {
        const base = '/europepmc/webservices/rest/';
        if (url.pathname === `${base}search`) {
            if ([...url.searchParams.keys()].some(key => !['query', 'format', 'resultType', 'pageSize', 'cursorMark'].includes(key)) ||
                url.searchParams.get('format') !== 'json' || url.searchParams.get('resultType') !== 'core' || !url.searchParams.get('query'))
                throw new Error('unsupported Europe PMC search endpoint');
        } else if (!new RegExp(`^${base}PMC[1-9][0-9]*/fullTextXML$`).test(url.pathname) || url.search)
            throw new Error('unsupported Europe PMC endpoint');
        return url;
    }
    if (url.origin === 'https://api.crossref.org' && !url.search && url.pathname.startsWith('/works/')) {
        let doi: string;
        try { doi = decodeURIComponent(url.pathname.slice('/works/'.length)); } catch { throw new Error('invalid encoded DOI endpoint'); }
        if (doiPattern.test(doi)) return url;
    }
    throw new Error('scholarly destination is not an official supported endpoint');
}

function validSnapshot(snapshot: ScholarlySnapshot): void {
    officialScholarlyUrl(snapshot.url);
    if (!string(snapshot.text, 524288) || Buffer.byteLength(snapshot.text) > 524288 || hash(snapshot.text) !== snapshot.sha256 ||
        !string(snapshot.contentType, 200) || !Number.isFinite(Date.parse(snapshot.fetchedAt)) || new Date(snapshot.fetchedAt).toISOString() !== snapshot.fetchedAt)
        throw new Error('invalid scholarly source snapshot');
}
export interface EuropePmcRecord {
    readonly id: string;
    readonly doi: string;
    readonly title: string;
    readonly authors: readonly string[];
    readonly year: number;
    readonly publicationTypes: readonly string[];
}
interface EuropePmcPage { records: unknown[]; nextCursorMark?: string }
function europePage(snapshot: ScholarlySnapshot): EuropePmcPage {
    validSnapshot(snapshot);
    if (!jsonType.test(snapshot.contentType) || !snapshot.url.startsWith('https://www.ebi.ac.uk/europepmc/webservices/rest/search?')) throw new Error('invalid Europe PMC metadata snapshot');
    const parsed: unknown = JSON.parse(snapshot.text);
    if (!object(parsed) || !object(parsed.resultList) || !Array.isArray(parsed.resultList.result) || parsed.resultList.result.length > 100)
        throw new Error('invalid bounded Europe PMC search response');
    if (parsed.nextCursorMark !== undefined && !validCursor(parsed.nextCursorMark)) throw new Error('invalid Europe PMC next cursor');
    return { records: parsed.resultList.result, ...(parsed.nextCursorMark ? { nextCursorMark: parsed.nextCursorMark as string } : {}) };
}
export function parseEuropePmcRecord(snapshot: ScholarlySnapshot, id: string): EuropePmcRecord {
    if (!/^PMC[1-9][0-9]*$/.test(id)) throw new Error('invalid Europe PMC paper identity');
    const entries = europePage(snapshot).records.filter(entry => object(entry) && entry.pmcid === id);
    if (entries.length !== 1 || !object(entries[0])) throw new Error('Europe PMC paper identity missing or duplicated');
    const record = entries[0];
    const doi = string(record.doi, 256) ? normalizeScholarlyDoi(record.doi) : '';
    if (!doiPattern.test(doi) || !string(record.title) || record.isOpenAccess !== 'Y' || record.inPMC !== 'Y') throw new Error('Europe PMC record lacks an open-access bibliography');
    const year = Number(record.pubYear);
    const types = object(record.pubTypeList) && Array.isArray(record.pubTypeList.pubType) ? record.pubTypeList.pubType : [];
    if (record.isRetracted === 'Y' || record.isRetracted === true ||
        [record.status, record.publicationStatus].some(value => typeof value === 'string' && /retract|withdraw|concern/i.test(value)) ||
        types.some(value => typeof value === 'string' && /retract|withdraw|concern|correction|erratum/i.test(value)))
        throw new Error('Europe PMC metadata reports a publication notice or retraction');
    const list = object(record.authorList) && Array.isArray(record.authorList.author) ? record.authorList.author : [];
    const authors = list.flatMap(author => {
        if (!object(author)) return [];
        const name = [author.firstName, author.lastName].filter(value => string(value)).join(' ') || (string(author.fullName) ? author.fullName : '') || (string(author.collectiveName) ? author.collectiveName : '');
        return name ? [normalized(name)] : [];
    });
    if (!authors.length && string(record.authorString)) authors.push(...record.authorString.split(',').map(normalized).filter(Boolean));
    if (!authors.length || !Number.isInteger(year) || year < 1600 || year > 2200 || !types.length || types.some(value => !string(value, 200)))
        throw new Error('Europe PMC bibliography is incomplete');
    return { id, doi, title: normalized(record.title), authors, year, publicationTypes: types as string[] };
}

/** Projection is rederivable from raw registered metadata; notices conservatively stop admission. */
export function parseCrossrefRecord(snapshot: ScholarlySnapshot): CrossrefRecord {
    validSnapshot(snapshot);
    if (!jsonType.test(snapshot.contentType) || new URL(snapshot.url).origin !== 'https://api.crossref.org') throw new Error('invalid Crossref metadata snapshot');
    const parsed: unknown = JSON.parse(snapshot.text);
    if (!object(parsed) || parsed.status !== 'ok' || !object(parsed.message)) throw new Error('invalid Crossref work response');
    const record = parsed.message;
    const doi = string(record.DOI, 256) ? normalizeScholarlyDoi(record.DOI) : '';
    const requested = normalizeScholarlyDoi(decodeURIComponent(new URL(snapshot.url).pathname.slice('/works/'.length)));
    const titles = Array.isArray(record.title) ? record.title : [];
    const dates = [record['published-print'], record['published-online'], record.published, record.issued].filter(object);
    const years = dates.flatMap(date => Array.isArray(date['date-parts']) && Array.isArray(date['date-parts'][0]) ? [Number(date['date-parts'][0][0])] : []);
    const year = years.find(value => Number.isInteger(value) && value >= 1600 && value <= 2200);
    const authors = (Array.isArray(record.author) ? record.author : []).flatMap(author => {
        if (!object(author)) return [];
        const name = [author.given, author.family].filter(value => string(value)).join(' ') || (string(author.name) ? author.name : '');
        return name ? [normalized(name)] : [];
    });
    if (!doiPattern.test(doi) || doi !== requested || !string(titles[0]) || year === undefined || !authors.length || !string(record.type, 200))
        throw new Error('Crossref bibliography is incomplete or mismatches the requested DOI');
    const updates = record['update-to'];
    if ((updates !== undefined && !Array.isArray(updates)) || (record.relation !== undefined && !object(record.relation)))
        throw new Error('malformed Crossref publication status metadata');
    const relations = object(record.relation) ? record.relation : {};
    const retractionSignal = (Array.isArray(updates) && updates.length > 0) ||
        Object.entries(relations).some(([name, entries]) => /retract|withdraw|concern|correct|update/i.test(name) &&
            entries !== undefined && entries !== null && (!Array.isArray(entries) || entries.length > 0)) ||
        /^(?:retraction|withdrawal|correction|expression of concern|retracted)\b/i.test(normalized(titles[0])) ||
        /retract|withdraw|correction|erratum|editorial/i.test(record.type);
    const referenceDois = [...new Set((Array.isArray(record.reference) ? record.reference : []).flatMap(reference => {
        const referenceDoi = object(reference) && string(reference.DOI, 256) ? normalizeScholarlyDoi(reference.DOI) : '';
        return doiPattern.test(referenceDoi) ? [referenceDoi] : [];
    }))];
    return { doi, title: normalized(titles[0]), year, authors, type: record.type, retractionSignal, referenceDois, snapshot };
}

export interface ScholarlyApiCollectorOptions {
    readonly fetchPage?: (url: string, signal: AbortSignal, maximum: number, options: PublicFetchOptions) => Promise<PublicPage>;
    readonly cacheDirectory?: string;
    readonly cacheTtlMs?: number;
    readonly maxRequests?: number;
    readonly delayMs?: number;
    readonly clock?: () => number;
    readonly pause?: (milliseconds: number, signal: AbortSignal) => Promise<void>;
}

/** Serial public API acquisition with per-run request limits and immutable disk snapshot objects. */
export class ScholarlyApiCollector implements ScholarlyCollector {
    private readonly options: ScholarlyApiCollectorOptions;
    private readonly cache = new Map<string, ScholarlySnapshot>();
    private readonly clock: () => number;
    private readonly ttl: number;
    private readonly pace: number;
    private readonly limit: number;
    private requests = 0;
    private lastRequest: number | undefined;
    private queue: Promise<void> = Promise.resolve();
    constructor(options: ScholarlyApiCollectorOptions = {}) {
        this.options = options; this.clock = options.clock ?? Date.now;
        this.ttl = options.cacheTtlMs ?? 24 * 60 * 60 * 1000;
        this.pace = options.delayMs ?? 1000; this.limit = options.maxRequests ?? 100;
        if (!Number.isSafeInteger(this.ttl) || this.ttl < 1 || this.ttl > 24 * 60 * 60 * 1000 ||
            !Number.isSafeInteger(this.pace) || this.pace < 0 || this.pace > 60000 ||
            !Number.isSafeInteger(this.limit) || this.limit < 1 || this.limit > 1000) throw new Error('invalid scholarly acquisition limits');
        if (this.pace < 1000 && !options.fetchPage) throw new Error('production scholarly requests require at least one second pacing');
    }
    private current(snapshot: ScholarlySnapshot): boolean {
        const age = this.clock() - Date.parse(snapshot.fetchedAt);
        return age >= 0 && age < this.ttl;
    }
    private async cached(url: string): Promise<ScholarlySnapshot | undefined> {
        const memory = this.cache.get(url);
        if (memory && this.current(memory)) return memory;
        if (!this.options.cacheDirectory) return undefined;
        try {
            const pointer: unknown = JSON.parse(await readFile(join(this.options.cacheDirectory, 'urls', `${hash(url)}.json`), 'utf8'));
            if (!object(pointer) || !string(pointer.object, 64) || !/^[a-f0-9]{64}$/.test(pointer.object)) return undefined;
            const snapshot = JSON.parse(await readFile(join(this.options.cacheDirectory, 'objects', `${pointer.object}.json`), 'utf8')) as ScholarlySnapshot;
            validSnapshot(snapshot);
            if (snapshot.url !== url || !this.current(snapshot)) return undefined;
            this.cache.set(url, snapshot); return snapshot;
        } catch { return undefined; }
    }
    private async discoveryCursor(key: string): Promise<string | undefined> {
        if (!this.options.cacheDirectory) return undefined;
        try {
            const path = join(this.options.cacheDirectory, 'discovery', `${key}.json`);
            if ((await stat(path)).size > 8192) return undefined;
            const progress: unknown = JSON.parse(await readFile(path, 'utf8'));
            if (!object(progress) || Object.keys(progress).length !== 2 || progress.version !== 1 ||
                !Object.hasOwn(progress, 'cursor') || (progress.cursor !== null && !validCursor(progress.cursor))) return undefined;
            return progress.cursor === null ? undefined : progress.cursor;
        } catch { return undefined; }
    }
    private async saveDiscoveryCursor(key: string, cursor: string | undefined): Promise<void> {
        if (!this.options.cacheDirectory) return;
        if (cursor !== undefined && !validCursor(cursor)) throw new Error('invalid persisted Europe PMC cursor');
        const directory = join(this.options.cacheDirectory, 'discovery');
        await mkdir(directory, { recursive: true });
        await writeFile(join(directory, `${key}.json`), JSON.stringify({ version: 1, cursor: cursor ?? null }));
    }
    private async snapshot(value: string, signal: AbortSignal, requestCeiling = this.limit): Promise<ScholarlySnapshot> {
        const url = officialScholarlyUrl(value).href;
        signal.throwIfAborted();
        const cached = await this.cached(url);
        signal.throwIfAborted();
        if (cached) return structuredClone(cached);
        // Even separately requested reference lookups share one paced acquisition lane.
        const previous = this.queue;
        let release = () => {};
        this.queue = new Promise<void>(resolve => { release = resolve; });
        let entered = false;
        try {
            await abortable(previous, signal);
            entered = true;
            signal.throwIfAborted();
            const reused = await this.cached(url);
            if (reused) return structuredClone(reused);
            if (this.requests >= requestCeiling) throw new Error(requestCeiling < this.limit ?
                'scholarly discovery request budget exhausted; remaining requests reserved for verification' : 'scholarly API request budget exhausted');
            const remaining = this.lastRequest === undefined ? 0 : Math.max(0, this.pace - (this.clock() - this.lastRequest));
            if (remaining) await abortable(this.options.pause ? this.options.pause(remaining, signal) : delay(remaining, undefined, { signal }), signal);
            signal.throwIfAborted();
            this.requests++; this.lastRequest = this.clock();
            const options: PublicFetchOptions = { allowXml: true, validateUrl: officialScholarlyUrl };
            const page = await abortable(this.options.fetchPage ? this.options.fetchPage(url, signal, 524288, options) : fetchPublic(url, signal, 524288, {}, options), signal);
            signal.throwIfAborted();
            if (officialScholarlyUrl(page.url).href !== url) throw new Error('scholarly response changed the requested endpoint');
            const snapshot: ScholarlySnapshot = { url, text: page.text, contentType: page.contentType, sha256: hash(page.text), fetchedAt: new Date(this.clock()).toISOString() };
            validSnapshot(snapshot);
            if (this.options.cacheDirectory) {
                const objects = join(this.options.cacheDirectory, 'objects'), urls = join(this.options.cacheDirectory, 'urls');
                await mkdir(objects, { recursive: true }); await mkdir(urls, { recursive: true });
                const identity = hash(JSON.stringify(snapshot));
                try { await writeFile(join(objects, `${identity}.json`), JSON.stringify(snapshot), { flag: 'wx' }); }
                catch (error) { if (!object(error) || error.code !== 'EEXIST') throw error; }
                await writeFile(join(urls, `${hash(url)}.json`), JSON.stringify({ object: identity }));
            }
            this.cache.set(url, snapshot); return structuredClone(snapshot);
        } finally {
            if (entered) release();
            else void previous.then(release, release);
        }
    }
    async collect(queries: readonly string[], maximum: number, signal: AbortSignal): Promise<{ papers: readonly ScholarlyPaper[]; excluded: readonly { id: string; reason: string }[] }> {
        if (!Array.isArray(queries) || !queries.length || queries.length > 20 || queries.some(query => !string(query, 512)) ||
            !Number.isSafeInteger(maximum) || maximum < 1 || maximum > 100) throw new Error('invalid bounded scholarly collection request');
        const papers: ScholarlyPaper[] = [], excluded: { id: string; reason: string }[] = [];
        const seen = new Set<string>(), dois = new Set<string>();
        // One own-work and two cited-work DOI checks per acquired paper. Small injected
        // budgets still leave at least half the allowance available for discovery.
        const discoveryCeiling = this.limit - Math.min(maximum * 3, Math.floor(this.limit / 2));
        const searches = [];
        for (const query of queries) {
            const fullQuery = `(${query}) AND OPEN_ACCESS:Y AND HAS_FT:Y AND IN_PMC:Y`;
            const key = hash(JSON.stringify({ query: fullQuery, pageSize: maximum }));
            const cursor = await this.discoveryCursor(key);
            signal.throwIfAborted();
            searches.push({ query, fullQuery, key, cursor, visited: false, finished: false,
                cursors: new Set<string>(cursor ? [cursor] : []) });
        }
        let exhausted = false;
        // Advance each query by one page per round so a broad first topic cannot starve the others.
        while (papers.length < maximum && !exhausted && searches.some(search => !search.finished)) for (const search of searches) {
            if (search.finished || papers.length >= maximum || exhausted) continue;
            signal.throwIfAborted();
            const url = new URL('https://www.ebi.ac.uk/europepmc/webservices/rest/search');
            url.searchParams.set('query', search.fullQuery);
            url.searchParams.set('format', 'json'); url.searchParams.set('resultType', 'core'); url.searchParams.set('pageSize', String(maximum));
            if (search.cursor) url.searchParams.set('cursorMark', search.cursor);
            let metadata: ScholarlySnapshot;
            try { metadata = await this.snapshot(url.href, signal, discoveryCeiling); }
            catch (error) {
                signal.throwIfAborted();
                if (this.requests >= discoveryCeiling) {
                    excluded.push({ id: `query:${search.query}`, reason: error instanceof Error ? error.message : 'scholarly discovery budget exhausted' });
                    exhausted = true; break;
                }
                throw error;
            }
            const page = europePage(metadata);
            search.visited = true;
            if (!page.records.length || !page.nextCursorMark || page.nextCursorMark === search.cursor || search.cursors.has(page.nextCursorMark)) search.finished = true;
            else { search.cursors.add(page.nextCursorMark); search.cursor = page.nextCursorMark; }
            for (const candidate of page.records) {
                if (papers.length >= maximum) break;
                if (!object(candidate) || !string(candidate.pmcid, 64) || !/^PMC[1-9][0-9]*$/.test(candidate.pmcid)) continue;
                const id = candidate.pmcid;
                if (seen.has(id)) continue;
                seen.add(id);
                try {
                    const record = parseEuropePmcRecord(metadata, id);
                    if (dois.has(record.doi)) throw new Error('duplicate canonical scholarly DOI');
                    const fullText = await this.snapshot(`https://www.ebi.ac.uk/europepmc/webservices/rest/${id}/fullTextXML`, signal, discoveryCeiling);
                    if (!xmlType.test(fullText.contentType)) throw new Error('scholarly full text is not XML');
                    const parsed = parseScholarlyXml(fullText.text);
                    const comparable = (value: string) => value.normalize('NFKC').toLowerCase().replace(/[^\p{L}\p{N}]+/gu, '');
                    const authorsMatch = scholarlyAuthorsAgree(parsed.authors, record.authors);
                    if (parsed.doi !== record.doi || comparable(parsed.title) !== comparable(record.title) || parsed.year !== record.year ||
                        !authorsMatch || (parsed.id && parsed.id.replace(/^PMC/i, '') !== id.slice(3))) throw new Error('Europe PMC bibliography disagrees with full text');
                    papers.push({ id, doi: parsed.doi, title: parsed.title, authors: parsed.authors, year: parsed.year,
                        licenseUrl: parsed.licenseUrl, publicationTypes: record.publicationTypes, text: parsed.text,
                        references: parsed.references, metadata, fullText });
                    dois.add(record.doi);
                } catch (error) {
                    signal.throwIfAborted(); excluded.push({ id, reason: error instanceof Error ? error.message : 'scholarly paper acquisition failed' });
                    if (error instanceof Error && /request budget exhausted/.test(error.message)) { exhausted = true; break; }
                }
            }
        }
        // Persist only a successful bounded collection. No fetched URL or source-supplied
        // instructions enter progress state; the next request is constructed above.
        for (const search of searches) if (search.visited) {
            signal.throwIfAborted();
            await this.saveDiscoveryCursor(search.key, search.finished ? undefined : search.cursor);
        }
        return { papers, excluded };
    }
    async crossref(value: string, signal: AbortSignal): Promise<CrossrefRecord> {
        if (!string(value, 256)) throw new Error('invalid scholarly DOI');
        const doi = normalizeScholarlyDoi(value);
        if (!doiPattern.test(doi)) throw new Error('invalid scholarly DOI');
        return parseCrossrefRecord(await this.snapshot(`https://api.crossref.org/works/${encodeURIComponent(doi)}`, signal));
    }
}
