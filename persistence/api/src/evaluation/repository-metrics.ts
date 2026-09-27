/** Score source retrieval separately from citation integrity and annotated answer support. */
import type { Document } from './codebase-data.ts';
import { validPassage } from '../knowledge/retrieval.ts';
import type { Hit, RetrievalResult } from '../knowledge/retrieval.ts';

export interface RepositoryQuestion {
    id: string;
    question: string;
    evidence: { path: string; quote: string }[];
}

/** Fail on stale or missing gold evidence, rather than quietly changing evaluation denominators. */
export function validateQuestions(value: unknown, documents: Document[]): RepositoryQuestion[] {
    const suite = value as { version?: number; questions?: RepositoryQuestion[] } | null;
    if (!suite || suite.version !== 1 || !Array.isArray(suite.questions) || !suite.questions.length) throw new Error('Invalid question suite');
    const ids = new Set<string>();
    for (const row of suite.questions) {
        if (!row || typeof row.id !== 'string' || !row.id.trim() || ids.has(row.id) ||
            typeof row.question !== 'string' || !row.question.trim() || !Array.isArray(row.evidence)) throw new Error('Invalid or duplicate question');
        ids.add(row.id);
        for (const evidence of row.evidence) {
            if (!evidence || typeof evidence.path !== 'string' || typeof evidence.quote !== 'string' || !evidence.quote.trim() ||
                !documents.some(document => document.sources.some(source => source.path === evidence.path) && document.text.includes(evidence.quote))) {
                throw new Error(`Missing gold evidence in snapshot: ${row.id}`);
            }
        }
    }
    if (!suite.questions.some(row => row.evidence.length) || !suite.questions.some(row => !row.evidence.length)) {
        throw new Error('Suite needs answerable and unanswerable questions');
    }
    return suite.questions;
}

/** Gold support requires both the expected path and the literal supporting quote in the returned span. */
function supports(hit: Hit, evidence: RepositoryQuestion['evidence'][number]) {
    return hit.citations.some(citation => citation.path === evidence.path) && hit.text.includes(evidence.quote);
}

export function scoreQuestion(question: RepositoryQuestion, result: RetrievalResult, documents: Document[]) {
    const files = [...new Set(question.evidence.map(evidence => evidence.path))];
    const supported = result.hits.map(hit => question.evidence.some(evidence => supports(hit, evidence)));
    const rank = supported.indexOf(true);
    return {
        id: question.id, question: question.question, expectedEvidence: question.evidence,
        answerable: files.length > 0, result,
        sourceRecall: files.length ? files.filter(path => result.hits.some(hit => hit.citations.some(c => c.path === path))).length / files.length : null,
        passageRecall: question.evidence.length ? question.evidence.filter(evidence => result.hits.some(hit => supports(hit, evidence))).length / question.evidence.length : null,
        reciprocalRank: files.length ? (rank < 0 ? 0 : 1 / (rank + 1)) : null,
        supportingHit: rank >= 0, supportedPassages: supported.filter(Boolean).length,
        validCitations: result.hits.reduce((sum, hit) => sum + hit.citations.filter(citation =>
            validPassage({ ...hit, citations: [citation] }, documents)).length, 0),
        citations: result.hits.reduce((sum, hit) => sum + hit.citations.length, 0),
        supportedCitations: result.hits.reduce((sum, hit) => sum + hit.citations.filter(citation =>
            question.evidence.some(evidence => evidence.path === citation.path && hit.text.includes(evidence.quote))).length, 0),
    };
}

/** Null means no denominator; in particular an all-abstaining system cannot earn perfect citation scores. */
export function summarizeQuestions(rows: ReturnType<typeof scoreQuestion>[]) {
    const known = rows.filter(row => row.answerable);
    const unknown = rows.filter(row => !row.answerable);
    const ratio = (numerator: number, denominator: number) => denominator ? numerator / denominator : null;
    const totalCitations = rows.reduce((sum, row) => sum + row.citations, 0);
    return {
        questions: rows.length, answerable: known.length, unanswerable: unknown.length,
        sourceRecallAtK: ratio(known.reduce((sum, row) => sum + row.sourceRecall!, 0), known.length),
        passageRecallAtK: ratio(known.reduce((sum, row) => sum + row.passageRecall!, 0), known.length),
        supportingPassageHitAtK: ratio(known.filter(row => row.supportingHit).length, known.length),
        supportingPassageMrrAtK: ratio(known.reduce((sum, row) => sum + row.reciprocalRank!, 0), known.length),
        citationValidity: ratio(rows.reduce((sum, row) => sum + row.validCitations, 0), totalCitations),
        annotatedCitationPrecision: ratio(rows.reduce((sum, row) => sum + row.supportedCitations, 0), totalCitations),
        answerableAbstentionRate: ratio(known.filter(row => row.result.status === 'abstained').length, known.length),
        unanswerableAbstentionRate: ratio(unknown.filter(row => row.result.status === 'abstained').length, unknown.length),
        unanswerableEvidenceRate: ratio(unknown.filter(row => row.result.status === 'evidence').length, unknown.length),
        citations: totalCitations,
    };
}
