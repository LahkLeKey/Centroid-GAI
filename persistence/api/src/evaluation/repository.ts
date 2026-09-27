/** Reproducible offline repository-question evaluation; no native addon, database, or network needed. */
import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { sha256 } from './codebase-data.ts';
import { loadRepositoryIndex, retrievalPolicy, validPassage } from '../knowledge/retrieval.ts';
import { scoreQuestion, summarizeQuestions, validateQuestions } from './repository-metrics.ts';

const { values } = parseArgs({ options: {
    snapshot: { type: 'string' }, output: { type: 'string' }, questions: { type: 'string' }, limit: { type: 'string' },
} });
if (!values.snapshot || !values.output) throw new Error('Usage: node repository.ts --snapshot <directory> --output <new-report.json> [--questions <suite.json>] [--limit 5]');
const { snapshot, documents, retriever } = loadRepositoryIndex(values.snapshot);
const suiteBytes = readFileSync(values.questions ?? new URL('../../../../examples/evaluation/repository-questions-v1.json', import.meta.url));
const questions = validateQuestions(JSON.parse(suiteBytes.toString('utf8')), documents);
const limit = values.limit === undefined ? retrievalPolicy.defaultLimit : Number(values.limit);
const rows = questions.map(question => {
    const result = retriever.search(question.question, limit);
    assert.deepEqual(result, retriever.search(question.question, limit), 'Retrieval changed on repeat');
    assert(result.hits.every(hit => validPassage(hit, documents)), 'Invalid citation or quote');
    return scoreQuestion(question, result, documents);
});
const report = {
    version: 1, purpose: 'Development-set passage retrieval and citation evaluation; not generative answer accuracy.',
    sourceCommit: snapshot.manifest.source.commit, snapshotManifestSha256: snapshot.manifestSha256,
    suiteSha256: sha256(suiteBytes), environment: { node: process.version },
    implementationSha256: Object.fromEntries(['evaluation/repository.ts', 'evaluation/repository-metrics.ts',
        'evaluation/codebase-data.ts', 'knowledge/retrieval.ts'].map(path =>
        [path, sha256(readFileSync(new URL(`../${path}`, import.meta.url)))])),
    policy: retrievalPolicy, limit, documents: retriever.documents, passages: retriever.passages,
    indexPolicy: 'Both snapshot splits are searchable source material. Questions/gold labels are never given to the retriever.',
    citationPolicy: 'Exact quotes and coordinates verified against normalized snapshot records. Git line offsets are not inferred.',
    scoringPolicy: 'Gold-path recall and literal gold-quote support; unannotated relevant passages may be marked unsupported. Unknown cases are manually labeled.',
    integrityPassed: true, scores: summarizeQuestions(rows), rows,
};
const output = resolve(values.output);
mkdirSync(dirname(output), { recursive: true });
writeFileSync(output, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
console.log(JSON.stringify({ report: output, ...report.scores }, null, 2));
