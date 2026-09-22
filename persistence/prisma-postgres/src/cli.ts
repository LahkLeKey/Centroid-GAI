import { readFile, writeFile } from "node:fs/promises";
import { generateNativeModel, inspectNativeModel, trainNativeModel } from "./native.ts";
import {
  closeModelRepository,
  loadModelArtifact,
  saveModelArtifact,
} from "./model-repository.ts";

function usage(): never {
  console.error("Usage:");
  console.error("  npm run model -- put <name> <model.cgai>");
  console.error("  npm run model -- get <name> <model.cgai>");
  console.error("  npm run model -- train <name> <corpus.txt>");
  console.error('  npm run model -- generate <name> "prompt"');
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
