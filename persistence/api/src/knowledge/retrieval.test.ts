import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { loadSnapshot, sha256 } from '../evaluation/codebase-data.ts';
import type { Document } from '../evaluation/codebase-data.ts';
import { scoreQuestion, summarizeQuestions, validateQuestions } from '../evaluation/repository-metrics.ts';
import { createRetriever, loadRepositoryIndex, retrievalTerms, validPassage } from './retrieval.ts';

function document(text: string, path: string): Document {
    return { text, sha256: sha256(text), sources: [{ path, commit: 'a'.repeat(40), blob: 'b'.repeat(40) }] };
}

/** Model an immutable snapshot with all source text in train and an intentionally empty validation split. */
function fixture() {
    const directory = mkdtempSync(join(tmpdir(), 'cgai-retrieval-'));
    const documents = [document('The amber tokenizer preserves Unicode.\nRelease its tokens with amber_destroy.', 'src/amber.ts'),
        document('Sapphire storage checks a checksum before loading.', 'src/sapphire.ts')];
    const content = { 'train.txt': documents.map(doc => doc.text + '\n\n').join(''),
        'train.jsonl': documents.map(doc => JSON.stringify(doc) + '\n').join(''),
        'validation.txt': '', 'validation.jsonl': '', 'SOURCE_LICENSE.txt': 'MIT\n' };
    for (const [name, text] of Object.entries(content)) writeFileSync(join(directory, name), text);
    const manifest = { schema_version: 1, source: { commit: 'a'.repeat(40) }, documents: { train: documents.length, validation: 0 },
        entries: documents.flatMap(doc => doc.sources.map(source => ({ path: source.path, oid: source.blob }))),
        files: Object.fromEntries(Object.entries(content).map(([name, text]) => [name, sha256(text)])), rejected: [] };
    writeFileSync(join(directory, 'manifest.json'), JSON.stringify(manifest));
    return { directory, documents, manifest };
}

test('retrieval uses the entire question and code identifiers; ordering and repeated results are stable', () => {
    const docs = [document('Amber tokenizer preserves Unicode words.', 'src/amber.ts'),
        document('Sapphire database stores model artifacts.', 'src/sapphire.ts')];
    const question = 'Amber tokenizer: how does this work?';
    const result = createRetriever(docs).search(question);
    assert.equal(result.hits[0]?.citations[0]?.path, 'src/amber.ts');
    assert.deepEqual(result, createRetriever([...docs].reverse()).search(question));
    assert(retrievalTerms('contextWindow cgai_model_train').includes('window'));
    assert(retrievalTerms('contextWindow cgai_model_train').includes('cgai_model_train'));
    assert(retrievalTerms('contextWindow cgai_model_train').includes('train'));
});

test('chunks preserve exact Unicode lines, tail evidence, and all deduplicated source aliases', () => {
    const doc = document(Array.from({ length: 55 }, (_, index) => index === 52 ? 'café raretail résumé' : `ordinary line ${index}`).join('\n'), 'a.ts');
    doc.sources.push({ ...doc.sources[0]!, path: 'duplicate.ts' });
    const result = createRetriever([doc]).search('café raretail résumé');
    assert.equal(result.status, 'evidence');
    const hit = result.hits[0]!;
    assert.equal(hit.citations.length, 2);
    assert.equal(hit.citations[0]?.endLine, 55);
    assert(validPassage(hit, [doc]));
    assert(hit.text.includes('café raretail résumé'));
    const broad = createRetriever([doc]).search('ordinary line', 20);
    for (let index = 1; index < broad.hits.length; index++) {
        const current = broad.hits[index]!.citations[0]!;
        for (const previous of broad.hits.slice(0, index)) {
            const other = previous.citations[0]!;
            assert(current.endLine < other.startLine || other.endLine < current.startLine);
        }
    }
});

test('citation verification rejects fabricated paths, blobs, commits, coordinates, and modified quotes', () => {
    const doc = document('Amber tokenizer preserves Unicode.\nSecond line.', 'src/amber.ts');
    const hit = createRetriever([doc]).search('amber tokenizer').hits[0]!;
    assert(validPassage(hit, [doc]));
    for (const patch of [{ path: 'invented.ts' }, { commit: 'c'.repeat(40) }, { blob: 'd'.repeat(40) },
        { startLine: 0 }, { endLine: 99 }, { documentSha256: 'e'.repeat(64) }]) {
        assert.equal(validPassage({ ...hit, citations: [{ ...hit.citations[0]!, ...patch }] }, [doc]), false);
    }
    const text = 'invented quote';
    assert.equal(validPassage({ ...hit, text, textSha256: sha256(text) }, [doc]), false);
    assert.equal(validPassage({ ...hit, citations: [] }, [doc]), false);
});

test('empty vocabulary and low coverage abstain; invalid questions and limits fail explicitly', () => {
    const retriever = createRetriever([document('Amber tokenizer preserves Unicode.', 'src/amber.ts')]);
    for (const question of ['the and how?', 'extraterrestrial nebula', 'amber unknownalpha unknownbeta unknowngamma']) {
        const result = retriever.search(question);
        assert.equal(result.status, 'abstained');
        assert.deepEqual(result.hits, []);
    }
    for (const limit of [0, 21, 1.5, NaN]) assert.throws(() => retriever.search('amber', limit), /Limit/);
    for (const question of ['', ' \t', 'bad\0query', 'é'.repeat(8193)]) assert.throws(() => retriever.search(question), /Question/);
});

