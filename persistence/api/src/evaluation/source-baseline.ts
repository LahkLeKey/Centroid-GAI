/** Conservative provided-evidence extraction. This is a lexical engineering baseline, not entailment. */
import type { ChatDialogueMessage } from '../../../shared/chat.ts';
import type { DialogueEvidence } from '../chat/dataset.ts';
import { evidenceUnits, normalizeEvidence } from '../chat/evidence.ts';
import type { FactualObservation } from './factuality.ts';

export const SOURCE_BASELINE_VERSION = 'provided-evidence-lexical-v2';
const ABSTENTION = 'I cannot answer from the supplied evidence.';
const CLARIFICATION = 'Please name the setting or source you want me to check.';
const ignored = new Set(('a an the what which who why how is are was were does do did will would could should can may must ' +
    'it its they their them that this those these there here one ones of to for from in on at by with and or as be been being ' +
    'i me my we our you your please tell show quote exact supporting supplied source excerpt unit relevant unrelated despite ' +
    'use uses used using get give inspect check checking checked look mean meant rather than not instead correct specify ' +
    'declare declares declared declaration describe describes document documents documented documentation guide header comment contract ' +
    'line public root there detail reviewing review need help want make has have when then about per each across between itself ' +
    'field function structure call code default configured configuration define defines return returns').split(' '));
const aliases: Readonly<Record<string, string>> = {
    releases: 'release', released: 'release', destruction: 'destroy', destroying: 'destroy', destroyed: 'destroy',
    cleanup: 'release', freeing: 'release', free: 'release', owned: 'own', ownership: 'own', owning: 'own',
    settings: 'setting', config: 'config', configure: 'config', configuration: 'config', configured: 'config',
    build: 'build', builds: 'build', building: 'build', compile: 'build', compiling: 'build', compilation: 'build',
    minimum: 'min', maximum: 'max', limits: 'limit', limiting: 'limit', bound: 'limit', bounds: 'limit', bounded: 'limit',
    bytes: 'byte', sizes: 'size', length: 'size', count: 'size', counts: 'size',
    entries: 'entry', admitted: 'admit', admitting: 'admit', admission: 'admit',
    increments: 'increment', advances: 'advance', advancing: 'advance',
    stops: 'stop', stopping: 'stop', prevents: 'prevent', rejects: 'reject', rejecting: 'reject',
    sharing: 'share', shared: 'share', forbids: 'forbid', prohibit: 'forbid', prohibits: 'forbid',
    tests: 'test', testing: 'test', tested: 'test', ctest: 'test', skipped: 'skip', skipping: 'skip',
    fail: 'failure', failed: 'failure', failing: 'failure', successful: 'pass', success: 'pass',
    training: 'train', trained: 'train', train: 'train', generation: 'generate', generated: 'generate',
    encoded: 'encode', encoding: 'encode', initialized: 'initialize', initialization: 'initialize',
    formatting: 'format', statements: 'statement', branches: 'branch', parameters: 'parameter',
    neural: 'neural', tokens: 'token', tokenizer: 'tokenizer',
    zero: '0', one: '1', two: '2', three: '3', four: '4', five: '5', eight: '8',
};
const propertyTerms = new Set(('password credential secret port ttl timeout retry retries latency throughput hostname pid checksum ' +
    'protocol tokenizer vocabulary accuracy revision retention expiry').split(' '));
const genericCapitals = new Set(('What Which Who Why How Does Do Did Is Are Can May Must Please Quote Use Check Inspect ' +
    'Correct We I For At Until Two One C API ISO Node CMake Doxygen Adam').split(' '));
const meaningful = (word: string) => aliases[word] ?? (word.length > 4 && word.endsWith('s') && !word.endsWith('ss') ? word.slice(0, -1) : word);
function terms(text: string): Set<string> {
    const split = text.replace(/([A-Z]+)([A-Z][a-z])/g, '$1 $2').replace(/([a-z0-9])([A-Z])/g, '$1 $2');
    return new Set((split.toLowerCase().match(/[\p{L}\p{N}]+/gu) ?? [])
        .filter(word => !ignored.has(word) && word.length > 1).map(meaningful));
}
function focusedQuestion(question: string): string {
    return question.replace(/\b(?:rather than|instead of)\b.*$/i, '').replace(/,?\s+not\s+(?:the|a|an|whether)\b.*$/i, '');
}
const refuse = (clarify = false): FactualObservation => ({ content: clarify ? CLARIFICATION : ABSTENTION,
    finishReason: clarify ? 'clarification' : 'abstained', citations: [] });
interface Candidate { text: string; source: DialogueEvidence; words: Set<string>; currentMatches: number; coverage: number; score: number }

/** Detect incompatible values or polarity for otherwise equivalent supplied propositions. */
function conflicts(left: Candidate, right: Candidate): boolean {
    const semantic = (text: string) => new Set([...terms(text)].filter(term => !/^\d+$/.test(term) &&
        !['no', 'never', 'cannot', 'enabled', 'disabled', 'enable', 'disable'].includes(term)));
    const a = semantic(left.text), b = semantic(right.text);
    const overlap = [...a].filter(term => b.has(term)).length;
    if (overlap < 2 || overlap / Math.max(a.size, b.size) < 0.8) return false;
    const numbers = (text: string) => [...text.matchAll(/\b\d+(?:\.\d+)?\b/g)].map(match => match[0]).join('|');
    const negative = (text: string) => /\b(not|no|never|cannot|disabled|forbidden)\b/i.test(text);
    return numbers(left.text) !== numbers(right.text) || negative(left.text) !== negative(right.text);
}

