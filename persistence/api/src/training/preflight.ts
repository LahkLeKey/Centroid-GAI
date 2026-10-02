/** Addon-free checks of protocol-two training representability and native allocation limits. */
import type { ChatDialogueMessage, ChatModelConfig } from '../../../shared/chat.ts';
import type { FactualDialogueDataset, RepositoryDialogueDataset, FactualDialogueRecord } from '../chat/dataset.ts';
import { cLocaleTokens } from '../evaluation/codebase-data.ts';

const limits = Object.freeze({ vocabulary: 8192, parameters: 2000000, tokens: 1000000, textBytes: 16777216, tokenBytes: 1048576, examples: 10000, messages: 1024 });
const tokens = (text: string) => cLocaleTokens(Buffer.from(text, 'utf8').toString('utf8'));

function validateMessages(messages: readonly ChatDialogueMessage[]) {
    if (!Array.isArray(messages) || !messages.length || messages.length > limits.messages)
        throw new Error('preflight requires 1..1024 messages per example');
    let expected = 'user';
    for (const message of messages) {
        if (!message || typeof message.content !== 'string' || message.content.includes('\0')) throw new Error('preflight invalid message text');
        if (message.role === 'evidence' && expected === 'user') continue;
        if (message.role !== expected) throw new Error('preflight invalid dialogue order');
        expected = expected === 'user' ? 'assistant' : 'user';
    }
    if (messages.at(-1)!.role !== 'user') throw new Error('preflight current user question required');
}

/** Simulate only the C formatter's whole-message selection; all supplied lengths include role/end. */
export function nativePromptAdmission(messages: readonly ChatDialogueMessage[], lengths: readonly number[], unknown: readonly number[],
    windows: { prompt: number; evidence: number }) {
    let retainedTokens = 1 + lengths.at(-1)!, retainedUnknownTokens = unknown.at(-1)!;
    if (retainedTokens > windows.prompt) return { currentQuestionRejected: true, retainedTokens: null, retainedUnknownTokens: null,
        retainedEvidenceTokens: null, droppedMessages: null, droppedEvidence: null };
    let retainedEvidenceTokens = 0, droppedMessages = 0, droppedEvidence = 0;
    const admit = (indices: readonly number[]) => {
        const slots = indices.reduce((total, index) => total + lengths[index]!, 0);
        const evidence = indices.length === 1 && messages[indices[0]!]!.role === 'evidence';
        if (retainedTokens + slots > windows.prompt || (evidence && retainedEvidenceTokens + slots > windows.evidence)) {
            droppedMessages += indices.length;
            if (evidence) droppedEvidence++;
        } else {
            retainedTokens += slots;
            retainedUnknownTokens += indices.reduce((total, index) => total + unknown[index]!, 0);
            if (evidence) retainedEvidenceTokens += slots;
        }
    };
    let start = messages.length - 1;
    while (start > 0 && messages[start - 1]!.role === 'evidence') start--;
    for (let index = start; index < messages.length - 1; index++) admit([index]);
    for (let end = start; end > 0;) {
        const pair = messages[end - 1]!.role === 'assistant';
        admit(pair ? [end - 2, end - 1] : [end - 1]);
        end -= pair ? 2 : 1;
    }
    return { currentQuestionRejected: false, retainedTokens, retainedUnknownTokens, retainedEvidenceTokens, droppedMessages, droppedEvidence };
}

function shape(config: ChatModelConfig) {
    if (!config || typeof config !== 'object' || Array.isArray(config)) throw new Error('preflight config must be an object');
    const integer = (value: number | undefined, fallback: number, low: number, high: number, name: string) => {
        const actual = value === undefined ? fallback : value;
        if (!Number.isSafeInteger(actual) || actual < low || actual > high) throw new Error(`preflight invalid ${name}`);
        return actual;
    };
    const embedding = integer(config.embeddingDimensions, 8, 1, 64, 'embeddingDimensions');
    const hidden = integer(config.hiddenDimensions, 16, 1, 128, 'hiddenDimensions');
    const centroids = integer(config.centroidCount, 16, 1, 128, 'centroidCount');
    const prompt = integer(config.promptWindow, 160, 4, 255, 'promptWindow');
    const response = integer(config.responseWindow, 32, 1, 256, 'responseWindow');
    const requestedEvidence = integer(config.evidenceWindow, 0, 0, 256, 'evidenceWindow');
    const routing = config.routingTemperature === undefined ? 1 : config.routingTemperature;
    if (prompt + response > 256 || requestedEvidence > prompt - 4) throw new Error('preflight native context/evidence limits exceeded');
    if (typeof routing !== 'number' || !Number.isFinite(routing) || routing < 0.01 || routing > 100) throw new Error('preflight invalid routingTemperature');
    if (config.seed !== undefined && (typeof config.seed !== 'string' || !/^\d{1,20}$/.test(config.seed) || BigInt(config.seed) > 18446744073709551615n))
        throw new Error('preflight invalid seed');
    return { embedding, hidden, centroids, prompt, response, evidence: requestedEvidence || Math.min(Math.floor(prompt / 2), prompt - 4) };
}

