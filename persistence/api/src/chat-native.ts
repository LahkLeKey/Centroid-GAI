/** Typed boundary for the separate versioned neural conversation addon. */
import { createRequire } from 'node:module';
import type { ChatDialogueMessage, ChatExample, ChatMetrics, ChatModelConfig, ChatModelMetadata, ChatReply, ChatSendRequest, ChatTrainingOptions } from '../../shared/chat.ts';

interface Binding {
    trainChat(examples: unknown, settings: Float64Array, seed: bigint): { payload: Buffer; before: ChatMetrics; after: ChatMetrics };
    inspectChat(payload: Buffer): string;
    evaluateChat(payload: Buffer, examples: unknown): ChatMetrics;
    replyChat(payload: Buffer, messages: unknown, settings: Float64Array, seed: bigint): ChatReply;
}
const binding = createRequire(import.meta.url)('../build/Release/centroid_gai_native.node') as Binding;

/** Reject JSON coercions before the typed-array boundary loses the original input types. */
function settingsObject(value: unknown, label: string): void {
    if (!value || typeof value !== 'object' || Array.isArray(value)) throw new TypeError(`${label} must be an object`);
}
function numeric(value: unknown, fallback: number, label: string): number {
    if (value === undefined) return fallback;
    if (typeof value !== 'number' || !Number.isFinite(value)) throw new TypeError(`${label} must be a finite number`);
    return value;
}
function seed(value: string | undefined, fallback: bigint): bigint {
    if (value === undefined) return fallback;
    if (typeof value !== 'string' || !/^\d{1,20}$/.test(value)) throw new TypeError('seed must be an unsigned decimal string');
    const parsed = BigInt(value);
    if (parsed > 18446744073709551615n) throw new TypeError('seed exceeds uint64');
    return parsed;
}
function messages(values: readonly ChatDialogueMessage[]) {
    if (!Array.isArray(values)) throw new TypeError('messages must be an array');
    return values.map((value) => {
        const role = ['user', 'assistant', 'evidence'].indexOf(value?.role);
        if (role < 0) throw new TypeError('invalid chat role');
        return { role, content: value.content };
    });
}
function examples(values: readonly ChatExample[]) {
    if (!Array.isArray(values)) throw new TypeError('examples must be an array');
    return values.map((value) => ({ messages: messages(value?.messages), answer: value.answer }));
}
export function inspectChatModel(payload: Buffer): ChatModelMetadata {
    return JSON.parse(binding.inspectChat(payload)) as ChatModelMetadata;
}
export function trainChatModel(values: readonly ChatExample[], config: ChatModelConfig = {}, training: ChatTrainingOptions = {}) {
    settingsObject(config, 'config');
    settingsObject(training, 'training');
    const settings = new Float64Array([
        numeric(config.embeddingDimensions, 8, 'embeddingDimensions'),
        numeric(config.hiddenDimensions, 16, 'hiddenDimensions'),
        numeric(config.centroidCount, 16, 'centroidCount'),
        numeric(config.promptWindow, 48, 'promptWindow'),
        numeric(config.responseWindow, 8, 'responseWindow'),
        numeric(config.routingTemperature, 1, 'routingTemperature'),
        numeric(training.epochs, 20, 'epochs'), numeric(training.learningRate, 0.01, 'learningRate'), 5,
    ]);
    const initialSeed = seed(config.seed, 42n);
    const result = binding.trainChat(examples(values), settings, seed(training.seed, initialSeed));
    return { ...result, metadata: inspectChatModel(result.payload) };
}
export type ChatTrainResult = ReturnType<typeof trainChatModel>;
export function evaluateChatModel(payload: Buffer, values: readonly ChatExample[]): ChatMetrics {
    return binding.evaluateChat(payload, examples(values));
}
export function replyChatModel(payload: Buffer, values: readonly ChatDialogueMessage[], options: Pick<ChatSendRequest, 'maxTokens' | 'temperature' | 'seed'> = {}): ChatReply {
    settingsObject(options, 'options');
    return binding.replyChat(payload, messages(values), new Float64Array([
        numeric(options.maxTokens, 128, 'maxTokens'), numeric(options.temperature, 0, 'temperature'),
    ]), seed(options.seed, 0n));
}
