/**
 * @file seed-models.ts
 * @brief Rebuilds named example models through the local HTTP API.
 *
 * The source corpora are committed under `examples/model_corpora/`; generated `.cgai` files are
 * deliberately not source-controlled. Each request uses the API training upsert, so rerunning
 * this command replaces the named model payload and metadata with artifacts trained from the
 * current corpus files. This makes the examples reproducible after a clean checkout or database
 * reset without coupling the seed workflow to Prisma internals.
 */
import {readFile} from "node:fs/promises";
import {fileURLToPath} from "node:url";

interface ExampleModel {
    readonly name: string;
    readonly corpusPath: string;
    readonly description: string;
}

interface TrainResponse {
    readonly name: string;
    readonly id: string;
    readonly metadata: {
        readonly formatVersion: number; readonly libraryVersion : string; readonly dimensions : number; readonly centroidCount : number; readonly contextWindow : number; readonly vocabularySize : string; readonly examplesSeen :
                                                                                                                                                                                                                         string;
    };
}

const repositoryRoot = fileURLToPath(new URL("../..", import.meta.url));
const apiUrl = process.env.CGAI_API_URL ?? "http://127.0.0.1:3000";

const exampleModels: readonly ExampleModel[] = [
    {
        name : "tiny-contexts",
        corpusPath : "examples/model_corpora/tiny_contexts.txt",
        description : "A compact corpus for inspecting the basic training flow.",
    },
    {
        name : "generation-patterns",
        corpusPath : "examples/model_corpora/generation_patterns.txt",
        description : "A corpus focused on nearest-context generation behavior.",
    },
    {
        name : "persistence-workflow",
        corpusPath : "examples/model_corpora/persistence_workflow.txt",
        description : "A corpus that follows train, persist, load, and generate steps.",
    },
];

/** Trains one named model by sending its committed corpus to the API upsert endpoint. */
async function seedModel(model: ExampleModel): Promise<void> {
    // Step 1: Resolve and read the committed corpus so the seed is independent of the current
    // working directory and always reports the source file used for this model.
    const corpusFile = `${repositoryRoot}/${model.corpusPath}`;
    const text = await readFile(corpusFile, "utf8");

    // Step 2: Send the corpus through the public training route. The API performs native training,
    // native inspection, checksum calculation, and database replacement as one application flow.
    const response = await fetch(`${apiUrl}/api/v1/models/${model.name}/train`, {
        method : "POST",
        headers : {"content-type" : "application/json"},
        body : JSON.stringify({text}),
    });
    if (!response.ok) {
        throw new Error(
            `${model.name} failed with HTTP ${response.status}: ${await response.text()}`,
        );
    }

    // Step 3: Print the authoritative response so a local operator can see which artifact and
    // native metadata were produced without opening the database directly.
    const result = (await response.json()) as TrainResponse;
    console.log(`${result.name}: ${model.description}`);
    console.log(`  id=${result.id}`);
    console.log(
        `  format=${result.metadata.formatVersion} library=${result.metadata.libraryVersion}` +
            ` dimensions=${result.metadata.dimensions} centroids=${result.metadata.centroidCount}` +
            ` context=${result.metadata.contextWindow} vocabulary=${
                result.metadata.vocabularySize}` +
            ` examples=${result.metadata.examplesSeen}`,
    );
}

/** Waits for the API before seeding so a fresh Compose start has a deterministic failure mode. */
async function waitForApi(): Promise<void> {
    // Step 1: Poll the same health contract used by the Compose healthcheck rather than guessing
    // how long PostgreSQL initialization and native API startup will take.
    for (let attempt = 1; attempt <= 60; attempt += 1) {
        try {
            const response = await fetch(`${apiUrl}/health`);
            if (response.ok) {
                return;
            }
        } catch {
            // The next attempt handles normal startup connection refusal.
        }
        await Bun.sleep(1_000);
    }
    throw new Error(`API did not become healthy at ${apiUrl}`);
}

// Step 1: Make the command safe to run immediately after `docker compose up -d`.
await waitForApi();
// Step 2: Train each committed example corpus sequentially to keep logs readable and database
// pressure predictable on a developer laptop.
for (const model of exampleModels) {
    await seedModel(model);
}
