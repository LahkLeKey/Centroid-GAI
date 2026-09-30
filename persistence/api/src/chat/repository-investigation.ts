/** Deterministic software research over the selected source snapshot. No execution or remote lookup. */
import type { ChatSource } from '../../../shared/chat.ts';
import type { RepositoryInvestigation, RepositoryResearchRequest, RepositorySnapshotIdentity, RepositoryState } from '../../../shared/repository.ts';
import { fileReferences, referenceQuestion } from './repository-context.ts';
import { retrievalTerms } from '../knowledge/retrieval.ts';
import type { createRepositoryCodeIndex } from './repository-code-index.ts';

export const investigationContinuation = (question: string): boolean =>
    /^(?:continue|resume|repeat)\s+(?:(?:this|that|the)\s+)?(?:investigation|research)(?:\s+please)?[.!?]?$/i.test(question.trim());

export function validRepositoryResearch(value: unknown): value is RepositoryResearchRequest {
    if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
    const request = value as RepositoryResearchRequest;
    if (Object.keys(value).some(key => !['kind', 'target'].includes(key)) ||
        !['impact', 'symbol'].includes(request.kind) || typeof request.target !== 'string' ||
        !request.target.trim() || request.target !== request.target.trim() || Buffer.byteLength(request.target) > 1000 ||
        /[\x00-\x1f\x7f]/.test(request.target)) return false;
    return request.kind === 'symbol' ? /^[A-Za-z_$][\w$]{0,119}$/.test(request.target) :
        !/^[\\/]|^[A-Za-z]:|\\/.test(request.target) && request.target.split('/').every(part => part && part !== '.' && part !== '..');
}

