/** Reproducible candidate corpus from authored questions and exact current repository excerpts. */
import { execFile } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFile, realpath, stat } from 'node:fs/promises';
import { isAbsolute, relative, resolve, sep } from 'node:path';
import { promisify } from 'node:util';
import type { ChatDialogueMessage } from '../../../shared/chat.ts';
import { validateRepositoryDataset, type DialogueEvidence, type RepositoryDialogueDataset, type RepositoryDialogueRecord,
    type RepositoryDialogueSource } from '../chat/dataset.ts';
import { evidenceUnits, normalizeEvidence } from '../chat/evidence.ts';
import { cLocaleTokens } from '../evaluation/codebase-data.ts';
import { repositorySeeds, type RepositorySeed } from './repository-seeds.ts';

const exec = promisify(execFile);
const sha256 = (text: string) => createHash('sha256').update(text, 'utf8').digest('hex');
const unavailableAnswer = 'I do not have supporting evidence for that claim.';
const clarifyAnswer = 'Please name the setting or source you want me to check.';
interface ResolvedSeed { seed: RepositorySeed; evidence: DialogueEvidence; answer: string }

function sourceEvidence(seed: RepositorySeed, document: RepositoryDialogueSource): ResolvedSeed {
    const anchor = document.text.includes(seed.anchor) ? seed.anchor : seed.anchor.replaceAll('\n', '\r\n');
    const offset = document.text.indexOf(anchor);
    if (offset < 0 || document.text.indexOf(anchor, offset + 1) >= 0)
        throw new Error(`Missing or nonunique source anchor: ${seed.id} (${seed.path})`);
    const startLine = document.text.slice(0, offset).split('\n').length;
    const endLine = startLine + seed.anchor.split('\n').length - 1;
    const excerpt = document.text.split('\n').slice(startLine - 1, endLine).join('\n');
    // A comment terminator may be a separate punctuation unit; the answer itself remains exact.
    const needle = anchor.replace(/\s*\*\/$/, '').trim();
    const candidates = evidenceUnits(excerpt).filter(unit => unit.text.includes(needle));
    if (candidates.length !== 1) throw new Error(`Anchor is not one complete evidence unit: ${seed.id}`);
    const answer = candidates[0]!.text;
    if (cLocaleTokens(answer).length + 1 > 64 || cLocaleTokens(excerpt).length + 2 > 100)
        throw new Error(`Authored source unit exceeds bounded training windows: ${seed.id}`);
    return { seed, evidence: { id: document.id, excerpt, startLine, endLine }, answer };
}

function makeRecord(resolved: ResolvedSeed, suffix: string, category: RepositoryDialogueRecord['category'],
    question: string, evidence: readonly DialogueEvidence[], history: readonly ChatDialogueMessage[] = [],
    target = resolved.answer, expected: 'answer' | 'abstain' = 'answer'): RepositoryDialogueRecord {
    const messages: ChatDialogueMessage[] = [...history, ...evidence.map(source => ({ role: 'evidence' as const, content: source.excerpt })),
        { role: 'user', content: question }];
    const evidenceTokens = evidence.reduce((sum, source) => sum + cLocaleTokens(source.excerpt).length + 2, 0);
    const promptTokens = messages.reduce((sum, message) => sum + cLocaleTokens(message.content).length + 2, 1);
    if (evidenceTokens > 128 || promptTokens > 192) throw new Error(`Candidate exceeds recommended prompt windows: ${resolved.seed.id}/${suffix}`);
    return { id: `repository-${resolved.seed.id}-${suffix}`, family: `repository-${resolved.seed.domain}-${resolved.seed.id}`,
        category, sources: [...new Set(evidence.map(source => source.id))], messages, answer: target, expected,
        acceptedAnswers: [target], requiredClaims: expected === 'answer' ? [target] : [], evidence: [...evidence] };
}

