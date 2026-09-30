/** Repository-chat benchmark contracts and checks; gold labels never enter API requests. */
import type { ChatMessage, Conversation } from '../../../shared/chat.ts';
import type { Document } from './codebase-data.ts';
import { sha256 } from './codebase-data.ts';
import { passageId } from '../knowledge/retrieval.ts';

export interface GoldEvidence { path: string; quote: string }
export interface ResearchSelection { kind: 'impact' | 'symbol'; target: string }
export interface ExpectedInvestigation extends ResearchSelection {
    findings?: { kind: 'target' | 'dependency' | 'dependent' | 'symbol-occurrence'; path: string; relatedPath?: string; quote?: string }[];
    relatedTasks?: string[];
}
export interface ExpectedRepositoryState {
    taskId?: string | null;
    focusPath?: string | null;
    candidates?: 'none' | 'multiple';
    candidateIncludes?: string[];
    taskCandidates?: 'none' | 'multiple';
    taskCandidateIncludes?: string[];
    reportedProgressContains?: string;
    investigation?: ResearchSelection | null;
}
export interface ExpectedReply {
    outcome: 'supported' | 'unsupported' | 'action' | 'clarification';
    evidence?: GoldEvidence[];
    sourcePaths?: string[];
    containsAny?: string[];
    excludes?: string[];
    task?: string;
    finishReason?: 'sources' | 'clarification' | 'abstained';
    noAction?: boolean;
    state?: ExpectedRepositoryState;
    investigation?: ExpectedInvestigation | null;
}
export interface ScenarioTurn {
    content: string;
    expected: ExpectedReply;
    session?: string;
    restartBefore?: boolean;
    repositoryResearch?: ResearchSelection;
}
export interface RepositoryScenario {
    id: string;
    group: 'questions' | 'context' | 'refresh' | 'http';
    split: 'development' | 'acceptance';
    description: string;
    turns: ScenarioTurn[];
    operation?: 'switch' | 'failed-check' | 'invalid-snapshot' | 'stale-evidence' |
        'retry-conflict' | 'cancel-recover' | 'restart-resume' | 'concurrent-sessions';
}
export interface RepositoryScenarioSuite {
    version: 1;
    description: string;
    scenarios: RepositoryScenario[];
}
export interface RepositoryFollowupSuite extends RepositoryScenarioSuite { purpose: 'repository-followup-development' }
export interface RepositoryResearchSuite extends RepositoryScenarioSuite { purpose: 'repository-research-development' }
export interface Assertion {
    name: string; passed: boolean; detail?: string; turn?: string; conversationId?: string;
    numerator?: number; denominator?: number;
}

