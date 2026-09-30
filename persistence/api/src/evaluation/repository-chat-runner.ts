/** Real HTTP scenarios with immutable-snapshot citation checks and replayable raw transcripts. */
import { appendFileSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { cpus, hostname, platform, totalmem } from 'node:os';
import { randomUUID } from 'node:crypto';
import { performance } from 'node:perf_hooks';
import { isDeepStrictEqual } from 'node:util';
import type { ChatSendResponse, Conversation } from '../../../shared/chat.ts';
import { loadRepositoryIndex } from '../knowledge/retrieval.ts';
import { sha256 } from './codebase-data.ts';
import { latencyPercentiles, scoreRepositoryReply, validateRepositoryFollowups, validateRepositoryResearch, validateRepositorySuite } from './repository-chat-metrics.ts';
import type { Assertion, RepositoryScenario, ScenarioTurn } from './repository-chat-metrics.ts';

interface Identity { repository: string; commit: string; manifestSha256: string }
interface Knowledge { ready: boolean; snapshot?: Identity; snapshots: Identity[]; paths: string[]; error?: string }
interface HttpResult<T> { status: number; value: T; durationMs: number }
export interface ScenarioResult {
    id: string; group: string; split: string; status: 'passed' | 'failed' | 'not-run'; assertions: Assertion[];
    durationMs: number; error?: string;
}
export interface RepositoryRunOptions {
    baseUrl: string; token?: string; snapshotDirectory: string; additionalSnapshotDirectory?: string;
    suitePath?: string; followupsSuitePath?: string; researchSuitePath?: string; outputDirectory: string; lane?: 'first' | 'full'; restart?: () => Promise<void>;
}

/** Keep markdown readable without changing the captured response or introducing executable HTML. */
function markdown(text: string): string {
    return text.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;')
        .replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/g, character => `\\u${character.charCodeAt(0).toString(16).padStart(4, '0')}`);
}

/** Render captured exchanges, including concurrent and retry requests, from the saved JSONL itself. */
function transcriptFromJsonl(rawPath: string, introduction: string[]): string {
    const lines = [...introduction];
    for (const line of readFileSync(rawPath, 'utf8').trim().split('\n').filter(Boolean)) {
        const row = JSON.parse(line) as { request: { scenarioId: string; method: string; path: string; body?: { content?: string; requestId?: string } };
            response?: { status: number; body: Partial<ChatSendResponse> }; error?: string; durationMs: number };
        if (row.request.method !== 'POST' || !row.request.path.endsWith('/messages') || typeof row.request.body?.content !== 'string') continue;
        lines.push(`## ${row.request.scenarioId}`, '', `**User:** ${markdown(row.request.body.content)}`, '');
        const conversation = row.response?.body.conversation;
        const reply = conversation?.messages.find(message => message.role === 'assistant' && message.requestId === row.request.body?.requestId);
        if (!reply) {
            lines.push(`HTTP ${row.response?.status ?? 'error'}: ${markdown(row.error ?? JSON.stringify(row.response?.body))}`, '');
            continue;
        }
        lines.push(`**Assistant:** ${markdown(reply.content)}`, '',
            `Status: ${reply.status}; finish reason: ${reply.finishReason ?? 'none'}; revision: ${conversation!.revision}; HTTP ${row.response!.status}; ${row.durationMs.toFixed(1)} ms.`, '',
            `Routing: ${reply.research?.status ?? 'none'}; public queries: ${reply.research?.queries ?? 'unavailable'}; fetched pages: ${reply.research?.fetched ?? 'unavailable'}.`, '');
        for (const source of reply.sources ?? []) lines.push(`- ${markdown(source.path)} (${source.commit}, normalized lines ${source.startLine}–${source.endLine})`);
        lines.push('');
    }
    return lines.join('\n');
}

