/** Offline representation coverage; this does not train, change, or assess a predictive tokenizer/model. */
import { createHash } from 'node:crypto';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { validateFactualDataset, type FactualDialogueDataset, type RepositoryDialogueDataset, type FactualDialogueRecord } from '../chat/dataset.ts';
import { cLocaleTokens } from './codebase-data.ts';
import { nativePromptAdmission } from '../training/preflight.ts';

const hash = (value: unknown) => createHash('sha256').update(JSON.stringify(value)).digest('hex');
const windows = Object.freeze({ prompt: 160, response: 32, evidence: 80, maximumCombinedContext: 256 });
// N-API transcodes JavaScript strings to UTF-8, replacing lone UTF-16 surrogates before C sees them.
const nativeText = (text: string) => Buffer.from(text, 'utf8').toString('utf8');
const wordTokens = (text: string) => cLocaleTokens(nativeText(text));

/** Match native output punctuation spacing; neither word casing nor original whitespace is restored. */
function renderWords(tokens: readonly string[]): string {
    let output = '';
    for (const token of tokens) {
        if (output.length && !(token.length === 1 && '.,!?;:)]}'.includes(token))) output += ' ';
        output += token;
    }
    return output;
}

function inspectText(text: string, vocabulary: ReadonlySet<string>) {
    const tokens = wordTokens(text), bytes = Buffer.from(text, 'utf8');
    const unknown = tokens.filter(token => !vocabulary.has(token));
    return {
        frozenWord: { tokens: tokens.length, unknownTokens: unknown.length, unknownSpellings: [...new Set(unknown)].sort(),
            exactRoundTripWithFrozenIds: renderWords(tokens.map(token => vocabulary.has(token) ? token : '<unk>')) === text },
        hypotheticalBytes: { tokens: bytes.length, unknownTokens: 0, exactRoundTrip: bytes.toString('utf8') === text },
    };
}

/** Gold-informed representability oracle; it finds existing bytes, never predicts which answer to copy. */
function copyOracle(record: Pick<FactualDialogueRecord, 'answer' | 'expected' | 'evidence'>) {
    const spans = record.evidence.flatMap(source => {
        const start = source.excerpt.indexOf(record.answer);
        if (start < 0) return [];
        const startByte = Buffer.byteLength(source.excerpt.slice(0, start), 'utf8');
        const endByte = startByte + Buffer.byteLength(record.answer, 'utf8');
        return [{ sourceId: source.id, sourceStartLine: source.startLine, sourceEndLine: source.endLine,
            startByte, endByte, exactRoundTrip: Buffer.from(source.excerpt, 'utf8').subarray(startByte, endByte).toString('utf8') === record.answer }];
    });
    return { label: 'gold-informed exact source-span representability oracle' as const, expected: record.expected,
        representable: spans.some(span => span.exactRoundTrip), spans };
}

function inspectRecord(record: FactualDialogueRecord, vocabulary: ReadonlySet<string>) {
    const messages = record.messages.map(message => inspectText(message.content, vocabulary));
    const promptControls = record.messages.length * 2 + 1; // Role/end per message, then assistant-start.
    const wordLengths = messages.map(message => message.frozenWord.tokens + 2);
    const byteLengths = messages.map(message => message.hypotheticalBytes.tokens + 2);
    const wordPrompt = wordLengths.reduce((sum, value) => sum + value, 1);
    const bytePrompt = byteLengths.reduce((sum, value) => sum + value, 1);
    const answer = inspectText(record.answer, vocabulary);
    return { id: record.id, expected: record.expected, messageCount: messages.length, promptControls, answerControls: 1,
        frozenWord: {
            promptTokensBeforeAdmission: wordPrompt, promptOverflowAt256: Math.max(0, wordPrompt - 256),
            promptUnknownTokensBeforeAdmission: messages.reduce((sum, message) => sum + message.frozenWord.unknownTokens, 0),
            admissionAtNativeDefaults: nativePromptAdmission(record.messages, wordLengths, messages.map(message => message.frozenWord.unknownTokens), windows),
            answer: { ...answer.frozenWord, tokensIncludingEos: answer.frozenWord.tokens + 1 },
        },
        hypotheticalBytes: {
            promptTokensBeforeAdmission: bytePrompt, promptOverflowAt256: Math.max(0, bytePrompt - 256), promptUnknownTokensBeforeAdmission: 0,
            admissionAtNativeDefaults: nativePromptAdmission(record.messages, byteLengths, messages.map(() => 0), windows),
            allPromptTextRoundTrips: messages.every(message => message.hypotheticalBytes.exactRoundTrip),
            answer: { ...answer.hypotheticalBytes, tokensIncludingEos: answer.hypotheticalBytes.tokens + 1 },
        },
        sourceCopyOracle: copyOracle(record),
    };
}
type CoverageRecord = ReturnType<typeof inspectRecord>;