function validateScenarios(suite: RepositoryScenarioSuite, documents: Document[]): void {
    const ids = new Set<string>();
    for (const scenario of suite.scenarios) {
        if (!scenario.id || ids.has(scenario.id) || !['questions', 'context', 'refresh', 'http'].includes(scenario.group) ||
            !['development', 'acceptance'].includes(scenario.split) || !Array.isArray(scenario.turns) || !scenario.turns.length)
            throw new Error('Invalid or duplicate repository scenario');
        ids.add(scenario.id);
        if (scenario.group === 'context' && scenario.turns.length < 4) throw new Error(`Context requires four turns: ${scenario.id}`);
        for (const turn of scenario.turns) {
            if (!turn.content?.trim() || !['supported', 'unsupported', 'action', 'clarification'].includes(turn.expected?.outcome))
                throw new Error(`Invalid turn: ${scenario.id}`);
            if ((turn.expected.finishReason !== undefined && !['sources', 'clarification', 'abstained'].includes(turn.expected.finishReason)) ||
                (turn.expected.noAction !== undefined && typeof turn.expected.noAction !== 'boolean') ||
                (turn.expected.sourcePaths !== undefined && (!Array.isArray(turn.expected.sourcePaths) ||
                    turn.expected.sourcePaths.some(path => typeof path !== 'string' || !path.trim()))))
                throw new Error(`Invalid reply expectation: ${scenario.id}`);
            for (const selection of [turn.repositoryResearch, turn.expected.investigation, turn.expected.state?.investigation]) {
                if (selection && (!['impact', 'symbol'].includes(selection.kind) || typeof selection.target !== 'string' || !selection.target.trim()))
                    throw new Error(`Invalid investigation selection: ${scenario.id}`);
            }
            const state = turn.expected.state;
            if (state && ((state.taskId !== undefined && state.taskId !== null && typeof state.taskId !== 'string') ||
                (state.focusPath !== undefined && state.focusPath !== null && typeof state.focusPath !== 'string') ||
                (state.candidates !== undefined && !['none', 'multiple'].includes(state.candidates)) ||
                (state.taskCandidates !== undefined && !['none', 'multiple'].includes(state.taskCandidates)) ||
                [state.candidateIncludes, state.taskCandidateIncludes].some(values => values !== undefined &&
                    (!Array.isArray(values) || values.some(value => typeof value !== 'string' || !value.trim())))))
                throw new Error(`Invalid state expectation: ${scenario.id}`);
            for (const evidence of turn.expected.evidence ?? []) {
                if (!evidence.path || !evidence.quote?.trim() || !documents.some(document =>
                    document.sources.some(source => source.path === evidence.path) && document.text.includes(evidence.quote)))
                    throw new Error(`Missing gold evidence in snapshot: ${scenario.id}: ${evidence.path}`);
            }
            for (const path of turn.expected.sourcePaths ?? []) if (!documents.some(document => document.sources.some(source => source.path === path)))
                throw new Error(`Missing required source path in snapshot: ${scenario.id}: ${path}`);
            for (const finding of turn.expected.investigation?.findings ?? []) {
                if (!['target', 'dependency', 'dependent', 'symbol-occurrence'].includes(finding.kind) || !documents.some(document =>
                    document.sources.some(source => source.path === finding.path) && (!finding.quote || document.text.includes(finding.quote))))
                    throw new Error(`Missing investigation finding evidence: ${scenario.id}: ${finding.path}`);
                if (finding.relatedPath && !documents.some(document => document.sources.some(source => source.path === finding.relatedPath)))
                    throw new Error(`Missing investigation relationship target: ${scenario.id}: ${finding.relatedPath}`);
            }
        }
    }
}

/** An invalid or stale fixture is a setup failure, never a removed denominator. */
export function validateRepositorySuite(value: unknown, documents: Document[]): RepositoryScenarioSuite {
    const suite = value as RepositoryScenarioSuite;
    if (!suite || suite.version !== 1 || !Array.isArray(suite.scenarios)) throw new Error('Invalid repository scenario suite');
    validateScenarios(suite, documents);
    const counts = { questions: 24, context: 8, refresh: 4, http: 4 };
    for (const [group, count] of Object.entries(counts)) {
        if (suite.scenarios.filter(scenario => scenario.group === group).length !== count) throw new Error(`Expected ${count} ${group} scenarios`);
    }
    const questions = suite.scenarios.filter(scenario => scenario.group === 'questions');
    if (questions.filter(scenario => scenario.turns[0]!.expected.outcome === 'unsupported').length !== 6)
        throw new Error('Expected six unsupported and eighteen answerable questions');
    if (!suite.scenarios.some(scenario => scenario.id === 'core-development-loop' && scenario.turns.length === 6))
        throw new Error('Missing six-turn core development loop');
    return suite;
}

/** Calibration cases are explicitly development data and never alter the forty-case regression gate. */
export function validateRepositoryFollowups(value: unknown, documents: Document[]): RepositoryFollowupSuite {
    const suite = value as RepositoryFollowupSuite;
    if (!suite || suite.version !== 1 || suite.purpose !== 'repository-followup-development' ||
        !Array.isArray(suite.scenarios) || suite.scenarios.length !== 8 || suite.scenarios.some(scenario =>
            scenario.group !== 'context' || scenario.split !== 'development' || !scenario.id.startsWith('followup-')))
        throw new Error('Expected eight development-only follow-up scenarios');
    validateScenarios(suite, documents);
    return suite;
}

/** Investigation development cases do not replace or alter either existing scenario suite. */
export function validateRepositoryResearch(value: unknown, documents: Document[]): RepositoryResearchSuite {
    const suite = value as RepositoryResearchSuite;
    if (!suite || suite.version !== 1 || suite.purpose !== 'repository-research-development' ||
        !Array.isArray(suite.scenarios) || suite.scenarios.length !== 8 || suite.scenarios.some(scenario =>
            scenario.group !== 'context' || scenario.split !== 'development' || !scenario.id.startsWith('research-')))
        throw new Error('Expected eight development-only repository research scenarios');
    validateScenarios(suite, documents);
    return suite;
}

