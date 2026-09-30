/** Bounded, offline source relationships. These are lexical dependencies, never a runtime call graph. */
import { posix } from 'node:path';
import ts from 'typescript';
import type { ChatSource } from '../../../shared/chat.ts';
import { sha256, type Document } from '../evaluation/codebase-data.ts';
import { passageId } from '../knowledge/retrieval.ts';

export type RepositoryCodeReferenceKind = 'typescript-import' | 'typescript-export' |
    'typescript-dynamic-import' | 'c-include';
export interface RepositoryCodeEdge {
    from: string;
    to: string;
    kind: RepositoryCodeReferenceKind;
    specifier: string;
    source: ChatSource;
}
export interface RepositoryCodeReference {
    from: string;
    kind: RepositoryCodeReferenceKind;
    specifier: string;
    reason: 'missing' | 'ambiguous' | 'unsupported';
    source: ChatSource;
}
export interface RepositoryCodeInspection {
    path: string;
    dependencies: RepositoryCodeEdge[];
    dependents: RepositoryCodeEdge[];
    unresolved: RepositoryCodeReference[];
    truncated: boolean;
}
export const repositoryCodeIndexLimits = {
    paths: 5000, references: 20000, bytes: 32 * 1024 * 1024, documentBytes: 2 * 1024 * 1024,
    excerptBytes: 16384, inspection: 50, symbolMatches: 20, symbolMatchesPerPath: 2,
} as const;

interface File {
    document: Document;
    provenance: Document['sources'][number];
    lines: string[];
}
interface Reference { kind: RepositoryCodeReferenceKind; specifier: string; start: number; end: number }
const compare = (a: string, b: string) => a < b ? -1 : a > b ? 1 : 0;
const validPath = (path: string) => path.length > 0 && path.length <= 1024 &&
    !/[\\\0:\r\n]/.test(path) && !path.startsWith('/') &&
    path.split('/').every(part => !!part && part !== '.' && part !== '..');
function bound(limit: number, maximum: number) {
    if (!Number.isInteger(limit) || limit < 1 || limit > maximum) {
        throw new Error(`Limit must be an integer from 1 to ${maximum}`);
    }
}
function source(file: File, start: number, end: number): ChatSource | undefined {
    const excerpt = file.lines.slice(start - 1, end).join('\n');
    if (Buffer.byteLength(excerpt) > repositoryCodeIndexLimits.excerptBytes) return undefined;
    return { id: passageId(file.document.sha256, start, end), path: file.provenance.path, excerpt,
        commit: file.provenance.commit, blob: file.provenance.blob, documentSha256: file.document.sha256,
        passageSha256: sha256(excerpt), contentHash: sha256(excerpt),
        coordinateSystem: 'snapshot-normalized-lines', startLine: start, endLine: end };
}

/** The parser does not resolve modules, typecheck, execute code, or read the filesystem. */
function typescriptReferences(path: string, text: string): Reference[] {
    const parsed = ts.createSourceFile(path, text, ts.ScriptTarget.Latest, false);
    const result: Reference[] = [];
    const pending: ts.Node[] = [parsed];
    while (pending.length && result.length <= repositoryCodeIndexLimits.references) {
        const node = pending.pop()!;
        let kind: RepositoryCodeReferenceKind | undefined;
        let specifier: ts.Expression | undefined;
        if (ts.isImportDeclaration(node)) { kind = 'typescript-import'; specifier = node.moduleSpecifier; }
        else if (ts.isExportDeclaration(node) && node.moduleSpecifier) {
            kind = 'typescript-export'; specifier = node.moduleSpecifier;
        } else if (ts.isCallExpression(node) && node.expression.kind === ts.SyntaxKind.ImportKeyword &&
            node.arguments.length >= 1) {
            kind = 'typescript-dynamic-import'; specifier = node.arguments[0];
        } else if (ts.isImportTypeNode(node) && ts.isLiteralTypeNode(node.argument)) {
            kind = 'typescript-import'; specifier = node.argument.literal;
        }
        // Computed module names are deliberately outside the static-literal scope.
        if (kind && specifier && (ts.isStringLiteral(specifier) || ts.isNoSubstitutionTemplateLiteral(specifier))) {
            result.push({ kind, specifier: specifier.text,
                start: parsed.getLineAndCharacterOfPosition(node.getStart(parsed)).line + 1,
                end: parsed.getLineAndCharacterOfPosition(Math.max(node.getStart(parsed), node.getEnd() - 1)).line + 1 });
        }
        ts.forEachChild(node, child => { pending.push(child); });
    }
    return result.sort((a, b) => a.start - b.start || a.end - b.end || compare(a.specifier, b.specifier));
}

