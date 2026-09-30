import { fetchPublic, publicUrl, type PublicPage } from './public-fetch.ts';
import { SearxProvider, type SearchProvider, type SearchResult } from './research.ts';

const wikiOrigin = 'https://en.wikipedia.org';
const articleUrl = (title: string) => `${wikiOrigin}/wiki/${encodeURIComponent(title.replace(/ /g, '_'))}`;
function apiUrl(parameters: Record<string, string>): string {
    const url = new URL('/w/api.php', wikiOrigin);
    url.search = new URLSearchParams({ action: 'query', format: 'json', formatversion: '2', ...parameters }).toString();
    return url.href;
}
function object(value: unknown): value is Record<string, unknown> {
    return value !== null && typeof value === 'object' && !Array.isArray(value);
}
function responseList(page: PublicPage, key: string): unknown[] {
    const value: unknown = JSON.parse(page.text);
    if (!object(value) || !object(value.query) || !Array.isArray(value.query[key]))
        throw new Error('invalid Wikipedia response');
    return value.query[key];
}

/** Keyless encyclopedia lookup; extracts are fetched separately from search results.
 * https://www.mediawiki.org/wiki/API:Search
 * https://www.mediawiki.org/wiki/Extension:TextExtracts#API
 */
export class WikipediaProvider implements SearchProvider {
    readonly name = 'wikipedia';
    private readonly request: typeof fetchPublic;
    constructor(request: typeof fetchPublic = fetchPublic) { this.request = request; }
    async search(query: string, signal: AbortSignal): Promise<readonly SearchResult[]> {
        const page = await this.request(apiUrl({ list: 'search', srsearch: query, srlimit: '5', srprop: '', srnamespace: '0' }), signal);
        return responseList(page, 'search').slice(0, 5).flatMap((entry) => {
            if (!object(entry) || typeof entry.title !== 'string' || !entry.title.trim() || entry.title.length > 500) return [];
            return [{ url: articleUrl(entry.title), title: entry.title }];
        });
    }
    /** Fetch bounded article text, retaining its canonical public page as the citation. */
    readonly fetchPage: typeof fetchPublic = async (value, signal) => {
        const url = publicUrl(value);
        if (url.origin !== wikiOrigin || !url.pathname.startsWith('/wiki/') || url.search)
            throw new Error('Wikipedia fetch requires an encyclopedia article URL');
        const title = decodeURIComponent(url.pathname.slice('/wiki/'.length)).replace(/_/g, ' ');
        if (!title.trim() || title.length > 500) throw new Error('invalid Wikipedia article title');
        const page = await this.request(apiUrl({ prop: 'extracts', titles: title, redirects: '1',
            explaintext: '1', exchars: '1200', exlimit: '1' }), signal);
        const article = responseList(page, 'pages')[0];
        if (!object(article) || article.missing || typeof article.title !== 'string' || !article.title.trim() ||
            article.title.length > 500 || typeof article.extract !== 'string' || !article.extract.trim())
            throw new Error('Wikipedia article text unavailable');
        return { url: articleUrl(article.title), contentType: 'text/plain', text: article.extract.slice(0, 65536) };
    };
}

/** Explicit configuration wins; an existing SearXNG URL retains its previous meaning. */
export function configuredResearch(environment: NodeJS.ProcessEnv = process.env): {
    provider: SearchProvider | undefined; fetchPage: typeof fetchPublic;
} {
    const selected = environment.CGAI_SEARCH_PROVIDER?.trim() || (environment.CGAI_SEARCH_URL ? 'searxng' : 'wikipedia');
    if (selected === 'disabled') return { provider: undefined, fetchPage: fetchPublic };
    if (selected === 'wikipedia') {
        const provider = new WikipediaProvider();
        return { provider, fetchPage: provider.fetchPage };
    }
    if (selected === 'searxng' && environment.CGAI_SEARCH_URL)
        return { provider: new SearxProvider(environment.CGAI_SEARCH_URL), fetchPage: fetchPublic };
    throw new Error('CGAI_SEARCH_PROVIDER must be wikipedia, disabled, or searxng with CGAI_SEARCH_URL');
}
