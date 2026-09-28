/** Build and verify immutable categorized native artifacts with a static spatial index. */
import assert from 'node:assert/strict';
import {
    existsSync,
    mkdirSync,
    mkdtempSync,
    readdirSync,
    readFileSync,
    renameSync,
    statSync,
    writeFileSync
} from 'node:fs';
import {arch, endianness} from 'node:os';
import {dirname, resolve} from 'node:path';
import {gunzipSync, gzipSync} from 'node:zlib';

import {cLocaleTokens, loadSnapshot, sha256} from '../evaluation/codebase-data.ts';
import type {Document} from '../evaluation/codebase-data.ts';
import {inspectNativeContents, inspectNativeModel, trainNativeModel} from '../native.ts';

import {corpusPolicy, prepareCodebase} from './codebase-corpus.ts';
import type {SourceChunk} from './codebase-corpus.ts';
import {createNativeCentroidSearch} from './native-spatial.ts';
import {buildSpatialIndex, exhaustive, nearest} from './spatial.ts';
import type {SpatialIndex, SpatialPoint} from './spatial.ts';

export const nativeConfig = {
    dimensions : 32,
    centroidCount : 16,
    contextWindow : 3,
    seed : 42n
} as const;
const fileLimit = 1024 * 1024;
const releaseLimit = 128 * fileLimit;
const json = (value: unknown) =>
    JSON.stringify(value, (_, item: unknown) => typeof item === 'bigint' ? item.toString() : item) +
    '\n';
interface OutputFile {
    path: string;
    bytes: number;
    sha256: string
}
interface ReleaseShard {
    id: string;
    category: string;
    source: string;
    model: string;
    corpusSha256: string;
    artifactSha256: string;
    examplesSeen: string;
    centroids: number;
    chunks: number
}
interface EncyclopediaShard {
    id: string;
    cluster: string;
    source: string;
    model: string;
    articles: number;
    corpusSha256: string;
    artifactSha256: string;
    examplesSeen: string;
    centroids: number
}
interface EncyclopediaRelease {
    sourceManifestSha256: string;
    modelManifestSha256: string;
    upstreamRevision: string;
    articles: number;
    shards: EncyclopediaShard[];
    centroids: number;
    examplesSeen: string
}
interface DocumentRecord {
    sha256: string;
    lines: number;
    sources: Document['sources']
}
interface ReleaseManifest {
    version: 1;
    sourceCommit: string;
    snapshotManifestSha256: string;
    files: OutputFile[];
    corpusPolicy: typeof corpusPolicy;
    nativeConfig:
        {dimensions: number; centroidCount : number; contextWindow : number; seed : string};
    statistics: ReturnType<typeof prepareCodebase>[ 'statistics' ];
    shards: ReleaseShard[];
    centroids: number;
    examplesSeen: string;
    repeatTrainingVerified: boolean;
    environment: {
        node: string; architecture : string; byteOrder : string; libraryVersion : string;
        addonSha256 : string
    };
    implementationSha256: Record<string, string>;
    encyclopedia?: EncyclopediaRelease;
}

interface EncyclopediaSourceShard {
    id: string;
    cluster: string;
    path: string;
    articles: number;
    bytes: number;
    raw_bytes: number;
    sha256: string;
    content_sha256: string
}
interface EncyclopediaSources {
    schema_version: number;
    articles: number;
    source: {revision: string};
    license_sha256: string;
    shards: EncyclopediaSourceShard[]
}
interface EncyclopediaOutputFile {
    path: string;
    bytes: number;
    sha256: string
}
interface EncyclopediaModelShard {
    id: string;
    cluster: string;
    source: string;
    articles: number;
    corpusSha256: string;
    artifactSha256: string;
    artifactBytes: number;
    model: EncyclopediaOutputFile;
    inspection: EncyclopediaOutputFile;
    examplesSeen: string;
    initializedCentroids: number
}
interface EncyclopediaModels {
    schemaVersion: number;
    sourceManifestSha256: string;
    modelConfig: object;
    deterministicRepeatVerified: boolean;
    articles: number;
    shards: EncyclopediaModelShard[];
    totalExamplesSeen: string;
    totalCentroids: number
}

