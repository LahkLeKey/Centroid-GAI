# Repository training verification

The repository can now consume its committed code and documentation through the
same attributable snapshot pipeline as the encyclopedia. The first verification
run passes training-integrity checks, but its generated answers are not useful as
a codebase helper yet.

## Reproduce a run

Build the native addon with `bun run --cwd persistence/api native:build` if needed,
then run from the repository root. Both output directories must be new:

```powershell
python -m tools.knowledge snapshot --config examples/knowledge/codebase.json --repo . --ref HEAD --output build/knowledge/codebase-v1
node persistence/api/src/evaluation/codebase.ts --snapshot build/knowledge/codebase-v1 --output build/evaluation/codebase-v1
```

The [profile](../examples/knowledge/codebase.json) explicitly selects core code,
headers, tests, documentation, knowledge tools, and persistence application code.
It imports Git blobs at the resolved commit, ignoring staged, dirty, and untracked
content. It does not include generated models, downloaded encyclopedia shards,
dependencies, or environment files. Each document retains its source path, blob
ID, content hash, and commit. Review the manifest inventory for actual coverage.

Source extensions are opt-in: `.c`, `.h`, `.ts`, `.tsx`, `.js`, `.py`, `.json`,
`.sql`, `.prisma`, and `.css` are supported alongside the original prose formats.
Code bypasses the prose alphabetic-content and repetition filters because valid
source contains punctuation and repeated syntax. UTF-8, NUL/control-byte, LFS,
minimum-word, size, and regular-Git-blob checks still apply. Unicode NFC, line-ending,
and trailing-whitespace normalization still applies; syntax is retained as text
and never executed. Existing encyclopedia profiles retain their original policy.

The evaluation writes two native `.cgai` artifacts and `report.json`. The report
records source commit, corpus and manifest hashes, evaluator hashes, native-addon
hash, environment, configuration, artifact hashes, every probe result, and sample
generations. CI runs these checks and uploads the snapshot and evaluation artifacts.

## What is verified

- Snapshot file checksums, document hashes, source provenance, text/JSONL agreement,
  nonempty splits, and absence of exact duplicate documents across splits.
- Independent native training produces identical bytes for each configuration.
- Native example count equals token count plus EOS; vocabulary size and summed
  centroid observations agree with the corpus. Centroid norms/distances are finite.
- Saved artifacts reload unchanged. Generation repeats after reload, and inference
  leaves the artifact buffer unchanged.
- Native predictions are compared with unigram and three-token backoff references
  trained on exactly the same concatenated training text.

The two native configurations use 32 dimensions, 24 or 72 centroids, a three-token
context window, and seed 42. All models are newly trained from the snapshot; this
does not merge repository code into the encyclopedia or deploy to the API catalog.

## First measured result

Source commit: `50c7f509b30800c81c7da335045fcde964954c79`. The snapshot contains 140
documents: 118 training and 22 validation, with no rejected files. Both native runs
see 153,280 training examples and a vocabulary of 4,314 tokens including controls.
Both pass repeat-training, count, save/reload, and deterministic-generation checks.

The evaluator samples up to eight evenly spaced **word** targets per document,
using their three preceding tokens. There are 938 recall probes and 176 held-out
probes. Predictions can still be punctuation; punctuation is excluded only as an
expected target so trivial syntax frequency cannot inflate word accuracy.

| Model | Recall top 1 | Held-out top 1 | Held-out top 5 | Novel-transition top 1 |
| --- | ---: | ---: | ---: | ---: |
| Native, 24 centroids | 0.75% | 0.57% | 10.80% | 1.33% |
| Native, 72 centroids | 2.13% | 0.00% | 12.50% | 0.00% |
| Unigram reference | 0.00% | 0.00% | 0.00% | 0.00% |
| Backoff-3 reference | 64.93% | 33.52% | 58.52% | 4.00% |

The novel-transition slice contains 75 held-out context/target sequences absent
from training. Exact document separation does not prevent shared code fragments
or near duplicates; this slice makes that limitation visible. Even backoff scores
poorly on novel transitions. These are sampled diagnostics, not whole-corpus
perplexity, an independent question-answer benchmark, or proof of generalization.

96.59% of held-out targets exist in the training vocabulary, so missing words alone
do not explain the low native accuracy. For `How do I train a model?`, both models
generate repeated underscores. Other questions yield repeated comment syntax.
Increasing centroid count from 24 to 72 does not establish useful answer quality.

The compiled encyclopedia table contains 2,352 sorted native centroids from the
same 147 trained shards. The native spatial test checks row order, metadata, and
exact lookup. Compressed article/model payloads are intentionally not committed;
recreate them locally to rerun training or inspect full token distributions.

## Criteria for a useful helper

Keep training integrity and helper quality separate. Successful verification exits
zero even when measured accuracy is low; the report deliberately makes no helper
readiness claim. Failed integrity assertions exit nonzero.

The [source retrieval and question evaluation](repository-retrieval.md) now
searches snapshot passages, preserves source identities in citations, and measures
file recall, supporting excerpts, citation validity, and unanswerable-question
abstention. Its first baseline finds annotated evidence for 9 of 12 answerable
questions, but returns irrelevant evidence for 2 of 4 unanswerable questions.
Native centroid matches still return token distributions and use only the
configured context suffix. The source retriever is a separate lexical baseline;
its citation integrity does not establish answer correctness or helper readiness.

Run focused regression checks with:

```powershell
python -m unittest tools.knowledge.tests.test_knowledge
node --test persistence/api/src/evaluation/codebase-data.test.ts persistence/api/src/evaluation/metrics.test.ts
bun run --cwd persistence/api typecheck
```
