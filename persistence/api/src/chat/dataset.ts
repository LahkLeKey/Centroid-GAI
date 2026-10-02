import { createHash } from 'node:crypto';
import type { ChatExample } from '../../../shared/chat.ts';
import { evidenceUnits, normalizeEvidence } from './evidence.ts';

export interface DialogueRecord extends ChatExample {
    readonly id: string;
    readonly family: string;
    readonly category: 'direct' | 'follow-up' | 'correction' | 'clarification' | 'abstention' | 'citation' | 'distractor' | 'conflict';
    readonly sources: readonly string[];
}
export interface DialogueDataset { version: 1; train: DialogueRecord[]; development: DialogueRecord[]; test: DialogueRecord[] }
export interface DialogueSource {
    readonly id: string;
    readonly text: string;
    readonly provenance: { readonly kind: 'synthetic'; readonly description: string };
}
export interface DialogueEvidence { readonly id: string; readonly excerpt: string; readonly startLine: number; readonly endLine: number }
export interface FactualDialogueRecord extends DialogueRecord {
    readonly expected: 'answer' | 'abstain';
    readonly acceptedAnswers: readonly string[];
    readonly requiredClaims: readonly string[];
    readonly evidence: readonly DialogueEvidence[];
}
export interface FactualDialogueDataset {
    version: 2;
    purpose: 'synthetic-engineering';
    description: string;
    sourceDocuments: DialogueSource[];
    train: FactualDialogueRecord[];
    development: FactualDialogueRecord[];
    test: FactualDialogueRecord[];
}
export interface RepositoryDialogueSource {
    readonly id: string;
    readonly text: string;
    readonly provenance: {
        readonly kind: 'repository';
        readonly repository: string;
        readonly commit: string;
        readonly path: string;
        readonly sha256: string;
        /** Workspace snapshots bind actual bytes independently of the base commit. */
        readonly snapshot: 'workspace' | 'commit';
    };
}
export interface RepositoryDialogueReview {
    readonly status: 'candidate' | 'approved' | 'rejected';
    readonly split: 'train' | 'development' | 'test';
    readonly reviewer: { readonly kind: 'agent' | 'human'; readonly id: string };
    readonly reviewedAt: string;
    readonly recordSha256: string;
    readonly sourceSha256: Readonly<Record<string, string>>;
    readonly sourceBindingsSha256: Readonly<Record<string, string>>;
    readonly notes: string;
}
export interface RepositoryDialogueRecord extends FactualDialogueRecord {
    /** Omitted means unreviewed. An agent review must never be labeled a human review. */
    readonly review?: RepositoryDialogueReview;
}
export interface RepositoryDialogueDataset {
    version: 3;
    purpose: 'repository-engineering';
    description: string;
    sourceDocuments: RepositoryDialogueSource[];
    train: RepositoryDialogueRecord[];
    development: RepositoryDialogueRecord[];
    test: RepositoryDialogueRecord[];
}
const digest = (value: unknown) => createHash('sha256').update(JSON.stringify(value)).digest('hex');
type SupportedDataset = DialogueDataset | FactualDialogueDataset | RepositoryDialogueDataset;
type FactualDataset = FactualDialogueDataset | RepositoryDialogueDataset;
type SourceDocument = DialogueSource | RepositoryDialogueSource;

/** Canonical object-key ordering makes a review independent of JSON formatting and key order. */
function canonical(value: unknown): unknown {
    if (Array.isArray(value)) return value.map(canonical);
    if (value !== null && typeof value === 'object') return Object.fromEntries(Object.entries(value).sort(([left], [right]) => left < right ? -1 : left > right ? 1 : 0)
        .map(([key, item]) => [key, canonical(item)]));
    return value;
}

/** Bind every record field except its review; source binding hashes separately bind bytes and provenance. */
export function recordReviewHash(record: RepositoryDialogueRecord): string {
    const { review: _review, ...content } = record;
    return digest(canonical(content));
}

/** A review applies to this source identity, complete content, and provenance together. */
export function sourceReviewHash(source: RepositoryDialogueSource): string {
    return digest(canonical({ id: source.id, text: source.text, provenance: source.provenance }));
}

