/** Offline, commit-pinned repository answers. Retrieved text is evidence, never executable instructions. */
import { readFileSync, statSync } from 'node:fs';
import { resolve } from 'node:path';
import type { ChatSendRequest, ChatSource, Conversation } from '../../../shared/chat.ts';
import type { RepositoryAction, RepositoryAnswer, RepositoryKnowledgeInfo, RepositorySnapshotIdentity,
    RepositoryState, RepositoryVerificationReport } from '../../../shared/repository.ts';
import type { Document } from '../evaluation/codebase-data.ts';
import { sha256 } from '../evaluation/codebase-data.ts';
import { loadRepositoryIndex, passageId, type Passage } from '../knowledge/retrieval.ts';
import { ChatServiceError } from './service.ts';
import { fileReferences, matchingTasks as resolveTaskNames, referenceQuestion } from './repository-context.ts';
import { createRepositoryRetriever } from './repository-retrieval.ts';
import { createRepositoryCodeIndex } from './repository-code-index.ts';
import { inferredRepositoryResearch, investigateRepository, investigationContinuation, validRepositoryResearch } from './repository-investigation.ts';

const registryPath = 'docs/codebase-tasks.json';
const maximumSourceBytes = 16384;
interface Task {
    id: string; title: string; status: 'planned' | 'implemented' | 'verified'; priority: number;
    dependsOn: string[]; summary: string; paths: string[]; checks: { command: string; cwd: string }[];
    evidence: { path: string; quote: string }[]; doneWhen: string; keywords: string[];
}
interface Loaded {
    identity: RepositorySnapshotIdentity;
    index: ReturnType<typeof loadRepositoryIndex>;
    paths: string[];
    tasks: Task[];
    retriever: ReturnType<typeof createRepositoryRetriever>;
    codeIndex: ReturnType<typeof createRepositoryCodeIndex>;
    verification?: RepositoryVerificationReport;
}
const sameIdentity = (left: RepositorySnapshotIdentity, right: RepositorySnapshotIdentity) =>
    left.repository === right.repository && left.commit === right.commit && left.manifestSha256 === right.manifestSha256;
const safePath = (path: string) => path.length <= 1000 && !/^[\\/]|^[A-Za-z]:|\\|\0/.test(path) &&
    path.split('/').every(part => part !== '..' && part !== '.' && part.length > 0);
const boundedText = (value: unknown, maximum = 2000): value is string =>
    typeof value === 'string' && value.trim().length > 0 && !value.includes('\0') && Buffer.byteLength(value) <= maximum;
const strings = (value: unknown, maximum = 64): value is string[] => Array.isArray(value) && value.length <= maximum &&
    value.every(entry => boundedText(entry));

/** Registry entries are reviewed source material, but malformed or stale references fail closed. */
function tasks(index: Loaded['index'], paths: readonly string[]): Task[] {
    const registry = index.documents.find(document => document.sources.some(source => source.path === registryPath));
    if (!registry) return [];
    const value = JSON.parse(registry.text) as { version?: unknown; tasks?: unknown };
    if (value.version !== 1 || !Array.isArray(value.tasks) || value.tasks.length > 100) throw new Error('Invalid repository task registry');
    const result: Task[] = [];
    for (const candidate of value.tasks as unknown[]) {
        if (!candidate || typeof candidate !== 'object') throw new Error('Invalid repository task');
        const task = candidate as Task;
        if (!boundedText(task.id, 120) || !/^[a-z0-9][a-z0-9-]*$/.test(task.id) || !boundedText(task.title, 300) ||
            !['planned', 'implemented', 'verified'].includes(task.status) || !Number.isSafeInteger(task.priority) || task.priority < 0 ||
            !strings(task.dependsOn, 32) || !boundedText(task.summary) || !strings(task.paths, 32) || !task.paths.length ||
            task.paths.some(path => !paths.includes(path)) || !boundedText(task.doneWhen) || !strings(task.keywords, 32) ||
            !Array.isArray(task.checks) || !task.checks.length || task.checks.length > 20 ||
            task.checks.some(check => !check || !boundedText(check.command, 2000) || !boundedText(check.cwd, 1000) ||
                (check.cwd !== '.' && !safePath(check.cwd))) ||
            !Array.isArray(task.evidence) || !task.evidence.length || task.evidence.length > 20 ||
            task.evidence.some(evidence => !evidence || !boundedText(evidence.path, 1000) || !boundedText(evidence.quote, 4000) ||
                evidence.path === registryPath || !index.documents.some(document => document.text.includes(evidence.quote) &&
                    document.sources.some(source => source.path === evidence.path)))) throw new Error('Invalid or stale repository task evidence');
        if (result.some(previous => previous.id === task.id)) throw new Error('Duplicate repository task ID');
        result.push(task);
    }
    for (const task of result) if (task.dependsOn.some(id => id === task.id || !result.some(candidate => candidate.id === id)))
        throw new Error('Invalid repository task prerequisite');
    const visited = new Set<string>(), pending = new Set<string>();
    const visit = (task: Task): void => {
        if (pending.has(task.id)) throw new Error('Cyclic repository task prerequisites');
        if (visited.has(task.id)) return;
        pending.add(task.id);
        for (const id of task.dependsOn) visit(result.find(candidate => candidate.id === id)!);
        pending.delete(task.id); visited.add(task.id);
    };
    for (const task of result) visit(task);
    return result.sort((left, right) => left.priority - right.priority || left.id.localeCompare(right.id));
}