/** Read, validate, and optionally bundle the committed encyclopedia cluster release. */
function readEncyclopediaRelease(directory: string, inputPrefix = '',
                                 copy?: (path: string, bytes: Uint8Array) => void) {
    const root = resolve(directory);
    function read(path: string) {
        if (!path || path.includes('\\') || path.startsWith('/') ||
            path.split('/').some(part => !part || part === '.' || part === '..')) {
            throw new Error('Invalid encyclopedia release path');
        }
        const bytes = readFileSync(resolve(root, inputPrefix, path));
        copy?.(path, bytes);
        return bytes;
    }
    const sourceBytes = read('sources.json');
    const modelBytes = read('models.json');
    const license = read('LICENSE.md');
    const sources = JSON.parse(sourceBytes.toString('utf8')) as EncyclopediaSources;
    const models = JSON.parse(modelBytes.toString('utf8')) as EncyclopediaModels;
    assert.equal(sources.schema_version, 1);
    assert.equal(models.schemaVersion, 1);
    assert.equal(models.sourceManifestSha256, sha256(sourceBytes));
    assert.equal(sha256(license), sources.license_sha256);
    assert.equal(models.deterministicRepeatVerified, true);
    assert.deepEqual(models.modelConfig, {...nativeConfig, seed : nativeConfig.seed.toString()});
    assert.equal(models.articles, sources.articles);
    assert.equal(models.shards.length, sources.shards.length);

    const shards: EncyclopediaShard[] = [];
    const points: SpatialPoint[] = [];
    let examples = 0n;
    for (const [index, sourceShard] of sources.shards.entries()) {
        const modelShard = models.shards[index];
        assert(modelShard);
        assert.equal(modelShard.id, sourceShard.id);
        assert.equal(modelShard.cluster, sourceShard.cluster);
        assert.equal(modelShard.source, sourceShard.path);
        assert.equal(modelShard.articles, sourceShard.articles);

        const compressedSource = read(sourceShard.path);
        assert.equal(compressedSource.length, sourceShard.bytes);
        assert.equal(sha256(compressedSource), sourceShard.sha256);
        const rawSource = gunzipSync(compressedSource, {maxOutputLength : fileLimit});
        assert.equal(rawSource.length, sourceShard.raw_bytes);
        assert.equal(sha256(rawSource), sourceShard.content_sha256);
        const sourceText = rawSource.toString('utf8');
        assert(Buffer.from(sourceText, 'utf8').equals(rawSource),
               'Invalid UTF-8 in encyclopedia source shard');
        const articles = sourceText.trim().split('\n').map(line => JSON.parse(line) as {
            title: string;
            text: string
        });
        assert.equal(articles.length, sourceShard.articles);
        assert(articles.every(article => typeof article.title === 'string' &&
                                         typeof article.text === 'string'));
        const corpus = articles.map(article => `${article.title}\n${article.text}`).join('\n\n');
        assert.equal(sha256(corpus), modelShard.corpusSha256);

        const compressedModel = read(modelShard.model.path);
        assert.equal(compressedModel.length, modelShard.model.bytes);
        assert.equal(sha256(compressedModel), modelShard.model.sha256);
        const artifact = gunzipSync(compressedModel, {maxOutputLength : 32 * fileLimit});
        assert.equal(artifact.length, modelShard.artifactBytes);
        assert.equal(sha256(artifact), modelShard.artifactSha256);
        const metadata = inspectNativeModel(artifact);
        assert.equal(metadata.examplesSeen.toString(), modelShard.examplesSeen);
        assert.equal(metadata.examplesSeen, BigInt(cLocaleTokens(corpus).length + 1));
        const shardPoints = nativePoints(artifact, `encyclopedia:${modelShard.id}`,
                                         `encyclopedia/${modelShard.cluster}`);
        assert.equal(shardPoints.length, modelShard.initializedCentroids);
        points.push(...shardPoints);

        const inspection = read(modelShard.inspection.path);
        assert.equal(inspection.length, modelShard.inspection.bytes);
        assert.equal(sha256(inspection), modelShard.inspection.sha256);
        shards.push({
            id : modelShard.id,
            cluster : modelShard.cluster,
            source : `encyclopedia/${sourceShard.path}`,
            model : `encyclopedia/${modelShard.model.path}`,
            articles : modelShard.articles,
            corpusSha256 : modelShard.corpusSha256,
            artifactSha256 : modelShard.artifactSha256,
            examplesSeen : modelShard.examplesSeen,
            centroids : shardPoints.length
        });
        examples += metadata.examplesSeen;
    }
    assert.equal(points.length, models.totalCentroids);
    assert.equal(examples.toString(), models.totalExamplesSeen);
    return {
        points,
        manifest : {
            sourceManifestSha256 : sha256(sourceBytes),
            modelManifestSha256 : sha256(modelBytes),
            upstreamRevision : sources.source.revision,
            articles : sources.articles,
            shards,
            centroids : points.length,
            examplesSeen : examples.toString()
        } satisfies EncyclopediaRelease
    };
}