/** Freeze families and source partitions before paraphrasing; reject exact normalized leakage. */
export function validateDataset(value: unknown): { dataset: SupportedDataset; hashes: Record<string, string> } {
    if (!value || typeof value !== 'object' || !('version' in value) || ![1, 2, 3].includes(value.version as number)) throw new Error('dataset version must be 1, 2 or 3');
    const dataset = value as SupportedDataset;
    const ids = new Set<string>(), content = new Set<string>();
    const families = new Map<string, string>(), sources = new Map<string, string>();
    for (const split of ['train', 'development', 'test'] as const) {
        const records = dataset[split];
        if (!Array.isArray(records) || !records.length || records.length > 10000) throw new Error(`invalid ${split} split`);
        for (const record of records) {
            if (!record || typeof record.id !== 'string' || !record.id || ids.has(record.id)) throw new Error('duplicate or missing example ID');
            ids.add(record.id);
            if (typeof record.family !== 'string' || !record.family || !['direct', 'follow-up', 'correction', 'clarification', 'abstention', 'citation', 'distractor', 'conflict'].includes(record.category))
                throw new Error('invalid question family or category');
            if (families.has(record.family) && families.get(record.family) !== split) throw new Error('question family leakage');
            families.set(record.family, split);
            if (!Array.isArray(record.sources) || record.sources.some((source) => typeof source !== 'string' || !source) || new Set(record.sources).size !== record.sources.length) throw new Error('invalid source references');
            for (const source of record.sources) {
                if (sources.has(source) && sources.get(source) !== split) throw new Error('source leakage');
                sources.set(source, split);
            }
            if (typeof record.answer !== 'string' || !record.answer.trim() || record.answer.includes('\0')) throw new Error('invalid assistant target');
            if (!Array.isArray(record.messages) || !record.messages.length || record.messages.length > 1024) throw new Error('invalid messages');
            let expected = 'user';
            for (const message of record.messages) {
                if (!message || typeof message.content !== 'string' || !message.content.trim() || message.content.includes('\0')) throw new Error('invalid message content');
                if (message.role === 'evidence' && expected === 'user') continue;
                if (message.role !== expected) throw new Error('invalid dialogue order');
                expected = expected === 'user' ? 'assistant' : 'user';
            }
            if (record.messages.at(-1)!.role !== 'user') throw new Error('current user question required');
            const identity = digest(record.messages.map((message) => [message.role, normalizeEvidence(message.content)]));
            if (content.has(identity)) throw new Error('duplicate normalized dialogue');
            content.add(identity);
        }
    }
    if (Buffer.byteLength(JSON.stringify(dataset)) > 16 * 1024 * 1024) throw new Error('dataset exceeds byte limit');
    if (dataset.version !== 1) validateRubrics(dataset);
    return { dataset, hashes: { corpus: digest(dataset), train: digest(dataset.train), development: digest(dataset.development), test: digest(dataset.test),
        ...(dataset.version !== 1 ? { sources: digest(dataset.sourceDocuments) } : {}) } };
}

