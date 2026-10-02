/** Content-bound review and reproducible offline training releases. */
import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import type { ChatTrainRequest } from '../../../shared/chat.ts';
import { recordReviewHash, sourceReviewHash, validateRepositoryDataset, type RepositoryDialogueDataset, type RepositoryDialogueRecord } from '../chat/dataset.ts';
import { validateTrainingValidation } from '../chat/training-quality.ts';
import { validateTrainingPreflight } from './preflight.ts';

export type ReviewPolicy = 'human' | 'agent-or-human';
export interface ReviewDocument {
    version: 1;
    datasetSha256: string;
    reviewer: { kind: 'agent' | 'human'; id: string };
    reviewedAt: string;
    decisions: { id: string; status: 'candidate' | 'approved' | 'rejected'; recordSha256: string; notes: string }[];
}
export interface ReleaseOptions {
    name: string;
    reviewPolicy: ReviewPolicy;
    config: NonNullable<ChatTrainRequest['config']>;
    training: NonNullable<ChatTrainRequest['training']>;
}
const splits = ['train', 'development', 'test'] as const;
const files = ['dataset.json', 'sources.json', 'split-assignments.json', 'train-request.json'] as const;
type ReleaseFile = typeof files[number];
const json = (value: unknown) => JSON.stringify(value, null, 2) + '\n';
export const sha256 = (value: string | Buffer) => createHash('sha256').update(value).digest('hex');
const digest = (value: unknown) => sha256(JSON.stringify(value));
const records = (dataset: RepositoryDialogueDataset) => splits.flatMap(split => dataset[split]);

export const defaultReleaseOptions: ReleaseOptions = {
    name: 'repository-engineering-v1', reviewPolicy: 'human',
    config: { embeddingDimensions: 8, hiddenDimensions: 16, centroidCount: 16,
        promptWindow: 192, responseWindow: 64, evidenceWindow: 128, seed: '42' },
    training: { epochs: 20, learningRate: 0.005, seed: '42' },
};

/** The template records no approval. Reviewers explicitly edit individual decisions. */
export function createReviewTemplate(value: unknown, reviewer: ReviewDocument['reviewer'], reviewedAt = new Date().toISOString()): ReviewDocument {
    const { dataset, hashes } = validateRepositoryDataset(value);
    return { version: 1, datasetSha256: hashes.corpus!, reviewer, reviewedAt,
        decisions: records(dataset).map(record => ({ id: record.id, status: 'candidate', recordSha256: recordReviewHash(record), notes: 'Awaiting review.' })) };
}

/** Apply a review only to the exact corpus and example snapshot the reviewer saw. */
export function applyReviews(value: unknown, review: ReviewDocument): RepositoryDialogueDataset {
    const { dataset, hashes } = validateRepositoryDataset(value);
    if (!review || review.version !== 1 || review.datasetSha256 !== hashes.corpus || !Array.isArray(review.decisions) || !review.decisions.length)
        throw new Error('review must identify this exact dataset and contain decisions');
    const result = structuredClone(dataset);
    const byId = new Map(records(result).map(record => [record.id, record]));
    const sourceHashes = new Map(result.sourceDocuments.map(source => [source.id, source.provenance.sha256]));
    const sourceBindings = new Map(result.sourceDocuments.map(source => [source.id, sourceReviewHash(source)]));
    const visited = new Set<string>();
    for (const decision of review.decisions) {
        const record = byId.get(decision?.id);
        if (!record || visited.has(decision.id) || decision.recordSha256 !== recordReviewHash(record))
            throw new Error('unknown, duplicate, or stale review decision');
        visited.add(decision.id);
        if (!['candidate', 'approved', 'rejected'].includes(decision.status) || typeof decision.notes !== 'string' ||
            (decision.status !== 'candidate' && !decision.notes.trim())) throw new Error('review decisions require a status and notes');
        const replacement: RepositoryDialogueRecord = { ...record, review: { status: decision.status, reviewer: review.reviewer,
            reviewedAt: review.reviewedAt, recordSha256: decision.recordSha256, notes: decision.notes,
            split: splits.find(split => result[split].some(item => item.id === record.id))!,
            sourceSha256: Object.fromEntries(record.sources.map(id => [id, sourceHashes.get(id)!])),
            sourceBindingsSha256: Object.fromEntries(record.sources.map(id => [id, sourceBindings.get(id)!])) } };
        for (const split of splits) {
            const index = result[split].findIndex(item => item.id === decision.id);
            if (index !== -1) result[split][index] = replacement;
        }
    }
    return validateRepositoryDataset(result).dataset;
}

