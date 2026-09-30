/** Fixed dialogue evaluation; exact match is a diagnostic, never a substitute for claim review. */
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { parseArgs } from 'node:util';
import { cpus, platform, arch } from 'node:os';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { validateDataset } from '../chat/dataset.ts';
import { evaluateChatModel, replyChatModel, trainChatModel } from '../chat-native.ts';
import { fingerprintBytes } from '../chat/service.ts';
import { trainNativeModel, generateNativeModel } from '../native.ts';

const { values } = parseArgs({ options: { dataset: { type: 'string' }, output: { type: 'string' } } });
const root = new URL('../../../..', import.meta.url);
const path = values.dataset ?? new URL('examples/chat/dialogues-v1.json', root);
const { dataset, hashes } = validateDataset(JSON.parse(await readFile(path, 'utf8')));
const config = { embeddingDimensions: 8, hiddenDimensions: 16, centroidCount: 16, promptWindow: 64, responseWindow: 8, seed: '42' };
const training = { epochs: 100, learningRate: 0.003 };
const start = performance.now();
const trained = trainChatModel(dataset.train, config, training);
const trainingMs = performance.now() - start;
const baseline = trainNativeModel(dataset.train.map((entry) => `${entry.messages.map((message) => message.content).join('\n')}\n${entry.answer}`).join('\n'));
const normalize = (text: string) => text.toLowerCase().replace(/[^\p{L}\p{N}]+/gu, ' ').trim();
const results = dataset.test.map((entry) => {
    const started = performance.now();
    const reply = replyChatModel(trained.payload, entry.messages, { maxTokens: 64 });
    const elapsedMs = performance.now() - started;
    const baselineReply = generateNativeModel(baseline, entry.messages.map((message) => message.content).join('\n'), 64, 0, 42n);
    return { id: entry.id, category: entry.category, expected: entry.answer, reply, elapsedMs,
        exactMatch: normalize(reply.content) === normalize(entry.answer), baselineReply,
        baselineExactMatch: normalize(baselineReply) === normalize(entry.answer),
        suppliedEvidence: entry.messages.filter((message) => message.role === 'evidence').map((message) => message.content),
        claimSupport: 'requires human review' };
});
const sourceDirectory = resolve(fileURLToPath(root));
const git = (...args: string[]) => execFileSync('git', ['-c', `safe.directory=${sourceDirectory.replace(/\\/g, '/')}`, ...args], { cwd: root, encoding: 'utf8' }).trim();
const implementationFiles = [
    'src/chat/chat_model.c', 'src/chat/chat_prompt.c', 'src/chat/chat_evaluation.c', 'src/chat/chat_codec.c',
    'src/neural/neural_model.c', 'src/neural/neural_math.c', 'src/neural/neural_training.c', 'src/neural/neural_generation.c',
    'src/neural/neural_vocabulary.c', 'src/internal/chat_internal.h', 'src/internal/neural_internal.h',
    'include/centroid_gai_chat.h', 'include/centroid_gai_neural.h',
    'persistence/api/src/chat-native.ts', 'persistence/api/src/chat/dataset.ts',
    'persistence/api/src/evaluation/chat.ts',
];
const implementationSha256 = Object.fromEntries(await Promise.all(implementationFiles.map(async (file) =>
    [file, fingerprintBytes(await readFile(new URL(file, root)))])));
const report = { version: 1, createdAt: new Date().toISOString(), sourceCommit: git('rev-parse', 'HEAD'),
    sourceDirty: git('status', '--porcelain').length > 0, implementationSha256, hashes, protocolVersion: 1, tokenizerVersion: 1,
    artifactSha256: fingerprintBytes(trained.payload), config, training, trainingMs,
    hardware: { platform: platform(), arch: arch(), cpu: cpus()[0]?.model },
    trainingMetrics: trained.after, developmentMetrics: evaluateChatModel(trained.payload, dataset.development),
    testMetrics: evaluateChatModel(trained.payload, dataset.test),
    summary: { denominator: results.length, exactMatches: results.filter((entry) => entry.exactMatch).length,
        naturalTermination: results.filter((entry) => entry.reply.finishReason === 'eos').length,
        baselineExactMatches: results.filter((entry) => entry.baselineExactMatch).length,
        pilotReady: false, reason: 'small fixed engineering fixture; no agreed hardware quality/latency release thresholds or claim-support review' }, results };
const output = resolve(values.output ?? fileURLToPath(new URL('build/evaluation/chat', root)));
await mkdir(output, { recursive: true });
await writeFile(resolve(output, 'model.cgchat'), trained.payload);
await writeFile(resolve(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(report.summary));
