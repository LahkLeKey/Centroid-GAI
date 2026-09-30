/** CLI for actual repository-chat transcripts and frozen scenario gates. */
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { runRepositoryChatSuite } from './repository-chat-runner.ts';

const { values } = parseArgs({ options: {
    snapshot: { type: 'string' }, 'additional-snapshot': { type: 'string' }, output: { type: 'string' },
    suite: { type: 'string' }, followups: { type: 'string' }, research: { type: 'string' }, lane: { type: 'string', default: 'first' }, url: { type: 'string' },
} });
const baseUrl = values.url ?? process.env.CGAI_API_URL;
if (!values.snapshot || !values.output || !baseUrl || !['first', 'full'].includes(values.lane)) {
    throw new Error('Usage: node repository-chat.ts --snapshot <directory> --output <new-directory> [--additional-snapshot <directory>] [--followups <development-suite.json>] [--research <development-suite.json>] [--lane first|full] [--url <API-base-URL>]. CGAI_API_URL and CGAI_CHAT_API_TOKEN are supported.');
}
const project = process.env.CGAI_E2E_PROJECT;
const restart = project && /^centroid-gai-e2e-\d+$/.test(project) ? async () => {
    await new Promise<void>((resolve, reject) => {
        const child = spawn('docker', ['compose', '-p', project, 'restart', 'api'], {
            cwd: fileURLToPath(new URL('../../../..', import.meta.url)), stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true,
        });
        let output = '';
        const collect = (chunk: Buffer) => { output = (output + chunk.toString()).slice(-8192); };
        child.stdout.on('data', collect);
        child.stderr.on('data', collect);
        child.on('error', reject);
        child.on('close', code => code === 0 ? resolve() : reject(new Error(`API restart failed (${code}): ${output}`)));
    });
} : undefined;
const report = await runRepositoryChatSuite({ baseUrl, snapshotDirectory: values.snapshot, outputDirectory: values.output,
    lane: values.lane as 'first' | 'full', ...(process.env.CGAI_CHAT_API_TOKEN ? { token: process.env.CGAI_CHAT_API_TOKEN } : {}),
    ...(values['additional-snapshot'] ? { additionalSnapshotDirectory: values['additional-snapshot'] } : {}),
    ...(values.suite ? { suitePath: values.suite } : {}), ...(restart ? { restart } : {}),
    ...(values.followups ? { followupsSuitePath: values.followups } : {}),
    ...(values.research ? { researchSuitePath: values.research } : {}),
});
console.log(JSON.stringify({ output: values.output, firstDeliveryPassed: report.firstDeliveryPassed,
    fullSuitePassed: report.fullSuitePassed, allScenariosPassed: report.allScenariosPassed,
    scores: report.scores, performance: report.performance,
    followups: report.followups ? { passed: report.followups.passed, score: report.followups.score } : null,
    repositoryResearch: report.repositoryResearch ? { passed: report.repositoryResearch.passed, score: report.repositoryResearch.score } : null }, null, 2));
if (!(values.lane === 'full' ? report.fullSuitePassed : report.firstDeliveryPassed) ||
    report.followups?.passed === false || report.repositoryResearch?.passed === false) process.exitCode = 1;
