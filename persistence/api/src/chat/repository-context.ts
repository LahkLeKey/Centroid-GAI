/** Bounded reference classification; explicit subjects take precedence over conversational pronouns. */
import { retrievalTerms } from '../knowledge/retrieval.ts';

const referenceWords = new Set(('file files path paths module modules function functions implements implement implemented implementation one ones ' +
    'test tests testing check checks verify verification validate validation change changes ' +
    'affected affect affects first should would could need needed needs necessary use run start begin ' +
    'cover covers belong belongs does do me tell about prerequisite prerequisites dependency dependencies there any ' +
    'required requirements now next again today then here exactly quickly safely please afterwards afterward ' +
    'still remaining remain remains task tasks step steps action actions it its that those these them ' +
    'we our us i my you your get give show explain list more detail details mean means meant ' +
    'choose select selected pick focus on for to also actually already yet how can will which what ' +
    'only just current currently continue continuing proceed proceeding before after did edit edits editing ' +
    'two both each either latter former second third ' +
    'repository codebase chatbot project unfinished smallest priority prioritize verify verifying verification').split(' '));

export function referenceQuestion(question: string): boolean {
    const terms = retrievalTerms(question);
    return terms.length > 0 && terms.every(term => referenceWords.has(term));
}

export function fileReferences(question: string, paths: readonly string[]) {
    const mentions = [...new Set(question.match(/(?<![\w./-])(?:[\w.-]+\/)*[\w.-]+\.(?:c|h|cpp|hpp|ts|tsx|js|mjs|py|md|json|ya?ml|txt|sql|prisma)\b|(?<![\w./-])(?:[\w.-]+\/)*Dockerfile\b/g) ?? [])];
    const found: string[] = [], missing: string[] = [];
    for (const mention of mentions) {
        const matches = paths.includes(mention) ? [mention] : mention.includes('/') ? [] :
            paths.filter(path => path.split('/').at(-1) === mention);
        if (!matches.length) missing.push(mention);
        for (const path of matches) if (!found.includes(path)) found.push(path);
    }
    return { found, missing };
}

interface NamedTask { id: string; title: string; keywords: string[]; priority: number }
const taskTerms = (text: string) => retrievalTerms(text).map(term => term.replace(/s$/, ''));

/** Exact IDs and separately named alternatives cannot lose to a larger bag-of-words score. */
export function matchingTasks<T extends NamedTask>(tasks: readonly T[], question: string): T[] {
    const exact = (text: string) => {
        const lower = text.toLowerCase();
        return tasks.filter(task => new RegExp(`(?<![a-z0-9-])${task.id}(?![a-z0-9-])`).test(lower) || lower.includes(task.title.toLowerCase()));
    };
    const rank = (text: string) => {
        const query = new Set(taskTerms(text));
        const scores = tasks.map(task => ({ task, score: new Set(taskTerms(task.id + ' ' + task.title + ' ' + task.keywords.join(' '))) }))
            .map(entry => ({ task: entry.task, score: [...entry.score].filter(term => query.has(term)).length }))
            .filter(entry => entry.score >= 2).sort((a, b) => b.score - a.score || a.task.priority - b.task.priority);
        return scores.filter(entry => entry.score === scores[0]!.score).map(entry => entry.task);
    };
    const resolve = (text: string) => { const named = exact(text); return named.length ? named : rank(text); };
    const clauses = question.split(/\b(?:or|and|versus|vs)\b/i);
    if (clauses.length > 1) {
        const alternatives = [...new Set(clauses.flatMap(resolve))];
        if (alternatives.length > 1) return alternatives;
    }
    return resolve(question);
}