function load(directory: string): Loaded {
    let total = 0;
    for (const file of ['manifest.json', 'train.jsonl', 'validation.jsonl', 'train.txt', 'validation.txt', 'SOURCE_LICENSE.txt']) {
        const stat = statSync(resolve(directory, file));
        if (!stat.isFile() || stat.size > 64 * 1024 * 1024 || (total += stat.size) > 128 * 1024 * 1024)
            throw new Error('Repository snapshot exceeds the file size limit');
    }
    const index = loadRepositoryIndex(directory);
    const paths = [...new Set(index.documents.flatMap(document => document.sources.map(source => source.path)))].sort();
    if (paths.some(path => !safePath(path)) || paths.length > 5000) throw new Error('Invalid repository source paths');
    const source = index.snapshot.manifest.source as { name?: unknown; commit: string };
    const identity: RepositorySnapshotIdentity = { repository: boundedText(source.name, 200) ? source.name : 'repository',
        commit: source.commit, manifestSha256: index.snapshot.manifestSha256 };
    const loaded: Loaded = { identity, index, paths, tasks: tasks(index, paths), retriever: createRepositoryRetriever(index.documents),
        codeIndex: createRepositoryCodeIndex(index.documents) };
    const verification = loadVerification(directory, loaded);
    if (verification) loaded.verification = verification;
    return loaded;
}

/** Only an operator-mounted sidecar can record runs. Hash binding is provenance, not independent attestation. */
function loadVerification(directory: string, loaded: Loaded): RepositoryVerificationReport | undefined {
    const path = resolve(directory, 'verification.json');
    let stat;
    try { stat = statSync(path); }
    catch (error) { if ((error as NodeJS.ErrnoException).code === 'ENOENT') return; throw error; }
    if (!stat.isFile() || stat.size > 1024 * 1024) throw new Error('Repository verification report exceeds size limit');
    const report = JSON.parse(readFileSync(path, 'utf8')) as RepositoryVerificationReport;
    const timestamp = (value: unknown) => typeof value === 'string' && /^\d{4}-\d\d-\d\dT/.test(value) && Number.isFinite(Date.parse(value));
    const output = (value: unknown) => typeof value === 'string' && Buffer.byteLength(value) <= 65536;
    const task = loaded.tasks.find(task => task.id === report?.taskId);
    if (!report || report.version !== 1 || report.provenance !== 'operator-captured' || !report.snapshot ||
        !sameIdentity(report.snapshot, loaded.identity) || !task || !timestamp(report.startedAt) || !timestamp(report.finishedAt) ||
        Date.parse(report.finishedAt) < Date.parse(report.startedAt) || !['passed', 'failed'].includes(report.status) ||
        !Array.isArray(report.checks) || report.checks.length !== task.checks.length || report.checks.some((check, index) =>
            !check || check.command !== task.checks[index]!.command || check.cwd !== task.checks[index]!.cwd ||
            !Number.isSafeInteger(check.exitCode) || Math.abs(check.exitCode) > 2147483648 || !output(check.stdout) || !output(check.stderr) ||
            !Number.isFinite(check.durationMs) || check.durationMs < 0 || check.durationMs > 86400000 || typeof check.outputTruncated !== 'boolean') ||
        (report.status === 'passed') !== report.checks.every(check => check.exitCode === 0))
        throw new Error('Invalid repository verification report identity, commands, or results');
    return report;
}