/** Natural-language shortcuts use explicit subjects; ordinary factual questions keep their existing route. */
export function inferredRepositoryResearch(question: string, state: RepositoryState, paths: readonly string[]): RepositoryResearchRequest | undefined {
    const symbol = /^(?:find|show|research|investigate)\s+(?:(?:exact\s+)?(?:references|occurrences|usages)\s+(?:to|of|for)\s+|(?:the\s+)?symbol\s+)[`'"]?([A-Za-z_$][\w$]{0,119})[`'"]?(?:\(\))?[?.!]?$/i.exec(question.trim());
    if (symbol) return { kind: 'symbol', target: symbol[1]! };
    const mentions = fileReferences(question, paths);
    const impact = /\b(?:research|investigate|investigation|impact|dependents|dependencies|imports?|includes?)\b/i.test(question) || /\bdepends?\s+on\b/i.test(question);
    const choices = [...mentions.found, ...mentions.missing];
    if (impact && choices.length === 1) return { kind: 'impact', target: choices[0]! };
    if (state.investigation && referenceQuestion(question) && /\b(?:checks?|tests?|verify|verification|files?|paths?|prerequisites?)\b/i.test(question)) return state.investigation;
    const researchWords = /\b(?:dependents?|dependencies|depends?|imports?|includes?|references?|referenced|used|occurrences?|usages?|findings?|evidence|other)\b/gi;
    if (state.investigation && researchWords.test(question)) {
        const remainder = question.replace(researchWords, ' ');
        if (!retrievalTerms(remainder).length || referenceQuestion(remainder)) return state.investigation;
    }
    if (state.investigation && investigationContinuation(question)) return state.investigation;
    return undefined;
}

interface ResearchTask {
    id: string; title: string; paths: string[]; checks: { command: string; cwd: string }[];
}
interface InvestigationInputs {
    snapshot: RepositorySnapshotIdentity;
    index: ReturnType<typeof createRepositoryCodeIndex>;
    tasks: readonly ResearchTask[];
    fileSources: (path: string) => ChatSource[];
    taskSources: (taskId: string) => ChatSource[];
}
const testPath = (path: string) => /(?:^|\/)(?:tests?|__tests__|e2e)(?:\/|$)|(?:\.test|\.spec)\.[^.]+$|(?:^|\/)test_[^/]+$/.test(path);

export function investigateRepository(request: RepositoryResearchRequest, input: InvestigationInputs): {
    investigation: RepositoryInvestigation; sources: ChatSource[]; content: string;
} | undefined {
    const sources: ChatSource[] = [];
    const findings: RepositoryInvestigation['findings'][number][] = [];
    const bind = (source: ChatSource): string => {
        const bound = { ...source, manifestSha256: input.snapshot.manifestSha256 };
        if (!sources.some(previous => previous.path === bound.path && previous.id === bound.id)) sources.push(bound);
        return bound.id;
    };
    let truncated = false;
    let unresolved = 0;
    if (request.kind === 'impact') {
        const inspection = input.index.inspect(request.target, 20);
        if (!inspection) return;
        const target = input.fileSources(request.target)[0];
        if (!target) return;
        findings.push({ kind: 'target', path: request.target, sourceId: bind(target) });
        const dependencies = inspection.dependencies.slice(0, 6), dependents = inspection.dependents.slice(0, 7);
        for (const edge of dependencies) findings.push({ kind: 'dependency', path: edge.from, relatedPath: edge.to, sourceId: bind(edge.source) });
        for (const edge of dependents) findings.push({ kind: 'dependent', path: edge.from, relatedPath: edge.to, sourceId: bind(edge.source) });
        truncated = inspection.truncated || inspection.dependencies.length > dependencies.length || inspection.dependents.length > dependents.length;
        unresolved = inspection.unresolved.length;
    } else {
        const result = input.index.findSymbol(request.target, 12);
        if (!result.matches.length) return;
        for (const match of result.matches) findings.push({ kind: 'symbol-occurrence', path: match.path, sourceId: bind(match.source) });
        truncated = result.truncated;
    }
    const subjectPaths = request.kind === 'impact' ? [request.target] : findings.map(finding => finding.path);
    const related = input.tasks.filter(task => task.paths.some(path => subjectPaths.includes(path)));
    const relatedTasks: RepositoryInvestigation['relatedTasks'][number][] = [];
    for (const task of related.slice(0, 3)) {
        const source = input.taskSources(task.id)[0];
        if (source) relatedTasks.push({ taskId: task.id, title: task.title, checks: task.checks, sourceId: bind(source) });
        else truncated = true;
    }
    truncated ||= related.length > 3;
    const limitations = [
        'Only the pinned snapshot is searched; excluded files and uncommitted changes are unavailable.',
        request.kind === 'impact' ? 'Dependencies cover TypeScript/JavaScript literal imports and C quoted includes only. Build flags, aliases, computed loading, other languages and transitive effects are not evaluated.' :
            'Symbol occurrences are exact lexical matches, including comments and documentation; they do not prove calls, definitions or runtime behavior.',
        'Test file references identify places to inspect, not demonstrated coverage. Proposed checks have not been run.',
    ];
    if (unresolved) limitations.push(`The index returned ${unresolved} import/include references that could not be resolved uniquely within the snapshot.`);
    if (truncated) limitations.push('Results are bounded and incomplete; narrow the target before drawing conclusions about missing references.');
    const investigation: RepositoryInvestigation = { ...request, snapshot: input.snapshot, findings, relatedTasks, limitations, truncated };
    const number = (id: string, path?: string) => sources.findIndex(source => source.id === id && (!path || source.path === path)) + 1;
    const findingText = findings.map(finding => {
        const label = finding.kind === 'dependency' ? `${finding.path} imports/includes ${finding.relatedPath}` :
            finding.kind === 'dependent' ? `${finding.path} directly references ${finding.relatedPath}` :
                finding.kind === 'symbol-occurrence' ? `Exact occurrence in ${finding.path}` : `Selected file: ${finding.path}`;
        return `- ${label}${testPath(finding.path) ? ' (test file)' : ''} [${number(finding.sourceId, finding.path)}]`;
    }).join('\n');
    const checks = relatedTasks.length ? relatedTasks.map(task => `${task.title} [${number(task.sourceId)}]\n` +
        task.checks.map(check => `- ${check.command} (working directory: ${check.cwd}; proposed, not run)`).join('\n')).join('\n\n') :
        'No reviewed task lists these subject files. No check command is inferred from filenames.';
    const excerpts = sources.map((source, index) => `[${index + 1}] ${source.path} (snapshot-normalized-lines ${source.startLine}-${source.endLine})\n${source.excerpt}`).join('\n\n');
    const content = `Software research: ${request.kind === 'impact' ? 'change impact' : 'symbol references'} for ${request.target}\n` +
        `Snapshot: ${input.snapshot.commit}\n\nFindings:\n${findingText}\n\nReviewed verification proposals:\n${checks}\n\n` +
        `Limits:\n${limitations.map(limit => `- ${limit}`).join('\n')}\n\nEvidence:\n${excerpts}`;
    return { investigation, sources, content };
}
