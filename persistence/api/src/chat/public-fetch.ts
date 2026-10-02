import { lookup } from 'node:dns/promises';
import { request as httpRequest } from 'node:http';
import { request as httpsRequest } from 'node:https';
import { isIP } from 'node:net';

/** Conservative IPv4-only public fetch policy. IPv6 is refused, including mapped addresses. */
export function publicAddress(address: string): boolean {
    if (isIP(address) !== 4) return false;
    const [a, b, c] = address.split('.').map(Number) as [number, number, number, number];
    return a !== 0 && a !== 10 && a !== 127 && a < 224 &&
        !(a === 100 && b >= 64 && b <= 127) && !(a === 169 && b === 254) &&
        !(a === 172 && b >= 16 && b <= 31) && !(a === 192 && (b === 168 || b === 0 || (b === 88 && c === 99))) &&
        !(a === 198 && (b === 18 || b === 19 || (b === 51 && c === 100))) &&
        !(a === 203 && b === 0 && c === 113);
}
export function publicUrl(value: string): URL {
    const url = new URL(value);
    if (!['https:', 'http:'].includes(url.protocol) || url.username || url.password ||
        (url.port && url.port !== '443' && url.port !== '80') ||
        url.hostname === 'localhost' || url.hostname.endsWith('.localhost') || url.hostname.endsWith('.local'))
        throw new Error('research destination is not public HTTP(S)');
    if ((isIP(url.hostname) || url.hostname.startsWith('[')) && !publicAddress(url.hostname))
        throw new Error('research address is not public IPv4');
    url.hash = '';
    return url;
}
export interface PublicPage { url: string; text: string; contentType: string }
/** Explicit acquisition policies; existing research callers keep their original media policy. */
export interface PublicFetchOptions {
    readonly allowXml?: boolean;
    /** Runs before DNS and again before every redirected destination is contacted. */
    readonly validateUrl?: (url: URL) => void;
}
/** Injectable transport for deterministic tests; production keeps the system resolver and HTTP clients. */
export interface PublicFetchDependencies {
    lookup: (hostname: string) => Promise<readonly { address: string }[]>;
    httpRequest: typeof httpRequest;
    httpsRequest: typeof httpsRequest;
}
/** Observe cancellation even when an adapter does not settle its operation after abort. */
export async function abortable<T>(operation: Promise<T>, signal: AbortSignal): Promise<T> {
    let cancel = () => {};
    const aborted = new Promise<never>((_, reject) => { cancel = () => reject(signal.reason); });
    signal.addEventListener('abort', cancel, { once: true });
    try {
        if (signal.aborted) cancel();
        return await Promise.race([operation, aborted]);
    } finally { signal.removeEventListener('abort', cancel); }
}
/** Resolve then pin the selected address to the socket, validating every redirect independently. */
export async function fetchPublic(value: string, signal: AbortSignal, maximum = 524288,
    dependencies: Partial<PublicFetchDependencies> = {}, options: PublicFetchOptions = {}): Promise<PublicPage> {
    if (!Number.isSafeInteger(maximum) || maximum < 1 || maximum > 524288)
        throw new Error('research byte limit must be between 1 and 524288');
    let url = publicUrl(value);
    for (let redirect = 0; redirect <= 4; redirect++) {
        options.validateUrl?.(url);
        signal.throwIfAborted();
        const records = await abortable((dependencies.lookup ?? ((hostname) => lookup(hostname, { all: true, family: 4 })))(url.hostname), signal);
        signal.throwIfAborted();
        if (!records.length || records.some((record) => !publicAddress(record.address))) throw new Error('research DNS resolved to a nonpublic address');
        const address = records[0]!.address;
        const result = await abortable(new Promise<PublicPage | { redirect: string }>((resolve, reject) => {
            const send = url.protocol === 'https:' ? dependencies.httpsRequest ?? httpsRequest : dependencies.httpRequest ?? httpRequest;
            const request = send(url, { signal, agent: false, family: 4,
                lookup: (_hostname, _options, callback) => callback(null, address, 4),
                headers: { accept: options.allowXml ? 'application/json, application/xml, text/xml' : 'text/html, text/plain, application/json', 'accept-encoding': 'identity', 'user-agent': 'Centroid-GAI-research/1' } }, (response) => {
                const status = response.statusCode ?? 0;
                if ([301, 302, 303, 307, 308].includes(status) && response.headers.location) {
                    response.destroy(); resolve({ redirect: response.headers.location }); return;
                }
                if (status !== 200 || (response.headers['content-encoding'] && response.headers['content-encoding'] !== 'identity')) {
                    response.destroy(); reject(new Error(`research page unavailable (${status}) or encoded`)); return;
                }
                const contentType = response.headers['content-type'] ?? '';
                if (!/^(text\/(plain|html)|application\/json)(;|$)/i.test(contentType) &&
                    !(options.allowXml && /^(application\/xml|text\/xml)(;|$)/i.test(contentType))) {
                    response.destroy(); reject(new Error('unsupported research media type')); return;
                }
                const chunks: Buffer[] = [];
                let length = 0;
                const declaredLength = response.headers['content-length'];
                if (declaredLength && (!/^\d+$/.test(declaredLength) || Number(declaredLength) > maximum)) {
                    response.destroy(); reject(new Error('research page exceeds byte limit')); return;
                }
                response.on('data', (chunk: Buffer) => {
                    length += chunk.length;
                    if (length > maximum) response.destroy(new Error('research page exceeds byte limit'));
                    else chunks.push(chunk);
                });
                response.once('error', reject);
                response.once('aborted', () => reject(new Error('research response was interrupted')));
                response.once('end', () => resolve({ url: url.href, text: Buffer.concat(chunks).toString('utf8'), contentType }));
            });
            request.once('error', reject);
            request.end();
        }), signal);
        signal.throwIfAborted();
        if ('text' in result) return result;
        url = publicUrl(new URL(result.redirect, url).href);
    }
    throw new Error('research redirect limit exceeded');
}
/** Bounded extraction only: no HTML execution, tool interpretation, or publication authority. */
export function readableText(page: PublicPage): string {
    if (!/^text\/html(?:;|$)/i.test(page.contentType)) return page.text.slice(0, 65536);
    return page.text.replace(/<(script|style|noscript)\b[^>]*>[\s\S]*?<\/\1\s*>/gi, ' ')
        .replace(/<!--[^]*?-->/g, ' ').replace(/<[^>]{0,4096}>/g, ' ')
        .replace(/&(?:nbsp|amp|lt|gt|quot|#39);/g, (entity) => ({ '&nbsp;': ' ', '&amp;': '&', '&lt;': '<', '&gt;': '>', '&quot;': '"', '&#39;': "'" })[entity]!)
        .replace(/\s+/g, ' ').trim().slice(0, 65536);
}
