import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { EventEmitter } from 'node:events';
import { mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import type { ClientRequest, IncomingMessage, RequestOptions } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { PassThrough } from 'node:stream';
import test from 'node:test';
import { fetchPublic, type PublicFetchDependencies, type PublicFetchOptions, type PublicPage } from '../chat/public-fetch.ts';
import { officialScholarlyUrl, parseCrossrefRecord, parseEuropePmcRecord, ScholarlyApiCollector } from './scholarly-provider.ts';
import type { ScholarlySnapshot } from './scholarly-types.ts';
import { eligibleScholarlyLicense, parseScholarlyXml, scholarlyAuthorsAgree } from './scholarly-xml.ts';

const signal = () => new AbortController().signal;
const time = Date.UTC(2026, 9, 2, 12);
const doi = '10.1234/example.1';
const title = 'Verified bibliography & bounded acquisition';
const xml = (license = 'http://creativecommons.org/licenses/by/4.0/') => `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE article PUBLIC "-//NLM//DTD JATS//EN" "https://untrusted.invalid/article.dtd">
<article xmlns:xlink="http://www.w3.org/1999/xlink"><front><article-meta>
<article-id pub-id-type="pmc">123</article-id><article-id pub-id-type="doi">${doi}</article-id>
<title-group><article-title>Verified bibliography &amp; bounded acquisition</article-title></title-group>
<contrib-group><contrib contrib-type="author"><name><surname>Example</surname><given-names>Alice</given-names></name></contrib></contrib-group>
<pub-date pub-type="epub"><year>2024</year></pub-date>
<permissions><license xlink:href="${license}"><license-p>Creative Commons Attribution License.</license-p></license></permissions>
<abstract><p>Complete <italic>nested</italic> text &amp; Unicode &#x03b1;.</p></abstract>
</article-meta></front><body><sec><title>Methods</title><p><![CDATA[The measured value is < 2.]]></p>
<p>Keep qualifications and citation markers <xref ref-type="bibr" rid="R1">[1]</xref>.</p>
<script><p>Never extract executable instructions.</p></script></sec></body>
<back><ref-list><ref id="R1"><element-citation><article-title>Another work</article-title><pub-id pub-id-type="doi">10.1234/reference.1</pub-id></element-citation></ref></ref-list></back></article>`;
function search(): string {
    return JSON.stringify({ resultList: { result: [{ pmcid: 'PMC123', doi, title, pubYear: '2024', isOpenAccess: 'Y', inPMC: 'Y',
        authorList: { author: [{ firstName: 'Alice', lastName: 'Example' }] }, pubTypeList: { pubType: ['Journal Article'] } }] } });
}
function crossref(overrides: Record<string, unknown> = {}): string {
    return JSON.stringify({ status: 'ok', message: { DOI: doi, title: [title], author: [{ given: 'Alice', family: 'Example' }],
        published: { 'date-parts': [[2024, 1, 1]] }, type: 'journal-article', reference: [{ DOI: '10.1234/reference.1' }], ...overrides } });
}
const searchUrl = 'https://www.ebi.ac.uk/europepmc/webservices/rest/search?query=learning&format=json&resultType=core';
function snapshot(text: string, url: string, contentType = 'application/json'): ScholarlySnapshot {
    return { text, url, contentType, sha256: createHash('sha256').update(text).digest('hex'), fetchedAt: new Date(time).toISOString() };
}
function fixture() {
    const requests: { url: string; maximum: number; options: PublicFetchOptions }[] = [];
    return { requests, async fetchPage(url: string, _signal: AbortSignal, maximum: number, options: PublicFetchOptions): Promise<PublicPage> {
        requests.push({ url, maximum, options }); options.validateUrl?.(new URL(url));
        return { url, text: url.includes('/search?') ? search() : url.endsWith('/fullTextXML') ? xml() : crossref(),
            contentType: url.endsWith('/fullTextXML') ? 'application/xml; charset=utf-8' : 'application/json; charset=utf-8' };
    } };
}

test('JATS parser preserves complete paragraphs, entities and bibliography without loading the external DTD', () => {
    const parsed = parseScholarlyXml(xml());
    assert.deepEqual(parsed, { id: '123', doi, title, authors: ['Alice Example'], year: 2024,
        licenseUrl: 'https://creativecommons.org/licenses/by/4.0/',
        text: 'Complete nested text & Unicode α.\n\nThe measured value is < 2.\n\nKeep qualifications and citation markers [1].',
        references: [{ doi: '10.1234/reference.1', title: 'Another work' }] });
    assert.equal(eligibleScholarlyLicense('https://creativecommons.org/publicdomain/zero/1.0/'), 'https://creativecommons.org/publicdomain/zero/1.0/');
    for (const value of ['https://creativecommons.org/licenses/by-nc/4.0/', 'https://creativecommons.org/licenses/by-sa/4.0/',
        'https://creativecommons.org.evil.example/licenses/by/4.0/', 'https://creativecommons.org/licenses/by/4.0/?foo=1'])
        assert.equal(eligibleScholarlyLicense(value), null);
});

test('modern JATS author groups supply author identity without labeling editors as authors', () => {
    const grouped = xml().replace('<contrib-group>', '<contrib-group content-type="author">').replace(' contrib-type="author"', '')
        .replace('</contrib-group>', '<contrib contrib-type="editor"><name><surname>Editor</surname><given-names>Bob</given-names></name></contrib></contrib-group>');
    assert.deepEqual(parseScholarlyXml(grouped).authors, ['Alice Example']);
    assert.throws(() => parseScholarlyXml(grouped.replace('content-type="author"', 'content-type="editor"')), /bibliography/);
});

test('JATS mixed citations admit typed DOI links and exact resolver destinations, with per-reference deduplication', () => {
    const citations = `<ref id="R1"><mixed-citation><article-title>Another work</article-title>
        <pub-id pub-id-type="doi">10.1234/reference.1</pub-id>
        <ext-link ext-link-type="doi" xlink:href="10.1234/reference.1"/>
        <ext-link ext-link-type="doi" xlink:href="10.1234/reference.2"/>
        <ext-link ext-link-type="uri" xlink:href="https://doi.org/10.1234%2Freference.3"/>
        <ext-link ext-link-type="uri" xlink:href="https://dx.doi.org/10.1234/reference.4"/>
        <ext-link ext-link-type="doi">10.1234/reference.5</ext-link>
        <ext-link ext-link-type="doi" xlink:href="https://malicious.example/10.1234/reference.6"/>
        <ext-link ext-link-type="uri" xlink:href="https://doi.org.evil.example/10.1234/reference.7"/>
        <ext-link ext-link-type="google-scholar" xlink:href="10.1234/reference.8"/>
        <ext-link ext-link-type="uri" xlink:href="https://doi.org/10.1234/reference.9?redirect=private"/>
        </mixed-citation></ref>`;
    const input = xml().replace(/<ref id="R1">[\s\S]*?<\/ref>/, citations);
    assert.deepEqual(parseScholarlyXml(input).references, [1, 2, 3, 4, 5].map(number => ({ doi: `10.1234/reference.${number}`, title: 'Another work' })));
});

test('author agreement requires complete surname identity and compatible given names', () => {
    for (const [left, right] of [
        ['Alice Beth Jones', 'Jones AB'], ['Alice Beth Jones', 'A. B. Jones'], ['Nir A. Dayan', 'Nir ADayan'],
        ['Gaetan De Waele', 'De Waele G'], ['Vivien Erzsébet Resch', 'Vivien Erzse\u0301bet Resch'],
        ['JUNSEOB KIM', 'Junseob Kim'],
    ]) {
        assert.equal(scholarlyAuthorsAgree([left!], [right!]), true, `${left} / ${right}`);
        assert.equal(scholarlyAuthorsAgree([right!], [left!]), true, `${right} / ${left}`);
    }
    for (const [left, right] of [
        ['Alice Smith', 'Alice Jones'], ['Alice Jones', 'Andrew Jones'], ['Alice Beth Jones', 'Alice Jane Jones'],
        ['Gaetan De Waele', 'Gaetan Waele'], ['Alice Beth Jones', 'Jones AC'], ['A. Jones', 'B. Jones'],
    ]) assert.equal(scholarlyAuthorsAgree([left!], [right!]), false, `${left} / ${right}`);
    assert.equal(scholarlyAuthorsAgree(['Alice Jones', 'Bob Example'], ['Bob Example', 'Alice Jones']), false);
    assert.equal(scholarlyAuthorsAgree(['Alice Jones'], ['Alice Jones', 'Bob Example']), false);
    assert.equal(scholarlyAuthorsAgree([], []), false);
});

test('untrusted XML fails closed on declarations, malformed markup and unknown or restricted licenses', () => {
    for (const value of [
        xml().replace('<!DOCTYPE article PUBLIC', '<!ENTITY x SYSTEM "file:///secret"><!DOCTYPE article PUBLIC'),
        xml().replace('<!DOCTYPE article PUBLIC "-//NLM//DTD JATS//EN" "https://untrusted.invalid/article.dtd">', '<!DOCTYPE article [<!ELEMENT article ANY>]>'),
        xml().replace('</italic>', '</other>'), xml().replace('&amp;', '&unknown;'), xml().replace('&amp;', '&bad'),
        xml('https://creativecommons.org/licenses/by-nc/4.0/'), xml().replace('Creative Commons Attribution License.', 'Non-commercial use only.'),
        xml().replace('xlink:href="http://creativecommons.org/licenses/by/4.0/"', ''), xml() + '<article/>',
        '<article><front/></article>',
    ]) assert.throws(() => parseScholarlyXml(value));
});

test('official endpoints exclude arbitrary origins, credentials, ports, unsafe routes and request parameters', () => {
    assert.equal(officialScholarlyUrl(searchUrl).href, searchUrl);
    assert.equal(officialScholarlyUrl(`https://api.crossref.org/works/${encodeURIComponent(doi)}`).origin, 'https://api.crossref.org');
    for (const value of ['https://127.0.0.1/works/10.1234/foo', 'http://api.crossref.org/works/10.1234/foo',
        'https://api.crossref.org:8443/works/10.1234/foo', 'https://user:secret@api.crossref.org/works/10.1234/foo',
        'https://api.crossref.org/works/10.1234/foo?redirect=1', 'https://www.ebi.ac.uk/other/path',
        'https://www.ebi.ac.uk/europepmc/webservices/rest/PMC0/fullTextXML', searchUrl + '&callback=malicious'])
        assert.throws(() => officialScholarlyUrl(value));
});

test('raw metadata projections are hash bound and Crossref notices cannot masquerade as ordinary records', () => {
    const metadata = snapshot(search(), searchUrl);
    assert.deepEqual(parseEuropePmcRecord(metadata, 'PMC123'), { id: 'PMC123', doi, title, authors: ['Alice Example'], year: 2024, publicationTypes: ['Journal Article'] });
    const url = `https://api.crossref.org/works/${encodeURIComponent(doi)}`;
    assert.equal(parseCrossrefRecord(snapshot(crossref(), url)).retractionSignal, false);
    assert.deepEqual(parseCrossrefRecord(snapshot(crossref(), url)).referenceDois, ['10.1234/reference.1']);
    for (const notices of [{ 'update-to': [{ type: 'retraction' }] }, { relation: { 'is-retracted-by': [{ id: '10.1234/notice' }] } },
        { title: ['Correction: another work'] }]) assert.equal(parseCrossrefRecord(snapshot(crossref(notices), url)).retractionSignal, true);
    assert.throws(() => parseCrossrefRecord(snapshot(crossref({ DOI: '10.1234/other' }), url)), /mismatch/);
    assert.throws(() => parseEuropePmcRecord({ ...metadata, text: search() + ' ' }, 'PMC123'), /snapshot/);
    const retracted = JSON.parse(search()); retracted.resultList.result[0].isRetracted = 'Y';
    assert.throws(() => parseEuropePmcRecord(snapshot(JSON.stringify(retracted), searchUrl), 'PMC123'), /retraction/);
});

test('collector fetches official sources serially, attaches original snapshots, caches and honors the request budget', async () => {
    const transport = fixture();
    let now = time;
    const waits: number[] = [];
    const collector = new ScholarlyApiCollector({ fetchPage: transport.fetchPage, clock: () => now,
        pause: async milliseconds => { waits.push(milliseconds); now += milliseconds; }, maxRequests: 3 });
    const collected = await collector.collect(['machine learning'], 1, signal());
    assert.equal(collected.papers.length, 1); assert.deepEqual(collected.excluded, []);
    const paper = collected.papers[0]!;
    assert.equal(paper.text, parseScholarlyXml(xml()).text); assert.equal(paper.fullText.text, xml());
    assert.equal(paper.metadata.text, search()); assert.deepEqual(paper.publicationTypes, ['Journal Article']);
    const registered = await collector.crossref(doi, signal());
    assert.equal(registered.doi, doi);
    await collector.crossref(doi, signal());
    await collector.collect(['machine learning'], 1, signal());
    assert.equal(transport.requests.length, 3); assert.deepEqual(waits, [1000, 1000]);
    assert.ok(transport.requests.every(request => request.maximum === 524288 && request.options.allowXml));
    await assert.rejects(collector.crossref('10.1234/other', signal()), /budget/);
    for (const maximum of [0, 101, NaN]) await assert.rejects(collector.collect(['learning'], maximum, signal()), /bounded/);
    assert.throws(() => new ScholarlyApiCollector({ delayMs: 0 }), /pacing/);
});

test('inconsistent or restricted full text is excluded and an altered final endpoint is refused', async () => {
    for (const text of [xml('https://creativecommons.org/licenses/by-nc/4.0/'), xml().replace('<year>2024</year>', '<year>2025</year>')]) {
        const transport = fixture();
        const collector = new ScholarlyApiCollector({ fetchPage: async (...args) => {
            const page = await transport.fetchPage(...args); return page.url.endsWith('/fullTextXML') ? { ...page, text } : page;
        }, delayMs: 0, clock: () => time });
        const result = await collector.collect(['learning'], 1, signal());
        assert.equal(result.papers.length, 0); assert.equal(result.excluded.length, 1);
    }
    const collector = new ScholarlyApiCollector({ delayMs: 0, clock: () => time, fetchPage: async () =>
        ({ url: 'https://malicious.example/article', text: search(), contentType: 'application/json' }) });
    await assert.rejects(collector.collect(['learning'], 1, signal()), /official/);
});

test('collection follows cursors round robin until valid papers fill the bounded target', async () => {
    const requested: string[] = [];
    const collector = new ScholarlyApiCollector({ delayMs: 0, maxRequests: 20, clock: () => time, fetchPage: async url => {
        requested.push(url);
        const parsed = new URL(url);
        if (parsed.pathname.endsWith('/fullTextXML')) return { url, contentType: 'application/xml',
            text: parsed.pathname.includes('/PMC124/') ? xml().replace('>123<', '>124<').replace(doi, '10.1234/example.2') : xml() };
        const firstTopic = parsed.searchParams.get('query')!.includes('first topic');
        const value = JSON.parse(search());
        if (firstTopic && !parsed.searchParams.get('cursorMark')) {
            delete value.resultList.result[0].doi; value.resultList.result[0].pmcid = 'PMC999'; value.nextCursorMark = 'next-first';
        } else if (firstTopic) {
            value.resultList.result[0].doi = '10.1234/example.2'; value.resultList.result[0].pmcid = 'PMC124';
        }
        return { url, contentType: 'application/json', text: JSON.stringify(value) };
    } });
    const result = await collector.collect(['first topic', 'second topic'], 2, signal());
    assert.deepEqual(result.papers.map(paper => paper.id), ['PMC123', 'PMC124']);
    assert.equal(result.excluded.length, 1);
    const searches = requested.filter(url => url.includes('/search?')).map(url => new URL(url));
    assert.equal(searches.length, 3);
    assert.ok(searches[0]!.searchParams.get('query')!.includes('first topic'));
    assert.ok(searches[1]!.searchParams.get('query')!.includes('second topic'));
    assert.equal(searches[2]!.searchParams.get('cursorMark'), 'next-first');
    assert.ok(result.papers[1]!.metadata.url.includes('cursorMark=next-first'));
});

test('cursor loops terminate and acquisition reserves API capacity for later verification', async () => {
    for (const repeat of [true, false]) {
        let requests = 0;
        const collector = new ScholarlyApiCollector({ delayMs: 0, maxRequests: 4, clock: () => time, fetchPage: async url => {
            requests++;
            if (url.includes('/works/')) return { url, contentType: 'application/json', text: crossref() };
            const value = JSON.parse(search()); delete value.resultList.result[0].doi;
            value.resultList.result[0].pmcid = `PMC${1000 + requests}`;
            value.nextCursorMark = repeat ? 'repeated' : `cursor-${requests}`;
            return { url, contentType: 'application/json', text: JSON.stringify(value) };
        } });
        const result = await collector.collect(['learning'], 1, signal());
        assert.equal(result.papers.length, 0); assert.equal(requests, 2);
        assert.equal(result.excluded.some(record => record.reason.includes('reserved for verification')), !repeat);
        assert.equal((await collector.crossref(doi, signal())).doi, doi);
        assert.equal(requests, 3);
    }
});

test('disk cache preserves original immutable snapshots and expires Crossref metadata within one day', async () => {
    const cacheDirectory = await mkdtemp(join(tmpdir(), 'cgai-scholarly-provider-'));
    try {
        let now = time;
        const first = fixture();
        const options = { cacheDirectory, delayMs: 0, clock: () => now };
        const initial = await new ScholarlyApiCollector({ ...options, fetchPage: first.fetchPage }).crossref(doi, signal());
        const entries = await readdir(join(cacheDirectory, 'objects'));
        const original = await readFile(join(cacheDirectory, 'objects', entries[0]!), 'utf8');
        const second = fixture();
        const collector = new ScholarlyApiCollector({ ...options, fetchPage: second.fetchPage });
        const cached = await collector.crossref(doi, signal());
        assert.deepEqual(cached, initial); assert.equal(second.requests.length, 0);
        now += 24 * 60 * 60 * 1000;
        const refreshed = await collector.crossref(doi, signal());
        assert.equal(second.requests.length, 1); assert.notEqual(refreshed.snapshot.fetchedAt, initial.snapshot.fetchedAt);
        assert.equal((await readdir(join(cacheDirectory, 'objects'))).length, 2);
        assert.equal(await readFile(join(cacheDirectory, 'objects', entries[0]!), 'utf8'), original);
    } finally { await rm(cacheDirectory, { recursive: true, force: true }); }
});

test('separate collector cycles resume discovery cursors and reset at an exhausted page', async () => {
    const cacheDirectory = await mkdtemp(join(tmpdir(), 'cgai-scholarly-progress-'));
    const searched: URL[] = [];
    const fetchPage = async (url: string): Promise<PublicPage> => {
        const parsed = new URL(url), advanced = parsed.searchParams.get('cursorMark') === 'next-page';
        if (parsed.pathname.endsWith('/fullTextXML')) return { url, contentType: 'application/xml',
            text: parsed.pathname.includes('/PMC124/') ? xml().replace('>123<', '>124<').replace(doi, '10.1234/example.2') : xml() };
        searched.push(parsed);
        const value = JSON.parse(search());
        if (advanced) { value.resultList.result[0].pmcid = 'PMC124'; value.resultList.result[0].doi = '10.1234/example.2'; }
        value.nextCursorMark = 'next-page';
        return { url, contentType: 'application/json', text: JSON.stringify(value) };
    };
    const options = { cacheDirectory, fetchPage, delayMs: 0, clock: () => time };
    try {
        const first = await new ScholarlyApiCollector(options).collect(['learning'], 1, signal());
        assert.equal(first.papers[0]!.id, 'PMC123');
        const progressFile = join(cacheDirectory, 'discovery', (await readdir(join(cacheDirectory, 'discovery')))[0]!);
        assert.deepEqual(JSON.parse(await readFile(progressFile, 'utf8')), { version: 1, cursor: 'next-page' });
        const second = await new ScholarlyApiCollector(options).collect(['learning'], 1, signal());
        assert.equal(second.papers[0]!.id, 'PMC124');
        assert.equal(searched[1]!.searchParams.get('cursorMark'), 'next-page');
        assert.deepEqual(JSON.parse(await readFile(progressFile, 'utf8')), { version: 1, cursor: null });
        const restarted = await new ScholarlyApiCollector(options).collect(['learning'], 1, signal());
        assert.equal(restarted.papers[0]!.id, 'PMC123');
        assert.equal(searched.length, 2, 'reset reuses the immutable first-page cache');
        await new ScholarlyApiCollector(options).collect(['other topic'], 1, signal());
        assert.equal((await readdir(join(cacheDirectory, 'discovery'))).length, 2, 'progress identities bind the full public query');
        await new ScholarlyApiCollector(options).collect(['learning'], 2, signal());
        assert.equal((await readdir(join(cacheDirectory, 'discovery'))).length, 3, 'a different page size starts an independent cursor');
    } finally { await rm(cacheDirectory, { recursive: true, force: true }); }
});

test('failed collections preserve progress and malformed persisted cursors cannot choose a destination', async () => {
    const cacheDirectory = await mkdtemp(join(tmpdir(), 'cgai-scholarly-progress-validation-'));
    try {
        const transport = fixture();
        const options = { cacheDirectory, delayMs: 0, clock: () => time, fetchPage: async (...args: Parameters<typeof transport.fetchPage>) => {
            const page = await transport.fetchPage(...args);
            if (!page.url.includes('/search?')) return page;
            const value = JSON.parse(page.text); value.nextCursorMark = 'next-page'; return { ...page, text: JSON.stringify(value) };
        } };
        await new ScholarlyApiCollector(options).collect(['learning'], 1, signal());
        const progressFile = join(cacheDirectory, 'discovery', (await readdir(join(cacheDirectory, 'discovery')))[0]!);
        const original = await readFile(progressFile, 'utf8');
        await assert.rejects(new ScholarlyApiCollector({ ...options, fetchPage: async () => { throw new Error('provider failed'); } }).collect(['learning'], 1, signal()), /provider failed/);
        assert.equal(await readFile(progressFile, 'utf8'), original);
        for (const invalid of [{ version: 2, cursor: 'next-page' }, { version: 1, cursor: 'https://malicious.example/' },
            { version: 1, cursor: 'a'.repeat(4097) }, { version: 1, cursor: 'next-page', url: 'https://malicious.example/' }]) {
            await writeFile(progressFile, JSON.stringify(invalid));
            const result = await new ScholarlyApiCollector(options).collect(['learning'], 1, signal());
            assert.equal(result.papers[0]!.id, 'PMC123');
            assert.deepEqual(JSON.parse(await readFile(progressFile, 'utf8')), { version: 1, cursor: 'next-page' });
        }
        assert.ok(transport.requests.every(request => officialScholarlyUrl(request.url)));
    } finally { await rm(cacheDirectory, { recursive: true, force: true }); }
});

test('cancellation interrupts an uncooperative scholarly transport', async () => {
    const controller = new AbortController();
    const collector = new ScholarlyApiCollector({ delayMs: 0, fetchPage: async () => {
        controller.abort(new Error('cancelled research')); return new Promise(() => {});
    } });
    await assert.rejects(collector.collect(['learning'], 1, controller.signal), /cancelled research/);
});

test('cancelling a queued request retains the serial lane until earlier acquisition finishes', async () => {
    let finishFirst: (value: PublicPage) => void = () => {};
    const requests: string[] = [];
    const collector = new ScholarlyApiCollector({ delayMs: 0, clock: () => time, fetchPage: async url => {
        requests.push(url);
        if (requests.length === 1) return new Promise<PublicPage>(resolve => { finishFirst = resolve; });
        return { url, text: crossref({ DOI: '10.1234/third' }), contentType: 'application/json' };
    } });
    const first = collector.crossref(doi, signal());
    while (!requests.length) await new Promise<void>(resolve => setImmediate(resolve));
    const cancellation = new AbortController();
    const queued = collector.crossref('10.1234/second', cancellation.signal);
    const third = collector.crossref('10.1234/third', signal());
    cancellation.abort(new Error('queued cancellation'));
    await assert.rejects(queued, /queued cancellation/);
    assert.equal(requests.length, 1);
    finishFirst({ url: requests[0]!, text: crossref(), contentType: 'application/json' });
    await first; await third;
    assert.equal(requests.length, 2);
});

test('XML acquisition is opt in and disallowed redirects are checked before contacting their hosts', async () => {
    let calls = 0, resolutions = 0;
    const send = (_url: URL, _options: RequestOptions, callback: (response: IncomingMessage) => void) => {
        calls++;
        const response = Object.assign(new PassThrough(), { statusCode: 200, headers: { 'content-type': 'application/xml' } });
        return Object.assign(new EventEmitter(), { end() { queueMicrotask(() => {
            callback(response as unknown as IncomingMessage); if (!response.destroyed) response.end('<article/>');
        }); } }) as unknown as ClientRequest;
    };
    const dependencies: PublicFetchDependencies = { lookup: async () => { resolutions++; return [{ address: '8.8.8.8' }]; },
        httpRequest: send as unknown as PublicFetchDependencies['httpRequest'], httpsRequest: send as unknown as PublicFetchDependencies['httpsRequest'] };
    const url = 'https://www.ebi.ac.uk/europepmc/webservices/rest/PMC123/fullTextXML';
    await assert.rejects(fetchPublic(url, signal(), 1024, dependencies), /media type/);
    assert.equal((await fetchPublic(url, signal(), 1024, dependencies, { allowXml: true, validateUrl: officialScholarlyUrl })).text, '<article/>');
    const redirect = (_url: URL, _options: RequestOptions, callback: (response: IncomingMessage) => void) => {
        calls++;
        const response = Object.assign(new PassThrough(), { statusCode: 302, headers: { location: 'https://untrusted.example/private' } });
        return Object.assign(new EventEmitter(), { end() { queueMicrotask(() => callback(response as unknown as IncomingMessage)); } }) as unknown as ClientRequest;
    };
    dependencies.httpsRequest = redirect as unknown as PublicFetchDependencies['httpsRequest'];
    await assert.rejects(fetchPublic(url, signal(), 1024, dependencies, { allowXml: true, validateUrl: officialScholarlyUrl }), /official/);
    assert.equal(calls, 3); assert.equal(resolutions, 3);
});