function summarize(records: readonly CoverageRecord[]) {
    const encoding = (kind: 'frozenWord' | 'hypotheticalBytes') => ({
        promptTokensBeforeAdmission: records.reduce((sum, record) => sum + record[kind].promptTokensBeforeAdmission, 0),
        promptUnknownTokensBeforeAdmission: records.reduce((sum, record) => sum + record[kind].promptUnknownTokensBeforeAdmission, 0),
        answerTokens: records.reduce((sum, record) => sum + record[kind].answer.tokens, 0),
        answerUnknownTokens: records.reduce((sum, record) => sum + record[kind].answer.unknownTokens, 0),
        promptCasesExceeding256: records.filter(record => record[kind].promptOverflowAt256 > 0).length,
        currentQuestionsRejectedAtNativeDefaults: records.filter(record => record[kind].admissionAtNativeDefaults.currentQuestionRejected).length,
        casesDroppingEvidenceAtNativeDefaults: records.filter(record => (record[kind].admissionAtNativeDefaults.droppedEvidence ?? 0) > 0).length,
        retainedPromptUnknownTokensAtNativeDefaults: records.reduce((sum, record) => sum + (record[kind].admissionAtNativeDefaults.retainedUnknownTokens ?? 0), 0),
        exactAnswerRoundTrips: records.filter(record => kind === 'frozenWord' ? record.frozenWord.answer.exactRoundTripWithFrozenIds : record.hypotheticalBytes.answer.exactRoundTrip).length,
    });
    const answerable = records.filter(record => record.expected === 'answer');
    return { cases: records.length, frozenWord: encoding('frozenWord'), hypotheticalBytes: encoding('hypotheticalBytes'),
        sourceCopyOracle: { label: 'gold-informed representability, not model accuracy', answerableCases: answerable.length,
            answerableCasesRepresentable: answerable.filter(record => record.sourceCopyOracle.representable).length,
            abstentionCases: records.length - answerable.length } };
}

/** Create unseen-token probes deterministically without ever adding them to the frozen training vocabulary. */
function adversarialProbes(vocabulary: ReadonlySet<string>) {
    const unseen = (prefix: string) => {
        let candidate = prefix;
        while (vocabulary.has(wordTokens(candidate)[0]!)) candidate += '7';
        return candidate;
    };
    const identifier = unseen('cgaiProbeUnseenIdentifierX99');
    return [
        { kind: 'identifier', text: identifier },
        { kind: 'filepath', text: `src/Extensions/${identifier}.hpp` },
        { kind: 'number', text: unseen('18446744073709551615') },
        { kind: 'unicode', text: unseen('未登録識別子Ω🚀') },
        { kind: 'byte-expansion', text: unseen('x'.repeat(300)) },
    ].map(({ kind, text }) => {
        const excerpt = `Declared value: ${text}.`;
        const record: FactualDialogueRecord = { id: `probe-${kind}`, family: 'representation-probe', category: 'direct',
            sources: ['probe-source'], messages: [{ role: 'evidence', content: excerpt }, { role: 'user', content: 'What is the declared value?' }],
            answer: text, expected: 'answer', acceptedAnswers: [text], requiredClaims: [text],
            evidence: [{ id: 'probe-source', excerpt, startLine: 1, endLine: 1 }] };
        return { kind, text, coverage: inspectRecord(record, vocabulary) };
    });
}