/** Bound every file and the complete tree before publication and verification. */
function auditSize(directory: string) {
    const files = readdirSync(directory, {recursive : true, withFileTypes : true})
                      .filter(entry => entry.isFile());
    let bytes = 0;
    for (const file of files) {
        const size = statSync(resolve(file.parentPath, file.name)).size;
        if (size > fileLimit)
            throw new Error('Release file exceeds 1 MiB');
        bytes += size;
    }
    if (bytes > releaseLimit)
        throw new Error('Release exceeds 128 MiB');
    return {files : files.length, bytes};
}

/**
 * Keep large derived indexes within the per-file release budget without changing their JSON shape.
 */
function publishJson(path: string, value: unknown,
                     publish: (path: string, content: string|Uint8Array) => void) {
    const bytes = Buffer.from(json(value));
    if (bytes.length <= fileLimit)
        publish(path, bytes);
    else
        publish(`${path}.gz`, gzipSync(bytes, {level : 9}));
}

function parseJsonSidecar(read: (path: string) => Uint8Array, path: string, compressed: boolean) {
    const bytes = read(compressed ? `${path}.gz` : path);
    const contents = compressed ? gunzipSync(bytes, {maxOutputLength : releaseLimit}) : bytes;
    return JSON.parse(Buffer.from(contents).toString('utf8')) as unknown;
}

/** Native inspection is authoritative for row IDs, observations, and vector components. */
function nativePoints(payload: Uint8Array, id: string, category: string): SpatialPoint[] {
    const summary = inspectNativeContents(payload, 'summary');
    assert.equal(summary.dimensions, nativeConfig.dimensions);
    assert.equal(summary.contextWindow, nativeConfig.contextWindow);
    assert.equal(summary.seed, nativeConfig.seed.toString());
    assert.equal(summary.centroidCount, nativeConfig.centroidCount);
    return Array.from({length : summary.initializedCentroids}, (_, centroid) => {
        const detail = inspectNativeContents(payload, 'centroid', 0, 1, centroid);
        return {
            id : `${id}:${String(centroid).padStart(4, '0')}`,
            category,
            centroid,
            vector : detail.vector,
            observations : detail.observations
        };
    });
}

/**
 * Cache inter-centroid edges and audit every global and category-restricted query against a full
 * scan.
 */
