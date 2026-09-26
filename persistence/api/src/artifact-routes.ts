import type { IncomingMessage, ServerResponse } from 'node:http';
import type { CompositionRecipe, ContentsSection, MergeRequest } from '../../shared/artifacts.ts';
import { createComposedArtifact, loadModelArtifact } from './model-repository.ts';
import { loadCurrentModelArtifact } from './composed-models.ts';
import { inspectNativeContents, inspectNativeModel, mergeNativeModels, matchNativePatterns } from './native.ts';

type Send = (response: ServerResponse, status: number, value: unknown) => void;
type Read = <T>(request: IncomingMessage) => Promise<T>;
const MAX_BYTES = 64 * 1024 * 1024;

function integer(raw: unknown, name: string, min: number, max: number): number {
    if (typeof raw !== 'number' || !Number.isInteger(raw) || raw < min || raw > max) throw new TypeError(`${name} must be an integer from ${min} to ${max}`);
    return raw;
}

export async function artifactRoute(request: IncomingMessage, response: ServerResponse, url: URL, segments: string[], send: Send, read: Read): Promise<boolean> {
    if (segments[0] !== 'models' || segments.length !== 3) return false;
    const name = segments[1]!;
    if (request.method === 'POST' && segments[2] === 'match') {
        const input = await read<{ text?: unknown; limit?: unknown } | null>(request);
        if (typeof input?.text !== 'string' || !input.text.trim() || input.text.includes('\0'))
            throw new TypeError('Matching text must be a non-empty string without NUL characters');
        if (Buffer.byteLength(input.text, 'utf8') > 16384) throw new RangeError('Matching text exceeds 16384 UTF-8 bytes');
        const limit = integer(input.limit ?? 5, 'limit', 1, 10);
        const model = await loadCurrentModelArtifact(name);
        if (!model) { send(response, 404, { error: 'model not found' }); return true; }
        if (model.payload.byteLength > MAX_BYTES) throw new RangeError('Artifact exceeds the 64 MiB matching limit');
        const data = matchNativePatterns(model.payload, input.text, limit);
        send(response, 200, { name, checksumSha256: model.checksumSha256, data });
        return true;
    }
    if (request.method === 'GET' && segments[2] === 'contents') {
        const section = url.searchParams.get('section') ?? 'summary';
        if (!['summary', 'vocabulary', 'centroids', 'centroid', 'highlights'].includes(section)) throw new TypeError('unknown contents section');
        const offset = integer(Number(url.searchParams.get('offset') ?? 0), 'offset', 0, 4294967295);
        const limit = integer(Number(url.searchParams.get('limit') ?? 25), 'limit', 1, 100);
        const centroid = integer(Number(url.searchParams.get('centroid') ?? 0), 'centroid', 0, 65535);
        const model = await loadCurrentModelArtifact(name);
        if (!model) { send(response, 404, { error: 'model not found' }); return true; }
        const expected = url.searchParams.get('checksum');
        if (expected && expected !== model.checksumSha256) {
            send(response, 409, { error: 'Artifact changed. Refresh the model catalog before inspecting more pages.' }); return true;
        }
        if (model.payload.byteLength > MAX_BYTES) { send(response, 413, { error: 'Artifact exceeds the 64 MiB inspection limit.' }); return true; }
        const data = inspectNativeContents(model.payload, section as ContentsSection, offset, limit, centroid);
        send(response, 200, { name, section, checksumSha256: model.checksumSha256, artifactBytes: model.payload.byteLength,
            composition: model.compositionJson ? JSON.parse(model.compositionJson) as CompositionRecipe : null, data });
        return true;
    }
    if (request.method !== 'POST' || segments[2] !== 'merge') return false;
    const input = await read<MergeRequest>(request);
    if (!input || !Array.isArray(input.sources) || input.sources.length < 1 || input.sources.length > 32)
        throw new TypeError('sources must contain 1 to 32 models');
    const target = integer(input.targetCentroids ?? 0, 'targetCentroids', 0, 65536);
    if (input.autoRebuild !== undefined && typeof input.autoRebuild !== 'boolean') throw new TypeError('autoRebuild must be a boolean');
    if (!name.trim() || name.length > 200) throw new TypeError('destination name must contain 1 to 200 characters');
    const seen = new Set<string>();
    for (const source of input.sources) {
        if (!source || typeof source.name !== 'string' || !source.name.trim() || seen.has(source.name)) throw new TypeError('source names must be nonempty and unique');
        if (source.name === name) throw new TypeError('destination must be different from every source');
        if (source.checksumSha256 !== undefined && (typeof source.checksumSha256 !== 'string' || !/^[a-f0-9]{64}$/.test(source.checksumSha256))) throw new TypeError('invalid source checksum');
        seen.add(source.name);
    }
    if (await loadModelArtifact(name)) { send(response, 409, { error: 'Destination already exists. Choose a new model name.' }); return true; }
    const payloads: Uint8Array[] = [];
    const sources: CompositionRecipe['sources'] = [];
    const checksums = new Set<string>();
    let bytes = 0;
    for (const requested of input.sources) {
        const source = await loadCurrentModelArtifact(requested.name);
        if (!source) { send(response, 404, { error: `Source model not found: ${requested.name}` }); return true; }
        if (requested.checksumSha256 && requested.checksumSha256 !== source.checksumSha256) {
            send(response, 409, { error: `Source changed: ${requested.name}. Refresh and review the selection again.` }); return true;
        }
        if (checksums.has(source.checksumSha256)) throw new TypeError('Sources include byte-identical artifacts; choose distinct models to avoid duplicate contributions.');
        checksums.add(source.checksumSha256);
        bytes += source.payload.byteLength;
        if (bytes > MAX_BYTES) { send(response, 413, { error: 'Combined source artifacts exceed 64 MiB. Use a smaller group.' }); return true; }
        payloads.push(source.payload);
        const summary = inspectNativeContents(source.payload, 'summary');
        sources.push({ name: source.name, checksumSha256: source.checksumSha256, initializedCentroids: summary.initializedCentroids, examplesSeen: summary.examplesSeen });
    }
    const payload = mergeNativeModels(payloads, target);
    if (payload.byteLength > MAX_BYTES) { send(response, 413, { error: 'Merged artifact exceeds 64 MiB. Reduce the target centroid count or source group.' }); return true; }
    const metadata = inspectNativeModel(payload);
    // PostgreSQL's signed bigint is narrower than the native unsigned counters.
    if (metadata.examplesSeen > 9223372036854775807n) throw new TypeError('Merged examples exceed the database counter range');
    const composition: CompositionRecipe = { version: 1, autoRebuild: input.autoRebuild ?? true, algorithm: target ? 'weighted-streaming-v1' : 'preserve', targetCentroids: metadata.centroidCount, sources };
    try {
        const saved = await createComposedArtifact({ name, payload, metadata }, composition);
        send(response, 201, { id: saved.id, name: saved.name, metadata, composition });
    } catch (error) {
        // A second request may have created the same destination while we computed.
        if (await loadModelArtifact(name)) send(response, 409, { error: 'Destination already exists. Choose a new model name.' });
        else throw error;
    }
    return true;
}