interface RepositorySource {
    id?: string; path?: string; excerpt?: string; blob?: string; commit?: string;
    documentSha256?: string; passageSha256?: string; coordinateSystem?: string;
    startLine?: number; endLine?: number; manifestSha256?: string;
}

/** Check the entire returned span, hashes, Git identity and normalized coordinates. */
export function validRepositorySource(source: RepositorySource, documents: Document[], manifestSha256: string): boolean {
    const document = documents.find(item => item.sha256 === source.documentSha256);
    if (!document || source.manifestSha256 !== manifestSha256 || source.coordinateSystem !== 'snapshot-normalized-lines' ||
        !Number.isInteger(source.startLine) || !Number.isInteger(source.endLine) || source.startLine! < 1 ||
        source.endLine! < source.startLine! || source.id !== passageId(document.sha256, source.startLine!, source.endLine!)) return false;
    const lines = document.text.split('\n');
    return source.endLine! <= lines.length && typeof source.excerpt === 'string' && sha256(source.excerpt) === source.passageSha256 &&
        source.excerpt === lines.slice(source.startLine! - 1, source.endLine).join('\n') &&
        document.sources.some(item => item.path === source.path && item.blob === source.blob && item.commit === source.commit);
}

function supportedCommand(command: { command: string; cwd: string }, taskId: string | undefined, documents: Document[]): boolean {
    if (documents.some(document => document.text.includes(command.command))) return true;
    // JSON string escaping is a representation detail: compare the reviewed registry's decoded values.
    for (const document of documents.filter(document => document.sources.some(source => source.path === 'docs/codebase-tasks.json'))) {
        try {
            const registry = JSON.parse(document.text) as { tasks?: { id: string; checks?: { command: string; cwd: string }[] }[] };
            if (registry.tasks?.some(task => task.id === taskId && task.checks?.some(check =>
                check.command === command.command && check.cwd === command.cwd))) return true;
        } catch { /* A malformed registry cannot establish command provenance. */ }
    }
    return false;
}

