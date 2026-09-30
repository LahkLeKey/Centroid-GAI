import assert from 'node:assert/strict';
import test from 'node:test';
import type { ChatSource } from '../../../shared/chat.ts';
import { sha256, type Document } from '../evaluation/codebase-data.ts';
import { passageId } from '../knowledge/retrieval.ts';
import { createRepositoryCodeIndex, repositoryCodeIndexLimits } from './repository-code-index.ts';

function document(path: string, text: string): Document {
    return { text, sha256: sha256(text), sources: [{ path, commit: 'a'.repeat(40), blob: 'b'.repeat(40) }] };
}
function verify(citation: ChatSource, documents: Document[]) {
    const doc = documents.find(item => item.sha256 === citation.documentSha256)!;
    assert(doc, 'citation belongs to an input document');
    const provenance = doc.sources.find(item => item.path === citation.path)!;
    assert(provenance, 'citation uses the correct provenance alias');
    assert.equal(citation.commit, provenance.commit);
    assert.equal(citation.blob, provenance.blob);
    assert.equal(citation.coordinateSystem, 'snapshot-normalized-lines');
    const excerpt = doc.text.split('\n').slice(citation.startLine! - 1, citation.endLine).join('\n');
    assert.equal(citation.excerpt, excerpt);
    assert.equal(citation.id, passageId(doc.sha256, citation.startLine!, citation.endLine!));
    assert.equal(citation.passageSha256, sha256(excerpt));
    assert.equal(citation.contentHash, sha256(excerpt));
    assert(Buffer.byteLength(excerpt) <= repositoryCodeIndexLimits.excerptBytes);
}

test('TS AST recognizes literal import, export, dynamic and type references with exact multiline evidence', () => {
    const documents = [document('src/main.ts', [
        '// import "./fake.ts";',
        'const example = `import "./fake.ts";`;',
        'const regex = /import(".\/fake.ts")/;',
        'import type {',
        '  Settings,',
        '} from "./settings.js";',
        'export { create } from "./factory.ts";',
        'export * from "./barrel/index.ts";',
        'async function load() { return import(',
        '  "./lazy.ts"',
        '); }',
        'const literal = import(`./template.ts`);',
        'type Value = import("./value.ts").Value;',
        'const computed = import("./" + name);',
        'const template = import(`./${name}`);',
    ].join('\n')),
    ...['settings.ts', 'factory.ts', 'barrel/index.ts', 'lazy.ts', 'template.ts', 'value.ts', 'fake.ts']
        .map(path => document(`src/${path}`, 'export {};'))];
    const index = createRepositoryCodeIndex(documents);
    const found = index.inspect('src/main.ts')!;
    assert.deepEqual(found.dependencies.map(edge => [edge.to, edge.kind]), [
        ['src/settings.ts', 'typescript-import'], ['src/factory.ts', 'typescript-export'],
        ['src/barrel/index.ts', 'typescript-export'], ['src/lazy.ts', 'typescript-dynamic-import'],
        ['src/template.ts', 'typescript-dynamic-import'], ['src/value.ts', 'typescript-import'],
    ]);
    assert.equal(found.dependencies[0]!.source.startLine, 4);
    assert.equal(found.dependencies[0]!.source.endLine, 6);
    assert.equal(found.dependencies[3]!.source.startLine, 9);
    assert.equal(found.dependencies[3]!.source.endLine, 11);
    assert.deepEqual(found.unresolved, []);
    assert.equal(found.truncated, false);
    for (const edge of found.dependencies) verify(edge.source, documents);
    assert.deepEqual(index.inspect('src/fake.ts')!.dependents, []);
});

test('only unique snapshot targets resolve; external packages, escapes, missing and ambiguous modules never become edges', () => {
    const documents = [document('src/main.ts', [
        'import "./choice";', 'import "./pair.js";', 'import "./folder";',
        'import "./missing.ts";', 'import "../../outside.ts";', 'import "node:fs";',
        'import "library";', 'import "./exact.ts";', 'import "./single.js";',
        'import "./module.mjs";', 'import "./common.cjs";', 'import "./resolved";',
    ].join('\n')),
    ...['choice.ts', 'choice.tsx', 'pair.ts', 'pair.js', 'folder.ts', 'folder/index.ts',
        'exact.ts', 'exact.ts.ts', 'single.ts', 'module.mts', 'common.cts', 'resolved/index.ts']
        .map(path => document(`src/${path}`, 'export {};'))];
    const result = createRepositoryCodeIndex(documents).inspect('src/main.ts')!;
    assert.deepEqual(result.dependencies.map(edge => edge.to), [
        'src/exact.ts', 'src/single.ts', 'src/module.mts', 'src/common.cts', 'src/resolved/index.ts',
    ]);
    assert.deepEqual(result.unresolved.map(item => [item.specifier, item.reason]), [
        ['./choice', 'ambiguous'], ['./pair.js', 'ambiguous'], ['./folder', 'ambiguous'],
        ['./missing.ts', 'missing'], ['../../outside.ts', 'unsupported'], ['node:fs', 'unsupported'], ['library', 'unsupported'],
    ]);
    for (const unresolved of result.unresolved) verify(unresolved.source, documents);
});

