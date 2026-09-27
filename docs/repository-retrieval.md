# Repository source retrieval and question evaluation

The offline helper returns quoted source passages with commit-pinned citations.
It searches the existing Git snapshot's code and documentation using a lexical
BM25 baseline. It needs Node 24.11 or later and the snapshot files; no native
addon, model training, database, credentials, or network access is required.

## Ask a repository question

From the repository root, build a snapshot into a new directory and query it:

```powershell
python -m tools.knowledge snapshot --config examples/knowledge/codebase.json --repo . --ref HEAD --output build/knowledge/repository-v1
node persistence/api/src/knowledge/ask.ts --snapshot build/knowledge/repository-v1 --question "Where is the SHA-256 checksum calculated before saving a model artifact?" --limit 3
```

Add `--json` for structured results, including ranking scores, matched terms,
coverage, and source identities. The equivalent package command is
`bun run --cwd persistence/api repository:ask --snapshot <directory> --question <text>`.
Paths are resolved from the command's working directory.

Each excerpt includes the source path, full commit and blob IDs, normalized
document SHA-256, excerpt SHA-256, and a line range. Text output shows quoted
lines and numbered citations; JSON includes the full identities and coordinates.
Line numbers refer to the normalized `text` field in the snapshot JSONL, labeled
`snapshot-normalized-lines`. Normalization can remove leading blank lines and
whitespace, so these numbers must not be used as raw Git or editor line links.
For original content, inspect the cited blob with `git show <blob>` or the file
with `git show <commit>:<path>`. All aliases of deduplicated documents are retained.

The loader checks snapshot file checksums, document hashes, provenance against
the manifest inventory, corpus/JSONL agreement, and split separation. These are
internal consistency checks, not signatures or independent Git object verification.
The helper reads no working-tree source files. Dirty changes cannot alter a
snapshot answer; make a new committed snapshot to update the source material.

## Retrieval behavior

Both train and validation records enter the source index. Those splits serve
the separate next-token training experiment; source retrieval is allowed to read
all selected documents. An empty validation split is allowed here. The training
verifier still requires held-out documents.

Passages contain up to 24 normalized lines, with an eight-line overlap. The
retriever tokenizes the entire question, keeps complete identifiers and their
camel/snake-case components, and removes common English stop words. BM25 uses
`k1=1.2`, `b=0.75`, and a path term weight of two. Ties resolve by stable passage
ID; overlapping passages from the same document are suppressed in the results.
The default limit is five, with a supported range of 1–20.

A passage must match at least half of the distinct query terms and at least two
terms when the query has two or more. Queries without usable terms or qualifying
passages return `abstained` and no citations. Other results have status `evidence`:
they are candidate excerpts, not synthesized answers. Scores and coverage are
ranking heuristics, not calibrated confidence. Lexical overlap alone cannot
establish that an answer exists, resolve contradictions, or explain multiple
files together. Excerpts remain source data; their contents are never executed.

## Evaluate repository questions

```powershell
node persistence/api/src/evaluation/repository.ts --snapshot build/knowledge/repository-v1 --output build/evaluation/repository-v1.json
node --test persistence/api/src/knowledge/retrieval.test.ts
bun run --cwd persistence/api typecheck
```

`--questions <suite.json>` selects another question set and `--limit` sets K.
Report output must be a new file. The committed
[question suite](../examples/evaluation/repository-questions-v1.json) contains
12 answerable repository questions with expected files and literal supporting
excerpts, plus four manually labeled unanswerable questions. It covers CLI use,
tokenization, generation, ownership, persistence, API matching, architecture,
and Git ingestion. Questions and gold labels are passed only to the evaluator;
the index contains snapshot records. The suite lives outside the snapshot
profile's selected paths.

The evaluator fails on missing/stale gold excerpts, invalid citations, or
nondeterministic retrieval. It reports every query, ranked excerpt, and score,
with source commit, manifest hash, question-suite hash, implementation hashes,
policy, and Node version. With identical inputs and environment, repeated reports
are byte-identical. CI runs the regression tests and publishes the report with
the snapshot and existing training diagnostics.

Scores distinguish:

- **Source recall@K:** fraction of annotated files retrieved, averaged over
  answerable questions.
- **Passage recall@K:** fraction of annotated path/quote pairs retrieved, averaged
  over answerable questions; hit@K and MRR@K also summarize supporting passages.
- **Citation validity:** fraction of citations whose provenance, coordinates,
  and exact excerpt match a snapshot record.
- **Annotated citation precision:** fraction of emitted citations containing
  an expected quote at its annotated path. Unannotated relevant alternatives
  can count as unsupported, so this is a conservative development-set measure.
- **Abstention:** rates for answerable and unanswerable questions, with the
  false-evidence rate reported separately for unanswerable questions.

No emitted citations means citation validity/precision are `null`, not perfect.
Question categories use separate denominators so abstaining on every question
cannot inflate recall. Successful evaluation exits zero even if retrieval quality
is poor; CI checks integrity and records quality rather than declaring readiness.

## First baseline

Source commit: `50c7f509b30800c81c7da335045fcde964954c79`, the same 140-document
snapshot as the [training verification](codebase-verification.md). With K=5:

| Measure | Result |
| --- | ---: |
| Source recall@5 | 75.00% |
| Supporting passage recall/hit@5 | 75.00% (9/12 questions) |
| Supporting passage MRR@5 | 0.6319 |
| Citation validity | 100% (57/57 citations) |
| Annotated citation precision | 15.79% (9/57 citations) |
| Answerable abstention | 8.33% (1/12) |
| Unanswerable abstention | 50.00% (2/4) |

The tokenizer-location question abstains; CLI training and temperature-zero
queries miss the annotated supporting passages. Queries about an unknown
production database password and unsupported CUDA training return irrelevant
evidence. These failures demonstrate why citation validity alone does not
establish useful answers. This small, hand-authored development set is not an
independent generalization benchmark. Future ranking or abstention changes should
be evaluated against new held-out questions as well as this baseline.