/** Every finding and proposed check must carry source evidence, not merely a plausible file name. */
function scoreInvestigation(expected: ExpectedInvestigation | null, conversation: Conversation, documents: Document[]): Assertion[] {
    const reply = conversation.messages.at(-1);
    const investigation = reply?.investigation;
    if (expected === null) return [{ name: 'reply does not reuse a previous investigation', passed: !investigation }];
    if (!investigation) return [{ name: 'requested investigation is present', passed: false }];
    const sources = reply?.sources ?? [];
    const paths = new Set(documents.flatMap(document => document.sources.map(source => source.path)));
    const findings = Array.isArray(investigation.findings) ? investigation.findings : [];
    const relatedTasks = Array.isArray(investigation.relatedTasks) ? investigation.relatedTasks : [];
    const assertions: Assertion[] = [
        { name: 'requested investigation is present', passed: true },
        { name: 'investigation matches requested kind and target', passed: investigation.kind === expected.kind && investigation.target === expected.target },
        { name: 'investigation uses the conversation snapshot', passed: investigation.snapshot?.repository === conversation.repositorySnapshot?.repository &&
            investigation.snapshot?.commit === conversation.repositorySnapshot?.commit && investigation.snapshot?.manifestSha256 === conversation.repositorySnapshot?.manifestSha256 },
        { name: 'investigation declares static-analysis limitations and truncation', passed: Array.isArray(investigation.limitations) &&
            investigation.limitations.length > 0 && investigation.limitations.every(value => typeof value === 'string' && !!value.trim()) && typeof investigation.truncated === 'boolean' },
        { name: 'investigation distinguishes check proposals from execution', passed: investigation.limitations.some(value =>
            /\b(?:not|haven't|have not)\s+(?:been\s+)?(?:run|executed)\b/i.test(value)) && (!reply?.action || reply.action.verification === 'proposed') },
        { name: 'investigation respects finding and task bounds', passed: findings.length > 0 && findings.length <= 14 && relatedTasks.length <= 3 },
        { name: 'all investigation findings bind existing paths to cited evidence', passed: findings.every(finding =>
            ['target', 'dependency', 'dependent', 'symbol-occurrence'].includes(finding.kind) && paths.has(finding.path) &&
            (finding.relatedPath === undefined || paths.has(finding.relatedPath)) && sources.some(source => source.id === finding.sourceId && source.path === finding.path)) },
    ];
    for (const expectedFinding of expected.findings ?? []) assertions.push({
        name: `investigation finding: ${expectedFinding.kind} ${expectedFinding.path}${expectedFinding.relatedPath ? ` -> ${expectedFinding.relatedPath}` : ''}`,
        passed: findings.some(finding => finding.kind === expectedFinding.kind && finding.path === expectedFinding.path &&
            (expectedFinding.relatedPath === undefined || finding.relatedPath === expectedFinding.relatedPath) && sources.some(source =>
                source.id === finding.sourceId && source.path === finding.path && (!expectedFinding.quote || source.excerpt.includes(expectedFinding.quote)))),
    });
    const registry = documents.find(document => document.sources.some(source => source.path === 'docs/codebase-tasks.json'));
    let reviewed: { id: string; title: string; paths: string[]; checks: { command: string; cwd: string }[] }[] = [];
    try {
        const tasks = registry ? JSON.parse(registry.text).tasks : [];
        reviewed = Array.isArray(tasks) ? tasks : [];
    } catch { /* Malformed JSON cannot support a task proposal. */ }
    assertions.push({ name: 'all investigation check proposals match cited reviewed task records', passed: relatedTasks.every(task => {
        const record = reviewed.find(item => item.id === task.taskId);
        const escapedId = JSON.stringify(task.taskId).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
        const subjectPaths = investigation.kind === 'impact' ? [investigation.target] : findings.map(finding => finding.path);
        return !!record && record.title === task.title && Array.isArray(task.checks) && task.checks.length > 0 &&
            Array.isArray(record.paths) && record.paths.some(path => subjectPaths.includes(path)) &&
            task.checks.length === record.checks.length && task.checks.every((check: { command: string; cwd: string }, index: number) =>
                check.command === record.checks[index]?.command && check.cwd === record.checks[index]?.cwd) &&
            sources.some(source => source.id === task.sourceId && source.path === 'docs/codebase-tasks.json' &&
                new RegExp(`"id"\\s*:\\s*${escapedId}`).test(source.excerpt));
    }) });
    for (const id of expected.relatedTasks ?? []) assertions.push({ name: `investigation related task: ${id}`,
        passed: relatedTasks.some(task => task.taskId === id) });
    return assertions;
}

/** Assertions examine evidence and state, rather than requiring one exact prose answer. */
export function scoreRepositoryReply(expected: ExpectedReply, conversation: Conversation,
    documents: Document[], identity: { commit: string; manifestSha256: string }): Assertion[] {
    const reply = conversation.messages.at(-1) as ChatMessage & { action?: {
        taskId?: string; paths?: string[]; checks?: { command: string; cwd: string }[]; verification?: string;
    } } | undefined;
    const state = conversation as Conversation & { scope?: string; repositorySnapshot?: { commit: string; manifestSha256: string } };
    const sources = (reply?.sources ?? []) as RepositorySource[];
    const validSources = sources.filter(source => validRepositorySource(source, documents, identity.manifestSha256));
    const text = reply?.content.toLowerCase() ?? '';
    const assertions: Assertion[] = [
        { name: 'completed assistant reply', passed: reply?.role === 'assistant' && reply.status === 'complete' },
        { name: 'repository scope and pinned snapshot', passed: state.scope === 'repository' &&
            state.repositorySnapshot?.commit === identity.commit && state.repositorySnapshot?.manifestSha256 === identity.manifestSha256 },
        { name: 'zero public research', passed: reply?.research?.queries === 0 && reply.research.fetched === 0 },
        { name: 'all citations validate', passed: validSources.length === sources.length,
            numerator: validSources.length, denominator: sources.length,
            detail: `${validSources.length}/${sources.length} valid citations` },
    ];
    if (expected.outcome === 'unsupported' || expected.outcome === 'clarification') {
        assertions.push({ name: 'abstains or clarifies', passed: ['abstained', 'clarification'].includes(reply?.finishReason ?? '') });
    } else {
        assertions.push({ name: 'supported response has citations', passed: reply?.finishReason === 'sources' && sources.length > 0 });
    }
    for (const evidence of expected.evidence ?? []) assertions.push({ name: `supporting passage: ${evidence.path}`,
        passed: sources.some(source => source.path === evidence.path && source.excerpt?.includes(evidence.quote)) });
    for (const path of expected.sourcePaths ?? []) assertions.push({ name: `cited selected source path: ${path}`,
        passed: sources.some(source => source.path === path) });
    if (expected.containsAny?.length) assertions.push({ name: 'required meaning present',
        passed: expected.containsAny.some(value => text.includes(value.toLowerCase())), detail: expected.containsAny.join(' | ') });
    for (const forbidden of expected.excludes ?? []) assertions.push({ name: `forbidden claim absent: ${forbidden}`, passed: !text.includes(forbidden.toLowerCase()) });
    if (expected.outcome === 'action') {
        const paths = new Set(documents.flatMap(document => document.sources.map(source => source.path)));
        assertions.push({ name: 'proposed action is present', passed: !!reply?.action });
        assertions.push({ name: 'action is proposed, not claimed executed', passed: !reply?.action || reply.action.verification === 'proposed' });
        assertions.push({ name: 'action references existing paths', passed: !reply?.action ||
            (!!reply.action.paths?.length && reply.action.paths.every(path => paths.has(path))) });
        assertions.push({ name: 'action commands appear in reviewed snapshot', passed: !reply?.action || (!!reply.action.checks?.length &&
            reply.action.checks.every(check => supportedCommand(check, reply.action!.taskId, documents))) });
    }
    if (expected.task) assertions.push({ name: 'expected active task', passed: reply?.action?.taskId === expected.task, detail: expected.task });
    if (expected.finishReason) assertions.push({ name: 'exact expected reply outcome',
        passed: reply?.finishReason === expected.finishReason, detail: expected.finishReason });
    if (expected.noAction) assertions.push({ name: 'unresolved or factual topic does not propose an unrelated action', passed: !reply?.action });
    if (expected.investigation !== undefined) assertions.push(...scoreInvestigation(expected.investigation, conversation, documents));
    if (expected.state) {
        const stored = conversation.repositoryState as { taskId?: string; focusPath?: string; topicCandidates?: readonly string[];
            taskCandidates?: readonly string[]; userReportedProgress?: string; investigation?: ResearchSelection } | undefined;
        const selected = expected.state;
        if (selected.taskId !== undefined) assertions.push({ name: 'persisted task selection matches intent',
            passed: selected.taskId === null ? !stored?.taskId : stored?.taskId === selected.taskId });
        if (selected.focusPath !== undefined) assertions.push({ name: 'persisted file selection matches intent',
            passed: selected.focusPath === null ? !stored?.focusPath : stored?.focusPath === selected.focusPath });
        if (selected.candidates) assertions.push({ name: 'file or topic candidate lifecycle matches intent',
            passed: selected.candidates === 'none' ? !stored?.topicCandidates?.length : (stored?.topicCandidates?.length ?? 0) >= 2 });
        for (const path of selected.candidateIncludes ?? []) assertions.push({ name: `persisted candidate: ${path}`,
            passed: stored?.topicCandidates?.includes(path) ?? false });
        if (selected.taskCandidates) assertions.push({ name: 'task candidate lifecycle matches intent',
            passed: selected.taskCandidates === 'none' ? !stored?.taskCandidates?.length : (stored?.taskCandidates?.length ?? 0) >= 2 });
        for (const id of selected.taskCandidateIncludes ?? []) assertions.push({ name: `persisted task candidate: ${id}`,
            passed: stored?.taskCandidates?.includes(id) ?? false });
        if (selected.reportedProgressContains) assertions.push({ name: 'progress remains a persisted user report',
            passed: stored?.userReportedProgress?.includes(selected.reportedProgressContains) ?? false });
        if (selected.investigation !== undefined) assertions.push({ name: 'persisted investigation matches current intent',
            passed: selected.investigation === null ? !stored?.investigation : stored?.investigation?.kind === selected.investigation.kind &&
                stored?.investigation?.target === selected.investigation.target });
    }
    return assertions;
}

/** Nearest-rank percentiles retain observations, including failed requests. */
export function latencyPercentiles(values: number[]) {
    const sorted = [...values].sort((a, b) => a - b);
    const percentile = (p: number) => sorted.length ? sorted[Math.ceil(sorted.length * p) - 1]! : null;
    return { samples: sorted.length, p50Ms: percentile(0.5), p95Ms: percentile(0.95), maxMs: sorted.at(-1) ?? null };
}