test('C quoted include resolution respects current directory, known roots and ambiguity without fake comment or string directives', () => {
    const documents = [document('src/widget.c', [
        '/* #include "ghost.h" */', '/*', '#include "ghost.h"', '*/',
        'const char *example = "\\', '#include \\"ghost.h\\"";',
        '# include "local.h"', '#include/* gap */"public.h"', '#include "private.h"',
        '#include"root.h"', '#include "ambiguous.h"', '#include "absent.h"',
        '#include "/absolute.h"', '#include <stdlib.h>', '#include GENERATED_HEADER',
    ].join('\n')),
    ...['src/local.h', 'include/local.h', 'include/public.h', 'src/internal/private.h', 'root.h',
        'src/ambiguous.h', 'include/ambiguous.h', 'include/ghost.h'].map(path => document(path, '/* header */'))];
    const result = createRepositoryCodeIndex(documents).inspect('src/widget.c')!;
    // src/ambiguous.h is found in the current directory before fallback roots.
    assert.deepEqual(result.dependencies.map(edge => edge.to), [
        'src/local.h', 'include/public.h', 'src/internal/private.h', 'root.h', 'src/ambiguous.h',
    ]);
    assert.deepEqual(result.unresolved.map(item => [item.specifier, item.reason]), [
        ['absent.h', 'missing'], ['/absolute.h', 'unsupported'],
    ]);
    assert(result.dependencies.every(edge => edge.kind === 'c-include'));
    for (const edge of result.dependencies) verify(edge.source, documents);
    const ambiguous = createRepositoryCodeIndex([document('src/deep/widget.c', '#include "shared.h"'),
        document('include/shared.h', ''), document('src/internal/shared.h', '')]).inspect('src/deep/widget.c')!;
    assert.deepEqual(ambiguous.dependencies, []);
    assert.equal(ambiguous.unresolved[0]!.reason, 'ambiguous');
});

test('deduplicated document aliases resolve separately and reverse relationships are direct, deterministic and immutable', () => {
    const shared = document('one/main.ts', 'import "./value.ts";');
    shared.sources.push({ path: 'two/main.ts', blob: 'c'.repeat(40), commit: 'a'.repeat(40) });
    const documents = [shared, document('one/value.ts', 'import "../root.ts";'),
        document('two/value.ts', 'export {};'), document('root.ts', 'import "./one/main.ts";')];
    const index = createRepositoryCodeIndex(documents);
    const first = index.inspect('one/main.ts')!;
    assert.equal(first.dependencies[0]!.to, 'one/value.ts');
    assert.equal(index.inspect('two/main.ts')!.dependencies[0]!.to, 'two/value.ts');
    assert.equal(index.inspect('two/main.ts')!.dependencies[0]!.source.blob, 'c'.repeat(40));
    assert.deepEqual(index.inspect('root.ts')!.dependents.map(edge => edge.from), ['one/value.ts']);
    assert.deepEqual(first, createRepositoryCodeIndex([...documents].reverse()).inspect('one/main.ts'));
    first.dependencies[0]!.to = 'mutated';
    assert.equal(index.inspect('one/main.ts')!.dependencies[0]!.to, 'one/value.ts');
    for (const path of ['one/main.ts', 'two/main.ts', 'root.ts']) {
        for (const edge of index.inspect(path)!.dependencies) verify(edge.source, documents);
    }
});

test('C escaped newlines are spliced before comments and directives while citations retain all original lines', () => {
    const documents = [document('main.c', [
        '// comment continued \\', '#include "fake.h"',
        '#inc\\', 'lude \\', '  "real.h"',
        '/* comment \\', '#include "fake.h" */',
    ].join('\n')), document('real.h', ''), document('fake.h', '')];
    const result = createRepositoryCodeIndex(documents).inspect('main.c')!;
    assert.equal(result.dependencies.length, 1);
    assert.equal(result.dependencies[0]!.to, 'real.h');
    assert.equal(result.dependencies[0]!.source.startLine, 3);
    assert.equal(result.dependencies[0]!.source.endLine, 5);
    verify(result.dependencies[0]!.source, documents);
});

test('lexical symbols are case-sensitive exact identifiers across code, comments and docs, not substrings or caller claims', () => {
    const documents = [document('src/exact.ts', [
        'widgetExtra _widget widget_ widget2 $widget widget$ Widget \u03b1widget widget\u0301',
        'export function widget() {}', '// widget is documented here.', '// widget appears a third time.',
    ].join('\n')), document('docs/usage.md', '`widget` accepts input.'),
    document('src/dollar.ts', 'const $special = 1; const special = 2;')];
    const index = createRepositoryCodeIndex(documents);
    const result = index.findSymbol('widget');
    assert.deepEqual(result.matches.map(match => [match.path, match.source.startLine]), [
        ['src/exact.ts', 2], ['src/exact.ts', 3], ['docs/usage.md', 1],
    ]);
    assert.equal(result.truncated, true);
    assert.equal(index.findSymbol('Widget').matches[0]!.source.startLine, 1);
    assert.equal(index.findSymbol('$special').matches[0]!.path, 'src/dollar.ts');
    assert.equal(index.findSymbol('special').matches.length, 1);
    assert.deepEqual(index.findSymbol('absent'), { symbol: 'absent', matches: [], truncated: false });
    for (const match of result.matches) verify(match.source, documents);
});

