/** Offline evaluation of the shipped corpora, optionally auditing frozen catalog artifacts. */
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { generateNativeModel, inspectNativeContents, inspectNativeModel, matchNativePatterns, mergeNativeModels, trainNativeModel } from '../native.ts';
import { asciiTokens, groupedScores, referenceModel, type Prediction, type Probe } from './metrics.ts';

const root = fileURLToPath(new URL('../../../../', import.meta.url));
const sha256 = (bytes: string | Uint8Array) => createHash('sha256').update(bytes).digest('hex');
const config = { dimensions: 32, centroidCount: 24, contextWindow: 3, seed: 0xC3A17D5EEDn };
const generation = { maxTokens: 12, temperature: 0, seed: '42' };

interface Suite {
    version: number;
    description: string;
    sources: { name: string; path: string }[];
    probes: Probe[];
}

function validateSuite(suite: Suite) {
    if (suite.version !== 1 || !suite.sources.length || !suite.probes.length) throw new Error('Invalid suite');
    const ids = new Set<string>();
    for (const probe of suite.probes) {
        if (ids.has(probe.id) || !asciiTokens(probe.prompt).length) throw new Error(`Invalid probe: ${probe.id}`);
        ids.add(probe.id);
        if (probe.slice === 'unknown') {
            if (probe.expected !== undefined) throw new Error('Unknown probes cannot have a target');
        } else if (!probe.expected || asciiTokens(probe.expected).length !== 1 || asciiTokens(probe.expected)[0] !== probe.expected) {
            throw new Error(`Expected one normalized target token: ${probe.id}`);
        }
    }
}

function evaluateNative(name: string, payload: Buffer, probes: Probe[]) {
    const checksumSha256 = sha256(payload);
    const metadata = inspectNativeModel(payload);
    const summary = inspectNativeContents(payload, 'summary');
    const vocabulary = new Set<string>();
    for (let offset = 0; offset < Number(metadata.vocabularySize); offset += 100) {
        for (const item of inspectNativeContents(payload, 'vocabulary', offset, 100).items) vocabulary.add(item.token);
    }
    const rows = probes.map((probe) => {
        const result = matchNativePatterns(payload, probe.prompt, 1);
        const nearest = result.matches[0];
        const continuation = generateNativeModel(payload, probe.prompt, generation.maxTokens, generation.temperature, BigInt(generation.seed));
        return {
            probe,
            ranked: nearest?.targets.items.slice(0, 5).map((target) => target.token) ?? [],
            unknownTokens: result.unknownTokens,
            contextTokens: result.context.length,
            ...(probe.expected === undefined ? {} : { expectedInVocabulary: vocabulary.has(probe.expected) }),
            centroidId: nearest?.centroidId ?? null,
            squaredDistance: nearest?.squaredDistance ?? null,
            continuation,
            deterministic: continuation === generateNativeModel(payload, probe.prompt, generation.maxTokens, generation.temperature, BigInt(generation.seed)),
        };
    });
    if (sha256(payload) !== checksumSha256 || rows.some((row) => !row.deterministic)) throw new Error(`${name}: nondeterministic or mutated artifact`);
    return { name, kind: 'cgai', checksumSha256, artifactBytes: payload.length, metadata, summary, scores: groupedScores(rows), rows };
}

/** Enforce the application's artifact cap even if Content-Length is absent. */
async function downloadArtifact(url: string) {
    const response = await fetch(url, { signal: AbortSignal.timeout(30_000) });
    if (!response.ok || !response.body) throw new Error(`Download HTTP ${response.status}`);
    const chunks: Uint8Array[] = [];
    let size = 0;
    for await (const chunk of response.body) {
        size += chunk.length;
        if (size > 64 * 1024 * 1024) throw new Error('Artifact exceeds 64 MiB');
        chunks.push(chunk);
    }
    const payload = Buffer.concat(chunks);
    const etag = response.headers.get('etag');
    if (etag && etag !== `"${sha256(payload)}"`) throw new Error('Artifact checksum differs from its ETag');
    return payload;
}

