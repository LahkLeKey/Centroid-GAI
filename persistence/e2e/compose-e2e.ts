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

const repositoryRoot = fileURLToPath(new URL("../..", import.meta.url));
const persistenceRoot = fileURLToPath(new URL("..", import.meta.url));
const apiUrl = "http://127.0.0.1:3000";
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
            const response = await fetch(`${apiUrl}/health`);
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
 * Starts Compose, runs E2E HTTP tests, and removes containers while retaining the database volume.
 */
async function main(): Promise<void> {
    let composeStarted = false;

    try {
        // Step 1: Build and start every service so the test covers PostgreSQL, schema setup, the
        // native addon, and HTTP routing as one local deployment rather than a mocked process.
        if (
            run("docker", [
                "compose",
                "up",
                "--detach",
                "--build",
                "--force-recreate",
                "--remove-orphans",
            ]) !== 0
        ) {
            throw new Error("docker compose up failed");
        }
        composeStarted = true;

        // Step 2: Wait on the public health endpoint instead of guessing container startup timing;
        // this also proves the API can reach its initialized database before tests begin.
        await waitForHealth();

        // Step 3: Run the existing API integration suite against the Compose API. The native
        // training endpoint creates a real artifact, persists it in PostgreSQL, generates from it,
        // downloads it, and deletes it again.
        const environment = { ...process.env, CGAI_API_URL: apiUrl };
        const status = run(
            process.execPath,
            ["run", "--cwd", "api", "api:test"],
            environment,
            persistenceRoot,
        );
        if (status !== 0) {
            throw new Error(`Compose API E2E tests failed with exit code ${status}`);
        }
    } finally {
        // Step 4: Stop and remove containers even when health or an assertion fails. The named
        // postgres-data volume is retained so repeated local runs do not destroy operator data.
        if (composeStarted) {
            run("docker", ["compose", "down", "--remove-orphans"]);
        }
    }
}

await main();
