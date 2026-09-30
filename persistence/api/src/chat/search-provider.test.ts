import assert from 'node:assert/strict';
import test from 'node:test';
import { configuredResearch, WikipediaProvider } from './search-provider.ts';
import type { PublicPage } from './public-fetch.ts';

function response(value: unknown): PublicPage {
    return { url: 'https://en.wikipedia.org/w/api.php', contentType: 'application/json', text: JSON.stringify(value) };
}

test('Wikipedia searches bounded titles then fetches article extracts with canonical provenance', async () => {
    const requests: URL[] = [];
    const signal = new AbortController().signal;
    const provider = new WikipediaProvider(async (value, receivedSignal) => {
        assert.equal(receivedSignal, signal);
        const url = new URL(value); requests.push(url);
        assert.equal(url.origin, 'https://en.wikipedia.org');
        assert.equal(url.searchParams.get('action'), 'query');
        assert.equal(url.searchParams.get('formatversion'), '2');
        if (url.searchParams.get('list') === 'search') {
            assert.equal(url.searchParams.get('srsearch'), 'hello world & examples');
            assert.equal(url.searchParams.get('srlimit'), '5');
            assert.equal(url.searchParams.get('srprop'), '');
            return response({ query: { search: [{ title: 'Hello, world', snippet: 'untrusted snippet' }, null, { title: 12 }] } });
        }
        assert.equal(url.searchParams.get('prop'), 'extracts');
        assert.equal(url.searchParams.get('titles'), 'Hello, world');
        assert.equal(url.searchParams.get('exchars'), '1200');
        assert.equal(url.searchParams.get('explaintext'), '1');
        return response({ query: { pages: [{ title: 'Hello, World program', extract: 'Hello world is a short program.' }] } });
    });
    const hits = await provider.search('hello world & examples', signal);
    assert.deepEqual(hits, [{ url: 'https://en.wikipedia.org/wiki/Hello%2C_world', title: 'Hello, world' }]);
    assert.equal(requests.length, 1);
    const page = await provider.fetchPage(hits[0]!.url, signal);
    assert.deepEqual(page, { url: 'https://en.wikipedia.org/wiki/Hello%2C_World_program',
        contentType: 'text/plain', text: 'Hello world is a short program.' });
    assert.equal(requests.length, 2);
});

test('Wikipedia rejects invalid results, missing articles and foreign destinations', async () => {
    const signal = new AbortController().signal;
    let calls = 0;
    const provider = new WikipediaProvider(async () => { calls++; return response({ error: { info: 'unavailable' } }); });
    for (const url of ['https://example.com/wiki/Hello', 'http://en.wikipedia.org/wiki/Hello',
        'https://en.wikipedia.org/w/api.php', 'https://en.wikipedia.org/wiki/Hello?q=secret', 'https://en.wikipedia.org/wiki/'])
        await assert.rejects(provider.fetchPage(url, signal));
    assert.equal(calls, 0);
    await assert.rejects(provider.search('hello', signal), /invalid Wikipedia response/);
    await assert.rejects(provider.fetchPage('https://en.wikipedia.org/wiki/Hello', signal), /invalid Wikipedia response/);
    const missing = new WikipediaProvider(async () => response({ query: { pages: [{ missing: true, title: 'Hello' }] } }));
    await assert.rejects(missing.fetchPage('https://en.wikipedia.org/wiki/Hello', signal), /unavailable/);
});

test('research configuration defaults to Wikipedia, supports SearXNG and honors offline operation', () => {
    assert.equal(configuredResearch({}).provider?.name, 'wikipedia');
    assert.equal(configuredResearch({ CGAI_SEARCH_URL: 'https://example.com/search' }).provider?.name, 'searxng');
    assert.equal(configuredResearch({ CGAI_SEARCH_PROVIDER: 'disabled', CGAI_SEARCH_URL: 'https://example.com/search' }).provider, undefined);
    assert.equal(configuredResearch({ CGAI_SEARCH_PROVIDER: 'wikipedia', CGAI_SEARCH_URL: 'https://example.com/search' }).provider?.name, 'wikipedia');
    assert.throws(() => configuredResearch({ CGAI_SEARCH_PROVIDER: 'unknown' }), /CGAI_SEARCH_PROVIDER/);
    assert.throws(() => configuredResearch({ CGAI_SEARCH_PROVIDER: 'searxng' }), /CGAI_SEARCH_URL/);
    assert.throws(() => configuredResearch({ CGAI_SEARCH_URL: 'http://localhost/search' }), /public/);
});