function spatialData(points: SpatialPoint[]) {
    const index = buildSpatialIndex(points);
    const native = createNativeCentroidSearch(points);
    let comparisons = 0;
    let exhaustiveComparisons = 0;
    const graph = index.points.map(point => {
        const global = native.search(point.vector, 5, undefined, point.id);
        const local = native.search(point.vector, 5, point.category, point.id);
        assert.deepEqual(global.neighbors,
                         nearest(index, point.vector, 5, undefined, point.id).neighbors);
        assert.deepEqual(local.neighbors,
                         nearest(index, point.vector, 5, point.category, point.id).neighbors);
        assert.deepEqual(global.neighbors,
                         exhaustive(index.points, point.vector, 5, undefined, point.id));
        assert.deepEqual(local.neighbors,
                         exhaustive(index.points, point.vector, 5, point.category, point.id));
        comparisons += global.comparisons + local.comparisons;
        exhaustiveComparisons += points.length - 1 +
                                 points.filter(other => other.category === point.category).length -
                                 1;
        return {id : point.id, neighbors : global.neighbors, categoryNeighbors : local.neighbors};
    });
    return {
        index,
        graph,
        audit : {
            queries : points.length * 2,
            exactAgreement : true,
            comparisons,
            exhaustiveComparisons,
            distanceComparisonsSaved : exhaustiveComparisons - comparisons,
            policy :
                'KD bounding-box search over inspected coordinates, plus cached five-neighbor edges. Timings are not inferred from comparison counts.'
        }
    };
}

/**
 * Publish only a fully verified new release. Failed builds leave an identifiable partial
 * directory.
 */
export function buildCodebaseRelease(snapshotDirectory: string, destination: string,
                                     encyclopediaDirectory?: string) {
    const output = resolve(destination);
    if (existsSync(output))
        throw new Error('Release output must be a new directory');
    const snapshot = loadSnapshot(snapshotDirectory, {allowEmptyValidation : true});
    const documents = [...snapshot.train, ...snapshot.validation ];
    const prepared = prepareCodebase(documents);
    mkdirSync(dirname(output), {recursive : true});
    const staging = mkdtempSync(`${output}.partial-`);
    const files: OutputFile[] = [];
    function publish(path: string, content: string|Uint8Array) {
        const bytes = typeof content === 'string' ? Buffer.from(content) : content;
        if (bytes.length > fileLimit)
            throw new Error(`File exceeds 1 MiB: ${path}`);
        mkdirSync(dirname(resolve(staging, path)), {recursive : true});
        writeFileSync(resolve(staging, path), bytes, {flag : 'wx'});
        files.push({path, bytes : bytes.length, sha256 : sha256(bytes)});
    }
    publish('SOURCE_LICENSE.txt', readFileSync(resolve(snapshotDirectory, 'SOURCE_LICENSE.txt')));
    publish('snapshot-manifest.json', readFileSync(resolve(snapshotDirectory, 'manifest.json')));
    publish('documents.json', json(documents
                                       .map(document => ({
                                                sha256 : document.sha256,
                                                lines : document.text.split('\n').length,
                                                sources : document.sources
                                            }))
                                       .sort((a, b) => a.sha256 < b.sha256 ? -1 : 1)));
    const shards: ReleaseShard[] = [];
    const points: SpatialPoint[] = [];
    let examples = 0n;
    let libraryVersion = '';
    for (const shard of prepared.shards) {
        const payload = trainNativeModel(shard.text, nativeConfig);
        assert(payload.equals(trainNativeModel(shard.text, nativeConfig)),
               'Native repeat training differs');
        const metadata = inspectNativeModel(payload);
        assert.equal(metadata.examplesSeen, BigInt(cLocaleTokens(shard.text).length + 1));
        const rows = nativePoints(payload, shard.id, shard.category);
        assert.equal(rows.reduce((sum, point) => sum + BigInt(point.observations), 0n),
                     metadata.examplesSeen);
        const source = `sources/${shard.id}.jsonl.gz`;
        const model = `models/${shard.id}.cgai.gz`;
        publish(source, gzipSync(shard.chunks.map(chunk => json(chunk)).join(''), {level : 9}));
        publish(model, gzipSync(payload, {level : 9}));
        shards.push({
            id : shard.id,
            category : shard.category,
            source,
            model,
            chunks : shard.chunks.length,
            corpusSha256 : sha256(shard.text),
            artifactSha256 : sha256(payload),
            examplesSeen : metadata.examplesSeen.toString(),
            centroids : rows.length
        });
        points.push(...rows);
        examples += metadata.examplesSeen;
        libraryVersion = metadata.libraryVersion;
    }
    let encyclopedia: EncyclopediaRelease|undefined;
    if (encyclopediaDirectory) {
        const imported = readEncyclopediaRelease(
            encyclopediaDirectory, '', (path, bytes) => publish(`encyclopedia/${path}`, bytes));
        points.push(...imported.points);
        encyclopedia = imported.manifest;
    }
    const spatial = spatialData(points);
    publishJson('spatial.json', spatial.index, publish);
    publishJson('neighbors.json', spatial.graph, publish);
    publishJson('spatial-audit.json', spatial.audit, publish);
    const manifest: ReleaseManifest = {
        version : 1,
        sourceCommit : snapshot.manifest.source.commit,
        snapshotManifestSha256 : snapshot.manifestSha256,
        corpusPolicy,
        nativeConfig : {...nativeConfig, seed : nativeConfig.seed.toString()},
        statistics : prepared.statistics,
        shards,
        centroids : points.length,
        examplesSeen : examples.toString(),
        repeatTrainingVerified : true,
        environment : {
            node : process.version,
            architecture : arch(),
            byteOrder : endianness(),
            libraryVersion,
            addonSha256 : sha256(readFileSync(
                new URL('../../build/Release/centroid_gai_native.node', import.meta.url)))
        },
        implementationSha256 : Object.fromEntries([
            'codebase-release.ts', 'codebase-corpus.ts', 'spatial.ts', 'native-spatial.ts',
            '../native.ts', '../evaluation/codebase-data.ts'
        ].map(path => [path, sha256(readFileSync(new URL(path, import.meta.url)))])),
        files,
        ...(encyclopedia ? {encyclopedia} : {})
    };
    writeFileSync(resolve(staging, 'manifest.json'), json(manifest), {flag : 'wx'});
    verifyCodebaseRelease(staging);
    renameSync(staging, output);
    return {
        ...manifest.statistics,
        categories : new Set(shards.map(shard => shard.category)).size,
        shards : shards.length,
        centroids : points.length,
        examplesSeen : examples.toString(),
        spatial : spatial.audit,
        ...auditSize(output)
    };
}