test('retrieval accepts empty validation but still verifies snapshot checksums and provenance', () => {
    const { directory, manifest } = fixture();
    try {
        assert.equal(loadRepositoryIndex(directory).documents.length, 2);
        assert.throws(() => loadSnapshot(directory), /validation document count/);
        const recordPath = join(directory, 'train.jsonl');
        const records = readFileSync(recordPath, 'utf8').trim().split('\n').map(line => JSON.parse(line));
        records[0].sources[0].commit = 'c'.repeat(40);
        const bytes = records.map(record => JSON.stringify(record) + '\n').join('');
        writeFileSync(recordPath, bytes);
        assert.throws(() => loadRepositoryIndex(directory), /checksum/);
        manifest.files['train.jsonl'] = sha256(bytes);
        writeFileSync(join(directory, 'manifest.json'), JSON.stringify(manifest));
        assert.throws(() => loadRepositoryIndex(directory), /provenance/);
        records[0].sources[0] = { commit: manifest.source.commit };
        const missingIdentity = records.map(record => JSON.stringify(record) + '\n').join('');
        writeFileSync(recordPath, missingIdentity);
        manifest.files['train.jsonl'] = sha256(missingIdentity);
        writeFileSync(join(directory, 'manifest.json'), JSON.stringify(manifest));
        assert.throws(() => loadRepositoryIndex(directory), /provenance/);
    } finally { rmSync(directory, { recursive: true, force: true }); }
});

test('evaluation distinguishes valid citations, source recall, quote support and unanswerable false positives', () => {
    const doc = document('Amber tokenizer preserves Unicode.', 'src/amber.ts');
    const retriever = createRetriever([doc]);
    const good = { id: 'good', question: 'amber tokenizer', evidence: [{ path: 'src/amber.ts', quote: 'preserves Unicode' }] };
    const unknown = { id: 'unknown', question: 'amber tokenizer', evidence: [] };
    const rows = [good, unknown].map(question => scoreQuestion(question, retriever.search(question.question), [doc]));
    const scores = summarizeQuestions(rows);
    assert.equal(scores.sourceRecallAtK, 1);
    assert.equal(scores.passageRecallAtK, 1);
    assert.equal(scores.citationValidity, 1);
    assert.equal(scores.annotatedCitationPrecision, 0.5);
    assert.equal(scores.unanswerableAbstentionRate, 0);
    const irrelevant = scoreQuestion({ ...good, evidence: [{ path: 'src/amber.ts', quote: 'absent support' }] }, retriever.search(good.question), [doc]);
    assert.equal(irrelevant.sourceRecall, 1);
    assert.equal(irrelevant.passageRecall, 0);
    const abstained = scoreQuestion(unknown, retriever.search('extraterrestrial nebula'), [doc]);
    assert.equal(summarizeQuestions([abstained]).citationValidity, null);
    assert.equal(summarizeQuestions([abstained]).unanswerableAbstentionRate, 1);
    assert.throws(() => validateQuestions({ version: 1, questions: [{ ...good, evidence: [{ path: 'missing.ts', quote: 'oops' }] }, unknown] }, [doc]), /Missing gold/);
    assert.throws(() => validateQuestions({ version: 1, questions: [good, good, unknown] }, [doc]), /duplicate/);
    assert.deepEqual(validateQuestions({ version: 1, questions: [good, unknown] }, [doc]), [good, unknown]);
});

test('CLI retrieves a checksummed snapshot and evaluator writes a reproducible, immutable report', () => {
    const { directory } = fixture();
    try {
        const ask = spawnSync(process.execPath, [fileURLToPath(new URL('ask.ts', import.meta.url)),
            '--snapshot', directory, '--question', 'amber tokenizer', '--json'], { encoding: 'utf8' });
        assert.equal(ask.status, 0, ask.stderr);
        assert.equal(JSON.parse(ask.stdout).hits[0].citations[0].path, 'src/amber.ts');
        const suitePath = join(directory, 'questions.json');
        writeFileSync(suitePath, JSON.stringify({ version: 1, questions: [
            { id: 'known', question: 'amber tokenizer', evidence: [{ path: 'src/amber.ts', quote: 'preserves Unicode' }] },
            { id: 'unknown', question: 'extraterrestrial nebula', evidence: [] },
        ] }));
        const reportPath = join(directory, 'report.json');
        const args = [fileURLToPath(new URL('../evaluation/repository.ts', import.meta.url)),
            '--snapshot', directory, '--questions', suitePath, '--output', reportPath];
        const first = spawnSync(process.execPath, args, { encoding: 'utf8' });
        assert.equal(first.status, 0, first.stderr);
        const bytes = readFileSync(reportPath);
        const report = JSON.parse(bytes.toString());
        assert.equal(report.scores.supportingPassageHitAtK, 1);
        assert.equal(report.scores.unanswerableAbstentionRate, 1);
        assert.notEqual(spawnSync(process.execPath, args).status, 0);
        assert.deepEqual(readFileSync(reportPath), bytes);
        args[args.length - 1] = join(directory, 'repeat.json');
        const repeat = spawnSync(process.execPath, args, { encoding: 'utf8' });
        assert.equal(repeat.status, 0, repeat.stderr);
        assert.deepEqual(readFileSync(args[args.length - 1]!), bytes);
    } finally { rmSync(directory, { recursive: true, force: true }); }
});