function checkedOptions(options: ReleaseOptions): ReleaseOptions {
    if (!options || typeof options.name !== 'string' || !/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$/.test(options.name) || !['human', 'agent-or-human'].includes(options.reviewPolicy))
        throw new Error('release name and explicit review policy required');
    if (!options.config || typeof options.config !== 'object' || Array.isArray(options.config) || !options.training || typeof options.training !== 'object' || Array.isArray(options.training))
        throw new Error('config and training must be objects');
    const bounded = (value: unknown, low: number, high: number, integer: boolean) =>
        typeof value === 'number' && Number.isFinite(value) && value >= low && value <= high && (!integer || Number.isInteger(value));
    for (const [key, value] of Object.entries(options.config ?? {})) {
        if (key === 'seed') { if (typeof value !== 'string' || !/^\d{1,20}$/.test(value) || BigInt(value) > 18446744073709551615n) throw new Error('invalid config seed'); }
        else if (key === 'routingTemperature') { if (!bounded(value, 0.01, 100, false)) throw new Error('invalid routing temperature'); }
        else if (!['embeddingDimensions', 'hiddenDimensions', 'centroidCount', 'promptWindow', 'responseWindow', 'evidenceWindow'].includes(key) ||
            !bounded(value, key === 'evidenceWindow' ? 0 : 1, key === 'embeddingDimensions' ? 64 : ['hiddenDimensions', 'centroidCount'].includes(key) ? 128 : 256, true)) throw new Error(`invalid model setting: ${key}`);
    }
    const prompt = options.config?.promptWindow ?? 160, response = options.config?.responseWindow ?? 32;
    if (prompt + response > 256 || prompt < 4 || (options.config?.evidenceWindow ?? 0) > prompt - 4)
        throw new Error('model context/evidence windows exceed native limits');
    for (const [key, value] of Object.entries(options.training ?? {})) {
        if (key === 'seed') { if (typeof value !== 'string' || !/^\d{1,20}$/.test(value) || BigInt(value) > 18446744073709551615n) throw new Error('invalid training seed'); }
        else if (key === 'epochs') { if (!bounded(value, 1, 10000, true)) throw new Error('invalid epochs'); }
        else if (key !== 'learningRate' || !bounded(value, 0.000001, 1, false)) throw new Error('invalid training setting');
    }
    return structuredClone(options);
}

/** Only train and development enter the API request; final test labels stay offline. */
export function buildTrainingRequest(dataset: RepositoryDialogueDataset, options: ReleaseOptions, releaseId: string): ChatTrainRequest {
    const preflight = validateTrainingPreflight(dataset, options.config);
    const validation = validateTrainingValidation(dataset.train, { version: 1, cases: dataset.development.map(record => ({
        id: record.id, family: record.family, sources: record.sources, expected: record.expected,
        messages: record.messages.filter(message => message.role !== 'evidence'),
        acceptedAnswers: record.acceptedAnswers, evidence: record.evidence.map((span, index) => ({
            ...span, id: `${span.id}:${span.startLine}-${span.endLine}:${index}`,
        })),
    })) });
    return { name: options.name, examples: dataset.train, validation, config: options.config, training: options.training,
        provenance: { datasetRelease: releaseId, purpose: dataset.purpose, reviewPolicy: options.reviewPolicy,
            allReviewsDeclaredHuman: records(dataset).every(record => record.review?.reviewer.kind === 'human'), reviewerIdentityVerified: false,
            nativePreflight: { protocolVersion: preflight.protocolVersion, tokenizerVersion: preflight.tokenizerVersion,
                vocabularySize: preflight.vocabularySize, parameterCount: preflight.parameterCount, windows: preflight.windows },
            sourceSnapshot: 'embedded content hashes; repository commit is the workspace base when snapshot=workspace' } };
}

interface ReleaseIdentity {
    parentDatasetSha256: string;
    hashes: Record<string, string>;
    options: ReleaseOptions;
    excluded: { id: string; reason: string }[];
}
export interface ReleaseManifest {
    version: 1;
    releaseId: string;
    createdAt: string;
    identity: ReleaseIdentity;
    files: Record<ReleaseFile, string>;
    counts: Record<typeof splits[number], number>;
    review: { human: number; agent: number; allReviewsDeclaredHuman: boolean; reviewerIdentityVerified: false };
}

function releaseContents(dataset: RepositoryDialogueDataset, options: ReleaseOptions, releaseId: string): Record<ReleaseFile, string> {
    return { 'dataset.json': json(dataset), 'sources.json': json(dataset.sourceDocuments),
        'split-assignments.json': json(splits.flatMap(split => dataset[split].map(record => ({ id: record.id, split, family: record.family, sources: record.sources })))),
        'train-request.json': json(buildTrainingRequest(dataset, options, releaseId)) };
}