/** 50 source families, four bounded authored/context cases each; no generated fact or review approval. */
export async function buildRepositoryCorpus(repoRoot: string): Promise<RepositoryDialogueDataset> {
    const root = await realpath(resolve(repoRoot));
    const { stdout } = await exec('git', ['-c', `safe.directory=${root.replaceAll('\\', '/')}`, '-C', root, 'rev-parse', '--verify', 'HEAD'],
        { encoding: 'utf8', timeout: 10000, maxBuffer: 65536 });
    const commit = stdout.trim();
    if (!/^(?:[a-f0-9]{40}|[a-f0-9]{64})$/.test(commit)) throw new Error('Repository HEAD is not a full commit identity');
    const sourceSplits = new Map<string, RepositorySeed['split']>();
    for (const seed of repositorySeeds) {
        if (sourceSplits.has(seed.path) && sourceSplits.get(seed.path) !== seed.split) throw new Error(`Source path crosses corpus splits: ${seed.path}`);
        sourceSplits.set(seed.path, seed.split);
    }
    const sourceDocuments: RepositoryDialogueSource[] = [];
    for (const path of [...sourceSplits.keys()].sort()) {
        const absolute = resolve(root, path), inside = relative(root, absolute);
        if (!inside || isAbsolute(inside) || inside === '..' || inside.startsWith(`..${sep}`)) throw new Error('Corpus source escapes repository root');
        const resolvedSource = await realpath(absolute), resolvedInside = relative(root, resolvedSource);
        if (!resolvedInside || isAbsolute(resolvedInside) || resolvedInside === '..' || resolvedInside.startsWith(`..${sep}`))
            throw new Error('Corpus source symlink escapes repository root');
        const information = await stat(resolvedSource);
        if (!information.isFile() || information.size > 1024 * 1024) throw new Error(`Invalid bounded source file: ${path}`);
        const text = await readFile(resolvedSource, 'utf8');
        if (!text.trim() || text.includes('\0') || Buffer.byteLength(text) > 1024 * 1024) throw new Error(`Invalid bounded source text: ${path}`);
        sourceDocuments.push({ id: `repo:${path}`, text, provenance: { kind: 'repository', repository: 'Centroid-GAI',
            commit, path, sha256: sha256(text), snapshot: 'workspace' } });
    }
    const sources = new Map(sourceDocuments.map(source => [source.provenance.path, source]));
    const seeds = repositorySeeds.map(seed => sourceEvidence(seed, sources.get(seed.path)!));
    const dataset: RepositoryDialogueDataset = { version: 3, purpose: 'repository-engineering',
        description: 'Two hundred unreviewed repository candidates from fifty authored source families. Full current file text and hashes identify workspace evidence at the recorded base commit. Questions cover build, native contracts, persistence, training and evidence; source files and question families remain in one split. Exact extractive answers, hypothetical conflicting operator reports, distractors and missing-evidence cases require independent review. These candidates do not establish generalization or production readiness.',
        sourceDocuments, train: [], development: [], test: [] };
    for (const [index, resolved] of seeds.entries()) {
        const { seed, evidence } = resolved;
        const records = dataset[seed.split];
        records.push(makeRecord(resolved, 'direct', 'direct', seed.question, [evidence]));
        records.push(makeRecord(resolved, 'followup', 'follow-up', seed.followup, [evidence], [
            { role: 'user', content: `We are reviewing ${seed.topic}.` },
            { role: 'assistant', content: 'Which detail should I check?' },
        ]));
        records.push(index % 2 === 0 ? makeRecord(resolved, 'correction', 'correction', seed.correction, [evidence], [
            { role: 'user', content: `I need help checking ${seed.topic}.` },
            { role: 'assistant', content: 'Please specify the scope you want checked.' },
        ]) : makeRecord(resolved, 'citation', 'citation', `Quote the exact supporting source unit: ${seed.question}`, [evidence]));
        if (index % 4 === 0) {
            const distractor = seeds.find(other => other.seed.split === seed.split && other.seed.path !== seed.path &&
                other.seed.domain !== seed.domain && cLocaleTokens(other.evidence.excerpt).length < 24)!;
            if (!distractor) throw new Error(`No bounded distinct-source distractor for ${seed.id}`);
            records.push(makeRecord(resolved, 'distractor', 'distractor', `Use the relevant excerpt despite the unrelated one. ${seed.question}`,
                [distractor.evidence, evidence]));
        } else if (index % 4 === 1) {
            records.push(makeRecord(resolved, 'missing', 'abstention', seed.unavailable, [evidence], [], unavailableAnswer, 'abstain'));
        } else if (index % 4 === 2) {
            const question = seed.unavailable.startsWith('One ') || seed.unavailable.startsWith('Two ') ? seed.unavailable :
                `Two operators disagree about the deployed behavior for ${seed.topic}. Which report is correct for the running system?`;
            records.push(makeRecord(resolved, 'conflict', 'conflict', question, [evidence], [], unavailableAnswer, 'abstain'));
        } else {
            records.push(makeRecord(resolved, 'clarify', 'clarification', `For ${seed.topic}, is that one the right one?`, [], [], clarifyAnswer, 'abstain'));
        }
    }
    const normalized = new Set<string>();
    for (const record of [...dataset.train, ...dataset.development, ...dataset.test]) {
        const dialogue = JSON.stringify(record.messages.filter(message => message.role !== 'evidence').map(message => [message.role, normalizeEvidence(message.content)]));
        if (normalized.has(dialogue)) throw new Error(`Duplicate evidence-independent dialogue: ${record.id}`);
        normalized.add(dialogue);
    }
    return validateRepositoryDataset(dataset).dataset;
}