async function main() {
    const { values } = parseArgs({ options: { catalog: { type: 'string' }, output: { type: 'string' } } });
    const output = resolve(values.output ?? resolve(root, 'build/evaluation/baseline.json'));
    const suite = JSON.parse(await readFile(resolve(root, 'examples/evaluation/pattern-baseline-v1.json'), 'utf8')) as Suite;
    validateSuite(suite);
    const corpora = await Promise.all(suite.sources.map(async (source) => {
        const text = (await readFile(resolve(root, source.path), 'utf8')).replace(/\r\n/g, '\n');
        asciiTokens(text);
        return { ...source, text, sha256: sha256(text) };
    }));
    const errors: { name: string; error: string }[] = [];
    const models: ReturnType<typeof evaluateNative>[] = [];
    const referenceResults: { name: string; kind: string; scores: ReturnType<typeof groupedScores>; rows: Prediction[] }[] = [];
    const sources = corpora.map((corpus) => ({ name: corpus.name, payload: trainNativeModel(corpus.text, config) }));
    const artifacts = [
        ...sources,
        { name: 'superset-preserve', payload: mergeNativeModels(sources.map((source) => source.payload)) },
        { name: 'superset-compact-24', payload: mergeNativeModels(sources.map((source) => source.payload), 24) },
        { name: 'joint-24', payload: trainNativeModel(corpora.map((corpus) => corpus.text).join('\n'), config) },
        { name: 'joint-72', payload: trainNativeModel(corpora.map((corpus) => corpus.text).join('\n'), { ...config, centroidCount: 72 }) },
    ];
    for (const artifact of artifacts) models.push(evaluateNative(artifact.name, artifact.payload, suite.probes));
    for (const corpus of [...corpora.map((item) => ({ name: item.name, texts: [item.text] })), { name: 'all-sources', texts: corpora.map((item) => item.text) }]) {
        for (const window of [0, 3]) {
            const predict = referenceModel(corpus.texts, window);
            const rows = suite.probes.map((probe) => ({ probe, ranked: predict(probe.prompt).slice(0, 5) }));
            referenceResults.push({ name: `${corpus.name}/${window === 0 ? 'unigram' : 'backoff-3'}`, kind: 'reference', scores: groupedScores(rows), rows });
        }
    }
    if (values.catalog) {
        const endpoint = new URL('/api/v1/models', values.catalog);
        const response = await fetch(endpoint, { signal: AbortSignal.timeout(30_000) });
        if (!response.ok) throw new Error(`Catalog HTTP ${response.status}`);
        const catalog = await response.json() as { name: string }[];
        for (const model of catalog) {
            try {
                const payload = await downloadArtifact(`${endpoint}/${encodeURIComponent(model.name)}`);
                models.push(evaluateNative(`catalog/${model.name}`, payload, suite.probes));
            } catch (error) {
                errors.push({ name: model.name, error: String(error) });
            }
        }
    }
    let revision = 'unavailable';
    try { revision = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim(); } catch { /* Archives need no Git. */ }
    const report = {
        version: 1, createdAt: new Date().toISOString(), revision,
        environment: { node: process.version, platform: process.platform, arch: process.arch },
        suiteSha256: sha256(JSON.stringify(suite)), suite,
        config, generation, corpusNormalization: 'CRLF to LF',
        sources: corpora.map(({ text: _text, ...source }) => source),
        models, references: referenceResults, errors,
    };
    await mkdir(dirname(output), { recursive: true });
    await writeFile(output, `${JSON.stringify(report, (_, value: unknown) => typeof value === 'bigint' ? value.toString() : value, 2)}\n`);
    console.table([...models, ...referenceResults].map((model) => ({
        model: model.name,
        'recall top1': model.scores['slice:recall']?.top1,
        'challenge top1': model.scores['slice:challenge']?.top1,
        'challenge top5': model.scores['slice:challenge']?.top5,
    })));
    console.log(`Report: ${output}`);
    for (const error of errors) console.error(`${error.name}: ${error.error}`);
    if (errors.length) process.exitCode = 1;
}

await main();