test('bounded lexical results prioritize source code, then tests, then documentation without claiming definitions', () => {
    const documents = [document('00-docs/guide.md', 'searchTerm is explained here.'),
        document('01-tests/test_search.py', '# searchTerm is mentioned in a test'),
        document('zz-src/worker.py', '# searchTerm is mentioned in source'),
        document('zz-src/worker.test.ts', '// searchTerm is mentioned in another test')];
    const index = createRepositoryCodeIndex(documents);
    assert.deepEqual(index.findSymbol('searchTerm', 3).matches.map(match => match.path), [
        'zz-src/worker.py', '01-tests/test_search.py', 'zz-src/worker.test.ts',
    ]);
    assert.equal(index.findSymbol('searchTerm', 3).truncated, true);
    assert.equal(index.findSymbol('searchTerm', 4).matches[3]!.path, '00-docs/guide.md');
});

test('symbol and inspection output limits report omitted matches and reject invalid limits', () => {
    const documents = Array.from({ length: 25 }, (_, index) => document(`src/item${String(index).padStart(2, '0')}.ts`,
        'import "../target.ts";\nconst target = 1;'));
    documents.push(document('target.ts', 'export const target = 1;'));
    const index = createRepositoryCodeIndex(documents);
    assert.equal(index.findSymbol('target').matches.length, 20);
    assert.equal(index.findSymbol('target').truncated, true);
    assert.equal(index.findSymbol('target', 3).matches.length, 3);
    assert.equal(index.inspect('target.ts', 5)!.dependents.length, 5);
    assert.equal(index.inspect('target.ts', 5)!.truncated, true);
    assert.equal(index.inspect('target.ts', 50)!.truncated, false);
    for (const invalid of [0, -1, 1.5, NaN, 21]) assert.throws(() => index.findSymbol('target', invalid));
    for (const invalid of [0, 51, 1.5]) assert.throws(() => index.inspect('target.ts', invalid));
    for (const invalid of ['', 'has space', 'x.y', '[abc]', 'a'.repeat(129)]) assert.throws(() => index.findSymbol(invalid));
    assert.equal(index.inspect('../target.ts'), undefined);
    assert.equal(index.inspect('absent.ts'), undefined);
});

test('oversized citation lines are omitted honestly; input size, path and reference caps are enforced', () => {
    const long = '#include "target.h" // marker ' + 'x'.repeat(repositoryCodeIndexLimits.excerptBytes);
    const index = createRepositoryCodeIndex([document('src/long.c', long), document('include/target.h', '')]);
    assert.deepEqual(index.inspect('src/long.c')!.dependencies, []);
    assert.equal(index.inspect('src/long.c')!.truncated, true);
    assert.deepEqual(index.findSymbol('marker'), { symbol: 'marker', matches: [], truncated: true });
    assert.throws(() => createRepositoryCodeIndex([document('huge.ts', 'x'.repeat(repositoryCodeIndexLimits.documentBytes + 1))]), /byte limit/);
    assert.throws(() => createRepositoryCodeIndex([document('../outside.ts', '')]), /path/);
    assert.throws(() => createRepositoryCodeIndex([document('same.ts', 'a'), document('same.ts', 'b')]), /duplicate/);
    const aliases = document('first.ts', '');
    aliases.sources = Array.from({ length: repositoryCodeIndexLimits.paths + 1 }, (_, index) =>
        ({ path: `alias${index}.ts`, commit: 'a'.repeat(40), blob: 'b'.repeat(40) }));
    assert.throws(() => createRepositoryCodeIndex([aliases]), /path limit/);
    const repeated = document('first.ts', 'x'.repeat(repositoryCodeIndexLimits.documentBytes));
    repeated.sources = aliases.sources.slice(0, 17);
    assert.throws(() => createRepositoryCodeIndex([repeated]), /byte limit/);
    const many = createRepositoryCodeIndex([document('many.c', '#include "target.h"\n'.repeat(repositoryCodeIndexLimits.references + 1)),
        document('target.h', '')]);
    assert.equal(many.inspect('many.c')!.truncated, true);
    assert.equal(many.inspect('target.h')!.truncated, true);
    assert.equal(many.inspect('target.h')!.dependents.length, 20);
});

test('dotted paths and targets whose name matches a diagnostic are still ordinary snapshot paths', () => {
    const documents = [document('.config/main.ts', 'import "../missing";'), document('missing', 'export {};')];
    const result = createRepositoryCodeIndex(documents).inspect('.config/main.ts')!;
    assert.equal(result.dependencies[0]!.to, 'missing');
    assert.deepEqual(result.unresolved, []);
    verify(result.dependencies[0]!.source, documents);
});
