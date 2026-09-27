# Hugging Face import plan

## Constraint

CGAI artifacts store hashed token embeddings, online k-means centroids, and
per-centroid next-token frequency tables (see
[model-composition.md](model-composition.md)). Pretrained transformer weights
(attention matrices, layer norms, learned embeddings) have no structural
counterpart in that format, so a Hugging Face checkpoint cannot be converted
directly into a `.cgai` artifact. There is no import path that reads
`model.safetensors` and emits centroids.

The only supported path is **distillation**: run a pretrained Hugging Face
model as a *teacher* to generate text, then train a CGAI model natively on
that generated corpus through the existing `trainNativeModel` /
`POST /api/v1/models/:name/train` path. The teacher never becomes part of the
artifact; only its output text does, and that text is provenance-tracked like
any other training corpus.

## Pipeline

```
Hugging Face model (teacher)
  -> prompted generation, deterministic decoding settings recorded
  -> generated corpus file(s) on disk, source-tagged
  -> existing native training: trainNativeModel(text, config)
  -> ordinary .cgai artifact, stored through the API like any other model
  -> baseline harness (persistence/api/src/evaluation) scores it
```

Nothing new is required in the C core or ABI. The new work is a corpus-
generation step that sits in front of training, plus provenance metadata.

### 1. Model selection

Prioritize small, permissively licensed instruction/text models runnable
locally or via the Hugging Face Inference API without a paid endpoint (for
example distilled GPT-2-class or small Llama/Mistral-family checkpoints with
compatible licenses). Reject models whose license forbids using outputs to
train other models; record the license decision next to the generated corpus.

### 2. Prompted generation, not raw dumps

Generate corpora from a fixed, versioned prompt set that mirrors the shape of
`examples/model_corpora/*.txt` (short domain-scoped passages), not an
unstructured scrape. This keeps documents comparable to existing corpora and
lets the same tokenizer assumptions (`asciiTokens` in
[metrics.ts](../persistence/api/src/evaluation/metrics.ts)) apply for
evaluation later. Decoding parameters (temperature, seed, max tokens, model
id/revision) are recorded with the output so a corpus can be regenerated or
audited.

### 3. Provenance

Each generated corpus gets a sidecar record: teacher model id + revision,
license, prompt set version, decoding settings, generation timestamp, and a
SHA-256 of the resulting text. Trained artifacts already store their source
text hash implicitly through `checksumSha256`; the sidecar is what lets a
reviewer trace a `.cgai` file back to "which teacher, which prompts, which
settings" without re-deriving it from the binary.

### 4. Training and evaluation

Feed the generated corpus through the same `train` endpoint used for hand-
written corpora, under the `teacher/<name>` catalog name. Passing `--catalog`
to the baseline benchmark downloads and scores every catalog entry, so a
teacher-derived model is measured against the same recall/challenge/unknown
probes and the unigram/backoff-3 reference models as hand-written corpora
before it is treated as a real catalog entry, with no suite file changes
required.

### 5. What stays out of scope

- No transformer inference embedded in the C core, ABI, or CLI.
- No attempt to map attention/embedding weights onto centroids or token
  embeddings; CGAI's hashed embeddings are seed-derived, not learned.
- No unattended re-generation: prompt sets and decoding settings are versioned
  files reviewed like code, not fetched fresh on every run.

## Decisions

- **Generation runs through the Hugging Face Inference API**, not local
  transformer inference. This keeps the stack consistent with the rest of
  `persistence` (Bun/TypeScript, no Python or embedded model runtime) and
  needs no GPU. `HF_INFERENCE_ENDPOINT` can point at a self-hosted
  OpenAI/TGI-compatible endpoint later without changing the corpus-in,
  artifact-out shape.
- **Teacher-derived models use a `teacher/<name>` catalog prefix** (for
  example `teacher/distilgpt2-tiny-contexts`), distinct from hand-written
  names like `tiny-contexts`. The API and database accept slashes as an
  opaque part of the name; the CLI/HTTP client percent-encodes it. This lets
  the baseline report and catalog listing filter by provenance without a
  separate schema field.

## Implementation

[`examples/teacher-prompts/v1.json`](../examples/teacher-prompts/v1.json) is
the versioned, domain-scoped prompt set described above.
[`persistence/api/src/teacher/import-teacher-model.ts`](../persistence/api/src/teacher/import-teacher-model.ts)
is the end-to-end script:

```sh
HF_TOKEN=... bun run --cwd persistence teacher:import -- \
  --model distilgpt2 --name distilgpt2-v1 --license apache-2.0
```

It validates the license against a small reviewed allowlist (`apache-2.0`,
`mit`, `bsd-2-clause`, `bsd-3-clause`, `cc0-1.0`; anything else needs
`--allow-unlisted-license` after manual review), generates one paragraph per
prompt-set domain with retry-on-cold-start, sanitizes output to the ASCII
subset the baseline tokenizer scores, writes the corpus and a provenance
sidecar under `build/teacher-corpora/` (gitignored, matching how generated
`.cgai` files are already excluded from source control), and trains the
result through the existing `POST /api/v1/models/:name/train` route. Running
`bun run baseline --catalog http://127.0.0.1:3000` afterward automatically
scores the new `teacher/*` model alongside hand-written ones, since the
benchmark already downloads and evaluates every catalog entry when `--catalog`
is passed.
