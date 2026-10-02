/** Autonomous scholarly acquisition and C11 training; no interactive approval or generated reviews. */
import { access, readFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { parseArgs } from 'node:util';
import { ScholarlyApiCollector } from './scholarly-provider.ts';
import { NativeScholarlyCli } from './scholarly-native.ts';
import { ScholarlyTrainingWorkflow, validateScholarlyPolicy } from './scholarly-workflow.ts';
import { parseScholarlyPublishedHead, verifyScholarlyArtifacts } from './scholarly-release.ts';

const repository = fileURLToPath(new URL('../../../..', import.meta.url));
const help = `Unattended scholarly training (native model engine: ISO C11)
  node persistence/api/src/training/scholarly-cli.ts run [--config data/research/autotrain.json] [--cgai path] [--state path]
  node persistence/api/src/training/scholarly-cli.ts watch [--config path] [--cgai path] [--state path]
  node persistence/api/src/training/scholarly-cli.ts export [--config path] [--cgai path] [--state path]
  node persistence/api/src/training/scholarly-cli.ts verify [--config path] [--state path]

run performs one bounded discovery/verification/train/evaluate/replay/promotion cycle.
watch repeats cycles without prompts; Ctrl+C stops acquisition and native subprocesses.
export locks an accepted local model into compiled weights, an optimizer checkpoint and compact TSV records.
verify checks the published model and artifact hashes offline, including on a clean checkout.
Raw paper snapshots, datasets and quoted corpora remain in ignored local evidence files.
No confidence probability is invented; publication verification does not establish scientific truth.
Promotion moves current.tsv atomically after every gate passes. Failed cycles preserve the head.
New vocabulary triggers a deterministic rebuild from training papers only; unchanged text resumes Adam.
No Git commits or HTTP deployment are performed by these commands.`;
const object = (value: unknown): value is Record<string, unknown> => !!value && typeof value === 'object' && !Array.isArray(value);

export interface ScholarlyLoopConfiguration {
    readonly stateDirectory: string;
    readonly intervalSeconds: number;
    readonly cycleBudgetMs: number;
    readonly nativeBudgetMs: number;
    readonly requestDelayMs: number;
    readonly requestLimit: number;
    readonly policy: ReturnType<typeof validateScholarlyPolicy>;
}
export function scholarlyLoopConfiguration(value: unknown, base: string): ScholarlyLoopConfiguration {
    if (!object(value) || value.version !== 1 || typeof value.stateDirectory !== 'string' || !value.stateDirectory.trim() ||
        Object.keys(value).some(key => !['version', 'stateDirectory', 'intervalSeconds', 'cycleBudgetMs', 'nativeBudgetMs',
            'requestDelayMs', 'requestLimit', 'policy'].includes(key))) throw new Error('invalid scholarly loop configuration');
    const config = { stateDirectory: resolve(base, value.stateDirectory), intervalSeconds: value.intervalSeconds ?? 21600,
        cycleBudgetMs: value.cycleBudgetMs ?? 600000, nativeBudgetMs: value.nativeBudgetMs ?? 180000,
        requestDelayMs: value.requestDelayMs ?? 1000, requestLimit: value.requestLimit ?? 100,
        policy: validateScholarlyPolicy(value.policy ?? {}) };
    for (const [key, minimum, maximum] of [['intervalSeconds', 60, 604800], ['cycleBudgetMs', 1000, 3600000],
        ['nativeBudgetMs', 1000, 1800000], ['requestDelayMs', 1000, 60000], ['requestLimit', 1, 1000]] as const) {
        if (!Number.isSafeInteger(config[key]) || (config[key] as number) < minimum || (config[key] as number) > maximum)
            throw new Error(`${key} must be an integer in ${minimum}..${maximum}`);
    }
    return config as ScholarlyLoopConfiguration;
}
async function nativeExecutable(requested?: string): Promise<string> {
    if (requested) { const path = resolve(requested); await access(path); return path; }
    const paths = process.platform === 'win32' ? ['build/deterministic/Release/cgai.exe', 'build/dev/Release/cgai.exe',
        'build/dev/cgai.exe', 'build/Release/cgai.exe', 'build/cgai.exe'] : ['build/dev/cgai', 'build/cgai'];
    for (const relative of paths) {
        const path = join(repository, relative);
        try { await access(path); return path; } catch { /* Try the next normal CMake output. */ }
    }
    throw new Error('build cgai as C11 first, or provide --cgai <executable>');
}
export async function runScholarlyLoop(args: readonly string[]): Promise<void> {
    const { values, positionals } = parseArgs({ args: [...args], allowPositionals: true, options: {
        config: { type: 'string' }, cgai: { type: 'string' }, state: { type: 'string' }, help: { type: 'boolean' },
    } });
    if (values.help || positionals.length === 0) { console.log(help); return; }
    if (positionals.length !== 1 || !['run', 'watch', 'export', 'verify'].includes(positionals[0]!)) throw new Error('expected run, watch, export or verify');
    const path = resolve(values.config ?? join(repository, 'data/research/autotrain.json'));
    const config = scholarlyLoopConfiguration(JSON.parse(await readFile(path, 'utf8')), dirname(path));
    const directory = values.state ? resolve(values.state) : config.stateDirectory;
    if (positionals[0] === 'verify') {
        const path = join(directory, 'current.tsv');
        const head = parseScholarlyPublishedHead(await readFile(path, 'utf8'));
        await verifyScholarlyArtifacts(join(directory, 'releases', head.releaseId), head);
        console.log(JSON.stringify({ status: 'verified', releaseId: head.releaseId, epochs: head.epochs, steps: head.steps }));
        return;
    }
    const executable = await nativeExecutable(values.cgai);
    const controller = new AbortController();
    const stop = () => controller.abort(new Error('scholarly training stopped'));
    process.once('SIGINT', stop); process.once('SIGTERM', stop);
    try {
        do {
            const collector = new ScholarlyApiCollector({ cacheDirectory: join(directory, 'cache'),
                delayMs: config.requestDelayMs, maxRequests: config.requestLimit });
            const workflow = new ScholarlyTrainingWorkflow({ directory, collector,
                native: new NativeScholarlyCli(executable, config.nativeBudgetMs), policy: config.policy });
            const signal = AbortSignal.any([controller.signal, AbortSignal.timeout(config.cycleBudgetMs)]);
            try {
                if (positionals[0] === 'export') {
                    const head = await workflow.exportAccepted(signal);
                    console.log(JSON.stringify({ status: 'exported', releaseId: head.releaseId, epochs: head.epochs, steps: head.steps }));
                    break;
                }
                const result = await workflow.cycle(signal);
                console.log(JSON.stringify(result));
                if (positionals[0] === 'run' && result.status === 'blocked') process.exitCode = 1;
            } catch (error) {
                if (controller.signal.aborted) break;
                console.error(JSON.stringify({ status: 'blocked', reason: error instanceof Error ? error.message : String(error) }));
                if (positionals[0] !== 'watch') process.exitCode = 1;
            }
            if (positionals[0] !== 'watch' || controller.signal.aborted) break;
            await delay(config.intervalSeconds * 1000, undefined, { signal: controller.signal });
        } while (!controller.signal.aborted);
    } catch (error) { if (!controller.signal.aborted) throw error; }
    finally { process.removeListener('SIGINT', stop); process.removeListener('SIGTERM', stop); }
}
if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
    try { await runScholarlyLoop(process.argv.slice(2)); }
    catch (error) { console.error(error instanceof Error ? error.message : String(error)); process.exitCode = 1; }
}