const nonemptyText = (value: unknown): value is string => typeof value === 'string' && !!value.trim() && !value.includes('\0');
const sha256Pattern = /^[a-f0-9]{64}$/;
const object = (value: unknown): value is Record<string, unknown> => !!value && typeof value === 'object' && !Array.isArray(value);
function validRepositoryPath(value: unknown): value is string {
    return nonemptyText(value) && value.length <= 1024 && !/[\\:\x00-\x1f\x7f]/.test(value) &&
        value.split('/').every(segment => !!segment && segment !== '.' && segment !== '..');
}
function validateSource(source: SourceDocument, version: 2 | 3): void {
    if (version === 2) {
        if (source.provenance?.kind !== 'synthetic' || !nonemptyText(source.provenance.description)) throw new Error('invalid synthetic source provenance');
        return;
    }
    const provenance = source.provenance;
    if (provenance?.kind !== 'repository' || !nonemptyText(provenance.repository) || !/^(?:[a-f0-9]{40}|[a-f0-9]{64})$/.test(provenance.commit) ||
        !validRepositoryPath(provenance.path) || !['workspace', 'commit'].includes(provenance.snapshot) || !sha256Pattern.test(provenance.sha256) ||
        createHash('sha256').update(source.text, 'utf8').digest('hex') !== provenance.sha256)
        throw new Error('invalid repository source provenance or content hash');
}
function validateReview(record: RepositoryDialogueRecord, documents: Map<string, SourceDocument>, split: 'train' | 'development' | 'test'): void {
    const review = record.review;
    if (review === undefined) return;
    if (review.split !== split) throw new Error('review split binding is stale or missing');
    if (!object(review) || !['candidate', 'approved', 'rejected'].includes(review.status) || !object(review.reviewer) ||
        !['agent', 'human'].includes(review.reviewer.kind) || !nonemptyText(review.reviewer.id) ||
        typeof review.reviewedAt !== 'string' || !/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z$/.test(review.reviewedAt) ||
        !Number.isFinite(Date.parse(review.reviewedAt)) || new Date(review.reviewedAt).toISOString() !== review.reviewedAt ||
        !sha256Pattern.test(review.recordSha256) || !nonemptyText(review.notes) || !object(review.sourceSha256) || !object(review.sourceBindingsSha256))
        throw new Error('invalid repository review metadata');
    if (review.status === 'approved' && review.recordSha256 !== recordReviewHash(record)) throw new Error('approved review record hash is stale');
    const bindings = review.sourceSha256;
    if (Object.keys(bindings).length !== record.sources.length || record.sources.some(id => {
        const source = documents.get(id);
        return !Object.hasOwn(bindings, id) || source?.provenance.kind !== 'repository' || bindings[id] !== source.provenance.sha256;
    })) throw new Error('review source hashes are stale or incomplete');
    const sourceBindings = review.sourceBindingsSha256;
    if (Object.keys(sourceBindings).length !== record.sources.length || record.sources.some(id => {
        const source = documents.get(id);
        return !Object.hasOwn(sourceBindings, id) || source?.provenance.kind !== 'repository' ||
            sourceBindings[id] !== sourceReviewHash(source as RepositoryDialogueSource);
    })) throw new Error('review source bindings are stale or incomplete');
}
function validateRubrics(dataset: FactualDataset): void {
    const expectedPurpose = dataset.version === 2 ? 'synthetic-engineering' : 'repository-engineering';
    if (dataset.purpose !== expectedPurpose || !nonemptyText(dataset.description) || !Array.isArray(dataset.sourceDocuments) || !dataset.sourceDocuments.length)
        throw new Error('dataset description and source documents required');
    const documents = new Map<string, SourceDocument>();
    for (const source of dataset.sourceDocuments) {
        if (!source || !nonemptyText(source.id) || !nonemptyText(source.text) || documents.has(source.id))
            throw new Error('invalid or duplicate source document');
        validateSource(source, dataset.version);
        documents.set(source.id, source);
    }
    const used = new Set<string>();
    const sourcePartitions = new Map<string, string>();
    const pathPartitions = new Map<string, string>();
    for (const split of ['train', 'development', 'test'] as const) for (const record of dataset[split]) for (const id of record.sources) {
        const document = documents.get(id);
        if (!document) throw new Error('missing source document');
        const identity = normalizeEvidence(document.text);
        if (sourcePartitions.has(identity) && sourcePartitions.get(identity) !== split) throw new Error('normalized source content leakage');
        sourcePartitions.set(identity, split);
        if (document.provenance.kind === 'repository') {
            const path = document.provenance.path.toLowerCase();
            if (pathPartitions.has(path) && pathPartitions.get(path) !== split) throw new Error('repository source path leakage');
            pathPartitions.set(path, split);
        }
    }
    for (const split of ['train', 'development', 'test'] as const) for (const record of dataset[split]) {
        if (!['answer', 'abstain'].includes(record.expected) || !Array.isArray(record.acceptedAnswers) || !record.acceptedAnswers.length || record.acceptedAnswers.some(answer => !nonemptyText(answer)) ||
            !record.acceptedAnswers.includes(record.answer) || !Array.isArray(record.requiredClaims) || record.requiredClaims.some(claim => !nonemptyText(claim)) || !Array.isArray(record.evidence))
            throw new Error('invalid factual rubric');
        for (const id of record.sources) {
            if (!documents.has(id)) throw new Error('missing source document');
            used.add(id);
        }
        const quoted = new Set<string>();
        for (const evidence of record.evidence) {
            const document = documents.get(evidence?.id);
            if (!document || !record.sources.includes(evidence.id) || !Number.isInteger(evidence.startLine) || !Number.isInteger(evidence.endLine) ||
                evidence.startLine < 1 || evidence.endLine < evidence.startLine || evidence.endLine > document.text.split('\n').length ||
                document.text.split('\n').slice(evidence.startLine - 1, evidence.endLine).join('\n') !== evidence.excerpt)
                throw new Error('invalid exact source span');
            quoted.add(evidence.excerpt);
        }
        const messages = record.messages.filter(message => message.role === 'evidence').map(message => message.content);
        if (messages.some(message => !quoted.has(message)) || record.evidence.some(evidence => !messages.includes(evidence.excerpt)))
            throw new Error('evidence must match supplied prompt messages');
        if (record.expected === 'answer') {
            if (!record.requiredClaims.length || record.acceptedAnswers.some(answer => !record.evidence.some(evidence => evidenceUnits(evidence.excerpt).some(unit => unit.text === answer))) ||
                record.requiredClaims.some(claim => !record.acceptedAnswers.every(answer => answer.includes(claim))))
                throw new Error('answers must be complete supplied source units and contain required claims');
        } else if (record.requiredClaims.length) throw new Error('abstention must not require factual claims');
        if (dataset.version === 3) validateReview(record, documents, split);
    }
    if ([...documents.keys()].some(id => !used.has(id))) throw new Error('unreferenced source document');
}

export function validateFactualDataset(value: unknown): { dataset: FactualDataset; hashes: Record<string, string> } {
    const result = validateDataset(value);
    if (result.dataset.version === 1) throw new Error('factual evaluation requires version 2 or 3 source rubrics');
    return { dataset: result.dataset, hashes: result.hashes };
}

export function validateRepositoryDataset(value: unknown): { dataset: RepositoryDialogueDataset; hashes: Record<string, string> } {
    const result = validateDataset(value);
    if (result.dataset.version !== 3) throw new Error('repository evaluation requires version 3 source provenance');
    return { dataset: result.dataset, hashes: result.hashes };
}