/**
 * Verify source reconstruction, global deduplication, native counts/vectors, and exact spatial
 * results.
 */
export function verifyCodebaseRelease(directory: string) {
    const size = auditSize(directory);
    const manifest =
        JSON.parse(readFileSync(resolve(directory, 'manifest.json'), 'utf8')) as ReleaseManifest;
    assert.equal(manifest.version, 1);
    assert.deepEqual(manifest.corpusPolicy, corpusPolicy);
    assert.deepEqual(manifest.nativeConfig, {...nativeConfig, seed : nativeConfig.seed.toString()});
    const inventory = new Map(manifest.files.map(file => [file.path, file]));
    assert.equal(inventory.size, manifest.files.length, 'Duplicate file inventory');
    for (const file of manifest.files) {
        if (!/^[a-zA-Z0-9_./-]+$/.test(file.path) || file.path.startsWith('/') ||
            file.path.split('/').some(part => !part || part === '.' || part === '..')) {
            throw new Error('Invalid release path');
        }
        const bytes = readFileSync(resolve(directory, file.path));
        assert.equal(bytes.length, file.bytes, `Size mismatch: ${file.path}`);
        assert.equal(sha256(bytes), file.sha256, `Checksum mismatch: ${file.path}`);
    }
    assert.equal(size.files, inventory.size + 1, 'Unexpected release files');
    const read = (path: string) => {
        assert(inventory.has(path), `Missing file inventory: ${path}`);
        return readFileSync(resolve(directory, path));
    };
    const snapshotBytes = read('snapshot-manifest.json');
    assert.equal(sha256(snapshotBytes), manifest.snapshotManifestSha256);
    const snapshot = JSON.parse(snapshotBytes.toString('utf8'));
    assert.equal(snapshot.source.commit, manifest.sourceCommit);
    assert.equal(sha256(read('SOURCE_LICENSE.txt')), snapshot.files['SOURCE_LICENSE.txt']);
    const entries = new Map<string, string>(
        snapshot.entries.map((entry: {path: string; oid : string}) => [entry.path, entry.oid]));
    const records = JSON.parse(read('documents.json').toString('utf8')) as DocumentRecord[];
    const reconstructed = new Map(records.map(record => {
        assert(Number.isInteger(record.lines) && record.lines > 0 && record.lines <= 2_000_000);
        assert(record.sources.length > 0);
        for (const source of record.sources) {
            assert.equal(source.commit, manifest.sourceCommit);
            assert(entries.has(source.path));
            assert.equal(entries.get(source.path), source.blob);
        }
        return [ record.sha256, {record, lines : Array<string>(record.lines).fill('')} ] as const;
    }));
    assert.equal(reconstructed.size, records.length);
    const allChunks = new Set<string>();
    const points: SpatialPoint[] = [];
    const sourceGroups: SourceChunk[][] = [];
    let examples = 0n;
    for (const shard of manifest.shards) {
        const chunks = gunzipSync(read(shard.source), {maxOutputLength : 16 * fileLimit})
                           .toString('utf8')
                           .trim()
                           .split('\n')
                           .map(line => JSON.parse(line) as SourceChunk);
        assert.equal(chunks.length, shard.chunks);
        sourceGroups.push(chunks);
        for (const chunk of chunks) {
            assert.equal(sha256(chunk.text), chunk.sha256);
            assert(!allChunks.has(chunk.sha256), 'Duplicate training chunk');
            allChunks.add(chunk.sha256);
            for (const source of chunk.sources) {
                const document = reconstructed.get(source.documentSha256);
                assert(document, 'Missing source document');
                assert(document.record.sources.some(item => item.path === source.path &&
                                                            item.blob === source.blob &&
                                                            item.commit === source.commit));
                const lines = chunk.text.split('\n');
                assert(Number.isInteger(source.startLine) && Number.isInteger(source.endLine) &&
                       source.startLine >= 1 && source.endLine <= document.lines.length);
                assert.equal(source.endLine - source.startLine + 1, lines.length);
                lines.forEach(
                    (line, offset) => { document.lines[source.startLine - 1 + offset] = line; });
            }
        }
        const text = chunks.map(chunk => chunk.text).join('\n\n');
        assert.equal(sha256(text), shard.corpusSha256);
        const payload = gunzipSync(read(shard.model), {maxOutputLength : 64 * fileLimit});
        assert.equal(sha256(payload), shard.artifactSha256);
        const metadata = inspectNativeModel(payload);
        assert.equal(metadata.examplesSeen.toString(), shard.examplesSeen);
        assert.equal(metadata.examplesSeen, BigInt(cLocaleTokens(text).length + 1));
        const rows = nativePoints(payload, shard.id, shard.category);
        assert.equal(rows.length, shard.centroids);
        assert.equal(rows.reduce((sum, point) => sum + BigInt(point.observations), 0n),
                     metadata.examplesSeen);
        points.push(...rows);
        examples += metadata.examplesSeen;
    }
    if (manifest.encyclopedia) {
        const imported = readEncyclopediaRelease(directory, 'encyclopedia/');
        assert.deepEqual(imported.manifest, manifest.encyclopedia);
        points.push(...imported.points);
    } else {
        assert(!manifest.files.some(file => file.path.startsWith('encyclopedia/')),
               'Unmanifested encyclopedia data');
    }
    const documents = [...reconstructed.values() ].map(({record, lines}) => {
        const text = lines.join('\n');
        assert.equal(sha256(text), record.sha256, 'Source document reconstruction mismatch');
        return {sha256 : record.sha256, text, sources : record.sources};
    });
    const prepared = prepareCodebase(documents);
    assert.deepEqual(prepared.statistics, manifest.statistics);
    assert.deepEqual(prepared.shards.map(shard => shard.chunks), sourceGroups);
    assert.deepEqual(prepared.shards.map(shard => [shard.id, shard.category]),
                     manifest.shards.map(shard => [shard.id, shard.category]));
    assert.equal(points.length, manifest.centroids);
    assert.equal(examples.toString(), manifest.examplesSeen);
    const spatial = spatialData(points);
    assert.deepEqual(parseJsonSidecar(read, 'spatial.json', !inventory.has('spatial.json')),
                     spatial.index);
    assert.deepEqual(parseJsonSidecar(read, 'neighbors.json', !inventory.has('neighbors.json')),
                     spatial.graph);
    assert.deepEqual(
        parseJsonSidecar(read, 'spatial-audit.json', !inventory.has('spatial-audit.json')),
        spatial.audit);
    return {manifest, index : spatial.index, graph : spatial.graph, audit : spatial.audit, size};
}

