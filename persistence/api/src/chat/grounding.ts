import type { ChatDialogueMessage, ChatModelMetadata, ChatReply, ChatSource } from '../../../shared/chat.ts';
import type { ChatGrounding } from '../../../shared/grounding.ts';
import { retrievalTerms } from '../knowledge/retrieval.ts';
import { evidenceUnits, normalizeEvidence, type EvidenceUnit } from './evidence.ts';

export interface AdmittedEvidence extends EvidenceUnit { source: ChatSource }
export interface EvidenceSelection { entries: AdmittedEvidence[]; messages: ChatDialogueMessage[]; reason: string }

/** Rank complete source sentences/lines; lexical relevance is only an admission heuristic. */
export function selectEvidence(question: string, sources: readonly ChatSource[], metadata: ChatModelMetadata): EvidenceSelection {
    if (metadata.protocolVersion < 2) return { entries: [], messages: [], reason: 'legacy model requires retraining for evidence protocol two' };
    const terms = [...new Set(retrievalTerms(question))];
    const prompt = metadata.config.promptWindow ?? 160;
    const budget = metadata.config.evidenceWindow || Math.floor(prompt / 2);
    const ranked = sources.slice(0, 5).flatMap(source => evidenceUnits(source.excerpt).map(unit => {
        const words = new Set(retrievalTerms(unit.text));
        return { ...unit, source, score: terms.filter(term => words.has(term)).length };
    })).filter(unit => unit.score >= Math.max(1, Math.ceil(terms.length / 2)))
        .sort((a, b) => b.score - a.score || a.source.id.localeCompare(b.source.id) || a.start - b.start);
    const entries: AdmittedEvidence[] = [], seen = new Set<string>();
    let used = 0;
    for (const unit of ranked) {
        const normalized = normalizeEvidence(unit.text);
        const cost = normalized.split(' ').length + 2;
        if (seen.has(normalized) || used + cost > budget) continue;
        entries.push(unit); seen.add(normalized); used += cost;
        if (entries.length === 3) break;
    }
    return { entries, messages: entries.map(entry => ({ role: 'evidence', content: entry.text })),
        reason: entries.length ? 'complete relevant source units admitted' : 'no complete relevant passage fits the model evidence budget' };
}

export function groundingFallback(reason: string, selection?: EvidenceSelection, abstained = false): ChatGrounding {
    return { version: 1, mode: 'extractive', status: abstained ? 'abstained' : 'fallback', reason,
        evidenceSourceIds: [...new Set(selection?.entries.map(entry => entry.source.id) ?? [])], claims: [] };
}

/** Only a complete admitted quotation can be emitted as a checked neural result.
 * This validates attribution, not source truth or whether an excerpt fully answers the question. */
export function checkGroundedReply(reply: ChatReply, selection: EvidenceSelection): {
    grounding: ChatGrounding; content?: string; sources?: ChatSource[];
} {
    let reason: string | undefined;
    if (reply.unknownTokens !== 0) reason = 'prompt contains words outside the model vocabulary';
    else if (reply.droppedEvidence !== 0 || !reply.evidenceTokens) reason = 'evidence was omitted or its retention cannot be verified';
    else if (reply.finishReason !== 'eos') reason = 'generation did not finish naturally';
    const answer = normalizeEvidence(reply.content);
    const matched = answer ? selection.entries.find(entry => normalizeEvidence(entry.text) === answer) : undefined;
    if (!reason && !matched) reason = 'generated text does not match a complete admitted source unit';
    if (reason || !matched) return { grounding: groundingFallback(reason ?? 'unsupported output', selection) };
    return { content: `Quoted source [1]:\n\n${matched.text}\n\n[1] ${matched.source.path}`,
        sources: [matched.source], grounding: { version: 1, mode: 'extractive', status: 'quoted',
            reason: 'complete quotation matched to supplied evidence; source truth is not independently verified',
            evidenceSourceIds: [...new Set(selection.entries.map(entry => entry.source.id))],
            claims: [{ sourceId: matched.source.id, quote: matched.text, start: matched.start, end: matched.end }] } };
}
