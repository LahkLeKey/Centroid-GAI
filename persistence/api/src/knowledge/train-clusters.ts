/** Offline deterministic cluster training and verification of Git-sized encyclopedia artifacts. */
import {createHash} from 'node:crypto';
import {existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync} from 'node:fs';
import {arch, endianness} from 'node:os';
import {dirname, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {parseArgs} from 'node:util';
import {gunzipSync, gzipSync} from 'node:zlib';

import {
    generateNativeModel,
    inspectNativeContents,
    inspectNativeModel,
    trainNativeModel
} from '../native.ts';

const root = fileURLToPath(new URL('../../../../', import.meta.url));
const fileLimit = 1024 * 1024;
const releaseLimit = 128 * 1024 * 1024;
const config = {
    dimensions : 32,
    centroidCount : 16,
    contextWindow : 3,
    seed : 42n
};
const sha256 = (value: string|Uint8Array) => createHash('sha256').update(value).digest('hex');
const json = (value: unknown) =>
    `${JSON.stringify(value, (_, item) => typeof item === 'bigint' ? item.toString() : item)}\n`;

interface SourceShard {
    id: string;
    cluster: string;
    path: string;
    articles: number;
    sha256: string;
    content_sha256: string
}
interface SourceManifest {
    articles: number;
    source: {revision: string; sha256 : string};
    shards: SourceShard[]
}
interface Article {
    id: string;
    title: string;
    text: string
}
interface OutputFile {
    path: string;
    bytes: number;
    sha256: string
}
interface TrainedShard {
    id: string;
    cluster: string;
    source: string;
    articles: number;
    corpusSha256: string;
    artifactSha256: string;
    artifactBytes: number;
    model: OutputFile;
    inspection: OutputFile;
    examplesSeen: string;
    initializedCentroids: number
}
interface ModelManifest {
    schemaVersion: number;
    sourceManifestSha256: string;
    modelConfig: object;
    libraryVersion: string;
    architecture: string;
    byteOrder: string;
    deterministicRepeatVerified: boolean;
    articles: number;
    shards: TrainedShard[];
    totalExamplesSeen: string;
    totalCentroids: number
}

function publish(directory: string, path: string, content: Uint8Array|string): OutputFile {
    const bytes = typeof content === 'string' ? Buffer.from(content) : Buffer.from(content);
    if (bytes.length > fileLimit)
        throw new Error(`File exceeds 1 MiB: ${path}`);
    const destination = resolve(directory, path);
    mkdirSync(dirname(destination), {recursive : true});
    if (existsSync(destination)) {
        if (!readFileSync(destination).equals(bytes))
            throw new Error(`Existing output differs: ${path}; use a new release directory`);
    } else
        writeFileSync(destination, bytes, {flag : 'wx'});
    return {path, bytes : bytes.length, sha256 : sha256(bytes)};
}

function sourceText(directory: string, shard: SourceShard) {
    const packed = readFileSync(resolve(directory, shard.path));
    if (sha256(packed) !== shard.sha256)
        throw new Error(`Source checksum mismatch: ${shard.id}`);
    const raw = gunzipSync(packed, {maxOutputLength : 1024 * 1024});
    if (sha256(raw) !== shard.content_sha256)
        throw new Error(`Content checksum mismatch: ${shard.id}`);
    const articles =
        raw.toString('utf8').trim().split('\n').map(line => JSON.parse(line) as Article);
    if (articles.length !== shard.articles)
        throw new Error(`Article count mismatch: ${shard.id}`);
    return {
        articles,
        text : articles.map(article => `${article.title}\n${article.text}`).join('\n\n')
    };
}

function train(directory: string, source: SourceManifest, sourceHash: string) {
    const shards: TrainedShard[] = [];
    let totalExamples = 0n;
    let totalCentroids = 0;
    let libraryVersion = '';
    for (const shard of source.shards) {
        const {articles, text} = sourceText(directory, shard);
        const payload = trainNativeModel(text, config);
        const repeat = trainNativeModel(text, config);
        if (!payload.equals(repeat))
            throw new Error(`Nondeterministic native training: ${shard.id}`);
        const metadata = inspectNativeModel(payload);
        const summary = inspectNativeContents(payload, 'summary');
        libraryVersion = metadata.libraryVersion;
        const centroids =
            Array.from({length : summary.initializedCentroids},
                       (_, id) => inspectNativeContents(payload, 'centroid', 0, 12, id));
        const first = articles[0];
        if (!first)
            throw new Error(`Empty source shard: ${shard.id}`);
        const prompt = first.title;
        const continuation = generateNativeModel(payload, prompt, 16, 0, 42n);
        if (continuation !== generateNativeModel(payload, prompt, 16, 0, 42n))
            throw new Error('Generation changed on repeat');
        const compressed = gzipSync(payload, {level : 9});
        if (!compressed.equals(gzipSync(repeat, {level : 9})))
            throw new Error('Nondeterministic compression');
        const model = publish(directory, `models/${shard.cluster}/${shard.id}.cgai.gz`, compressed);
        const inspection = publish(
            directory, `centroids/${shard.cluster}/${shard.id}.json`, json({
                id : shard.id,
                source : shard.path,
                corpusSha256 : sha256(text),
                artifactSha256 : sha256(payload),
                summary,
                centroids,
                smokeProbe : {prompt, maxTokens : 16, temperature : 0, seed : '42', continuation},
            }));
        shards.push({
            id : shard.id,
            cluster : shard.cluster,
            source : shard.path,
            articles : shard.articles,
            corpusSha256 : sha256(text),
            artifactSha256 : sha256(payload),
            artifactBytes : payload.length,
            model,
            inspection,
            examplesSeen : metadata.examplesSeen.toString(),
            initializedCentroids : summary.initializedCentroids
        });
        totalExamples += metadata.examplesSeen;
        totalCentroids += summary.initializedCentroids;
        if (shards.length % 10 === 0)
            console.log(
                `Trained and repeat-verified ${shards.length}/${source.shards.length} shards`);
    }
    const manifest: ModelManifest = {
        schemaVersion : 1,
        sourceManifestSha256 : sourceHash,
        modelConfig : {...config, seed : config.seed.toString()},
        libraryVersion,
        architecture : arch(),
        byteOrder : endianness(),
        deterministicRepeatVerified : true,
        articles : source.articles,
        shards,
        totalExamplesSeen : totalExamples.toString(),
        totalCentroids
    };
    publish(directory, 'models.json', json(manifest));
    return manifest;
}

function verify(directory: string, source: SourceManifest, sourceHash: string) {
    const manifest =
        JSON.parse(readFileSync(resolve(directory, 'models.json'), 'utf8')) as ModelManifest;
    if (manifest.schemaVersion !== 1 || manifest.sourceManifestSha256 !== sourceHash ||
        manifest.shards.length !== source.shards.length)
        throw new Error('Model/source manifest mismatch');
    let examples = 0n;
    let centroids = 0;
    for (const [index, shard] of manifest.shards.entries()) {
        const sourceShard = source.shards[index];
        if (!sourceShard || shard.id !== sourceShard.id)
            throw new Error('Shard ordering mismatch');
        for (const file of [shard.model, shard.inspection]) {
            const bytes = readFileSync(resolve(directory, file.path));
            if (bytes.length !== file.bytes || sha256(bytes) !== file.sha256)
                throw new Error(`Output checksum mismatch: ${file.path}`);
        }
        const payload = gunzipSync(readFileSync(resolve(directory, shard.model.path)),
                                   {maxOutputLength : 32 * 1024 * 1024});
        if (payload.length !== shard.artifactBytes || sha256(payload) !== shard.artifactSha256)
            throw new Error('Native artifact checksum mismatch');
        const metadata = inspectNativeModel(payload);
        if (metadata.examplesSeen.toString() !== shard.examplesSeen)
            throw new Error('Native example count mismatch');
        const {text} = sourceText(directory, sourceShard);
        if (sha256(text) !== shard.corpusSha256)
            throw new Error('Training corpus checksum mismatch');
        examples += metadata.examplesSeen;
        centroids += inspectNativeContents(payload, 'summary').initializedCentroids;
    }
    if (examples.toString() !== manifest.totalExamplesSeen || centroids !== manifest.totalCentroids)
        throw new Error('Aggregate model count mismatch');
    return manifest;
}

function sizeAudit(directory: string) {
    const files = readdirSync(directory, {recursive : true, withFileTypes : true})
                      .filter(entry => entry.isFile())
                      .map(entry => resolve(entry.parentPath, entry.name));
    let total = 0;
    let largest = 0;
    for (const file of files) {
        const size = statSync(file).size;
        if (size > fileLimit)
            throw new Error(`File exceeds 1 MiB: ${file}`);
        largest = Math.max(largest, size);
        total += size;
    }
    if (total > releaseLimit)
        throw new Error('Release exceeds 128 MiB');
    return {files : files.length, bytes : total, largestFileBytes : largest};
}

const {values} =
    parseArgs({options : {directory : {type : 'string'}, verify : {type : 'boolean'}}});
const directory = resolve(values.directory ?? resolve(root, 'build/knowledge/rebuilt-release'));
const sourceBytes = readFileSync(resolve(directory, 'sources.json'));
const source = JSON.parse(sourceBytes.toString('utf8')) as SourceManifest;
sizeAudit(directory);
const manifest = values.verify ? verify(directory, source, sha256(sourceBytes))
                               : train(directory, source, sha256(sourceBytes));
console.log(json({
    articles : manifest.articles,
    shards : manifest.shards.length,
    centroids : manifest.totalCentroids,
    trainingExamples : manifest.totalExamplesSeen,
    deterministicRepeatVerified : manifest.deterministicRepeatVerified,
    ...sizeAudit(directory)
}));
