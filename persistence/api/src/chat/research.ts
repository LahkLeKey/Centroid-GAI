import { createHash } from 'node:crypto';
import type { ChatSendRequest, ChatSource } from '../../../shared/chat.ts';
import type { ResearchAnswer, ResearchTrace } from '../../../shared/research.ts';
import { loadRepositoryIndex, retrievalTerms } from '../knowledge/retrieval.ts';
import { MemoryService } from './memory.ts';
import { abortable, fetchPublic, publicUrl, readableText, type PublicPage } from './public-fetch.ts';
import { chatText } from './service.ts';
import { evidenceUnits } from './evidence.ts';

export interface SearchResult { url: string; title: string }
export interface SearchProvider { name: string; search(query: string, signal: AbortSignal): Promise<readonly SearchResult[]> }
/** SearXNG's JSON format must be enabled by the deployment. No transcript is sent. */
export class SearxProvider implements SearchProvider {
    readonly name = 'searxng';
    private readonly endpoint: string;
    constructor(endpoint: string) { publicUrl(endpoint); this.endpoint = endpoint; }
    async search(query: string, signal: AbortSignal): Promise<readonly SearchResult[]> {
        const url = new URL(this.endpoint);
        url.searchParams.set('q', query); url.searchParams.set('format', 'json');
        const page = await fetchPublic(url.href, signal);
        const value = JSON.parse(page.text) as { results?: unknown };
        if (!Array.isArray(value.results)) throw new Error('invalid search provider response');
        return value.results.slice(0, 20).flatMap((entry: unknown) => {
            if (!entry || typeof entry !== 'object' || !('url' in entry) || typeof entry.url !== 'string') return [];
            try { return [{ url: publicUrl(entry.url).href,
                title: 'title' in entry && typeof entry.title === 'string' ? entry.title.slice(0, 500) : entry.url }]; }
            catch { return []; }
        });
    }
}
const hash = (text: string) => createHash('sha256').update(text).digest('hex');
export const queryKey = (text: string) => text.normalize('NFC').toLowerCase().replace(/\s+/g, ' ').trim();
/** Decline obvious credentials rather than silently editing them into a different query. */
function hasCredentials(text: string): boolean {
    return /\b(?:password|passwd|pwd|secret|(?:api[_ -]?)?key|(?:access[_ -]?|auth[_ -]?)?token)\s*(?:[:=]|\bis\b)\s*\S+/i.test(text) ||
        /\bauthorization\s*:\s*\S+|\bbearer\s+[A-Za-z0-9._~+/-]+=*/i.test(text) ||
        /-----BEGIN (?:[A-Z ]+ )?PRIVATE KEY-----|\b(?:sk-[A-Za-z0-9_-]{16,}|gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|AKIA[A-Z0-9]{16})\b/.test(text) ||
        /\beyJ[A-Za-z0-9_-]+\.eyJ[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\b|\b[a-z][a-z0-9+.-]*:\/\/[^\s/@]+:[^\s/@]+@/i.test(text);
}
export function relevantExcerpt(text: string, query: string): string | null {
    const terms = [...new Set(retrievalTerms(query))];
    if (!terms.length) return null;
    let best: string | null = null, score = 0;
    const units = evidenceUnits(text);
    // Keep complete source units. Cutting at character 1600 can remove a qualifier or negation.
    for (let index = 0; index < units.length; index++) {
        const unit = units[index]!;
        if (unit.text.length > 1600) continue;
        let start = unit.start, end = unit.end;
        const previous = units[index - 1], next = units[index + 1];
        if (previous && end - previous.start <= 1600) start = previous.start;
        if (next && next.end - start <= 1600) end = next.end;
        const excerpt = text.slice(start, end);
        const words = new Set(retrievalTerms(excerpt));
        const matched = terms.filter((term) => words.has(term)).length;
        if (matched > score && matched >= Math.ceil(terms.length / 2)) { best = excerpt; score = matched; }
    }
    return best;
}

/** Explicit support routing. Source excerpts remain source results, never synthesized claims. */
export class ResearchService {
    private readonly local: ReturnType<typeof loadRepositoryIndex> | undefined;
    readonly memory: MemoryService;
    private readonly provider: SearchProvider | undefined;
    private readonly fetchPage: typeof fetchPublic;
    private readonly dailyQueries: number;
    private readonly clock: () => number;
    constructor(memory: MemoryService, provider?: SearchProvider,
        snapshot?: string, fetchPage: typeof fetchPublic = fetchPublic,
        dailyQueries = 100, clock = Date.now) {
        this.memory = memory; this.provider = provider; this.fetchPage = fetchPage; this.dailyQueries = dailyQueries; this.clock = clock;
        if (snapshot) this.local = loadRepositoryIndex(snapshot);
    }
    async answer(input: ChatSendRequest, conversationId: string, signal: AbortSignal): Promise<ResearchAnswer> {
        signal.throwIfAborted();
        const start = this.clock();
        const trace: ResearchTrace = { status: 'none', reason: '', provider: this.provider?.name ?? null,
            queries: 0, fetched: 0, elapsedMs: 0, mode: 'abstained' };
        const key = queryKey(input.content), applicability = input.applicability ?? '';
        const finish = (content: string, sources: readonly ChatSource[], research: Partial<ResearchTrace>, memoryIds: readonly string[] = []): ResearchAnswer => {
            signal.throwIfAborted();
            return { content, sources, memoryIds, research: { ...trace, ...research, elapsedMs: this.clock() - start } };
        };
        const explicit = input.publicQuery !== undefined;
        if (!explicit && !retrievalTerms(input.content).length)
            return finish('Please specify the topic, version, or source you want me to check.', [], { mode: 'clarification', reason: 'ambiguous question' });
        const fresh = explicit ? [] : await this.memory.retrieve(key, applicability);
        if (fresh.length) return finish(this.render(fresh.flatMap((entry) => entry.sources), true), fresh.flatMap((entry) => entry.sources),
            { status: 'memory', mode: 'memory', reason: 'fresh exact-query and applicability match; cached excerpts' }, fresh.map((entry) => entry.id));
        if (!explicit) {
            const document = await this.memory.read();
            const terms = [...new Set(retrievalTerms(input.content))];
            const asksMemory = /\b(remember about me|my preferences|did i (say|tell)|do you remember)\b/i.test(input.content);
            const statements = document.enabled ? document.records.filter((record) => {
                if (record.kind !== 'preference' || record.state !== 'user-stated') return false;
                const words = new Set(retrievalTerms(record.content));
                return asksMemory || terms.filter((term) => words.has(term)).length >= Math.max(2, Math.ceil(terms.length / 2));
            }).slice(0, 5) : [];
            if (statements.length) return finish('You previously stated (not independently verified):\n' +
                statements.map((record) => `- ${record.content}`).join('\n'), [],
                { status: 'memory', mode: 'preference', reason: 'eligible owner-attributed statements' }, statements.map((record) => record.id));
        }
        const sources = explicit ? [] : this.localSources(input.content);
        if (sources.length)
            return finish(this.render(sources), sources, { mode: 'sources', reason: 'repository excerpts; support must be assessed against the question' });
        if (!explicit && input.autoSearch === false)
            return finish('I do not have supporting source material for this question. Automatic research is disabled for this message.', [],
                { status: 'none', reason: 'no eligible evidence; autoSearch is false' });
        if (!explicit && (Buffer.byteLength(input.content) > 512 || hasCredentials(input.content)))
            return finish('Supply a specific publicQuery of at most 512 UTF-8 bytes containing only text you want to search publicly.', [],
                { mode: 'clarification', reason: 'automatic query withheld: message is too long or contains possible credentials' });
        const query = explicit ? input.publicQuery! : input.content.normalize('NFC').replace(/\s+/g, ' ').trim();
        chatText(query, 'publicQuery', 512);
        const route = explicit ? 'explicit public query' : 'automatic research for unsupported current message';
        if (!this.provider) return finish('Public research is disabled; no search was performed.', [],
            { status: 'disabled', reason: `${route}; no provider configured` });
        const day = new Date(this.clock()).toISOString().slice(0, 10);
        if (!await this.memory.reserveQuery(day, this.dailyQueries, signal)) return finish('The research quota is exhausted; no search was performed.', [],
            { status: 'unavailable', reason: `${route}; daily query quota exhausted` });
        const bounded = AbortSignal.any([signal, AbortSignal.timeout(15000)]);
        let fetched = 0;
        try {
            bounded.throwIfAborted();
            const hits = await abortable(this.provider.search(query, bounded), bounded);
            bounded.throwIfAborted();
            const seen = new Set<string>();
            const urls = new Set<string>();
            for (const hit of hits.slice(0, 5)) {
                bounded.throwIfAborted();
                try {
                    const url = publicUrl(hit.url).href;
                    if (urls.has(url)) continue;
                    urls.add(url);
                    const page: PublicPage = await abortable(this.fetchPage(url, bounded), bounded);
                    bounded.throwIfAborted();
                    ++fetched;
                    const text = readableText(page), contentHash = hash(text);
                    if (seen.has(contentHash)) continue;
                    seen.add(contentHash);
                    const excerpt = relevantExcerpt(text, query);
                    if (excerpt) sources.push({ id: contentHash, path: url, url: publicUrl(page.url).href, title: hit.title,
                        excerpt, contentHash, fetchedAt: new Date(this.clock()).toISOString() });
                } catch { bounded.throwIfAborted(); }
            }
            let memoryIds: string[] = [];
            let memoryReason = '';
            if (input.rememberSources && sources.length) {
                try {
                    bounded.throwIfAborted();
                    memoryIds = (await this.memory.sources(key, applicability, sources, conversationId, bounded, query)).map((entry) => entry.id);
                } catch {
                    bounded.throwIfAborted();
                    memoryReason = '; source excerpts could not be saved to memory';
                }
            }
            return finish(sources.length ? this.render(sources) : 'No fetched passage provided relevant evidence. I cannot support an answer.', sources,
                { status: 'searched', queries: 1, fetched, mode: sources.length ? 'sources' : 'abstained', reason: `${route}; bounded public research; snippets excluded from evidence${memoryReason}` }, memoryIds);
        } catch (error) {
            signal.throwIfAborted();
            return finish('Research failed or timed out; I cannot support an answer.', [],
                { status: 'failed', queries: 1, fetched, reason: `${route}; ${error instanceof Error ? error.message : 'provider failure'}` });
        }
    }
    private localSources(question: string): ChatSource[] {
        if (!this.local) return [];
        return this.local.retriever.search(question, 3).hits.map((hit) => {
            const citation = hit.citations[0]!;
            return { id: hit.id, path: citation.path, excerpt: hit.text, commit: citation.commit,
                startLine: citation.startLine, endLine: citation.endLine, contentHash: hit.textSha256,
                fetchedAt: new Date(this.clock()).toISOString() };
        });
    }
    private render(sources: readonly ChatSource[], cached = false): string {
        return `${cached ? 'Cached source excerpts' : 'Source excerpts'}; these passages may need further verification:\n\n` +
            sources.map((source, index) => `[${index + 1}] ${source.path}${source.commit ? `@${source.commit}` : ''}\n${source.excerpt}`).join('\n\n');
    }
}