/** Exact lines include every source alias. Passage hashes never describe a truncated quotation. */
function sourceSpan(document: Document, start: number, end: number, snapshot: RepositorySnapshotIdentity): ChatSource[] {
    const excerpt = document.text.split('\n').slice(start - 1, end).join('\n');
    if (!excerpt || Buffer.byteLength(excerpt) > maximumSourceBytes) return [];
    return document.sources.map(source => ({ id: passageId(document.sha256, start, end), path: source.path,
        excerpt, commit: source.commit, blob: source.blob, documentSha256: document.sha256,
        passageSha256: sha256(excerpt), contentHash: sha256(excerpt), manifestSha256: snapshot.manifestSha256,
        coordinateSystem: 'snapshot-normalized-lines', startLine: start, endLine: end }));
}

function quotedSources(loaded: Loaded, path: string, quote: string): ChatSource[] {
    const document = loaded.index.documents.find(item => item.sources.some(source => source.path === path));
    if (!document) return [];
    const offset = document.text.indexOf(quote);
    if (offset < 0) return [];
    const start = document.text.slice(0, offset).split('\n').length;
    const end = start + quote.split('\n').length - 1;
    return sourceSpan(document, start, end, loaded.identity).filter(source => source.path === path);
}

function fileSources(loaded: Loaded, paths: readonly string[]): ChatSource[] {
    return paths.flatMap(path => {
        const document = loaded.index.documents.find(item => item.sources.some(source => source.path === path));
        return document ? sourceSpan(document, 1, Math.min(32, document.text.split('\n').length), loaded.identity)
            .filter(source => source.path === path) : [];
    });
}

function passageSources(passage: Passage, snapshot: RepositorySnapshotIdentity): ChatSource[] {
    if (Buffer.byteLength(passage.text) > maximumSourceBytes) return [];
    return passage.citations.map(citation => ({ id: passage.id, excerpt: passage.text, ...citation,
        manifestSha256: snapshot.manifestSha256, passageSha256: passage.textSha256, contentHash: passage.textSha256 }));
}

function deduplicate(sources: readonly ChatSource[]): ChatSource[] {
    return sources.filter((source, index) => sources.findIndex(other => other.path === source.path && other.id === source.id) === index).slice(0, 10);
}

/** Find the exact JSON object span so the action and proposed commands have their own source citation. */
function taskSource(loaded: Loaded, task: Task): ChatSource[] {
    const document = loaded.index.documents.find(item => item.sources.some(source => source.path === registryPath));
    if (!document) return [];
    const id = new RegExp(`"id"\\s*:\\s*"${task.id}"`).exec(document.text);
    if (!id) return [];
    const start = document.text.lastIndexOf('{', id.index);
    let depth = 0, quoted = false, escaped = false;
    for (let offset = start; offset < document.text.length; offset++) {
        const character = document.text[offset];
        if (quoted) {
            if (escaped) escaped = false;
            else if (character === '\\') escaped = true;
            else if (character === '"') quoted = false;
        } else if (character === '"') quoted = true;
        else if (character === '{') depth++;
        else if (character === '}' && --depth === 0) return quotedSources(loaded, registryPath, document.text.slice(start, offset + 1));
    }
    return [];
}

function action(task: Task): RepositoryAction {
    return { taskId: task.id, title: task.title, rationale: task.summary, paths: task.paths, prerequisites: task.dependsOn,
        checks: task.checks, completionCondition: task.doneWhen, verification: 'proposed' };
}
function matchingTasks(loaded: Loaded, question: string): Task[] {
    return resolveTaskNames(loaded.tasks, question);
}

function eligible(loaded: Loaded, task: Task): boolean {
    return task.status !== 'verified' && task.dependsOn.every(id => loaded.tasks.find(candidate => candidate.id === id)?.status === 'verified');
}

function asksRunResult(question: string): boolean {
    return /\b(did|has|have)\b.*\b(tests?|checks?|build|suite)\b.*\b(pass|passed|succeed|succeeded|run|failed)\b|\b(did|has|have)\b.*\b(run|ran|executed|passed|failed)\b.*\b(tests?|checks?|build|suite)\b|\b(latest|last|my|our)\b.*\b(tests?|checks?|build)\b.*\b(result|passed|exit)\b/i.test(question);
}
function unsupported(question: string): string | undefined {
    if (/\b(production|actual|real|deployed)\b.*\b(password|secret|credential|private key|access token)\b|\b(password|secret|credential)\b.*\b(production|actual|deployed)\b/i.test(question))
        return 'Production credentials are not repository evidence. I cannot provide a deployment secret.';
    if (/\b(latency|throughput|p[0-9]{2,3}|benchmark)\b/i.test(question) && /\b(production|measured|actual|right now)\b/i.test(question))
        return 'No captured measurement report is attached to this conversation. Source code and proposed commands cannot establish measured performance.';
    if (asksRunResult(question))
        return 'I have not run these checks and have no captured execution report. Repository commands describe how to verify a change, not whether a run passed.';
    const unseenChanges = /\b(uncommitted|working[- ]tree|dirty|staged|untracked)\b|\b(my|our|local)\s+(edits|changes)\b/i.test(question);
    const procedural = /\bhow\b.*\b(snapshot|import|include|exclude|ignore|commit|prepare)\b|^does\b.*\b(importer|snapshot)\b/i.test(question);
    if (unseenChanges && !procedural)
        return 'The assistant sees only the pinned committed snapshot. Uncommitted edits and the mutable working tree are not available.';
    return undefined;
}

