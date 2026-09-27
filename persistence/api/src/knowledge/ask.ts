/** Print reviewable source evidence from a local snapshot, without native inference or network access. */
import { parseArgs } from 'node:util';
import { loadRepositoryIndex, retrievalPolicy } from './retrieval.ts';

const { values } = parseArgs({ options: {
    snapshot: { type: 'string' }, question: { type: 'string' }, limit: { type: 'string' }, json: { type: 'boolean' },
} });
if (!values.snapshot || !values.question) throw new Error('Usage: node ask.ts --snapshot <directory> --question <text> [--limit 5] [--json]');
const { snapshot, retriever } = loadRepositoryIndex(values.snapshot);
const result = retriever.search(values.question, values.limit === undefined ? undefined : Number(values.limit));
if (values.json) {
    console.log(JSON.stringify({ sourceCommit: snapshot.manifest.source.commit,
        snapshotManifestSha256: snapshot.manifestSha256, policy: retrievalPolicy, ...result }, null, 2));
} else {
    console.log(`${result.reason}\nSnapshot commit: ${snapshot.manifest.source.commit}`);
    for (const [index, hit] of result.hits.entries()) {
        console.log(`\n[${index + 1}] ${hit.citations.map(citation =>
            `${citation.path}@${citation.commit} (snapshot lines ${citation.startLine}-${citation.endLine}; blob ${citation.blob})`).join('\n')}`);
        console.log(hit.text.split('\n').map(line => `> ${line}`).join('\n'));
    }
}
