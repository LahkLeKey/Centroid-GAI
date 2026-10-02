/** Compact, source-free scholarly model artifacts for source control. */
import { createHash } from 'node:crypto';
import { readFile, stat, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { nativeScholarlyCandidate, nativeScholarlyMetrics, type NativeScholarlyMetrics } from './scholarly-native.ts';
import type { ScholarlyCorpus } from './scholarly-types.ts';
import type { ScholarlyHead } from './scholarly-workflow.ts';

export type ScholarlyArtifactIdentity = Omit<ScholarlyHead, 'manifestSha256'>;
export interface ScholarlyPublishedHead extends ScholarlyHead {
    readonly binarySha256: string;
    readonly artifactManifestSha256: string;
}
const hash = (value: string | Buffer) => createHash('sha256').update(value).digest('hex');
const object = (value: unknown): value is Record<string, unknown> => !!value && typeof value === 'object' && !Array.isArray(value);
const sha256 = (value: unknown): value is string => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const identityKeys = ['version', 'releaseId', 'parentReleaseId', 'corpusSha256', 'benchmarkSha256', 'checkpointSha256',
    'nativeSha256', 'learningRate', 'epochs', 'steps', 'promotedAt'] as const;
const artifactFiles = ['checkpoint.txt', 'model.cgnn', 'citations.tsv', 'metrics.tsv'] as const;
const limits = { 'checkpoint.txt': 256 * 1024 * 1024, 'model.cgnn': 256 * 1024 * 1024,
    'citations.tsv': 2 * 1024 * 1024, 'metrics.tsv': 32 * 1024, 'release.tsv': 32 * 1024 };
type Pair = readonly [string, string];

function escaped(value: string): string {
    if (value.includes('\0')) throw new Error('NUL is not allowed in scholarly artifact metadata');
    return value.replace(/\\/g, '\\\\').replace(/\t/g, '\\t').replace(/\r/g, '\\r').replace(/\n/g, '\\n');
}
function unescaped(value: string): string {
    if (value.includes('\0') || /[\r\n\t]/.test(value)) throw new Error('invalid scholarly TSV value');
    let result = '';
    for (let index = 0; index < value.length; index++) {
        const character = value[index]!;
        if (character !== '\\') { result += character; continue; }
        const next = value[++index];
        if (next === '\\') result += '\\';
        else if (next === 't') result += '\t';
        else if (next === 'r') result += '\r';
        else if (next === 'n') result += '\n';
        else throw new Error('invalid scholarly TSV escape');
    }
    return result;
}
function tsv(pairs: readonly Pair[]): string {
    const keys = new Set<string>();
    return pairs.map(([key, value]) => {
        if (!/^[a-zA-Z0-9][a-zA-Z0-9_.-]*$/.test(key) || keys.has(key)) throw new Error('duplicate or invalid scholarly TSV key');
        keys.add(key); return `${key}\t${escaped(value)}\n`;
    }).join('');
}
function fields(text: string, maximum = 32 * 1024): Record<string, string> {
    if (Buffer.byteLength(text) > maximum || !text.endsWith('\n') || text.includes('\r')) throw new Error('invalid scholarly TSV document');
    const values: Record<string, string> = Object.create(null);
    for (const line of text.slice(0, -1).split('\n')) {
        const parts = line.split('\t');
        if (parts.length !== 2 || !/^[a-zA-Z0-9][a-zA-Z0-9_.-]*$/.test(parts[0]!) || Object.hasOwn(values, parts[0]!))
            throw new Error('duplicate or invalid scholarly TSV field');
        values[parts[0]!] = unescaped(parts[1]!);
    }
    return values;
}
function exactKeys(value: Record<string, string>, expected: readonly string[]): void {
    if (Object.keys(value).length !== expected.length || expected.some(key => !Object.hasOwn(value, key)))
        throw new Error('invalid scholarly TSV schema');
}
function number(value: string | undefined): number {
    if (value === undefined || !Number.isFinite(Number(value)) || Number(value).toString() !== value)
        throw new Error('invalid scholarly TSV number');
    return Number(value);
}
function boundedText(value: unknown, maximum = 4096): string {
    if (typeof value !== 'string' || !value || value.length > maximum || value.includes('\0'))
        throw new Error('invalid scholarly citation metadata');
    return value;
}
function identity(value: unknown): ScholarlyArtifactIdentity {
    if (!object(value) || value.version !== 1 || !sha256(value.releaseId) ||
        !(value.parentReleaseId === null || sha256(value.parentReleaseId)) || !sha256(value.corpusSha256) ||
        !sha256(value.benchmarkSha256) || !sha256(value.checkpointSha256) || !sha256(value.nativeSha256) ||
        typeof value.learningRate !== 'number' || !Number.isFinite(value.learningRate) || value.learningRate <= 0 || value.learningRate > 1 ||
        !Number.isSafeInteger(value.epochs) || (value.epochs as number) < 1 || !Number.isSafeInteger(value.steps) || (value.steps as number) < 2 ||
        typeof value.promotedAt !== 'string' || !Number.isFinite(Date.parse(value.promotedAt)) ||
        new Date(value.promotedAt).toISOString() !== value.promotedAt) throw new Error('invalid scholarly artifact identity');
    return value as unknown as ScholarlyArtifactIdentity;
}
function identityPairs(value: ScholarlyArtifactIdentity): Pair[] {
    identity(value);
    return identityKeys.map(key => [key, key === 'parentReleaseId' && value[key] === null ? '-' : String(value[key])]);
}
function parsedIdentity(value: Record<string, string>): ScholarlyArtifactIdentity {
    return identity({ version: number(value.version), releaseId: value.releaseId,
        parentReleaseId: value.parentReleaseId === '-' ? null : value.parentReleaseId,
        corpusSha256: value.corpusSha256, benchmarkSha256: value.benchmarkSha256, checkpointSha256: value.checkpointSha256,
        nativeSha256: value.nativeSha256, learningRate: number(value.learningRate), epochs: number(value.epochs),
        steps: number(value.steps), promotedAt: value.promotedAt });
}

/** The durable head contains hashes and training counters, never captured source bodies. */
export function serializeScholarlyPublishedHead(head: ScholarlyPublishedHead): string {
    identity(head);
    if (!sha256(head.manifestSha256) || !sha256(head.binarySha256) || !sha256(head.artifactManifestSha256))
        throw new Error('invalid scholarly published head hash');
    return tsv([['format', 'scholarly-head-v1'], ...identityPairs(head), ['manifestSha256', head.manifestSha256],
        ['binarySha256', head.binarySha256], ['artifactManifestSha256', head.artifactManifestSha256]]);
}
export function parseScholarlyPublishedHead(text: string): ScholarlyPublishedHead {
    const value = fields(text);
    exactKeys(value, ['format', ...identityKeys, 'manifestSha256', 'binarySha256', 'artifactManifestSha256']);
    if (value.format !== 'scholarly-head-v1' || !sha256(value.manifestSha256) || !sha256(value.binarySha256) ||
        !sha256(value.artifactManifestSha256)) throw new Error('invalid scholarly published head');
    return { ...parsedIdentity(value), manifestSha256: value.manifestSha256, binarySha256: value.binarySha256,
        artifactManifestSha256: value.artifactManifestSha256 };
}

async function bytes(path: string, maximum: number, signal?: AbortSignal): Promise<Buffer> {
    signal?.throwIfAborted();
    const information = await stat(path);
    if (!information.isFile() || information.size < 1 || information.size > maximum) throw new Error('scholarly artifact file exceeds its size limit');
    const value = await readFile(path, { signal });
    if (value.length < 1 || value.length > maximum) throw new Error('scholarly artifact file exceeds its size limit');
    return value;
}
/** Existing immutable metadata may be reused only when every byte agrees. */
async function immutableText(directory: string, name: 'citations.tsv' | 'metrics.tsv' | 'release.tsv', value: string,
    signal: AbortSignal): Promise<void> {
    signal.throwIfAborted();
    if (Buffer.byteLength(value) > limits[name]) throw new Error('scholarly artifact metadata exceeds its size limit');
    try { await writeFile(join(directory, name), value, { flag: 'wx', encoding: 'utf8', signal }); }
    catch (error) {
        if (!object(error) || error.code !== 'EEXIST') throw error;
        if (!(await bytes(join(directory, name), limits[name], signal)).equals(Buffer.from(value)))
            throw new Error(`immutable scholarly artifact differs: ${name}`);
    }
}
function citations(corpus: ScholarlyCorpus): string {
    const pairs: Pair[] = [['format', 'scholarly-citations-v1'], ['policy', 'scholarly-attribution-v1'],
        ['scientificTruth', 'not-assessed'], ['papers', String(corpus.papers.length)]];
    const papers = [...corpus.papers].sort((first, second) => first.doi < second.doi ? -1 : first.doi > second.doi ? 1 : 0);
    for (const [index, paper] of papers.entries()) {
        const prefix = `paper.${index}`;
        if (!Number.isSafeInteger(paper.year) || paper.year < 1 || paper.year > 9999 || !Array.isArray(paper.authors) ||
            paper.authors.length < 1 || paper.authors.length > 256 || !sha256(paper.metadata.sha256) ||
            !sha256(paper.fullText.sha256) || !sha256(paper.crossref.snapshot.sha256) ||
            paper.verification.policy !== 'scholarly-attribution-v1' || paper.verification.scientificTruth !== 'not-assessed')
            throw new Error('invalid scholarly citation provenance');
        pairs.push([`${prefix}.doi`, boundedText(paper.doi, 512)], [`${prefix}.title`, boundedText(paper.title)],
            [`${prefix}.year`, String(paper.year)], [`${prefix}.license`, boundedText(paper.licenseUrl, 2048)],
            [`${prefix}.sourceSha256`, paper.fullText.sha256], [`${prefix}.metadataSha256`, paper.metadata.sha256],
            [`${prefix}.registrySha256`, paper.crossref.snapshot.sha256], [`${prefix}.authors`, String(paper.authors.length)]);
        paper.authors.forEach((author, authorIndex) => pairs.push([`${prefix}.author.${authorIndex}`, boundedText(author, 2048)]));
        const references = [...paper.verification.citedReferences].sort((first, second) => first.doi < second.doi ? -1 : first.doi > second.doi ? 1 : 0);
        if (references.length < 2 || references.length > 8) throw new Error('invalid scholarly certified reference count');
        pairs.push([`${prefix}.references`, String(references.length)]);
        references.forEach((reference, referenceIndex) => {
            if (!sha256(reference.metadataSha256)) throw new Error('invalid scholarly certified reference hash');
            pairs.push([`${prefix}.reference.${referenceIndex}.doi`, boundedText(reference.doi, 512)],
                [`${prefix}.reference.${referenceIndex}.metadataSha256`, reference.metadataSha256]);
        });
    }
    return tsv(pairs);
}
function metricPairs(prefix: string, value: NativeScholarlyMetrics): Pair[] {
    const parsed = nativeScholarlyMetrics(value);
    return (['tokens', 'unknownTokens', 'crossEntropy', 'accuracy'] as const).map(key => [`${prefix}.${key}`, String(parsed[key])]);
}
function metrics(quality: unknown, expected: ScholarlyArtifactIdentity): string {
    if (!object(quality) || quality.version !== 1 || quality.passed !== true || !Array.isArray(quality.failures) || quality.failures.length !== 0 ||
        quality.corpusSha256 !== expected.corpusSha256 || quality.benchmarkSha256 !== expected.benchmarkSha256 ||
        quality.nativeSha256 !== expected.nativeSha256 || quality.scope !== 'attributed-source-excerpts-and-next-token-regression' ||
        quality.scientificTruth !== 'not-assessed' || !['continue', 'rebuild-training-vocabulary'].includes(String(quality.mode)) ||
        !object(quality.before) || !object(quality.after) || !object(quality.replay) || quality.replay.required !== true ||
        quality.replay.verified !== true || quality.replay.sha256 !== expected.checkpointSha256) throw new Error('invalid scholarly accepted quality report');
    const candidate = nativeScholarlyCandidate(quality.candidate);
    if (candidate.epochs !== expected.epochs || candidate.steps !== expected.steps) throw new Error('scholarly artifact training counters differ');
    const beforeDevelopment = nativeScholarlyMetrics(quality.before.development), beforeTest = nativeScholarlyMetrics(quality.before.test);
    const afterDevelopment = nativeScholarlyMetrics(quality.after.development), afterTest = nativeScholarlyMetrics(quality.after.test);
    if (JSON.stringify(afterDevelopment) !== JSON.stringify(candidate.after.development)) throw new Error('scholarly artifact development metrics differ');
    return tsv([['format', 'scholarly-metrics-v1'], ['scope', quality.scope], ['scientificTruth', 'not-assessed'], ['passed', 'true'],
        ['mode', String(quality.mode)], ['epochs', String(candidate.epochs)], ['steps', String(candidate.steps)],
        ['replay.verified', 'true'], ['replay.sha256', expected.checkpointSha256],
        ...metricPairs('before.development', beforeDevelopment), ...metricPairs('after.development', afterDevelopment),
        ...metricPairs('before.test', beforeTest), ...metricPairs('after.test', afterTest),
        ...metricPairs('candidate.before.training', candidate.before.training), ...metricPairs('candidate.after.training', candidate.after.training)]);
}

/** Export only compact metadata; the caller creates model.cgnn through the C11 native exporter first. */
export async function createScholarlyArtifacts(directory: string, value: ScholarlyArtifactIdentity, corpus: ScholarlyCorpus,
    quality: unknown, signal: AbortSignal): Promise<{ binarySha256: string; artifactManifestSha256: string }> {
    const expected = identity(value);
    if (corpus.version !== 1 || corpus.policy !== 'scholarly-attribution-v1' || corpus.sha256 !== expected.corpusSha256 ||
        !Array.isArray(corpus.papers) || corpus.papers.length < 6 || corpus.papers.length > 48 ||
        !Array.isArray(corpus.passages) || corpus.passages.length < 1 || corpus.passages.length > 1536)
        throw new Error('invalid scholarly artifact corpus');
    const checkpoint = await bytes(join(directory, 'checkpoint.txt'), limits['checkpoint.txt'], signal);
    if (hash(checkpoint) !== expected.checkpointSha256) throw new Error('scholarly artifact checkpoint hash mismatch');
    const binarySha256 = hash(await bytes(join(directory, 'model.cgnn'), limits['model.cgnn'], signal));
    const citationText = citations(corpus), metricText = metrics(quality, expected);
    const fileHashes = { 'checkpoint.txt': expected.checkpointSha256, 'model.cgnn': binarySha256,
        'citations.tsv': hash(citationText), 'metrics.tsv': hash(metricText) };
    const splitCounts = (['train', 'development', 'test'] as const).map(split => {
        const count = corpus.splits[split].length;
        if (!Number.isSafeInteger(count) || count < 1 || count > corpus.passages.length) throw new Error('invalid scholarly artifact split count');
        return [`passages.${split}`, String(count)] as Pair;
    });
    if (corpus.splits.train.length + corpus.splits.development.length + corpus.splits.test.length !== corpus.passages.length)
        throw new Error('scholarly artifact split counts differ');
    const releaseText = tsv([['format', 'scholarly-release-v1'], ...identityPairs(expected), ['policy', 'scholarly-attribution-v1'],
        ['scientificTruth', 'not-assessed'], ['papers', String(corpus.papers.length)], ['passages', String(corpus.passages.length)],
        ...splitCounts, ...artifactFiles.map(name => [`file.${name}.sha256`, fileHashes[name]] as Pair)]);
    await immutableText(directory, 'citations.tsv', citationText, signal);
    await immutableText(directory, 'metrics.tsv', metricText, signal);
    await immutableText(directory, 'release.tsv', releaseText, signal);
    return { binarySha256, artifactManifestSha256: hash(releaseText) };
}

/** Verify the allowlisted Git artifacts without requiring raw sources or evaluation text. */
export async function verifyScholarlyArtifacts(directory: string, head: ScholarlyPublishedHead): Promise<void> {
    serializeScholarlyPublishedHead(head);
    const releaseBytes = await bytes(join(directory, 'release.tsv'), limits['release.tsv']);
    if (hash(releaseBytes) !== head.artifactManifestSha256) throw new Error('scholarly artifact manifest hash mismatch');
    const release = fields(releaseBytes.toString('utf8'));
    exactKeys(release, ['format', ...identityKeys, 'policy', 'scientificTruth', 'papers', 'passages', 'passages.train',
        'passages.development', 'passages.test', ...artifactFiles.map(name => `file.${name}.sha256`)]);
    if (release.format !== 'scholarly-release-v1' || release.policy !== 'scholarly-attribution-v1' || release.scientificTruth !== 'not-assessed')
        throw new Error('invalid scholarly artifact manifest');
    const parsed = parsedIdentity(release);
    if (identityKeys.some(key => parsed[key] !== head[key]) || release['file.checkpoint.txt.sha256'] !== head.checkpointSha256 ||
        release['file.model.cgnn.sha256'] !== head.binarySha256) throw new Error('scholarly artifact identity mismatch');
    for (const [key, minimum, maximum] of [['papers', 6, 48], ['passages', 1, 1536], ['passages.train', 1, 1536],
        ['passages.development', 1, 1536], ['passages.test', 1, 1536]] as const) {
        const count = number(release[key]);
        if (!Number.isSafeInteger(count) || count < minimum || count > maximum) throw new Error('invalid scholarly artifact manifest count');
    }
    if (number(release['passages.train']) + number(release['passages.development']) + number(release['passages.test']) !== number(release.passages))
        throw new Error('scholarly artifact split counts differ');
    for (const name of artifactFiles) {
        const expected = release[`file.${name}.sha256`];
        if (!sha256(expected) || hash(await bytes(join(directory, name), limits[name])) !== expected)
            throw new Error(`scholarly artifact hash mismatch: ${name}`);
    }
}