/** No category, expected answer, source path or rubric is available to this selector. */
export function sourceOnlyBaseline(messages: readonly ChatDialogueMessage[], evidence: readonly DialogueEvidence[]): FactualObservation {
    const current = messages.at(-1);
    if (!current || current.role !== 'user' || typeof current.content !== 'string' || current.content.includes('\0')) return refuse();
    const original = current.content;
    if (/\b(?:that|this) one\b.*\b(?:right|correct) one\b|\bwhich (?:one|of those|of these)\b/i.test(original)) return refuse(true);
    const question = focusedQuestion(original);
    if (/\bwhich\b.*\b(?:should|choose|use)\b.*\bor\b/i.test(question)) return refuse(true);
    const correction = /\b(i mean|i meant|correct|correction|rather than|instead of)\b/i.test(original);
    const references = /\b(it|its|they|their|them|that|those|there|these)\b/i.test(question);
    const previous = references && !correction ? messages.slice(0, -1).findLast(message => message.role === 'user')?.content ?? '' : '';
    const currentTerms = terms(question), historyTerms = terms(previous);
    if (!currentTerms.size && !historyTerms.size) return refuse(true);
    const privateFact = /\b(password|credential|secret|private (?:conversation|question|text)|different owner|another owner|remote user)\b/i.test(question);
    const asksPolicy = /\b(policy|protect|protection|reject|guard|forbid|prohibit|filter|prevent)\b/i.test(question);
    if (privateFact && !asksPolicy) return refuse();
    const runtime = /\b(production|deployed|running system|right now|uncaptured|inaccessible|uncommitted|yesterday|today|most recent|currently installed)\b/i.test(question);
    const captured = (text: string) => /^\s*(?:measured|observed|captured|recorded|run\s+[\w-]+:|\d{4}-\d\d-\d\d)\b[^\n]*\d/i.test(text);
    if (runtime && !evidence.some(source => captured(source.excerpt))) return refuse();
    if (/\b(?:factual|truth|semantic|production-ready)\b.*\b(?:rate|probability|imply|prove)\b|\b(?:rate|probability)\b.*\b(?:factual|truth|semantic)\b/i.test(question) &&
        !evidence.some(source => /\b(factual|truth|semantic)\b/i.test(source.excerpt))) return refuse();
    const named = (text: string) => (text.match(/\b[A-Z][A-Za-z0-9]+\b/g) ?? [])
        .filter(word => !genericCapitals.has(word)).map(word => word.toLowerCase());
    const currentEntities = named(question), entities = currentEntities.length ? currentEntities : named(previous);
    const requestedNumbers = [...question.matchAll(/\b\d+(?:\.\d+)?\b/g)].map(match => match[0]);
    const candidates: Candidate[] = [];
    for (const source of evidence) for (const unit of evidenceUnits(source.excerpt)) {
        if (runtime && !captured(unit.text)) continue;
        const words = terms(unit.text);
        const currentMatches = [...currentTerms].filter(term => words.has(term)).length;
        if (!currentMatches) continue;
        if (entities.some(entity => !unit.text.toLowerCase().includes(entity))) continue;
        if (requestedNumbers.some(number => !new RegExp(`\\b${number.replaceAll('.', '\\.')}\\b`).test(unit.text))) continue;
        if ([...currentTerms].some(term => propertyTerms.has(term) && !words.has(term))) continue;
        if (/^\s*(why|since|given that)\b/i.test(question) && !/\b(not|no|never|cannot|disabled)\b/i.test(question) &&
            /\b(not|no|never|cannot|disabled)\b/i.test(unit.text)) continue;
        const coverage = currentMatches / currentTerms.size;
        if (coverage < 0.3) continue;
        const historyMatches = [...historyTerms].filter(term => words.has(term)).length;
        const score = currentMatches * 3 + coverage * 2 + historyMatches * 0.6;
        const equivalent = candidates.find(candidate => normalizeEvidence(candidate.text) === normalizeEvidence(unit.text));
        if (!equivalent) candidates.push({ text: unit.text, source, words, currentMatches, coverage, score });
    }
    candidates.sort((left, right) => right.score - left.score || (normalizeEvidence(left.text) < normalizeEvidence(right.text) ? -1 :
        normalizeEvidence(left.text) > normalizeEvidence(right.text) ? 1 : 0));
    const best = candidates[0];
    if (!best) return refuse();
    const contender = candidates[1];
    if (contender && (conflicts(best, contender) || best.score - contender.score < 1.25))
        return refuse(/\bor\b/i.test(question));
    if (best.currentMatches === 1 && best.coverage < 0.5 && ![...currentTerms].some(term => propertyTerms.has(term) && best.words.has(term))) return refuse();
    return { content: best.text, finishReason: 'sources', citations: [best.source] };
}
