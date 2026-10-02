/** Offline dataset lifecycle. No command trains, publishes, or sends data to a server. */
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { parseArgs } from 'node:util';
import { validateRepositoryDataset } from '../chat/dataset.ts';
import { buildRepositoryCorpus } from './repository-corpus.ts';
import { applyReviews, createReviewTemplate, defaultReleaseOptions, exportDatasetRelease, verifyDatasetRelease, writeSnapshot,
    type ReviewDocument, type ReleaseOptions, type ReviewPolicy } from './workflow.ts';

const help = `Repository training data workflow (run from persistence/api):
  node src/training/cli.ts ingest --output <candidates.json> [--root <repository>]
  node src/training/cli.ts validate --dataset <dataset.json>
  node src/training/cli.ts review-template --dataset <dataset.json> --reviewer <id> --reviewer-kind <human|agent> --output <decisions.json>
  node src/training/cli.ts review --dataset <dataset.json> --decisions <decisions.json> --output <reviewed.json>
  node src/training/cli.ts export --dataset <reviewed.json> --output <new-release-directory> [--review-policy human|agent-or-human] [--name <model-name>] [--settings <config-and-training.json>]
  node src/training/cli.ts verify --release <directory>

Edit individual decisions and notes before review; templates approve nothing.
Export defaults to human review. Agent review is available for engineering experiments.
Outputs are exclusive new files/directories; select a new path for each revision.`;

async function readJson(path: string): Promise<unknown> {
    const bytes = await readFile(path);
    if (bytes.byteLength > 16 * 1024 * 1024) throw new Error('input exceeds 16 MiB');
    return JSON.parse(bytes.toString('utf8'));
}

export async function runWorkflow(args: string[]): Promise<void> {
    const { values, positionals } = parseArgs({ args, allowPositionals: true, options: {
        dataset: { type: 'string' }, output: { type: 'string' }, root: { type: 'string' }, decisions: { type: 'string' },
        reviewer: { type: 'string' }, 'reviewer-kind': { type: 'string' }, 'review-policy': { type: 'string' },
        name: { type: 'string' }, settings: { type: 'string' }, release: { type: 'string' }, help: { type: 'boolean' },
    } });
    if (values.help || positionals.length === 0) { console.log(help); return; }
    if (positionals.length !== 1) throw new Error('one workflow command required');
    const required = (key: keyof typeof values): string => {
        const value = values[key];
        if (typeof value !== 'string' || !value.trim()) throw new Error(`--${key} is required`);
        return value;
    };
    switch (positionals[0]) {
        case 'ingest': {
            const dataset = await buildRepositoryCorpus(values.root ? resolve(values.root) : fileURLToPath(new URL('../../../..', import.meta.url)));
            await writeSnapshot(required('output'), validateRepositoryDataset(dataset).dataset);
            console.log(JSON.stringify({ output: values.output, train: dataset.train.length, development: dataset.development.length, test: dataset.test.length, status: 'unreviewed' }));
            break;
        }
        case 'validate': {
            const { dataset, hashes } = validateRepositoryDataset(await readJson(required('dataset')));
            console.log(JSON.stringify({ hashes, counts: { train: dataset.train.length, development: dataset.development.length, test: dataset.test.length } }));
            break;
        }
        case 'review-template': {
            const kind = required('reviewer-kind');
            if (kind !== 'human' && kind !== 'agent') throw new Error('--reviewer-kind must be human or agent');
            const template = createReviewTemplate(await readJson(required('dataset')), { kind, id: required('reviewer') });
            await writeSnapshot(required('output'), template);
            console.log(JSON.stringify({ output: values.output, decisions: template.decisions.length, status: 'unreviewed' }));
            break;
        }
        case 'review': {
            const dataset = applyReviews(await readJson(required('dataset')), await readJson(required('decisions')) as ReviewDocument);
            await writeSnapshot(required('output'), dataset);
            console.log(JSON.stringify({ output: values.output, approved: [...dataset.train, ...dataset.development, ...dataset.test].filter(record => record.review?.status === 'approved').length }));
            break;
        }
        case 'export': {
            const settings = values.settings ? await readJson(values.settings) as Pick<ReleaseOptions, 'config' | 'training'> : defaultReleaseOptions;
            if (!settings || typeof settings !== 'object' || !settings.config || !settings.training || Object.keys(settings).some(key => !['config', 'training', 'name', 'reviewPolicy'].includes(key)))
                throw new Error('--settings requires config and training objects');
            const manifest = await exportDatasetRelease(await readJson(required('dataset')), required('output'), {
                name: values.name ?? defaultReleaseOptions.name, reviewPolicy: (values['review-policy'] ?? 'human') as ReviewPolicy,
                config: settings.config, training: settings.training,
            });
            console.log(JSON.stringify({ output: values.output, releaseId: manifest.releaseId, counts: manifest.counts, review: manifest.review }));
            break;
        }
        case 'verify': {
            const manifest = await verifyDatasetRelease(required('release'));
            console.log(JSON.stringify({ releaseId: manifest.releaseId, verified: true, counts: manifest.counts, review: manifest.review }));
            break;
        }
        default: throw new Error(`unknown workflow command: ${positionals[0]}`);
    }
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
    try { await runWorkflow(process.argv.slice(2)); }
    catch (error) { console.error(error instanceof Error ? error.message : String(error)); process.exitCode = 1; }
}