/** Compare representation limits on an already validated dataset; held-out text never grows the vocabulary. */
export function evaluateVocabulary(dataset: FactualDialogueDataset | RepositoryDialogueDataset) {
    const vocabulary = new Set<string>();
    for (const record of dataset.train) {
        // Native training_corpus() appends each answer before its prompt messages.
        for (const token of wordTokens(record.answer)) vocabulary.add(token);
        for (const message of record.messages) for (const token of wordTokens(message.content)) vocabulary.add(token);
    }
    const initialVocabulary = [...vocabulary].sort(), initialHash = hash(initialVocabulary);
    const inspectSplit = (input: readonly FactualDialogueRecord[]) => {
        const records = input.map(record => inspectRecord(record, vocabulary));
        return { summary: summarize(records), records };
    };
    const splits = { train: inspectSplit(dataset.train), development: inspectSplit(dataset.development), test: inspectSplit(dataset.test) };
    const probes = adversarialProbes(vocabulary);
    return { version: 1, experiment: 'offline-representation-coverage', datasetSha256: hash(dataset), datasetVersion: dataset.version,
        datasetPurpose: dataset.purpose, windows,
        vocabulary: { fitSplit: 'train', lexicalSize: vocabulary.size, nativeStructuralTokens: 7, nativeTotalSize: vocabulary.size + 7,
            nativeVocabularyLimit: 8192, fitsNativeVocabularyLimit: vocabulary.size + 7 <= 8192,
            sha256: initialHash, unchangedAfterHeldOutAndProbes: initialHash === hash([...vocabulary].sort()),
            hypotheticalByteAlphabet: 256, hypotheticalByteStructuralTokens: 7 },
        accounting: { prompt: 'two structural slots per message plus one assistant start; lexical/byte content counted before admission',
            answer: 'one EOS beyond content; answer length is not constrained by the rolling response window',
            overflowAt256: 'full prompt excess at the architectural ceiling, not an implementation that cuts source text',
            admission: 'protocol-two whole-message selection at the stated default windows for each hypothetical encoding',
            sourceCopy: 'gold answer exact occurrence inside supplied evidence; zero-based UTF-8 byte offsets, exclusive end, relative to the excerpt' },
        splits, probes,
        limitations: [
            'No model was trained or evaluated by this experiment; coverage and roundtrip counts are not answer accuracy.',
            'The byte alphabet is hypothetical and would require a new versioned tokenizer, decoder, vocabulary and retraining.',
            'Bytes remove unknown IDs for valid UTF-8 but consume more context slots; coverage does not establish learnability or factual reliability.',
            'Source-copy spans use gold answers as an oracle, require external source storage and a trained selector, and do not demonstrate prediction.',
            'Prompt admission is a length simulation; bytes are not implemented in production. The frozen word tokenizer and existing artifacts are unchanged.',
            'Word coverage assumes the default C locale. Lone UTF-16 surrogates are replaced at the Node UTF-8 boundary and fail exact byte roundtrip.',
        ] };
}

async function main() {
    const { values } = parseArgs({ options: { dataset: { type: 'string' }, output: { type: 'string' } } });
    const root = new URL('../../../..', import.meta.url);
    const datasetPath = values.dataset ? resolve(values.dataset) : new URL('data/chat/factual-dialogues-v2.json', root);
    const { dataset } = validateFactualDataset(JSON.parse(await readFile(datasetPath, 'utf8')));
    const report = evaluateVocabulary(dataset);
    const output = values.output ? resolve(values.output) : fileURLToPath(new URL('build/evaluation/vocabulary/report.json', root));
    await mkdir(dirname(output), { recursive: true });
    await writeFile(output, JSON.stringify(report, null, 2) + '\n');
    console.log(JSON.stringify({ output, vocabulary: report.vocabulary, splits: Object.fromEntries(Object.entries(report.splits).map(([name, split]) => [name, split.summary])) }));
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) await main();
