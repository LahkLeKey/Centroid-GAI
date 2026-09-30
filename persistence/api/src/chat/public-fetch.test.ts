import assert from 'node:assert/strict';
import test from 'node:test';
import { EventEmitter } from 'node:events';
import type { ClientRequest, IncomingMessage, RequestOptions } from 'node:http';
import { PassThrough } from 'node:stream';
import { fetchPublic, publicAddress, publicUrl, readableText, type PublicFetchDependencies } from './public-fetch.ts';

interface ResponseFixture { status?: number; headers?: Record<string, string>; chunks?: string[]; interrupted?: boolean }
/** In-memory HTTP streams exercise the real response handlers without opening any sockets. */
function transport(fixtures: ResponseFixture[], addresses = ['8.8.8.8']) {
    const resolved: string[] = [], requests: { url: string; options: RequestOptions }[] = [];
    const send = (url: URL, options: RequestOptions, callback: (response: IncomingMessage) => void) => {
        requests.push({ url: url.href, options });
        const fixture = fixtures.shift();
        assert.ok(fixture, 'unexpected HTTP request');
        const response = Object.assign(new PassThrough(), { statusCode: fixture.status ?? 200,
            headers: { 'content-type': 'text/plain', ...fixture.headers } });
        const request = Object.assign(new EventEmitter(), { end() {
            queueMicrotask(() => {
                callback(response as unknown as IncomingMessage);
                if (response.destroyed) return;
                for (const chunk of fixture.chunks ?? ['public evidence']) response.write(chunk);
                if (fixture.interrupted) response.emit('aborted');
                else response.end();
            });
        } });
        return request as unknown as ClientRequest;
    };
    const dependencies: PublicFetchDependencies = {
        async lookup(hostname) { resolved.push(hostname); return addresses.map((address) => ({ address })); },
        httpRequest: send as unknown as PublicFetchDependencies['httpRequest'],
        httpsRequest: send as unknown as PublicFetchDependencies['httpsRequest'],
    };
    return { dependencies, resolved, requests };
}
const signal = () => new AbortController().signal;

test('DNS validation refuses mixed public/private records before a socket is opened', async () => {
    const fixture = transport([], ['8.8.8.8', '169.254.169.254']);
    await assert.rejects(fetchPublic('https://example.com', signal(), 512, fixture.dependencies), /nonpublic address/);
    assert.equal(fixture.requests.length, 0);
    for (const address of ['0.1.2.3', '100.127.0.1', '172.31.0.1', '192.0.2.1', '198.18.0.1', '224.0.0.1', '255.255.255.255', '::ffff:8.8.8.8'])
        assert.equal(publicAddress(address), false, address);
    assert.throws(() => publicUrl('http://2130706433/'), /public/);
});

test('accepted DNS is pinned and each redirect is resolved again with final URL provenance', async () => {
    const fixture = transport([{ status: 302, headers: { location: 'https://other.example/final#fragment' } }, { chunks: ['answer'] }]);
    const page = await fetchPublic('https://example.com/start', signal(), 512, fixture.dependencies);
    assert.equal(page.url, 'https://other.example/final');
    assert.deepEqual(fixture.resolved, ['example.com', 'other.example']);
    for (const request of fixture.requests) {
        assert.equal(request.options.agent, false);
        assert.equal(request.options.family, 4);
        assert.deepEqual(request.options.headers, { accept: 'text/html, text/plain, application/json', 'accept-encoding': 'identity', 'user-agent': 'Centroid-GAI-research/1' });
        const address = await new Promise<string>((resolve, reject) => {
            request.options.lookup!('different-hostname.example', {}, (error, result) => error ? reject(error) : resolve(result as string));
        });
        assert.equal(address, '8.8.8.8');
    }
});

test('redirects cannot reach private addresses or exceed four hops', async () => {
    const blocked = transport([{ status: 302, headers: { location: 'http://127.0.0.1/admin' } }]);
    await assert.rejects(fetchPublic('https://example.com', signal(), 512, blocked.dependencies), /public/);
    assert.equal(blocked.requests.length, 1);
    const loop = transport(Array.from({ length: 5 }, () => ({ status: 302, headers: { location: '/again' } })));
    await assert.rejects(fetchPublic('https://example.com', signal(), 512, loop.dependencies), /redirect limit/);
    assert.equal(loop.requests.length, 5);
});

test('streamed and declared response sizes are bounded and truncated or encoded responses fail', async () => {
    for (const fixture of [
        { chunks: ['abcd', 'efgh'] }, { headers: { 'content-length': '1000' } },
        { headers: { 'content-encoding': 'gzip' } }, { headers: { 'content-type': 'application/pdf' } },
        { interrupted: true },
    ]) {
        await assert.rejects(fetchPublic('https://example.com', signal(), 4, transport([fixture]).dependencies));
    }
    for (const limit of [NaN, Infinity, 0, -1, 524289])
        await assert.rejects(fetchPublic('https://example.com', signal(), limit, transport([]).dependencies), /byte limit/);
    assert.equal((await fetchPublic('https://example.com', signal(), 4, transport([{ chunks: ['four'] }]).dependencies)).text, 'four');
});

test('cancellation bounds stalled DNS and responses without waiting for adapter cooperation', async () => {
    const controller = new AbortController();
    const waiting = fetchPublic('https://example.com', controller.signal, 512, { async lookup() {
        controller.abort(new Error('cancelled DNS')); return new Promise(() => {});
    } });
    await assert.rejects(waiting, /cancelled DNS/);
    const responseAbort = new AbortController();
    const fixture = transport([]);
    fixture.dependencies.httpsRequest = (() => ({ once() {}, end() { responseAbort.abort(new Error('cancelled response')); } })) as unknown as PublicFetchDependencies['httpsRequest'];
    await assert.rejects(fetchPublic('https://example.com', responseAbort.signal, 512, fixture.dependencies), /cancelled response/);
    assert.equal(readableText({ url: '', contentType: 'Text/HTML; charset=utf-8',
        text: '<STYLE>hidden</STYLE><SCRIPT>steal()</SCRIPT><p>visible &amp; inert</p>' }), 'visible & inert');
});
