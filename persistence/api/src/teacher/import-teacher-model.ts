/**
 * @file import-teacher-model.ts
 * @brief Distills a Hugging Face text model into a teacher-derived CGAI corpus and trains it.
 *
 * Pretrained transformer weights have no structural counterpart in a `.cgai` artifact (see
 * docs/huggingface-import-plan.md), so this script never touches model weights directly. It
 * prompts a hosted teacher model through the Hugging Face Inference API, writes the generated
 * text plus a provenance sidecar under `build/teacher-corpora/` (gitignored, like other generated
 * artifacts in this repo), and trains it through the same public `/train` route `seed-models.ts`
 * uses. Trained models are named `teacher/<name>` so the catalog and baseline report can filter
 * distilled models from hand-written ones by prefix.
 */
import { createHash } from "node:crypto";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { isAbsolute, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";

const root = fileURLToPath(new URL("../../../../", import.meta.url));
const sha256 = (text: string) => createHash("sha256").update(text).digest("hex");

/** SPDX-style tags accepted without an explicit override; anything else needs manual sign-off. */
const permissiveLicenses = new Set([
    "apache-2.0",
    "mit",
    "bsd-2-clause",
    "bsd-3-clause",
    "cc0-1.0",
]);

interface PromptDomain {
    readonly name: string;
    readonly prompts: string[];
}

interface PromptSet {
    readonly version: number;
    readonly domains: PromptDomain[];
}

interface Decoding {
    readonly temperature: number;
    readonly maxNewTokens: number;
    readonly seed?: number;
}

/** Drops characters the ASCII-only baseline tokenizer cannot score instead of failing generation. */
function sanitizeAscii(text: string): string {
    const normalized = text
        .replace(/[\u2018\u2019]/g, "'")
        .replace(/[\u201c\u201d]/g, '"')
        .replace(/[\u2013\u2014]/g, "-");
    let ascii = "";
    for (const char of normalized) {
        const code = char.codePointAt(0);
        ascii += code !== undefined && code <= 0x7f ? char : " ";
    }
    return ascii.replace(/\s+/g, " ").trim();
}

/**
 * Requests one continuation from the hosted teacher model, retrying while it is still loading.
 *
 * @param model Hugging Face model id, e.g. `distilgpt2`.
 * @param prompt Plain-text prompt sent as the generation input.
 * @param decoding Recorded, deterministic-as-possible decoding settings; hosted providers may not
 * honor `seed` exactly, which is why it is recorded rather than trusted for reproducibility.
 * @param token Hugging Face access token; required by the inference endpoint for most models.
 * @returns Sanitized ASCII continuation text, never empty.
 */
async function generateContinuation(
    model: string,
    prompt: string,
    decoding: Decoding,
    token: string,
): Promise<string> {
    const endpoint = `${process.env.HF_INFERENCE_ENDPOINT ?? "https://api-inference.huggingface.co/models"}/${model}`;
    // Step 1: Retry a bounded number of times while the hosted model cold-starts (HTTP 503).
    for (let attempt = 1; attempt <= 5; attempt += 1) {
        const response = await fetch(endpoint, {
            method: "POST",
            headers: { authorization: `Bearer ${token}`, "content-type": "application/json" },
            signal: AbortSignal.timeout(60_000),
            body: JSON.stringify({
                inputs: prompt,
                parameters: {
                    temperature: decoding.temperature,
                    max_new_tokens: decoding.maxNewTokens,
                    seed: decoding.seed,
                    return_full_text: false,
                },
                options: { wait_for_model: attempt > 1 },
            }),
        });
        if (response.status === 503 && attempt < 5) {
            const retryAfter = Number(response.headers.get("retry-after") ?? "2");
            await new Promise((resolve) => setTimeout(resolve, Math.min(retryAfter, 10) * 1000));
            continue;
        }
        if (!response.ok)
            throw new Error(`${model} HTTP ${response.status}: ${await response.text()}`);
        const body = (await response.json()) as
            | { generated_text?: string }[]
            | { generated_text?: string };
        const generated = Array.isArray(body) ? body[0]?.generated_text : body.generated_text;
        const sanitized = sanitizeAscii(generated ?? "");
        if (!sanitized) throw new Error(`${model}: empty continuation for prompt "${prompt}"`);
        return sanitized;
    }
    throw new Error(`${model}: still loading after retries`);
}

/** Builds one domain-scoped paragraph so the corpus stays comparable to examples/model_corpora/. */
async function generateDomain(
    model: string,
    domain: PromptDomain,
    decoding: Decoding,
    token: string,
): Promise<string> {
    const lines: string[] = [];
    for (const prompt of domain.prompts)
        lines.push(`${prompt} ${await generateContinuation(model, prompt, decoding, token)}`);
    return lines.join("\n");
}

async function main() {
    const { values } = parseArgs({
        options: {
            model: { type: "string" },
            name: { type: "string" },
            license: { type: "string" },
            revision: { type: "string", default: "main" },
            prompts: { type: "string", default: "examples/teacher-prompts/v1.json" },
            temperature: { type: "string", default: "0.7" },
            "max-tokens": { type: "string", default: "48" },
            seed: { type: "string", default: "42" },
            "api-url": { type: "string", default: "http://127.0.0.1:3000" },
            "allow-unlisted-license": { type: "boolean", default: false },
        },
    });
    // Step 1: Validate required inputs before any network call; a license decision is mandatory.
    if (!values.model || !values.name || !values.license) {
        throw new Error(
            "Usage: --model <hf-id> --name <catalog-slug> --license <spdx-tag> [--allow-unlisted-license]",
        );
    }
    if (!permissiveLicenses.has(values.license) && !values["allow-unlisted-license"]) {
        throw new Error(
            `License "${values.license}" is not in the reviewed allowlist; pass --allow-unlisted-license after manual review.`,
        );
    }
    const token = process.env.HF_TOKEN;
    if (!token) throw new Error("HF_TOKEN is required to call the Hugging Face Inference API");

    // Step 2: Load and hash the versioned prompt set so provenance can point back to exact inputs.
    const promptSetPath = resolvePromptSet(values.prompts);
    const promptSetText = await readFile(promptSetPath, "utf8");
    const promptSet = JSON.parse(promptSetText) as PromptSet;
    if (promptSet.version !== 1 || !promptSet.domains.length) throw new Error("Invalid prompt set");
    const decoding: Decoding = {
        temperature: Number(values.temperature),
        maxNewTokens: Number(values["max-tokens"]),
        seed: Number(values.seed),
    };

    // Step 3: Generate one paragraph per domain and join them the same way hand-written corpora are.
    const paragraphs: string[] = [];
    for (const domain of promptSet.domains)
        paragraphs.push(await generateDomain(values.model, domain, decoding, token));
    const corpus = `${paragraphs.join("\n\n")}\n`;

    // Step 4: Persist the corpus and its provenance sidecar before training, so a failed training
    // request still leaves an auditable record of what was generated and how.
    const outDir = resolve(root, "build/teacher-corpora");
    await mkdir(outDir, { recursive: true });
    const corpusPath = resolve(outDir, `${values.name}.txt`);
    const provenancePath = resolve(outDir, `${values.name}.provenance.json`);
    await writeFile(corpusPath, corpus, "utf8");
    await writeFile(
        provenancePath,
        `${JSON.stringify(
            {
                version: 1,
                catalogName: `teacher/${values.name}`,
                teacherModel: values.model,
                revision: values.revision,
                license: values.license,
                licenseListed: permissiveLicenses.has(values.license),
                promptSetPath: "examples/teacher-prompts/v1.json",
                promptSetSha256: sha256(promptSetText),
                decoding,
                generatedAt: new Date().toISOString(),
                corpusSha256: sha256(corpus),
            },
            null,
            2,
        )}\n`,
        "utf8",
    );

    // Step 5: Train through the same public route hand-written example corpora use; slashes in the
    // catalog name are percent-encoded because the router splits on raw path segments first.
    const catalogName = encodeURIComponent(`teacher/${values.name}`);
    const response = await fetch(`${values["api-url"]}/api/v1/models/${catalogName}/train`, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ text: corpus }),
    });
    if (!response.ok) throw new Error(`train HTTP ${response.status}: ${await response.text()}`);
    const result = (await response.json()) as { name: string; id: string };
    console.log(
        `Trained ${result.name} (id=${result.id}) from ${paragraphs.length} teacher-generated domains.`,
    );
    console.log(`Corpus: ${corpusPath}`);
    console.log(`Provenance: ${provenancePath}`);
}

function resolvePromptSet(path: string): string {
    return isAbsolute(path) ? path : resolve(root, path);
}

await main();