/** Mask comments while retaining quoted tokens and offsets; ignore apparent directives inside strings. */
function cReferences(text: string): Reference[] {
    // C removes escaped newlines before recognizing comments and directives. Retain a map
    // back to normalized snapshot offsets so both continued directives and their citations stay exact.
    const spliceChunks: string[] = [];
    const splices: { at: number; removed: number }[] = [];
    let spliceCursor = 0;
    let removed = 0;
    for (const match of text.matchAll(/\\\r?\n/g)) {
        spliceChunks.push(text.slice(spliceCursor, match.index));
        const at = match.index - removed;
        removed += match[0].length;
        splices.push({ at, removed });
        spliceCursor = match.index + match[0].length;
    }
    spliceChunks.push(text.slice(spliceCursor));
    const logical = spliceChunks.join('');
    const originalOffset = (offset: number) => {
        let low = 0, high = splices.length;
        while (low < high) {
            const middle = (low + high) >>> 1;
            if (splices[middle]!.at <= offset) low = middle + 1;
            else high = middle;
        }
        return offset + (low ? splices[low - 1]!.removed : 0);
    };
    const chunks: string[] = [];
    const quoted: { start: number; end: number }[] = [];
    let cursor = 0;
    let copied = 0;
    while (cursor < logical.length) {
        const character = logical[cursor];
        if (character === '"' || character === "'") {
            const start = cursor++;
            while (cursor < logical.length) {
                if (logical[cursor] === '\\') { cursor += 2; continue; }
                if (logical[cursor++] === character) break;
            }
            quoted.push({ start, end: cursor });
        } else if (character === '/' && (logical[cursor + 1] === '/' || logical[cursor + 1] === '*')) {
            const start = cursor;
            const block = logical[cursor + 1] === '*';
            cursor += 2;
            while (cursor < logical.length && (block ? logical.slice(cursor, cursor + 2) !== '*/' : logical[cursor] !== '\n')) cursor++;
            if (block) cursor = Math.min(cursor + 2, logical.length);
            chunks.push(logical.slice(copied, start), logical.slice(start, cursor).replace(/[^\n]/g, ' '));
            copied = cursor;
        } else cursor++;
    }
    chunks.push(logical.slice(copied));
    const masked = chunks.join('');
    const result: Reference[] = [];
    let quote = 0;
    let offset = 0;
    let line = 1;
    for (const match of masked.matchAll(/^[ \t]*#[ \t]*include[ \t]*"([^"\r\n]+)"/gm)) {
        const index = match.index;
        while (quote < quoted.length && quoted[quote]!.end <= index) quote++;
        if (quote < quoted.length && quoted[quote]!.start <= index) continue;
        const start = originalOffset(index), end = originalOffset(index + match[0].length - 1);
        while (offset < start) if (text[offset++] === '\n') line++;
        const startLine = line;
        while (offset < end) if (text[offset++] === '\n') line++;
        result.push({ kind: 'c-include', specifier: match[1]!, start: startLine, end: line });
        if (result.length > repositoryCodeIndexLimits.references) break;
    }
    return result;
}

/** Build only from already verified snapshot documents. No checkout, package, compiler, or network lookup occurs. */
export function createRepositoryCodeIndex(documents: readonly Document[]) {
    const files = new Map<string, File>();
    let bytes = 0;
    let pathBytes = 0;
    for (const document of documents) {
        const size = Buffer.byteLength(document.text);
        bytes += size;
        if (size > repositoryCodeIndexLimits.documentBytes || bytes > repositoryCodeIndexLimits.bytes) {
            throw new Error('Repository code index document byte limit exceeded');
        }
        const lines = document.text.split('\n');
        for (const provenance of document.sources) {
            if (!validPath(provenance.path) || files.has(provenance.path)) throw new Error('Invalid or duplicate repository code index path');
            if (files.size >= repositoryCodeIndexLimits.paths) throw new Error('Repository code index path limit exceeded');
            pathBytes += size;
            if (pathBytes > repositoryCodeIndexLimits.bytes) throw new Error('Repository code index path byte limit exceeded');
            files.set(provenance.path, { document, provenance, lines });
        }
    }
    const paths = [...files.keys()].sort(compare);
    const symbolPriority = (path: string) => {
        if (/(?:^|\/)(?:tests?|__tests__|e2e)(?:\/|$)|(?:\.test|\.spec)\.[^.]+$|(?:^|\/)test_[^/]+$/.test(path)) return 1;
        return /\.(?:c|h|[cm]?ts|tsx|[cm]?js|jsx|py)$/.test(path) ? 0 : 2;
    };
    const symbolPaths = [...paths].sort((a, b) => symbolPriority(a) - symbolPriority(b) || compare(a, b));
    const dependencies = new Map<string, RepositoryCodeEdge[]>();
    const dependents = new Map<string, RepositoryCodeEdge[]>();
    const unresolved = new Map<string, RepositoryCodeReference[]>();
    let referenceCount = 0;
    let truncated = false;
    const resolution = (from: string, reference: Reference): { target: string } | { reason: RepositoryCodeReference['reason'] } => {
        const specifier = reference.specifier;
        if (!specifier || specifier.length > 1024 || /[\\\0\r\n?#]/.test(specifier) || specifier.startsWith('/')) return { reason: 'unsupported' };
        let candidates: string[];
        if (reference.kind === 'c-include') {
            const local = posix.normalize(posix.join(posix.dirname(from), specifier));
            if (!validPath(local)) return { reason: 'unsupported' };
            if (files.has(local)) return { target: local };
            candidates = ['include', 'src/internal', 'src', '.'].map(root => posix.normalize(posix.join(root, specifier)));
        } else {
            if (!/^\.\.?\//.test(specifier)) return { reason: 'unsupported' };
            const base = posix.normalize(posix.join(posix.dirname(from), specifier));
            if (!validPath(base)) return { reason: 'unsupported' };
            const extension = posix.extname(base);
            candidates = [base];
            if (!extension) candidates.push(...['.ts', '.tsx', '.mts', '.cts', '.js', '.jsx', '.mjs', '.cjs', '.d.ts']
                .flatMap(ext => [base + ext, `${base}/index${ext}`]));
            else if (/^\.[cm]?jsx?$/.test(extension)) {
                const stem = base.slice(0, -extension.length);
                const replacements = extension === '.mjs' ? ['.mts', '.d.mts'] :
                    extension === '.cjs' ? ['.cts', '.d.cts'] : ['.ts', '.tsx', '.d.ts'];
                candidates.push(...replacements.map(ext => stem + ext));
            }
        }
        const found = [...new Set(candidates)].filter(path => validPath(path) && files.has(path));
        return found.length === 1 ? { target: found[0]! } : { reason: found.length > 1 ? 'ambiguous' : 'missing' };
    };
    for (const path of paths) {
        const file = files.get(path)!;
        let references: Reference[];
        try {
            references = /\.(?:[cm]?ts|tsx|[cm]?js|jsx)$/.test(path) ? typescriptReferences(path, file.document.text) :
                /\.(?:c|h)$/.test(path) ? cReferences(file.document.text) : [];
        } catch { truncated = true; continue; }
        for (const reference of references) {
            if (++referenceCount > repositoryCodeIndexLimits.references) { truncated = true; break; }
            const citation = source(file, reference.start, reference.end);
            if (!citation) { truncated = true; continue; }
            const target = resolution(path, reference);
            const common = { from: path, kind: reference.kind, specifier: reference.specifier, source: citation };
            if ('reason' in target) {
                const list = unresolved.get(path) ?? [];
                list.push({ ...common, reason: target.reason }); unresolved.set(path, list);
            } else {
                const edge = { ...common, to: target.target };
                const forward = dependencies.get(path) ?? []; forward.push(edge); dependencies.set(path, forward);
                const reverse = dependents.get(target.target) ?? []; reverse.push(edge); dependents.set(target.target, reverse);
            }
        }
        if (referenceCount > repositoryCodeIndexLimits.references) break;
    }
    return {
        inspect(path: string, limit = 20): RepositoryCodeInspection | undefined {
            bound(limit, repositoryCodeIndexLimits.inspection);
            if (!validPath(path) || !files.has(path)) return undefined;
            const forward = dependencies.get(path) ?? [];
            const reverse = dependents.get(path) ?? [];
            const unknown = unresolved.get(path) ?? [];
            return { path, dependencies: structuredClone(forward.slice(0, limit)), dependents: structuredClone(reverse.slice(0, limit)),
                unresolved: structuredClone(unknown.slice(0, limit)),
                truncated: truncated || forward.length > limit || reverse.length > limit || unknown.length > limit };
        },
        /** Exact identifier tokens, including comments/docs; one cited line per match, never semantic callers. */
        findSymbol(symbol: string, limit = 20): { symbol: string; matches: { path: string; source: ChatSource }[]; truncated: boolean } {
            bound(limit, repositoryCodeIndexLimits.symbolMatches);
            if (!/^[A-Za-z_$][A-Za-z0-9_$]{0,127}$/.test(symbol)) throw new Error('Symbol must be an identifier of at most 128 characters');
            const escaped = symbol.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
            const pattern = new RegExp(`(?<![\\p{L}\\p{M}\\p{N}_$\\u200c\\u200d])${escaped}(?![\\p{L}\\p{M}\\p{N}_$\\u200c\\u200d])`, 'u');
            const matches: { path: string; source: ChatSource }[] = [];
            let omitted = false;
            for (const path of symbolPaths) {
                const file = files.get(path)!;
                let count = 0;
                for (let line = 0; line < file.lines.length; line++) {
                    if (!pattern.test(file.lines[line]!)) continue;
                    if (matches.length >= limit) return { symbol, matches, truncated: true };
                    if (count >= repositoryCodeIndexLimits.symbolMatchesPerPath) { omitted = true; break; }
                    const citation = source(file, line + 1, line + 1);
                    if (!citation) { omitted = true; continue; }
                    matches.push({ path, source: citation }); count++;
                }
            }
            return { symbol, matches, truncated: omitted };
        },
    };
}
