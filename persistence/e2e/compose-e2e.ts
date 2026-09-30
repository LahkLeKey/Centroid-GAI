/**
 * @file compose-e2e.ts
 * @brief Starts the local Docker Compose stack and runs the API boundary tests against it.
 *
 * The API tests intentionally skip when `CGAI_API_URL` is absent so native tests remain cheap and
 * database-free. This runner is the explicit full-system entry point: Docker owns PostgreSQL,
 * database initialization, and the API process; the test process only observes HTTP behavior.
 */
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { randomUUID } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const repositoryRoot = fileURLToPath(new URL("../..", import.meta.url));
const persistenceRoot = fileURLToPath(new URL("..", import.meta.url));
const apiPort = process.env.CGAI_E2E_API_PORT ?? '3100';
const databasePort = process.env.CGAI_E2E_POSTGRES_PORT ?? '55432';
const apiUrl = `http://127.0.0.1:${apiPort}`;
const repositorySuite = process.argv.includes('--repository');
const fixtureDirectory = resolve(repositoryRoot, `build/knowledge/repository-e2e-${process.pid}`);
const composeArgs = ['compose', ...(repositorySuite ? ['-f', 'compose.yaml', '-f', 'compose.repository.yaml'] : []), '-p', `centroid-gai-e2e-${process.pid}`];
const composeEnvironment: NodeJS.ProcessEnv = { ...process.env, API_PORT: apiPort, POSTGRES_PORT: databasePort,
    CGAI_CHAT_API_TOKEN: randomUUID(),
    CGAI_SEARCH_PROVIDER: process.env.CGAI_E2E_RESEARCH_LIVE === '1' ? 'wikipedia' : 'disabled',
    CGAI_SEARCH_URL: '' };
const healthTimeoutMs = 180_000;
const healthIntervalMs = 1_000;

/** Runs one Docker or Bun subprocess and forwards its output to the operator. */
function run(
    command: string,
    args: string[],
    environment?: NodeJS.ProcessEnv,
    cwd = repositoryRoot,
): number {
    const result = spawnSync(command, args, {
        cwd,
        env: environment,
        stdio: "inherit",
    });
    return result.status ?? 1;
}

/** Waits until the API reports healthy or Docker startup exceeds the test budget. */
async function waitForHealth(): Promise<void> {
    const deadline = Date.now() + healthTimeoutMs;
    let lastError = "no response";

    while (Date.now() < deadline) {
        try {
            const response = await fetch(`${apiUrl}/health`, { signal: AbortSignal.timeout(5000) });
            if (response.ok) {
                return;
            }
            lastError = `HTTP ${response.status}`;
        } catch (error: unknown) {
            lastError = error instanceof Error ? error.message : "unknown fetch error";
        }
        await Bun.sleep(healthIntervalMs);
    }

    throw new Error(
        `Compose API did not become healthy within ${healthTimeoutMs} ms: ${lastError}`,
    );
}

/**
 * Runs E2E in a fresh process-specific Compose project, then removes only its test resources.
 */
async function main(): Promise<void> {
    let composeStarted = false;

    try {
        if (repositorySuite) {
            if (run(process.env.CGAI_PYTHON ?? (process.platform === 'win32' ? 'python' : 'python3'),
                ['-m', 'tools.knowledge.e2e_repository', '--repo', repositoryRoot, '--output', fixtureDirectory]) !== 0)
                throw new Error('repository fixture preparation failed');
            const fixture = JSON.parse(readFileSync(resolve(fixtureDirectory, 'fixture.json'), 'utf8')) as { root: string };
            composeEnvironment.CGAI_REPOSITORY_ROOT_HOST = fixture.root;
            composeEnvironment.CGAI_REPOSITORY_REF = 'a';
            composeEnvironment.CGAI_REPOSITORY_ADDITIONAL_SNAPSHOTS = '["/app/knowledge/snapshots/b"]';
            composeEnvironment.CGAI_SEARCH_PROVIDER = 'disabled';
            composeEnvironment.CGAI_E2E_RESEARCH_LIVE = '0';
        }
        // Step 1: Build and start every service so the test covers PostgreSQL, schema setup, the
        // native addon, and HTTP routing as one local deployment rather than a mocked process.
        composeStarted = true;
        if (
            run("docker", [
                ...composeArgs,
                "up",
                "--detach",
                "--build",
                "--force-recreate",
                "--remove-orphans",
            ], composeEnvironment) !== 0
        ) {
            throw new Error("docker compose up failed");
        }

        // Step 2: Wait on the public health endpoint instead of guessing container startup timing;
        // this also proves the API can reach its initialized database before tests begin.
        await waitForHealth();

        // Step 3: Run the existing API integration suite against the Compose API. The native
        // training endpoint creates a real artifact, persists it in PostgreSQL, generates from it,
        // downloads it, and deletes it again.
        const environment = { ...composeEnvironment, CGAI_API_URL: apiUrl, CGAI_E2E_PROJECT: `centroid-gai-e2e-${process.pid}` };
        const status = run(
            process.execPath,
            ["run", "--cwd", "api", "api:test"],
            environment,
            persistenceRoot,
        );
        if (status !== 0) {
            throw new Error(`Compose API E2E tests failed with exit code ${status}`);
        }
        if (repositorySuite && run('node', ['src/evaluation/repository-chat.ts',
            '--snapshot', resolve(fixtureDirectory, 'snapshots/a'),
            '--additional-snapshot', resolve(fixtureDirectory, 'snapshots/b'), '--lane', 'full',
            '--followups', resolve(repositoryRoot, 'examples/chat/repository-followups-v1.json'),
            '--research', resolve(repositoryRoot, 'examples/chat/repository-research-v1.json'),
            '--output', resolve(repositoryRoot, `build/evaluation/repository-chat/${process.pid}`)],
            environment, resolve(persistenceRoot, 'api')) !== 0)
            throw new Error('repository chat scenario suite failed; inspect build/evaluation/repository-chat');
    } finally {
        // Step 4: This process owns a unique project, so cleanup cannot stop the developer stack.
        if (composeStarted) {
            run("docker", [...composeArgs, 'logs', '--no-color'], composeEnvironment);
            run("docker", [...composeArgs, "down", "--volumes", "--remove-orphans"], composeEnvironment);
        }
    }
}

await main();