function renderSources(sources: readonly ChatSource[]): string {
    return sources.map((source, index) => `[${index + 1}] ${source.path} (snapshot-normalized-lines ${source.startLine}-${source.endLine})\n${source.excerpt}`).join('\n\n');
}

function cleanQuestion(content: string): { question: string; corrected: boolean } {
    const corrected = /^(?:i mean(?:t)?|actually|instead|rather|focus on|return to|let['’]s (?:discuss|switch to))\b/i.test(content.trim());
    const question = corrected ? content.trim().replace(/^(?:i mean(?:t)?|actually[, :]*|instead[, :]*|rather[, :]*|focus on|return to|let['’]s (?:discuss|switch to))\s*/i, '')
        .replace(/,?\s+not\s+.+$/i, '').trim() : content.trim();
    return { question, corrected };
}

/** No provider, memory store, executor, or mutable-checkout access exists in this answer layer. */
export class RepositoryService {
    private readonly loaded: Loaded[] = [];
    private readonly errors: string[] = [];
    private readonly selected: Loaded | undefined;
    constructor(snapshotDirectory?: string, additionalDirectories: readonly string[] = []) {
        for (const directory of [...new Set([...(snapshotDirectory ? [snapshotDirectory] : []), ...additionalDirectories])]) {
            try {
                const value = load(directory);
                if (directory === snapshotDirectory) this.selected = value;
                if (!this.loaded.some(previous => sameIdentity(previous.identity, value.identity))) this.loaded.push(value);
            } catch (error) { this.errors.push(error instanceof Error ? error.message : 'Invalid repository snapshot'); }
        }
    }
    info(): RepositoryKnowledgeInfo {
        const selected = this.selected;
        return { ready: !!selected, ...(selected ? { snapshot: selected.identity } : {}),
            ...(!selected || this.errors.length ? { error: this.errors.join('; ') || 'No repository snapshot configured' } : {}),
            snapshots: this.loaded.map(value => value.identity), documentCount: selected?.index.documents.length ?? 0,
            passageCount: selected?.index.retriever.passages ?? 0, paths: selected?.paths ?? [] };
    }
    identity(requested?: RepositorySnapshotIdentity): RepositorySnapshotIdentity {
        if (requested !== undefined && (!requested || typeof requested !== 'object' || !boundedText(requested.repository, 200) ||
            !/^(?:[a-f0-9]{40}|[a-f0-9]{64})$/.test(requested.commit) || !/^[a-f0-9]{64}$/.test(requested.manifestSha256)))
            throw new ChatServiceError('Invalid repository snapshot identity');
        const loaded = requested === undefined ? this.selected : this.loaded.find(value => sameIdentity(value.identity, requested));
        if (!loaded) throw new ChatServiceError(requested ? 'Requested repository snapshot is unavailable or unverified' :
            this.info().error ?? 'Repository snapshot unavailable', 503);
        return { ...loaded.identity };
    }
    async answer(input: ChatSendRequest, conversation: Conversation, signal: AbortSignal): Promise<RepositoryAnswer> {
        signal.throwIfAborted();
        if (!boundedText(input.content, 16384)) throw new ChatServiceError('Repository question must fit 16384 UTF-8 bytes');
        const started = Date.now();
        if (!conversation.repositorySnapshot) throw new ChatServiceError('Repository conversation has no pinned snapshot', 503);
        const snapshot = this.identity(conversation.repositorySnapshot);
        const loaded = this.loaded.find(value => sameIdentity(value.identity, snapshot))!;
        let state = this.context(conversation, loaded);
        const finish = (content: string, sources: readonly ChatSource[], mode: RepositoryAnswer['finishReason'], reason: string,
            proposed?: RepositoryAction, verification?: RepositoryVerificationReport): RepositoryAnswer => {
            signal.throwIfAborted();
            return { content, sources, memoryIds: [], repositoryState: state, finishReason: mode,
                ...(proposed ? { action: proposed } : {}), ...(verification ? { verification: structuredClone(verification) } : {}),
                research: { status: 'none', reason, provider: null, queries: 0,
                    fetched: 0, elapsedMs: Date.now() - started, mode } };
        };
        if (asksRunResult(input.content) && loaded.verification) {
            const report = loaded.verification;
            const requestedTasks = matchingTasks(loaded, input.content);
            if ((state.taskId && state.taskId !== report.taskId) || requestedTasks.some(task => task.id !== report.taskId))
                return finish(`The available operator-captured report covers ${report.taskId}. No matching execution report is available for the other requested or active task.`,
                    [], 'abstained', 'verification report applies to another task');
            const task = loaded.tasks.find(task => task.id === report.taskId)!;
            const sources = deduplicate([...taskSource(loaded, task), ...task.evidence.flatMap(evidence => quotedSources(loaded, evidence.path, evidence.quote))]);
            return finish(`Operator-captured verification (not independently attested) for ${task.title} at ${snapshot.commit}: ${report.status}.\n` +
                report.checks.map(check => `- ${check.command} (working directory: ${check.cwd}): exit ${check.exitCode}, ${check.durationMs} ms${check.outputTruncated ? '; captured output truncated' : ''}`).join('\n') +
                '\nThe report records those runs; the reviewed task registry still determines task state.', sources, 'sources',
                'operator-captured report bound to pinned snapshot and reviewed commands', undefined, report);
        }
        const unsupportedReason = unsupported(input.content);
        if (unsupportedReason) return finish(unsupportedReason, [], 'abstained', 'unsupported operational claim');
        if (/\b(switch|load|refresh|use)\b.*\b[a-f0-9]{7,64}\b/i.test(input.content))
            return finish(`This conversation remains pinned to ${snapshot.commit}. Use the explicit repository snapshot switch endpoint with a verified identity to change it.`,
                [], 'clarification', 'a mentioned commit does not change the pinned snapshot');
        if (/\b(?:i|we)\s+(?:finished|completed|ran|implemented|fixed|passed)\b|\bexit(?:\s+(?:status|code))?\s*[:=]?\s*0\b/i.test(input.content)) {
            state = { ...state, userReportedProgress: input.content.slice(0, 1000) };
            return finish('Recorded as user-reported progress, not independently verified. The pinned task state is unchanged. A committed snapshot and captured check report are needed to reassess completion.',
                [], 'abstained', 'user-reported progress is not execution evidence');
        }
        const { question, corrected } = cleanQuestion(input.content);
        const next = /\b(next|unfinished|remaining|smallest|priority|prioritize)\b.*\b(steps?|actions?|tasks?|work|changes?)\b|\bwhat remains\b|\bwhat(?:['’]s| is)? next\b/i.test(question);
        const files = /\b(files?|paths?)\b|\bwhere\b.*\b(change|implement|live)\b/i.test(question);
        const checks = /\b(verif(?:y|ication)|checks?|tests?|validate|prerequisites?|dependencies)\b/i.test(question);
        const reference = referenceQuestion(question);
        const mentions = fileReferences(question, loaded.paths);
        const matchedTasks = matchingTasks(loaded, question).filter(task => !mentions.found.length ||
            question.toLowerCase().includes(task.id) || question.toLowerCase().includes(task.title.toLowerCase()));
        const reset = (): RepositoryState => ({ snapshot,
            ...(state.userReportedProgress ? { userReportedProgress: state.userReportedProgress } : {}) });
        const clarifyTasks = (candidates: readonly Task[], focusPath?: string) => {
            state = { ...reset(), taskCandidates: candidates.slice(0, 8).map(task => task.id), pendingReference: 'task',
                ...(focusPath ? { focusPath } : {}) };
            return finish(`Which reviewed task do you mean: ${state.taskCandidates!.join(', ')}? Name one task ID before I choose its files or checks.`,
                [], 'clarification', 'multiple reviewed tasks require an explicit choice');
        };
        const clarifyFiles = (candidates: readonly string[]) => {
            const { focusPath: _focus, ...rest } = state;
            state = { ...rest, topicCandidates: candidates.slice(0, 8), pendingReference: 'file' };
            return finish(`Which file do you mean: ${state.topicCandidates!.join(', ')}? Choose a path so I can keep the follow-up on that file.`,
                [], 'clarification', 'multiple cited files require an explicit choice');
        };
        const researchRequest = input.repositoryResearch ?? inferredRepositoryResearch(question, state, loaded.paths);
        if (!researchRequest && investigationContinuation(question)) {
            state = reset();
            return finish('Which file or exact symbol should I investigate? This conversation has no completed software research topic.',
                [], 'clarification', 'no completed investigation context');
        }
        if (researchRequest) {
            if (!validRepositoryResearch(researchRequest)) throw new ChatServiceError('Invalid repository research target');
            state = reset();
            let target = researchRequest.target;
            if (researchRequest.kind === 'impact' && !loaded.paths.includes(target)) {
                const choices = target.includes('/') ? [] : loaded.paths.filter(path => path.split('/').at(-1) === target);
                if (choices.length > 1) return clarifyFiles(choices);
                if (!choices.length) return finish(`I cannot find ${target} in the pinned snapshot. Name an existing source path to research.`,
                    [], 'clarification', 'research target is unavailable');
                target = choices[0]!;
            }
            const request = { kind: researchRequest.kind, target };
            const result = investigateRepository(request, { snapshot, index: loaded.codeIndex, tasks: loaded.tasks,
                fileSources: path => fileSources(loaded, [path]),
                taskSources: id => taskSource(loaded, loaded.tasks.find(task => task.id === id)!) });
            if (!result) return finish(`No source evidence for ${target} was found in the pinned snapshot. Name an existing file or exact identifier.`,
                [], 'abstained', 'no repository investigation evidence');
            state = { ...state, topic: target, investigation: request, ...(request.kind === 'impact' ? { focusPath: target } : {}) };
            return { ...finish(result.content, result.sources, 'sources', 'bounded software research over pinned source evidence'),
                investigation: result.investigation };
        }
        // Explicit references are resolved before active-task fallback. Unknown choices do not erase pending choices.
        if (mentions.missing.length) {
            if (!state.pendingReference || !(state.topicCandidates?.length || state.taskCandidates?.length)) state = reset();
            return finish(`I cannot find ${mentions.missing.join(', ')} in the pinned snapshot. ` +
                (state.topicCandidates?.length ? `Choose one of: ${state.topicCandidates.join(', ')}.` :
                    state.taskCandidates?.length ? `Choose a reviewed task: ${state.taskCandidates.join(', ')}.` : 'Name an existing source path.'),
                [], 'clarification', 'explicit source path is unavailable');
        }
        if (matchedTasks.length > 1 && !reference) return clarifyTasks(matchedTasks);
        let active = state.taskId ? loaded.tasks.find(task => task.id === state.taskId) : undefined;
        if (mentions.found.length > 1) {
            state = { ...reset(), topic: question.slice(0, 500), topicCandidates: mentions.found.slice(0, 8), pendingReference: 'file',
                ...(active && mentions.found.every(path => active!.paths.includes(path)) ? { taskId: active.id } : {}) };
            const sources = deduplicate(fileSources(loaded, mentions.found.slice(0, 8)));
            return finish(`The named files are present in this snapshot. Name one path for a focused follow-up.\n\n${renderSources(sources)}`,
                sources, 'sources', 'explicit file comparison preserves pending source choices');
        }
        let focused = mentions.found[0];
        if (focused) {
            const candidates = loaded.tasks.filter(task => task.paths.includes(focused!));
            if (matchedTasks[0] && !matchedTasks[0].paths.includes(focused))
                return finish('That path is not listed for the named task. Choose the task or file you want to discuss.', [], 'clarification', 'conflicting explicit task and file');
            const constrained = state.taskCandidates?.length ? candidates.filter(task => state.taskCandidates!.includes(task.id)) : candidates;
            active = matchedTasks[0] ?? (active?.paths.includes(focused) && !state.taskCandidates?.length ? active :
                constrained.length === 1 ? constrained[0] : undefined);
            if (checks && !active && constrained.length > 1) return clarifyTasks(constrained, focused);
            state = { ...reset(), topic: focused, focusPath: focused, ...(active ? { taskId: active.id } : {}) };
        } else if (corrected) {
            state = { ...reset(), topic: question.slice(0, 500) };
            active = undefined;
        }
        let selected = !reference && matchedTasks.length === 1 ? matchedTasks[0] : undefined;
        const explicitTaskChoice = !!selected && (state.taskCandidates?.includes(selected.id) || question.toLowerCase().includes(selected.id));
        if (!selected && reference && state.taskCandidates?.length) {
            const candidates = loaded.tasks.filter(task => state.taskCandidates!.includes(task.id));
            return clarifyTasks(candidates, state.focusPath);
        }
        if (!selected && reference && state.topicCandidates && state.topicCandidates.length > 1 &&
            (state.pendingReference || !active)) return clarifyFiles(state.topicCandidates);
        const singularFile = /\b(?:which|what)\s+(?:one|file|path|module)\b|\b(?:that|this|the)\s+(?:file|path|module)\b/i.test(question);
        if (!selected && reference && singularFile && active && !state.focusPath && active.paths.length > 1)
            return clarifyFiles(active.paths);
        if (!selected && focused && active) selected = active;
        if (!selected && !corrected && (next || reference) && active) selected = active;
        if (next && reference && (!selected || !eligible(loaded, selected))) selected = loaded.tasks.find(task => eligible(loaded, task));
        if (!selected && next) selected = loaded.tasks.find(task => eligible(loaded, task));
        if (!selected && reference && checks && state.focusPath) {
            const candidates = loaded.tasks.filter(task => task.paths.includes(state.focusPath!));
            if (candidates.length > 1) return clarifyTasks(candidates, state.focusPath);
            selected = candidates[0];
            if (!selected) return finish(`No reviewed verification task lists ${state.focusPath}. I cannot infer a check command from that file alone.`,
                fileSources(loaded, [state.focusPath]), 'abstained', 'selected source has no reviewed verification commands');
        }
        if (selected && (next || checks || files || corrected || explicitTaskChoice)) {
            focused = state.focusPath && selected.paths.includes(state.focusPath) ? state.focusPath : undefined;
            state = { ...reset(), snapshot, topic: selected.title, taskId: selected.id, ...(focused ? { focusPath: focused } : {}) };
            const sources = deduplicate([...taskSource(loaded, selected), ...(focused ? fileSources(loaded, [focused]) : []),
                ...selected.evidence.flatMap(evidence => quotedSources(loaded, evidence.path, evidence.quote))]);
            if (!sources.some(source => source.path === registryPath)) return finish('The task registry could not supply a bounded exact citation for this action.', [], 'abstained', 'task citation unavailable');
            const proposed = action(selected);
            const content = `${corrected ? 'Topic corrected. ' : ''}` +
                (selected.status === 'verified' ? `Reviewed task (already verified in this snapshot): ${proposed.title}\n` :
                    `Proposed next action: ${proposed.title}\n`) + `${proposed.rationale}\n\n` +
                (focused ? `Selected file: ${focused}\n` : '') +
                `Affected paths: ${proposed.paths.join(', ')}\nPrerequisites: ${proposed.prerequisites.join(', ') || 'none'}\n` +
                (selected.dependsOn.some(id => loaded.tasks.find(task => task.id === id)?.status !== 'verified') ?
                    'This task has unverified prerequisites; it is not yet the next ready task.\n' : '') +
                `Completion condition: ${proposed.completionCondition}\n\nProposed checks (not run):\n` +
                proposed.checks.map(check => `- ${check.command} (working directory: ${check.cwd})`).join('\n') +
                `\n\nEvidence from ${snapshot.commit}:\n${renderSources(sources)}`;
            const verification = loaded.verification?.taskId === selected.id ? loaded.verification : undefined;
            return finish(content + (verification ? `\n\nCaptured verification: ${verification.status} (operator-captured, not independently attested). Reviewed task state: ${selected.status}.` : ''),
                sources, 'sources', 'reviewed task registry and exact source evidence', proposed, verification);
        }
        if (next) return finish('No reviewed unfinished task with satisfied prerequisites is available in this snapshot.', [], 'abstained', 'no eligible reviewed task');
        if (reference && !state.topic && !matchedTasks.length)
            return finish('Which repository topic or task should I use for this follow-up?', [], 'clarification', 'no completed topic context');
        if (focused) {
            const sources = deduplicate(fileSources(loaded, [focused]));
            return finish(`Selected ${focused} from the pinned snapshot.\n\n${renderSources(sources)}`, sources, 'sources', 'explicit existing file selection');
        }
        const effective = reference && state.topic && !corrected ? `${state.focusPath ?? state.topic} ${question}` : question;
        const changes = this.previousEvidence(conversation, input.content, loaded);
        if (changes.removed.length) return finish(`The earlier answer cited sources removed from the selected snapshot: ${changes.removed.join(', ')}. ` +
            'I cannot carry that evidence forward as a current answer. The previous messages retain their original snapshot citations.',
            [], 'abstained', 'previously supporting source was removed after an explicit snapshot change');
        const hits = loaded.retriever.search(effective);
        const sources = deduplicate(hits.flatMap(hit => passageSources(hit, snapshot)));
        if (!reference) state = reset();
        if (!sources.length) return finish('I cannot support an answer from the pinned repository snapshot. Name a file, symbol, or specific repository topic; no public search was performed.',
            [], 'abstained', 'no supporting repository passage; repository scope never searches publicly');
        const candidates = /\b(?:versus|vs|compare)\b/i.test(question) ? [...new Set(sources.map(source => source.path))].slice(0, 3) : [];
        state = { snapshot, topic: (reference && state.topic ? state.topic : question).slice(0, 500),
            ...(candidates.length > 1 ? { topicCandidates: candidates } : {}),
            ...(reference && state.focusPath ? { focusPath: state.focusPath } : {}),
            ...(state.userReportedProgress ? { userReportedProgress: state.userReportedProgress } : {}) };
        return finish((changes.changed.length ? `Previously cited source content changed: ${changes.changed.join(', ')}. The excerpts below come from the selected snapshot.\n\n` : '') +
            `Evidence from the pinned repository snapshot ${snapshot.commit}:\n\n${renderSources(sources)}\n\n` +
            'These are source excerpts. They do not establish results of unrecorded test runs or changes outside this snapshot.',
            sources, 'sources', 'offline lexical source retrieval; source relevance is not an execution result');
    }
    /** Repeated questions may compare bounded completed provenance, but old passages never become new evidence. */
    private previousEvidence(conversation: Conversation, question: string, current: Loaded): { removed: string[]; changed: string[] } {
        const normalize = (text: string) => text.normalize('NFC').toLowerCase().replace(/\s+/g, ' ').trim();
        const messages = conversation.messages.slice(-40);
        for (let index = messages.length - 2; index >= 0; index--) {
            const user = messages[index]!, assistant = messages[index + 1]!;
            if (user.role !== 'user' || user.status !== 'complete' || normalize(user.content) !== normalize(question) ||
                assistant.role !== 'assistant' || assistant.status !== 'complete' || assistant.requestId !== user.requestId) continue;
            const removed = new Set<string>(), changed = new Set<string>();
            for (const source of (assistant.sources ?? []).slice(0, 10)) {
                if (source.commit === current.identity.commit && source.manifestSha256 === current.identity.manifestSha256) continue;
                const previous = this.loaded.find(loaded => loaded.identity.commit === source.commit && loaded.identity.manifestSha256 === source.manifestSha256);
                const document = previous?.index.documents.find(document => document.sha256 === source.documentSha256 &&
                    document.sources.some(alias => alias.path === source.path && alias.blob === source.blob));
                if (!document || source.coordinateSystem !== 'snapshot-normalized-lines' || !source.startLine || !source.endLine ||
                    source.id !== passageId(document.sha256, source.startLine, source.endLine) ||
                    document.text.split('\n').slice(source.startLine - 1, source.endLine).join('\n') !== source.excerpt ||
                    sha256(source.excerpt) !== source.passageSha256) continue;
                const replacement = current.index.documents.find(document => document.sources.some(alias => alias.path === source.path));
                if (!replacement) removed.add(source.path);
                else if (replacement.sha256 !== document.sha256) changed.add(source.path);
            }
            return { removed: [...removed], changed: [...changed] };
        }
        return { removed: [], changed: [] };
    }
    /** Durable state is bounded and revision-scoped; failed messages never reconstruct a topic. */
    private context(conversation: Conversation, loaded: Loaded): RepositoryState {
        const snapshot = loaded.identity;
        const previous = conversation.repositoryState;
        if (previous?.snapshot && sameIdentity(previous.snapshot, snapshot)) return { snapshot,
            ...(boundedText(previous.topic, 2000) ? { topic: previous.topic.slice(0, 500) } : {}),
            ...(boundedText(previous.taskId, 120) && loaded.tasks.some(task => task.id === previous.taskId) ? { taskId: previous.taskId } : {}),
            ...(strings(previous.topicCandidates, 8) ? { topicCandidates: previous.topicCandidates.filter(path => loaded.paths.includes(path)) } : {}),
            ...(strings(previous.taskCandidates, 8) ? { taskCandidates: previous.taskCandidates.filter(id => loaded.tasks.some(task => task.id === id)) } : {}),
            ...(boundedText(previous.focusPath, 1000) && loaded.paths.includes(previous.focusPath) ? { focusPath: previous.focusPath } : {}),
            ...(previous.pendingReference && ['file', 'task', 'topic'].includes(previous.pendingReference) ? { pendingReference: previous.pendingReference } : {}),
            ...(validRepositoryResearch(previous.investigation) && (previous.investigation.kind === 'symbol' ||
                loaded.paths.includes(previous.investigation.target)) ? { investigation: previous.investigation } : {}),
            ...(boundedText(previous.userReportedProgress, 4000) ? { userReportedProgress: previous.userReportedProgress.slice(0, 1000) } : {}) };
        return { snapshot };
    }
}
