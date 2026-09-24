/**
 * @file cli.ts
 * @brief Local operator CLI for storing and retrieving `.cgai` artifacts without the HTTP API.
 *
 * Thin argument parsing over the same `native.ts`/`model-repository.ts` functions the server
 * uses, so behavior (validation, checksums, upsert semantics) cannot drift between the two entry
 * points.
 */
import { readFile, writeFile } from "node:fs/promises";

import { closeModelRepository, loadModelArtifact, saveModelArtifact } from "./model-repository.ts";
import { generateNativeModel, inspectNativeModel, trainNativeModel } from "./native.ts";

function usage(): never {
    console.error("Usage:");
    console.error("  bun run model -- put <name> <model.cgai>");
    console.error("  bun run model -- get <name> <model.cgai>");
    console.error("  bun run model -- train <name> <corpus.txt>");
    console.error('  bun run model -- generate <name> "prompt"');
    process.exit(2);
}

async function main(): Promise<void> {
    const [, , command, name, path] = process.argv;
    if (!command || !name || !path) {
        usage();
    }

    if (command === "put") {
        const payload = await readFile(path);
        const metadata = inspectNativeModel(payload);
        const saved = await saveModelArtifact({ name, payload, metadata });
        console.log(`saved ${saved.name} as ${saved.id} (${saved.checksumSha256})`);
        return;
    }

    if (command === "get") {
        const model = await loadModelArtifact(name);
        if (!model) {
            throw new Error(`model not found: ${name}`);
        }
        await writeFile(path, model.payload);
        console.log(`wrote ${path} (${model.checksumSha256})`);
        return;
    }

    if (command === "train") {
        const text = await readFile(path, "utf8");
        const payload = trainNativeModel(text);
        const metadata = inspectNativeModel(payload);
        const saved = await saveModelArtifact({ name, payload, metadata });
        console.log(`trained and saved ${saved.name} as ${saved.id}`);
        return;
    }

    if (command === "generate") {
        const model = await loadModelArtifact(name);
        if (!model) {
            throw new Error(`model not found: ${name}`);
        }
        console.log(`${path} ${generateNativeModel(model.payload, path)}`);
        return;
    }

    usage();
}

try {
    await main();
} finally {
    await closeModelRepository();
}