/** A new directory is reserved exclusively. An existing release is never overwritten. */
export async function exportDatasetRelease(value: unknown, destination: string, inputOptions: ReleaseOptions = defaultReleaseOptions): Promise<ReleaseManifest> {
    const { dataset, hashes: parentHashes } = validateRepositoryDataset(value);
    const options = checkedOptions(inputOptions);
    const admitted = (record: RepositoryDialogueRecord) => record.review?.status === 'approved' &&
        (options.reviewPolicy === 'agent-or-human' || record.review.reviewer.kind === 'human');
    const filtered = structuredClone(dataset);
    for (const split of splits) {
        filtered[split] = filtered[split].filter(admitted);
        if (!filtered[split].length) throw new Error(`no approved ${split} examples for ${options.reviewPolicy} review policy`);
    }
    const used = new Set(records(filtered).flatMap(record => record.sources));
    filtered.sourceDocuments = filtered.sourceDocuments.filter(source => used.has(source.id));
    const { hashes } = validateRepositoryDataset(filtered);
    const identity: ReleaseIdentity = { parentDatasetSha256: parentHashes.corpus!, hashes, options,
        excluded: records(dataset).filter(record => !admitted(record)).map(record => ({ id: record.id, reason: record.review?.status === 'approved' ? 'human review required' : record.review?.status ?? 'unreviewed' })) };
    const releaseId = digest(identity);
    const contents = releaseContents(filtered, options, releaseId);
    const human = records(filtered).filter(record => record.review?.reviewer.kind === 'human').length;
    const manifest: ReleaseManifest = { version: 1, releaseId, createdAt: new Date().toISOString(), identity,
        files: Object.fromEntries(files.map(file => [file, sha256(contents[file])])) as Record<ReleaseFile, string>,
        counts: Object.fromEntries(splits.map(split => [split, filtered[split].length])) as ReleaseManifest['counts'],
        review: { human, agent: records(filtered).length - human, allReviewsDeclaredHuman: human === records(filtered).length, reviewerIdentityVerified: false } };
    const output = resolve(destination);
    await mkdir(dirname(output), { recursive: true });
    await mkdir(output); // EEXIST is intentional, including for partially written releases.
    for (const file of files) await writeFile(resolve(output, file), contents[file], { flag: 'wx' });
    // The manifest is the completion marker; interrupted exports never appear complete.
    await writeFile(resolve(output, 'manifest.json'), json(manifest), { flag: 'wx' });
    return manifest;
}

/** Verify bytes and rederive the request/splits from the dataset before using a release. */
export async function verifyDatasetRelease(directory: string): Promise<ReleaseManifest> {
    const path = resolve(directory);
    const manifest = JSON.parse(await readFile(resolve(path, 'manifest.json'), 'utf8')) as ReleaseManifest;
    if (manifest.version !== 1 || !manifest.identity || manifest.releaseId !== digest(manifest.identity)) throw new Error('invalid release identity');
    const options = checkedOptions(manifest.identity.options);
    const contents = {} as Record<ReleaseFile, string>;
    for (const file of files) {
        contents[file] = await readFile(resolve(path, file), 'utf8');
        if (sha256(contents[file]) !== manifest.files?.[file]) throw new Error(`release checksum mismatch: ${file}`);
    }
    const { dataset, hashes } = validateRepositoryDataset(JSON.parse(contents['dataset.json']));
    if (digest(hashes) !== digest(manifest.identity.hashes)) throw new Error('release dataset identity mismatch');
    for (const record of records(dataset)) if (record.review?.status !== 'approved' || (options.reviewPolicy === 'human' && record.review.reviewer.kind !== 'human'))
        throw new Error('release contains unapproved examples');
    const expected = releaseContents(dataset, options, manifest.releaseId);
    for (const file of files) if (contents[file] !== expected[file]) throw new Error(`release derivation mismatch: ${file}`);
    const counts = Object.fromEntries(splits.map(split => [split, dataset[split].length]));
    const human = records(dataset).filter(record => record.review?.reviewer.kind === 'human').length;
    if (digest(manifest.counts) !== digest(counts) || manifest.review?.human !== human || manifest.review.agent !== records(dataset).length - human ||
        manifest.review.allReviewsDeclaredHuman !== (human === records(dataset).length) || manifest.review.reviewerIdentityVerified !== false) throw new Error('release review summary mismatch');
    return manifest;
}

/** CLI outputs are new snapshots; callers choose a new filename to revise them. */
export async function writeSnapshot(path: string, value: unknown): Promise<void> {
    await mkdir(dirname(resolve(path)), { recursive: true });
    await writeFile(path, json(value), { flag: 'wx' });
}