const nativeIndexes = new WeakMap<SpatialIndex, ReturnType<typeof createNativeCentroidSearch>>();

/**
 * Use cached five-neighbor edges; other requests traverse an immutable C index, reused per loaded
 * sidecar.
 */
export function queryCodebaseRelease(
    release: {index: SpatialIndex; graph : ReturnType<typeof spatialData>[ 'graph' ]}, id: string,
    limit = 5, category?: string) {
    const point = release.index.points.find(item => item.id === id);
    if (!point)
        throw new Error(`Unknown centroid: ${id}`);
    if (!Number.isInteger(limit) || limit < 1 || limit > 100)
        throw new Error('Neighbor limit must be 1–100');
    const cached = release.graph.find(row => row.id === id);
    if (cached && limit <= 5 && (category === undefined || category === point.category)) {
        return {
            id,
            cached : true,
            comparisons : 0,
            neighbors : (category === undefined ? cached.neighbors : cached.categoryNeighbors)
                            .slice(0, limit)
        };
    }
    let native = nativeIndexes.get(release.index);
    if (!native) {
        native = createNativeCentroidSearch(release.index.points);
        nativeIndexes.set(release.index, native);
    }
    return {
        id,
        cached : false,
        engine : 'native-c',
        ...native.search(point.vector, limit, category, id)
    };
}