function inspectPrompt(record: FactualDialogueRecord, vocabulary: ReadonlySet<string>, windows: ReturnType<typeof shape>) {
    validateMessages(record.messages);
    const words = record.messages.map(message => tokens(message.content));
    const unknown = words.map(sequence => sequence.filter(token => !vocabulary.has(token)).length);
    return { id: record.id, promptUnknownTokensBeforeAdmission: unknown.reduce((sum, count) => sum + count, 0),
        answerUnknownTokens: tokens(record.answer).filter(token => !vocabulary.has(token)).length,
        ...nativePromptAdmission(record.messages, words.map(sequence => sequence.length + 2), unknown, windows) };
}

/** Match training_corpus() and its frozen-vocabulary construction before checking individual prompts. */
function trainingCounts(records: readonly FactualDialogueRecord[]) {
    if (!Array.isArray(records) || !records.length || records.length > limits.examples) throw new Error('preflight requires 1..10000 training examples');
    const vocabulary = new Set<string>();
    let corpusTokens = 0, corpusBytesIncludingSeparators = 0, answerTargetsIncludingEos = 0;
    for (const record of records) {
        validateMessages(record.messages);
        for (const [index, text] of [record.answer, ...record.messages.map((message: ChatDialogueMessage) => message.content)].entries()) {
            if (typeof text !== 'string' || text.includes('\0')) throw new Error('preflight text must be a NUL-free string');
            corpusBytesIncludingSeparators += Buffer.byteLength(text, 'utf8') + 1;
            if (corpusBytesIncludingSeparators > limits.textBytes) throw new Error('preflight training corpus exceeds 16 MiB including separators');
            const sequence = tokens(text);
            corpusTokens += sequence.length;
            if (index === 0) answerTargetsIncludingEos += sequence.length + 1;
            if (corpusTokens > limits.tokens || answerTargetsIncludingEos > limits.tokens) throw new Error('preflight training token limit of 1000000 exceeded');
            for (const token of sequence) {
                if (Buffer.byteLength(token, 'utf8') > limits.tokenBytes) throw new Error('preflight token spelling exceeds 1 MiB');
                vocabulary.add(token);
                if (vocabulary.size + 7 > limits.vocabulary) throw new Error('preflight frozen vocabulary exceeds 8192 including controls');
            }
        }
    }
    if (!corpusTokens) throw new Error('preflight training vocabulary cannot be empty');
    return { vocabulary, corpusTokens, corpusBytesIncludingSeparators, answerTargetsIncludingEos };
}

/** Reject training omissions/allocation failures; held-out unknown words and context loss remain diagnostic. */
export function validateTrainingPreflight(dataset: FactualDialogueDataset | RepositoryDialogueDataset, config: ChatModelConfig = {}) {
    const windows = shape(config);
    const { vocabulary, ...counts } = trainingCounts(dataset.train);
    const vocabularySize = vocabulary.size + 7;
    const parameterCount = vocabularySize * windows.embedding + windows.hidden * (windows.prompt + windows.response) * windows.embedding +
        windows.hidden + windows.centroids * windows.hidden + windows.centroids * vocabularySize;
    if (parameterCount > limits.parameters) throw new Error('preflight native parameter count exceeds 2000000');
    const training = dataset.train.map(record => inspectPrompt(record, vocabulary, windows));
    const invalid = training.find(record => record.currentQuestionRejected || record.droppedEvidence);
    if (invalid) throw new Error(`preflight training example ${invalid.id}: ${invalid.currentQuestionRejected ? 'current question exceeds prompt window' : 'evidence would be dropped'}`);
    return { version: 1, protocolVersion: 2, tokenizerVersion: 1, limits, windows, vocabularySize, parameterCount,
        training: { examples: training.length, ...counts, prompts: training },
        heldOut: { development: dataset.development.map(record => inspectPrompt(record, vocabulary, windows)),
            test: dataset.test.map(record => inspectPrompt(record, vocabulary, windows)) },
        scope: 'native size and context admissibility only; held-out novelty is not rejected and no accuracy claim is made' };
}
