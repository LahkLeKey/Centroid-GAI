/** Synthetic snapshots for repository routing tests; no developer Git state is read or edited. */
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import type { Document } from '../evaluation/codebase-data.ts';
import { sha256 } from '../evaluation/codebase-data.ts';

export function writeRepositoryFixture(directory: string, commit = 'a'.repeat(40), completed = false) {
    mkdirSync(directory, { recursive: true });
    const files: Record<string, string> = {
        'docs/brief.md': 'Amber is a repository chatbot.\nAmber supports offline repository source follow-ups and cites exact source lines.\n' +
            'Verification commands are proposals.\nRun node --test amber.test.ts from the repository root.\nRun node --test sapphire.test.ts from the repository root.',
        'src/amber.ts': completed ? 'Amber source follow-ups now preserve selected task state after restart.' :
            'Amber source follow-ups should preserve selected task state after restart.',
        'src/sapphire.ts': 'Sapphire artifact checksums protect stored models.\nSapphire artifacts use local machine byte order.',
    };
    const tasks = [
        { id: 'foundation', title: 'Prepare source inventory', status: 'verified', priority: 0, dependsOn: [],
            summary: 'The offline repository inventory is ready.', paths: ['docs/brief.md'],
            checks: [{ command: 'node --test amber.test.ts', cwd: '.' }], evidence: [{ path: 'docs/brief.md', quote: 'Amber is a repository chatbot.' }],
            doneWhen: 'Inventory is present.', keywords: ['inventory'] },
        { id: 'source-followups', title: 'Calibrate source follow-ups', status: completed ? 'verified' : 'planned', priority: 1, dependsOn: ['foundation'],
            summary: 'Retain the selected task across source-mode follow-ups.', paths: ['src/amber.ts'],
            checks: [{ command: 'node --test amber.test.ts', cwd: '.' }],
            evidence: [{ path: 'src/amber.ts', quote: files['src/amber.ts']! }],
            doneWhen: 'Selected task context survives a restart and gives cited verification commands.', keywords: ['source', 'followups', 'follow-ups', 'context', 'restart'] },
        { id: 'artifact-checks', title: 'Verify Sapphire artifact checksums', status: 'planned', priority: 2, dependsOn: ['source-followups'],
            summary: 'Check artifact checksums after source follow-ups are verified.', paths: ['src/sapphire.ts'],
            checks: [{ command: 'node --test sapphire.test.ts', cwd: '.' }],
            evidence: [{ path: 'src/sapphire.ts', quote: 'Sapphire artifact checksums protect stored models.' }],
            doneWhen: 'Artifact checks pass in a captured report.', keywords: ['sapphire', 'artifact', 'checksums'] },
    ];
    files['docs/codebase-tasks.json'] = JSON.stringify({ version: 1, tasks }, null, 2);
    return writeSnapshotDocuments(directory, commit, files);
}

export function writeSnapshotDocuments(directory: string, commit: string, files: Record<string, string>) {
    mkdirSync(directory, { recursive: true });
    const documents: Document[] = Object.entries(files).map(([path, text]) => ({ text, sha256: sha256(text),
        sources: [{ path, commit, blob: sha256(path + text).slice(0, 40) }] }));
    const content: Record<string, string> = { 'train.txt': documents.map(document => document.text + '\n\n').join(''),
        'train.jsonl': documents.map(document => JSON.stringify(document) + '\n').join(''),
        'validation.txt': '', 'validation.jsonl': '', 'SOURCE_LICENSE.txt': 'MIT\n' };
    for (const [name, text] of Object.entries(content)) writeFileSync(join(directory, name), text);
    const manifest = { schema_version: 1, source: { name: 'synthetic-amber', commit }, documents: { train: documents.length, validation: 0 },
        entries: documents.flatMap(document => document.sources.map(source => ({ path: source.path, oid: source.blob }))),
        files: Object.fromEntries(Object.entries(content).map(([name, text]) => [name, sha256(text)])), rejected: [] };
    writeFileSync(join(directory, 'manifest.json'), JSON.stringify(manifest));
    return { directory, documents, manifest, files, identity: { repository: 'synthetic-amber', commit,
        manifestSha256: sha256(JSON.stringify(manifest)) } };
}