/**
 * Load checksummed spatial sidecars for queries without rerunning native verification or all-pairs
 * audits.
 */
export function loadCodebaseSpatial(directory: string) {
    const manifest =
        JSON.parse(readFileSync(resolve(directory, 'manifest.json'), 'utf8')) as ReleaseManifest;
    assert.equal(manifest.version, 1);
    assert.deepEqual(manifest.nativeConfig, {...nativeConfig, seed : nativeConfig.seed.toString()});
    const inventory = new Set(manifest.files.map(file => file.path));
    function readBytes(path: string) {
        const expected = manifest.files.filter(file => file.path === path);
        assert.equal(expected.length, 1, 'Missing/duplicate spatial file inventory');
        const bytes = readFileSync(resolve(directory, path));
        assert(bytes.length <= fileLimit);
        assert.equal(bytes.length, expected[0]!.bytes, 'Spatial file size mismatch');
        assert.equal(sha256(bytes), expected[0]!.sha256, 'Spatial file checksum mismatch');
        return bytes;
    }
    const index =
        parseJsonSidecar(readBytes, 'spatial.json', !inventory.has('spatial.json')) as SpatialIndex;
    const graph = parseJsonSidecar(readBytes, 'neighbors.json', !inventory.has('neighbors.json')) as
                  ReturnType<typeof spatialData>[ 'graph' ];
    assert.equal(index.version, 1);
    assert.equal(index.dimensions, manifest.nativeConfig.dimensions);
    assert.equal(index.points.length, manifest.centroids);
    assert.equal(graph.length, manifest.centroids);
    return {index, graph};
}
