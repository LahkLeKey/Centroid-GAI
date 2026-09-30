import assert from 'node:assert/strict';
import test from 'node:test';
import { sha256, type Document } from '../evaluation/codebase-data.ts';
import { createRetriever, validPassage } from '../knowledge/retrieval.ts';
import { createRepositoryRetriever, repositoryTerms } from './repository-retrieval.ts';

function document(path: string, text: string): Document {
    return { text, sha256: sha256(text), sources: [{ path, commit: 'a'.repeat(40), blob: 'b'.repeat(40) }] };
}
test('repository lexical morphology and neighboring context preserve exact evidence while baseline stays unchanged', () => {
    const source = document('docs/storage.md', '# Storage\n\nThe artifact preserves byte order.\nDifferent representations are incompatible.');
    assert(repositoryTerms('artifacts').includes('artifact'));
    const question = 'Are artifacts compatible across byte orders?';
    const result = createRepositoryRetriever([source]).search(question);
    assert(result.length > 0);
    assert(result[0]!.text.includes('Different representations are incompatible.'));
    assert(result.every(hit => validPassage(hit, [source])));
    assert.deepEqual(createRetriever([source]).search(question), createRetriever([source], {}).search(question));
});
test('location and configuration questions prioritize matching files, and unknown acronyms cannot acquire generic evidence', () => {
    const docs = [document('src/widget.c', Array.from({ length: 45 }, (_, index) => index === 18 ?
        'int widget_compute(int input) {' : index === 19 ? 'return input;' : `// supporting widget line ${index}`).join('\n')),
        document('docs/guide.md', 'A widget is implemented by a native function. The guide describes widgets.'),
        document('runtime.yaml', 'services:\n  worker:\n    depends_on:\n      initializer: successful\n'),
        document('docs/start.md', 'The runtime configuration starts the worker after initialization.')];
    const retriever = createRepositoryRetriever(docs);
    const location = retriever.search('Where is the widget implemented?');
    assert.equal(location[0]!.citations[0]!.path, 'src/widget.c');
    assert(location.every(hit => validPassage(hit, docs)));
    const config = retriever.search('How does runtime configuration start the worker?');
    assert.equal(config[0]!.citations[0]!.path, 'runtime.yaml');
    assert(config[0]!.text.includes('initializer: successful'));
    assert.deepEqual(retriever.search('How do I enable ZXQ worker training?'), []);
});

test('definition queries prefer a parameter or field relationship over scattered terms in competing wrappers', () => {
    const question = 'What does retry zero do during delivery?';
    const wrappers = ['bridge', 'adapter'].map(name => document(`include/${name}.h`, [
        '/** @brief Delivery delivery delivery interface.',
        ' * @param retry Retry retry retry count.',
        ' * @param capacity Zero zero zero chooses automatic allocation.',
        ' * @param seed Zero zero zero chooses the default random seed.',
        ' * @return Delivery result. */',
        `int ${name}_delivery(int retry, int capacity, int seed);`,
    ].join('\n')));
    const contract = document('include/transport.h', [
        '/** @brief Send a packet with bounded ownership.',
        ' * The caller retains its input allocation until this operation completes.',
        ' * @param retry Nonnegative count of additional attempts;',
        ' * zero disables automatic retries.',
        ' * @param destination Borrowed destination address.',
        ' * @return A delivery result after all requested attempts. */',
        'int transport_send(int retry, const char *destination);',
    ].join('\n'));
    const settings = document('include/options.h', [
        '/** Transport delivery settings are copied before sending. */',
        'struct delivery_options {',
        '    unsigned retry; /**< Zero disables retries after the initial attempt. */',
        '};',
    ].join('\n'));
    const docs = [...wrappers, contract, settings, document('docs/delivery.md',
        'Delivery delivery delivery. Retry retry retry. Zero zero zero. Configuration combines these settings.')];
    const lexical = createRetriever(docs.filter(doc => doc.sources[0]!.path.startsWith('include/')),
        { terms: repositoryTerms, minimumCoverage: 0.35, pathWeight: 4 }).search(question, 2).hits;
    assert(lexical.every(hit => /\/(bridge|adapter)\.h$/.test(hit.citations[0]!.path)),
        'fixture must exercise competing wrapper matches before declaration ranking');
    const retriever = createRepositoryRetriever(docs);
    const result = retriever.search(question);
    assert.deepEqual(new Set(result.slice(0, 2).map(hit => hit.citations[0]!.path)), new Set(['include/transport.h', 'include/options.h']));
    assert(result.some(hit => hit.text.includes('zero disables automatic retries.')));
    assert(result.every(hit => validPassage(hit, docs) && Buffer.byteLength(hit.text) <= 16384));
    assert(result.length <= 7);
    assert.deepEqual(result, createRepositoryRetriever([...docs].reverse()).search(question));
    assert.deepEqual(retriever.search('What does ZXQ retry delivery mean?'), []);
});

test('priority declaration evidence spans different documents when one long header has repeated matching blocks', () => {
    const repeated = document('include/large_adapter.h', Array.from({ length: 160 }, (_, index) => {
        if (index % 40 === 0) return '/** @param budget Zero disables retries for this delivery adapter. */';
        return index % 40 === 1 ? `int delivery_${index}(int budget);` : '// The caller owns the borrowed workspace.';
    }).join('\n'));
    const direct = document('include/base.h', ['/** @param budget Zero disables automatic delivery retries. */',
        'int send_packet(int budget);', ...Array.from({ length: 22 }, () =>
            '// Each caller retains ownership of input addresses and allocated workspace throughout the call.')].join('\n'));
    const docs = [repeated, direct];
    const question = 'What does budget zero do during delivery?';
    const lexical = createRetriever(docs, { terms: repositoryTerms, minimumCoverage: 0.35, pathWeight: 4 }).search(question, 2).hits;
    assert(lexical.every(hit => hit.citations[0]!.path === 'include/large_adapter.h'),
        'fixture must exercise one document occupying both unadjusted priority slots');
    const result = createRepositoryRetriever(docs).search(question);
    assert.equal(new Set(result.slice(0, 2).map(hit => hit.citations[0]!.documentSha256)).size, 2);
    assert(result.slice(0, 2).some(hit => hit.citations[0]!.path === 'include/base.h'));
    assert(result.every(hit => validPassage(hit, docs)));
});

test('function-location follow-up wording preserves the substantive topic and its declaration evidence', () => {
    const source = document('src/segmenter.c', Array.from({ length: 200 }, (_, index) => {
        if (index === 0) return '/** Segmenter implementation. */';
        if (index === 90) return '// The function below implements a private allocation detail.';
        if (index === 190) return 'int segmenter_run(const char *input) {';
        if (index === 191) return '    return input != 0;';
        return '// The caller retains ownership of its working allocation.';
    }).join('\n'));
    const unrelated = document('docs/functions.md', '# Functions\nAn unrelated function implements an independent subsystem.');
    const docs = [source, unrelated], retriever = createRepositoryRetriever(docs);
    const location = 'Where is the segmenter implemented?';
    const contextual = location + ' Which function implements it?';
    assert.deepEqual(repositoryTerms(contextual), repositoryTerms(location));
    const result = retriever.search(contextual);
    assert(result.some(hit => hit.citations[0]!.path === 'src/segmenter.c' && hit.text.includes('int segmenter_run(const char *input)')));
    assert(result.every(hit => validPassage(hit, docs)));
    assert(result.length <= 7);
});