export async function runRepositoryChatSuite(options: RepositoryRunOptions) {
    const primary = loadRepositoryIndex(options.snapshotDirectory);
    const secondary = options.additionalSnapshotDirectory ? loadRepositoryIndex(options.additionalSnapshotDirectory) : undefined;
    const suiteBytes = readFileSync(options.suitePath ?? new URL('../../../../examples/chat/repository-scenarios-v1.json', import.meta.url));
    const suite = validateRepositorySuite(JSON.parse(suiteBytes.toString('utf8')), primary.documents);
    const followupsBytes = options.followupsSuitePath ? readFileSync(options.followupsSuitePath) : undefined;
    const followupsSuite = followupsBytes ? validateRepositoryFollowups(JSON.parse(followupsBytes.toString('utf8')), primary.documents) : undefined;
    const researchBytes = options.researchSuitePath ? readFileSync(options.researchSuitePath) : undefined;
    const researchSuite = researchBytes ? validateRepositoryResearch(JSON.parse(researchBytes.toString('utf8')), primary.documents) : undefined;
    const output = resolve(options.outputDirectory);
    mkdirSync(output, { recursive: true });
    const rawPath = resolve(output, 'requests.jsonl');
    writeFileSync(rawPath, '', { flag: 'wx' });
    const requestTimings: number[] = [];
    const healthTimings: number[] = [];
    const transcript: string[] = ['# Captured repository chat examples', '',
        'These are actual HTTP responses. Gold labels are used only by the evaluator. Source coordinates refer to normalized snapshot text.', ''];
    const results: ScenarioResult[] = [];
    const followupResults: ScenarioResult[] = [];
    const researchResults: ScenarioResult[] = [];
    const created = new Set<string>();
    let scenarioId = 'setup';
    let peakClientRssBytes = process.memoryUsage().rss;
    async function call<T>(path: string, method = 'GET', body?: unknown): Promise<HttpResult<T>> {
        const started = performance.now();
        const request = { scenarioId, time: new Date().toISOString(), method, path, ...(body === undefined ? {} : { body }) };
        try {
            const response = await fetch(`${options.baseUrl.replace(/\/$/, '')}/api/v1/${path}`, {
                method, headers: { 'content-type': 'application/json', ...(options.token ? { authorization: `Bearer ${options.token}` } : {}) },
                ...(body === undefined ? {} : { body: JSON.stringify(body) }), signal: AbortSignal.timeout(30000),
            });
            const value = await response.json() as T;
            const durationMs = performance.now() - started;
            appendFileSync(rawPath, JSON.stringify({ request, response: { status: response.status, body: value }, durationMs }) + '\n');
            (path === 'health' ? healthTimings : requestTimings).push(durationMs);
            peakClientRssBytes = Math.max(peakClientRssBytes, process.memoryUsage().rss);
            return { status: response.status, value, durationMs };
        } catch (error) {
            const durationMs = performance.now() - started;
            (path === 'health' ? healthTimings : requestTimings).push(durationMs);
            appendFileSync(rawPath, JSON.stringify({ request, error: error instanceof Error ? error.message : String(error), durationMs }) + '\n');
            throw error;
        }
    }
    function requireStatus<T>(response: HttpResult<T>, status = 200): T {
        if (response.status !== status) throw new Error(`Expected HTTP ${status}, received ${response.status}: ${JSON.stringify(response.value)}`);
        return response.value;
    }
    const knowledge = requireStatus(await call<Knowledge>('knowledge/repository'));
    if (!knowledge.ready) throw new Error(`Repository unavailable: ${knowledge.error ?? 'unknown'}`);
    const identity = knowledge.snapshots.find(item => item.commit === primary.snapshot.manifest.source.commit &&
        item.manifestSha256 === primary.snapshot.manifestSha256);
    if (!identity) throw new Error('API has not loaded the exact supplied primary snapshot');
    transcript.push(`Snapshot: ${identity.repository} at ${identity.commit}.`, '',
        `Manifest SHA-256: ${identity.manifestSha256}. Suite SHA-256: ${sha256(suiteBytes)}.`, '');
    const secondIdentity = secondary && knowledge.snapshots.find(item => item.commit === secondary.snapshot.manifest.source.commit &&
        item.manifestSha256 === secondary.snapshot.manifestSha256);
    if (secondary && !secondIdentity) throw new Error('API has not loaded the exact supplied additional snapshot');
    async function create(pin: Identity = identity!): Promise<Conversation> {
        const conversation = requireStatus(await call<Conversation>('conversations', 'POST', {
            scope: 'repository', snapshot: pin, title: `repository-evaluation-${randomUUID()}`,
        }), 201);
        created.add(conversation.id);
        return conversation;
    }
    async function restart() {
        if (!options.restart) throw new Error('This scenario requires a real API restart hook');
        await options.restart();
        for (let attempt = 0; attempt < 100; attempt++) {
            try { if ((await call('health')).status === 200) return; } catch { /* Service is restarting. */ }
            await new Promise(resolve => setTimeout(resolve, 100));
        }
        throw new Error('API did not recover after restart');
    }
    async function send(conversation: Conversation, turn: ScenarioTurn, checks: Assertion[],
        pin: Identity = identity!, index = primary): Promise<Conversation> {
        const request = { requestId: randomUUID(), revision: conversation.revision, content: turn.content,
            answerMode: 'sources', autoSearch: false, rememberSources: false,
            ...(turn.repositoryResearch ? { repositoryResearch: turn.repositoryResearch } : {}) };
        const result = requireStatus(await call<ChatSendResponse>(`conversations/${conversation.id}/messages`, 'POST', request));
        checks.push({ name: 'response stays in the requested conversation', passed: result.conversation.id === conversation.id,
            turn: turn.content, conversationId: conversation.id });
        checks.push({ name: 'turn increments revision twice', passed: result.conversation.revision === conversation.revision + 2,
            turn: turn.content, conversationId: conversation.id });
        checks.push(...scoreRepositoryReply(turn.expected, result.conversation, index.documents, pin).map(assertion =>
            ({ ...assertion, turn: turn.content, conversationId: conversation.id })));
        return result.conversation;
    }
    async function context(scenario: RepositoryScenario, checks: Assertion[]) {
        const sessions = new Map<string, Conversation>();
        for (const turn of scenario.turns) {
            const key = turn.session ?? 'main';
            let conversation = sessions.get(key) ?? await create();
            if (turn.restartBefore) {
                await restart();
                const loaded = requireStatus(await call<Conversation>(`conversations/${conversation.id}`));
                checks.push({ name: 'restart preserves complete transcript and context', passed: isDeepStrictEqual(loaded, conversation) });
                conversation = loaded;
            }
            conversation = await send(conversation, turn, checks);
            sessions.set(key, conversation);
        }
        checks.push({ name: 'sessions are distinct', passed: new Set([...sessions.values()].map(item => item.id)).size === sessions.size });
    }
    async function refresh(scenario: RepositoryScenario, checks: Assertion[]) {
        let conversation = await create();
        conversation = await send(conversation, scenario.turns[0]!, checks);
        conversation = await send(conversation, scenario.turns[1]!, checks);
        const oldMessages = structuredClone(conversation.messages);
        if (scenario.operation === 'invalid-snapshot') {
            const switched = await call(`conversations/${conversation.id}/repository`, 'POST', {
                revision: conversation.revision, snapshot: { ...identity, commit: 'f'.repeat(40), manifestSha256: 'f'.repeat(64) },
            });
            checks.push({ name: 'unknown snapshot rejected', passed: switched.status === 404 });
            const loaded = requireStatus(await call<Conversation>(`conversations/${conversation.id}`));
            checks.push({ name: 'failed switch has no state mutation', passed: isDeepStrictEqual(loaded, conversation) });
            for (const turn of scenario.turns.slice(2)) conversation = await send(conversation, turn, checks);
            return;
        }
        if (!secondary || !secondIdentity) throw new Error('Refresh scenario requires an independently loaded additional snapshot');
        const switched = requireStatus(await call<Conversation>(`conversations/${conversation.id}/repository`, 'POST', {
            revision: conversation.revision, snapshot: secondIdentity,
        }));
        checks.push({ name: 'explicit switch increments revision once', passed: switched.revision === conversation.revision + 1 });
        checks.push({ name: 'old messages retain their original citations', passed: isDeepStrictEqual(switched.messages, oldMessages) });
        checks.push({ name: 'snapshot switch clears previous task and topic context',
            passed: isDeepStrictEqual(switched.repositoryState, { snapshot: secondIdentity }) });
        conversation = switched;
        if (scenario.operation === 'stale-evidence') {
            const primaryByPath = new Map(primary.documents.flatMap(document => document.sources.map(source => [source.path, document.sha256] as const)));
            checks.push({ name: 'fixture actually changes or removes a source', passed: [...primaryByPath].some(([path, hash]) =>
                !secondary.documents.some(document => document.sha256 === hash && document.sources.some(source => source.path === path))) });
        }
        for (const turn of scenario.turns.slice(2)) conversation = await send(conversation, turn, checks, secondIdentity, secondary);
        const activeTask = conversation.messages.at(-1)?.action?.taskId;
        const taskDocument = secondary.documents.find(document => document.sources.some(source => source.path === 'docs/codebase-tasks.json'));
        const reviewedTasks = taskDocument ? JSON.parse(taskDocument.text).tasks as {
            id: string; status: string; priority: number; dependsOn: string[];
        }[] : [];
        const nextTask = reviewedTasks.filter(task => task.status !== 'verified' &&
            task.dependsOn.every(id => reviewedTasks.some(candidate => candidate.id === id && candidate.status === 'verified')))
            .sort((left, right) => left.priority - right.priority || left.id.localeCompare(right.id))[0];
        checks.push({ name: 'explicit next action selects the eligible task from the new snapshot', passed: !!nextTask && activeTask === nextTask.id });
        if (scenario.operation === 'switch') {
            const previous = oldMessages.at(-1)?.action?.taskId;
            const current = (conversation.messages.at(-1) as { action?: { taskId?: string } } | undefined)?.action?.taskId;
            const registry = secondary.documents.find(document => document.sources.some(source => source.path === 'docs/codebase-tasks.json'));
            const tasks = registry ? JSON.parse(registry.text).tasks as { id: string; status: string }[] : [];
            checks.push({ name: 'completed reviewed task changes the recommendation', passed: !!previous && !!current && previous !== current &&
                tasks.some(task => task.id === previous && task.status === 'verified') });
        }
        if (scenario.operation === 'failed-check') {
            const captured = JSON.parse(readFileSync(resolve(options.additionalSnapshotDirectory!, 'verification.json'), 'utf8'));
            const reply = conversation.messages.at(-1);
            const report = reply?.verification;
            const registry = secondary.documents.find(document => document.sources.some(source => source.path === 'docs/codebase-tasks.json'));
            const tasks = registry ? JSON.parse(registry.text).tasks as { id: string; status: string }[] : [];
            checks.push({ name: 'failed verification is the actual operator-captured report', passed: !!report && isDeepStrictEqual(report, captured) &&
                report.provenance === 'operator-captured' && report.status === 'failed' && isDeepStrictEqual(report.snapshot, secondIdentity) &&
                report.checks.some(check => check.exitCode !== 0) });
            checks.push({ name: 'failed checks leave reviewed task unverified', passed: !!report && reply?.action?.verification === 'proposed' &&
                tasks.some(task => task.id === report.taskId && task.status !== 'verified') });
        }
        if (scenario.operation === 'stale-evidence') {
            const path = 'docs/repository-fixture-note.md';
            const original = primary.documents.find(document => document.sources.some(source => source.path === path));
            if (!original?.text.includes('The repository fixture flag is alpha.') ||
                secondary.documents.some(document => document.sources.some(source => source.path === path)))
                throw new Error('Stale-evidence scenario requires fixture flag alpha in A and that source removed in B');
            const oldSession = await create();
            const oldReply = await send(oldSession, { content: 'What is the repository fixture flag?', expected: { outcome: 'supported',
                evidence: [{ path, quote: 'The repository fixture flag is alpha.' }] } }, checks);
            const switched = requireStatus(await call<Conversation>(`conversations/${oldReply.id}/repository`, 'POST', {
                revision: oldReply.revision, snapshot: secondIdentity,
            }));
            await send(switched, { content: 'What is the repository fixture flag?', expected: { outcome: 'unsupported',
                excludes: ['the repository fixture flag is alpha'] } }, checks, secondIdentity, secondary);
        }
    }
    async function operations(scenario: RepositoryScenario, checks: Assertion[]) {
        let conversation = await create();
        const first = scenario.turns[0]!;
        if (scenario.operation === 'retry-conflict') {
            const body = { requestId: randomUUID(), revision: 0, content: first.content, autoSearch: false };
            const replies = await Promise.all([call<ChatSendResponse>(`conversations/${conversation.id}/messages`, 'POST', body),
                call<ChatSendResponse>(`conversations/${conversation.id}/messages`, 'POST', body)]);
            checks.push({ name: 'concurrent identical retries both complete', passed: replies.every(reply => reply.status === 200) });
            const firstReply = requireStatus(replies[0]!);
            const secondReply = requireStatus(replies[1]!);
            checks.push({ name: 'retry returns one authoritative turn', passed: firstReply.conversation.revision === 2 &&
                firstReply.conversation.messages.length === 2 && isDeepStrictEqual(firstReply, secondReply) });
            checks.push(...scoreRepositoryReply(first.expected, firstReply.conversation, primary.documents, identity!));
            conversation = firstReply.conversation;
            const stale = await call(`conversations/${conversation.id}/messages`, 'POST', { ...body, requestId: randomUUID() });
            const mutated = await call(`conversations/${conversation.id}/messages`, 'POST', { ...body, content: 'different content' });
            checks.push({ name: 'stale revision conflicts', passed: stale.status === 409 });
            checks.push({ name: 'request ID reuse with changed content conflicts', passed: mutated.status === 409 });
        } else if (scenario.operation === 'cancel-recover') {
            const rejected = await call(`conversations/${conversation.id}/messages`, 'POST', {
                requestId: randomUUID(), revision: 0, content: '\0', autoSearch: false,
            });
            checks.push({ name: 'invalid admission fails without leaving pending work', passed: rejected.status === 400 &&
                isDeepStrictEqual(requireStatus(await call<Conversation>(`conversations/${conversation.id}`)), conversation) });
            const requestId = randomUUID();
            const pending = call<ChatSendResponse>(`conversations/${conversation.id}/messages`, 'POST', {
                requestId, revision: 0, content: first.content, autoSearch: false,
            });
            const cancelled = await call(`conversations/${conversation.id}/cancel`, 'POST', { requestId });
            const returned = requireStatus(await pending);
            conversation = requireStatus(await call<Conversation>(`conversations/${conversation.id}`));
            checks.push({ name: 'cancel has documented race outcome', passed: [200, 404].includes(cancelled.status) });
            checks.push({ name: 'cancel or completion leaves no pending messages', passed: !conversation.messages.some(message => message.status === 'pending') });
            checks.push({ name: 'returned transcript matches durable terminal state', passed: isDeepStrictEqual(conversation, returned.conversation) });
            checks.push({ name: 'terminal outcome identified without claiming forced cancellation', passed: ['complete', 'cancelled'].includes(conversation.messages.at(-1)?.status ?? ''),
                detail: `cancel HTTP ${cancelled.status}; reply ${conversation.messages.at(-1)?.status}` });
            if (conversation.messages.at(-1)?.status === 'complete')
                checks.push(...scoreRepositoryReply(first.expected, conversation, primary.documents, identity!));
            if (conversation.messages.at(-1)?.status === 'cancelled') {
                // A cancelled turn must not become factual context; re-establish the task explicitly.
                conversation = await send(conversation, first, checks);
            }
        } else if (scenario.operation === 'concurrent-sessions') {
            const sessions = [conversation, await create(), await create(), await create()];
            const turns = sessions.map((_session, index) => ({ ...first, content: `${first.content} (Session ${index + 1}.)` }));
            const work = Promise.all(sessions.map((session, index) => send(session, turns[index]!, checks)));
            const health = await call('health');
            checks.push({ name: 'health responds during concurrent work', passed: health.status === 200 });
            const replies = await work;
            checks.push({ name: 'concurrent sessions keep independent messages and revisions', passed: replies.every((reply, index) =>
                reply.id === sessions[index]!.id && reply.messages.length === 2 && reply.revision === 2 && reply.messages[0]?.content === turns[index]!.content) &&
                new Set(replies.flatMap(reply => reply.messages.map(message => message.id))).size === replies.length * 2 });
            conversation = replies[0]!;
        } else {
            conversation = await send(conversation, first, checks);
            await restart();
            const loaded = requireStatus(await call<Conversation>(`conversations/${conversation.id}`));
            checks.push({ name: 'HTTP restart preserves exact durable session', passed: isDeepStrictEqual(loaded, conversation) });
            conversation = loaded;
        }
        for (const turn of scenario.turns.slice(1)) conversation = await send(conversation, turn, checks);
    }
    const startedAt = new Date().toISOString();
    async function runScenario(scenario: RepositoryScenario): Promise<ScenarioResult> {
        scenarioId = scenario.id;
        const started = performance.now();
        const assertions: Assertion[] = [];
        try {
            if (scenario.group === 'refresh') await refresh(scenario, assertions);
            else if (scenario.group === 'http') await operations(scenario, assertions);
            else await context(scenario, assertions);
            return { id: scenario.id, group: scenario.group, split: scenario.split,
                status: assertions.every(assertion => assertion.passed) ? 'passed' : 'failed', assertions, durationMs: performance.now() - started };
        } catch (error) {
            return { id: scenario.id, group: scenario.group, split: scenario.split, status: 'failed', assertions,
                durationMs: performance.now() - started, error: error instanceof Error ? error.message : String(error) };
        }
    }
    try {
        for (const scenario of suite.scenarios) {
            const selected = options.lane === 'full' || scenario.group === 'questions' || scenario.id === 'core-development-loop';
            if (!selected) {
                results.push({ id: scenario.id, group: scenario.group, split: scenario.split, status: 'not-run', assertions: [], durationMs: 0 });
                continue;
            }
            results.push(await runScenario(scenario));
        }
        for (const scenario of followupsSuite?.scenarios ?? []) followupResults.push(await runScenario(scenario));
        for (const scenario of researchSuite?.scenarios ?? []) researchResults.push(await runScenario(scenario));
    } finally {
        scenarioId = 'cleanup';
        for (const id of created) {
            try {
                const current = requireStatus(await call<Conversation>(`conversations/${id}`));
                requireStatus(await call(`conversations/${id}`, 'DELETE', { revision: current.revision, forgetMemory: true }));
            } catch (error) {
                results.push({ id: `cleanup:${id}`, group: 'cleanup', split: 'development', status: 'failed', assertions: [], durationMs: 0,
                    error: error instanceof Error ? error.message : String(error) });
            }
        }
    }
    const metric = (selected: ScenarioResult[]) => ({ numerator: selected.filter(row => row.status === 'passed').length,
        denominator: selected.length, failed: selected.filter(row => row.status === 'failed').map(row => row.id),
        notRun: selected.filter(row => row.status === 'not-run').map(row => row.id) });
    const questions = results.filter(row => row.group === 'questions');
    const answerableIds = new Set(suite.scenarios.filter(row => row.group === 'questions' && row.turns[0]!.expected.outcome !== 'unsupported').map(row => row.id));
    const known = metric(questions.filter(row => answerableIds.has(row.id)));
    const unknown = metric(questions.filter(row => !answerableIds.has(row.id)));
    const core = results.find(row => row.id === 'core-development-loop')!;
    const safetyNames = ['zero public research', 'all citations validate', 'repository scope and pinned snapshot',
        'response stays in the requested conversation', 'action is proposed, not claimed executed',
        'action references existing paths', 'action commands appear in reviewed snapshot'];
    const safetyFailures = results.flatMap(row => row.assertions.filter(check => safetyNames.includes(check.name) && !check.passed).map(check => `${row.id}: ${check.name}`));
    const citationMetric = (selected: ScenarioResult[]) => {
        const checks = selected.flatMap(row => row.assertions.filter(check => check.name === 'all citations validate'));
        return { numerator: checks.reduce((sum, check) => sum + (check.numerator ?? 0), 0),
            denominator: checks.reduce((sum, check) => sum + (check.denominator ?? 0), 0),
            failed: selected.filter(row => row.assertions.some(check => check.name === 'all citations validate' && !check.passed)).map(row => row.id) };
    };
    const citations = citationMetric(results);
    const firstDeliveryPassed = known.numerator >= 16 && known.denominator === 18 && unknown.numerator === 6 &&
        unknown.denominator === 6 && core.status === 'passed' && safetyFailures.length === 0 &&
        !results.some(row => row.group === 'cleanup' && row.status === 'failed');
    const contextScore = metric(results.filter(row => row.group === 'context'));
    const refreshScore = metric(results.filter(row => row.group === 'refresh'));
    const httpScore = metric(results.filter(row => row.group === 'http'));
    const fullSuitePassed = firstDeliveryPassed && contextScore.numerator >= 7 &&
        results.find(row => row.id === 'two-session-context')?.status === 'passed' && refreshScore.numerator === 4 &&
        httpScore.numerator === 4 && !results.some(row => row.status === 'not-run');
    const followups = followupsSuite && followupsBytes ? {
        version: followupsSuite.version, purpose: followupsSuite.purpose,
        evaluationOrigin: 'Development-only calibration scenarios; implementation may be adjusted using these cases. The forty regression scenarios and their denominators are unchanged.',
        suiteSha256: sha256(followupsBytes), score: metric(followupResults), citations: citationMetric(followupResults),
        passed: followupResults.length === followupsSuite.scenarios.length && followupResults.every(row => row.status === 'passed') &&
            !results.some(row => row.group === 'cleanup' && row.status === 'failed'),
        safetyFailures: followupResults.flatMap(row => row.assertions.filter(check => safetyNames.includes(check.name) && !check.passed)
            .map(check => `${row.id}: ${check.name}`)), results: followupResults,
    } : null;
    const repositoryResearch = researchSuite && researchBytes ? {
        version: researchSuite.version, purpose: researchSuite.purpose,
        evaluationOrigin: 'Development-only software investigation scenarios; bounded source relationships and proposed checks are not runtime analysis or evidence of test execution.',
        suiteSha256: sha256(researchBytes), score: metric(researchResults), citations: citationMetric(researchResults),
        passed: researchResults.length === researchSuite.scenarios.length && researchResults.every(row => row.status === 'passed') &&
            !results.some(row => row.group === 'cleanup' && row.status === 'failed'),
        safetyFailures: researchResults.flatMap(row => row.assertions.filter(check =>
            (safetyNames.includes(check.name) || ['all investigation findings bind existing paths to cited evidence',
                'all investigation check proposals match cited reviewed task records', 'investigation uses the conversation snapshot',
                'investigation distinguishes check proposals from execution'].includes(check.name)) && !check.passed)
            .map(check => `${row.id}: ${check.name}`)), results: researchResults,
    } : null;
    const report = {
        version: 1, startedAt, completedAt: new Date().toISOString(), lane: options.lane ?? 'first',
        evaluationOrigin: 'Frozen scenario fixtures reused for implementation regression testing. They are not an independent held-out generalization measurement.',
        snapshots: { primary: identity, additional: secondIdentity ?? null }, suiteSha256: sha256(suiteBytes),
        implementationHashOrigin: 'Evaluator checkout files; snapshot source identity is recorded separately. These hashes do not attest a remote server build.',
        implementationSha256: Object.fromEntries(['evaluation/repository-chat.ts', 'evaluation/repository-chat-runner.ts',
            'evaluation/repository-chat-metrics.ts', 'knowledge/retrieval.ts', 'chat/repository.ts', 'chat/repository-retrieval.ts',
            'chat/repository-context.ts', 'chat/repository-investigation.ts', 'chat/repository-code-index.ts', 'chat/service.ts',
            'chat/routes.ts', 'server.ts', '../../shared/chat.ts', '../../shared/repository.ts'].map(path =>
            [path, sha256(readFileSync(new URL(`../${path}`, import.meta.url)))])),
        environment: { node: process.version, platform: platform(), hostname: hostname(), cpu: cpus()[0]?.model ?? 'unknown',
            logicalCpus: cpus().length, hostMemoryBytes: totalmem(), measurementScope: 'HTTP client host; API worker memory and queue time are not instrumented' },
        corpus: { documents: primary.documents.length, passages: primary.retriever.passages, paths: primary.snapshot.manifest.entries.length },
        scores: { answerable: known, unsupported: unknown, citations, context: contextScore, refresh: refreshScore, http: httpScore },
        firstDeliveryPassed, fullSuitePassed, allScenariosPassed: results.every(row => row.status === 'passed'), safetyFailures, followups, repositoryResearch,
        gatePolicy: { answerable: 'at least16/18', unsupported: '6/6', context: 'at least7/8, including the full core loop and two-session isolation',
            refresh: '4/4', http: '4/4', invariants: 'All citations, snapshot/session isolation, zero public queries and action provenance pass; no cleanup failures or unrun scenarios for the full gate.' },
        operationCoverage: { cancellation: 'Completion may win the cancellation race. Raw responses and assertions identify the observed terminal state; this does not claim forced in-flight cancellation.',
            failureRecovery: 'Rejected admission and subsequent successful turns are exercised over HTTP. Interrupted in-flight process recovery remains covered by separate deterministic service tests.' },
        performance: { requests: latencyPercentiles(requestTimings), health: latencyPercentiles(healthTimings),
            peakClientRssBytes, serverPeakRssBytes: null, queueTimeMs: null, enforcedLatencyBudgetMs: null }, results,
    };
    writeFileSync(resolve(output, 'report.json'), JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
    writeFileSync(resolve(output, 'transcript.md'), transcriptFromJsonl(rawPath, transcript), { flag: 'wx' });
    return report;
}
