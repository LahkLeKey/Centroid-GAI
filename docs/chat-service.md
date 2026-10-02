# Neural conversations, research and memory

The service supports persistent conversations before any model has been trained.
Public conversations check reusable memory, then research an unsupported
message using Wikipedia. A fresh `hello world` message therefore triggers research.
Responses contain fetched excerpts and citations; they are not neural synthesis.

Use `scope: "repository"` for offline codebase questions and next-step follow-ups.
That scope pins verified source material, persists task context and never searches
the Internet. See [Repository chat](repository-chat.md) for snapshot preparation,
Compose startup, curl examples and the conversation scenario suite.

The C centroid network remains experimental. The new authored synthetic fixture
in `data/chat/factual-dialogues-v2.json` scores **0/6 native exact responses and
6/6 EOS terminations**, versus **6/6** for its source-extraction baseline. Native
test prompts contain 26 unknown tokens. These measurements diagnose a vocabulary
and generalization gap; they do not demonstrate realistic factual reliability.

Default `answerMode: "sources"` uses that research path, with clarification or
abstention when research cannot supply evidence. Explicit `answerMode: "neural"`
requires a conversation pinned to a trained model. For protocol-two models it
retrieves current evidence, admits complete relevant source units, and accepts
only output matching a complete supplied quotation. It returns the original
source spelling with citation metadata and `grounding` source IDs/UTF-16 offsets.
Unknown prompt words, dropped evidence, non-EOS termination or unmatched output
return source excerpts instead. Missing evidence produces clarification/abstention.
This is an attribution check, not semantic truth verification. Protocol-one models
remain readable but HTTP neural requests use source fallback until retrained.

## Start without training

Run one API replica per database. Choose a private token; Docker-network clients
must provide it. Baseline routes retain their unauthenticated contract.

PowerShell, from the repository root:

```powershell
$env:CGAI_CHAT_API_TOKEN = 'replace-with-a-private-token'
docker compose up --build --detach --wait
$chatHeaders = @('-H', "Authorization: Bearer $env:CGAI_CHAT_API_TOKEN", '-H', 'Content-Type: application/json')
$conversation = '{}' | curl.exe --fail-with-body @chatHeaders --data-binary '@-' http://localhost:3000/api/v1/conversations | ConvertFrom-Json
$message = @{ requestId = [guid]::NewGuid().ToString(); revision = $conversation.revision; content = 'hello world'; rememberSources = $true } | ConvertTo-Json -Compress
$reply = $message | curl.exe --fail-with-body @chatHeaders --data-binary '@-' "http://localhost:3000/api/v1/conversations/$($conversation.id)/messages" | ConvertFrom-Json
$reply.conversation.messages[-1] | ConvertTo-Json -Depth 8
```

Creating a conversation with `{}` returns `modelName: null` and `modelChecksum: null`.
No training job or dummy model is created. The message above sends `hello world`
with `rememberSources: true`. A successful first reply reports `research.status:
"searched"`, provider `wikipedia`, citations and memory IDs. Repeat the same question
with a new request ID and the returned revision to reuse fresh memory (`status:
"memory"`, zero network queries). Retrying the original request ID returns the
authoritative transcript without executing it again.

Set `autoSearch: false` on a message to use only stored evidence, or set
`CGAI_SEARCH_PROVIDER=disabled` before starting Compose to keep research offline.
An explicit `publicQuery` still requests research even when `autoSearch` is false;
deployment-level `disabled` prevents all public research. Failed or unavailable
research is recorded in the reply instead of claiming an answer was found.

## Train and select a neural model

Neural generation is a separate explicit mode. The new training request includes
training examples plus independent development checks. From the repository root:

```sh
export CGAI_CHAT_API_TOKEN='replace-with-a-private-token'
docker compose up --build --detach --wait
curl --fail-with-body http://localhost:3000/api/v1/health
curl --fail-with-body -H "Authorization: Bearer $CGAI_CHAT_API_TOKEN" \
  -H 'Content-Type: application/json' \
  --data-binary @data/chat/train-request-v1.json \
  http://localhost:3000/api/v1/chat-models/train
```

PowerShell uses `$env:CGAI_CHAT_API_TOKEN` and `curl.exe`. Training returns HTTP 202
with a job ID. Poll `GET /api/v1/chat-jobs/<id>` until `complete`, `rejected` or
`error`. A candidate is saved before validation. Only a passing candidate changes
the named model; rejection leaves its previous model available. The supplied
synthetic fixture currently fails the quality gate, so expect `rejected` rather
than a new selectable model. Jobs retain `candidateChecksum`, quality, case results
and measured failures. Quality belongs to the job; immutable artifact provenance
identifies its original training run. A completion-recording failure after a
successful publication explicitly retains the published model in the error job.

```sh
curl --fail-with-body -H "Authorization: Bearer $CGAI_CHAT_API_TOKEN" \
  -H 'Content-Type: application/json' -d '{"modelName":"your-passing-model"}' \
  http://localhost:3000/api/v1/conversations
curl --fail-with-body -H "Authorization: Bearer $CGAI_CHAT_API_TOKEN" \
  -H 'Content-Type: application/json' -d '{"requestId":"question-1","revision":0,"content":"Is automatic training enabled?","answerMode":"neural"}' \
  http://localhost:3000/api/v1/conversations/<id>/messages
```

Create that conversation only after its model's job passes. Use the returned
revision on the next new request. Admission and completion each
increment it; the first successful turn normally returns revision 2. Retries use
the same request ID and content/options; changed input returns 409. GET reloads
history after restart. Interrupted pending work becomes error during recovery.

## Routes

Paths follow `/api/v1`. Chat/memory routes use one configured owner
(`CGAI_CHAT_OWNER_ID`, default `local-owner`). A bearer token is mandatory for
non-loopback requests. This is a single-owner pilot, not multi-user authentication.

| Method | Path | Body / result |
| --- | --- | --- |
| GET | `/chat-models` | Named model heads and checksums |
| POST | `/chat-models/train` | `name`, structured `examples`, required independent `validation`, optional `config`, `training`, `provenance`; returns job |
| GET | `/chat-jobs/:id` | Status, metrics, model identity or error |
| GET | `/knowledge/repository` | Snapshot readiness, identities and included source paths |
| POST | `/conversations` | Optional `title`, `scope`, `snapshot` or public `modelName`; `{}` starts public research without a model |
| GET | `/conversations/:id` | Authoritative transcript and revision |
| POST | `/conversations/:id/messages` | `requestId`, `revision`, `content`, optional generation/source settings |
| POST | `/conversations/:id/repository` | `revision`, verified `snapshot`; explicitly switches a repository conversation |
| POST | `/conversations/:id/cancel` | `requestId`; terminates matching active computation |
| DELETE | `/conversations/:id` | `revision`, optional `forgetMemory: true` |
| GET | `/memory` | Owner's records and settings |
| POST | `/memory` | `content`, optional owned `conversationId`; user-attributed statement |
| PATCH | `/memory/:id` | `content` to correct, or `disputed: true` to exclude from reuse |
| DELETE | `/memory/:id` | Forget one record |
| DELETE | `/memory` | Forget all owner records |
| PATCH | `/memory/settings` | `enabled` and `retentionDays` (1–365) |

Message responses contain `conversation` and `requestId`. Assistant records include
status, finish reason, usage, sources, research trace and memory IDs when applicable.
Native stopping reasons are `eos`, `length` and `repetition`; source-mode reasons are `sources`,
`clarification` and `abstained`. Errors/cancellations retain explicit states.
The repetition guard stops after four consecutive copies of a one-to-four-token
suffix, once at least eight tokens have been emitted. It preserves the emitted text.
Streaming is not implemented.

## Evidence and research

Set `CGAI_REPOSITORY_SNAPSHOT` to a verified source snapshot directory in the API
process/container and create a repository-scoped conversation to use it. The
`compose.repository.yaml` overlay mounts prepared snapshots read-only. Passages cite
snapshot-normalized lines and commits, not mutable working-tree coordinates.

The default provider searches English Wikipedia and fetches article text separately
through the [search API](https://www.mediawiki.org/wiki/API:Search) and
[TextExtracts API](https://www.mediawiki.org/wiki/Extension:TextExtracts#API).
It needs no key and covers encyclopedia content. For broader web search, set
`CGAI_SEARCH_PROVIDER=searxng` and `CGAI_SEARCH_URL` to a public SearXNG `/search`
endpoint with [JSON output enabled](https://docs.searxng.org/dev/search_api.html).
An existing `CGAI_SEARCH_URL` also selects SearXNG when no provider is specified.

In public scope, `autoSearch` defaults to true. After a memory miss, the current message
becomes the public query, with whitespace normalized. History, stored preferences
and repository passages are never appended. Automatic queries over 512 UTF-8 bytes
or containing recognized credential patterns prompt for a separate `publicQuery`.
This pattern check cannot identify every private message: use `autoSearch: false`
for private text, or supply only the public terms in `publicQuery`. An explicit
query overrides the message and forces a refresh rather than using cached evidence.
The bounds are one query, five page attempts, a 15-second deadline, 512 KiB per
response, four redirects, and 100 persisted owner queries per UTC day.

Fetching supports public IPv4 HTTP(S) on standard ports. DNS results are validated
and pinned; redirects repeat validation. Compressed responses and unsupported
media types are refused. Extracted HTML never executes. Snippets are not saved as
factual evidence; copied pages are deduplicated by content hash. Failures, disabled
providers and quotas return their actual status.

`rememberSources: true` saves relevant fetched excerpts in owner-private memory.
Optional `applicability` distinguishes versions/scopes. Reuse requires an exact
normalized question/applicability match. Queries about current/latest facts,
prices, weather, officeholders or releases use a five-minute TTL; other excerpts
use one day. Expiry starts at fetch time, and explicit public queries supply the
freshness cue. These are versioned heuristics, not calibrated guarantees.
Refreshing a canonical URL replaces its old excerpt within the same query and
applicability. New records use state `sourced`; legacy `supported` also means an
attributed excerpt. Original provenance remains visible. This is excerpt reuse,
not automatic verification of every claim.

Memory is enabled by default but has no automatic preference extraction. Explicit
POST/PATCH requests admit user statements. Retention defaults to 30 days; expired
records are excluded immediately and purged at startup/hourly. Forgetting removes
records. Interactions create no weight updates or training copies. Explicit
conversation deletion removes derived memories when `forgetMemory` is true.
Startup/hourly retention also deletes inactive transcripts older than the owner's
retention period and their solely derived memories, including stored research traces.

## Limits and verification

Training accepts at most 10,000 examples, 16 MiB of text and one million targets.
Vocabulary comes only from supplied training examples. Prompt/answer windows total
at most 256 slots; current questions must fit in full. Protocol two defaults to
160 prompt and 32 response slots, with 80 evidence slots. `config.evidenceWindow`
sets the ceiling (zero selects half the prompt). Current evidence takes priority
over older turns; usage includes `evidenceTokens` and `droppedEvidence`. New
training rejects examples whose evidence cannot fit. The tokenizer remains the
frozen word tokenizer; unknown spellings are observable and trigger HTTP fallback.
Adam moments span examples/epochs within one call but are not stored in artifacts.
New chat models use zero BOS padding held fixed during training and distinct initial
token preferences across centroid experts to prevent early training collapse. Other
embeddings and all output logits remain trainable. Existing artifacts keep their
stored inference behavior; the standalone neural prototype keeps its own initialization.

Workers have one training/two inference slots, sixteen queued tasks, 30-second
inference and 600-second training/validation deadlines. Validation shares the
training lane so replies remain available. Children use a 512 MiB JS heap limit;
native allocations have separate count/shape bounds. Conversations are capped at
1,000 entries and checked against 8 MiB before admission. Memory has 1,000 records.

```sh
cmake --build build/dev --config Release
ctest --test-dir build/dev -C Release --output-on-failure
cd persistence
bun run typecheck
bun run test:native
bun run --cwd api chat:quality:test
bun run test:e2e
node api/src/evaluation/chat.ts
```

The evaluator writes `build/evaluation/chat/report.json` and `model.cgchat`, recording
corpus/split and implementation hashes, protocol/tokenizer identity, source commit/dirty flag, seed,
hardware, losses, exact-answer comparisons and per-case termination/latency. Dataset
validation rejects repeated IDs, native-token-equivalent dialogues, source-content
aliases and cross-split source/family leakage. Two bounded candidate restarts are
selected by development exact answers, then development loss. `--candidates 1`
limits the run; `--dataset` and `--output` select inputs/destination. Final test
examples are never vocabulary inputs or checkpoint selectors. Reports distinguish
exact extraction support, answer correctness, citation validity, irrelevant
evidence, appropriate/unnecessary abstention and unknown/dropped context.

Report version three also records free-running answers on the selected model's
training examples, separately from teacher-forced token accuracy. Training answers
do not select candidates. `--development-only` skips final-test native generation,
token metrics, baseline predictions and scorer controls during iteration; final-test
summary fields are `null`, not zero scores. Dataset integrity validation still covers
the entire release. Use development results to make implementation choices.

The offline scorer reports **routing decisions separately from exact wording**.
It recognizes a small fixed set of complete abstention/clarification phrases and
treats other nonempty output as an answer attempt. Consequently, route accuracy
can be high for incorrect text; use strict answer accuracy and source support to
judge factual output. `correctAbstentionRate` retains its historical exact-wording
meaning, also exposed as `exactAbstentionWordingRate`; `abstentionActionRecall`
and `clarificationActionRecall` measure the two distinct decisions. Errors receive
no answer, action, provenance or termination credit. Per-category results expose
follow-up, correction, conflict and clarification failures.

The revised source baseline ranks complete evidence units using question/history
text, with conservative checks for ambiguity, conflicting values, missing
properties and unsupported deployment facts. It does not consume gold answers,
categories or source identity metadata. Scorer positive controls insert accepted
gold outputs to verify exact support and citation accounting; those controls are
measurement checks, not predictions. Inconsistent category/answer-action rubrics
are reported separately and never silently rewritten in an immutable release.

HTTP training requires `validation: {"version":1,"cases":[...]}` with 2–64 cases,
including answerable and unanswerable questions. Each has `id`, ordered `messages`,
`expected` (`answer` or `abstain`) and `acceptedAnswers`; answer cases also supply
`evidence` records with `id` and `excerpt`. Accepted factual answers must be complete
evidence units. Optional source/family lineage is checked against training, and
question identity ignores evidence when checking held-out leakage. The fixed
`extractive-development-v1` policy requires all expected answers/abstentions and
EOS, complete source support, no unknown words, and no dropped history/evidence.
This deliberately strict engineering gate is not a general conversational release
threshold. Do not loosen it to make a weak model appear ready.

## Learning sanity checks

Run the bounded native memorization suite before larger training experiments:

```sh
node persistence/api/src/evaluation/learning-sanity.ts
node persistence/api/src/evaluation/chat.ts --release data/chat/releases/repository-v1 --development-only --require-quality --output build/evaluation/repository-development
```

The sanity command writes `build/evaluation/learning-sanity/report.json` and fails
unless the final declared configuration reproduces all 13 short training examples
with EOS, no unknown tokens and no dropped context. Six evidence swaps/removals
must also produce their explicitly authored changed answers. The report retains
the bounded calibration attempts, model settings, teacher-forced loss/accuracy,
free-running outputs, native metadata, implementation/input/artifact hashes and
hardware. `--epochs 1..2000` runs one bounded epoch override; `--no-require-pass`
allows diagnostic failures to return success, while preserving failed results.

All sanity cases are seen during training. Passing establishes that the small
model can reproduce these toy patterns, not repository accuracy or unseen evidence
reasoning. The small-model settings and longer training budget are isolated from
production defaults. `bun run --cwd persistence/api chat:sanity:test` exercises the
fixture, accounting checks, failure reporting and the actual native reproduction.
`chat:quality:test` includes these checks and the evaluation baseline/scorer tests.

## Repository training data and releases

The offline data workflow lives in `persistence/api/src/training/`. Its first
repository corpus has 200 examples from 50 authored question families, partitioned
as 120 training, 40 development and 40 final-test records. It covers build options,
native contracts, persistence, training and evidence handling. Variants exercise
direct questions, follow-ups, corrections, quotation requests, distractors,
missing evidence, clarification and hypothetical conflicting deployment reports.
Those conflict cases require abstention; they do not assert contradictory repository facts.

The version-three schema stores full source text, repository/base commit, relative
path, exact UTF-8 SHA-256, and exact evidence line spans. A workspace snapshot
records actual current contents, including uncommitted changes; its base commit
does not claim those contents are committed. Whole files and question families
remain in one split. Existing synthetic version-two fixtures remain regression tests.

From the repository root, create a new candidate snapshot and review worksheet:

```sh
node persistence/api/src/training/cli.ts ingest --output build/training/candidates-v1.json
node persistence/api/src/training/cli.ts review-template --dataset build/training/candidates-v1.json --reviewer your-name --reviewer-kind human --output build/training/decisions-v1.json
```

Review each question, source context, answer and split in the candidate file.
Edit individual worksheet decisions to `approved` or `rejected` with meaningful
notes; unchanged `candidate` decisions approve nothing. Apply and release:

```sh
node persistence/api/src/training/cli.ts review --dataset build/training/candidates-v1.json --decisions build/training/decisions-v1.json --output build/training/reviewed-v1.json
node persistence/api/src/training/cli.ts export --dataset build/training/reviewed-v1.json --output build/training/releases/local-v1
node persistence/api/src/training/cli.ts verify --release build/training/releases/local-v1
```

Reviews bind record content, split assignment, full source content and provenance.
Edits invalidate approval. Export includes only approved rows and defaults to human
review; each split must remain nonempty and development must have both answerable
and unanswerable cases within the API's 64-case limit. Reviewer names and kinds are
declared audit metadata, not authenticated identities or proof of independence.
`--review-policy agent-or-human` explicitly permits agent-reviewed engineering data.

Local engineering releases contain the reviewed dataset, source snapshots,
split assignments, generated `train-request.json` and a checksummed manifest.
Generate candidates from the authored repository seeds, review them with the
commands above, and export to `build/training/releases/local-v1`. Generated release
JSON and source payloads stay ignored by Git; authored synthetic fixtures and
compact compiled model releases remain source controlled.
Export refuses to overwrite existing files/directories; verification recomputes
hashes and rederives the request from the approved dataset. Checksums detect drift,
but do not provide cryptographic reviewer authentication. Requests contain only
training examples and development validation; final-test answers remain offline.
Use the generated request with `POST /api/v1/chat-models/train` as shown above.
The service's existing quality gate still controls publication.

```sh
node persistence/api/src/training/cli.ts verify --release build/training/releases/local-v1
node persistence/api/src/evaluation/vocabulary.ts --dataset build/training/releases/local-v1/dataset.json --output build/evaluation/repository-vocabulary.json
node persistence/api/src/evaluation/chat.ts --release build/training/releases/local-v1 --require-quality
```

The release-aware evaluator uses the released model/training settings and shared
development quality gate. `--require-quality` returns a failing exit code when
that gate fails; ordinary diagnostic evaluation still records poor scores without
treating them as runtime errors. No offline command publishes a model. The
vocabulary experiment compares frozen training-only word coverage, hypothetical
UTF-8 bytes and exact source-span representability. Its source-copy result uses
gold answers and measures representability, **not prediction accuracy**. Byte
coverage does not establish that evidence fits the current context window.

To add data, extend the authored anchors and questions in
`persistence/api/src/training/repository-seeds.ts`, assign new source files to one
split before adding variants, and run the workflow into new snapshot paths.
Missing or nonunique source anchors fail ingestion instead of silently changing
labels. Tests run with `bun run --cwd persistence/api chat:data:test`.

The first configured 20-epoch repository run produced **0/40 native exact test
responses** and failed the development gate, with no runtime evaluation errors.
All test evidence was retained, but 429 of 1,780 encoded prompt slots contained
unknown words. The separate byte experiment eliminated unknowns while dropping
evidence in 27/40 test prompts at the native default 160/32/80 windows. Exact
source copying represented all 33 answerable test targets when given the gold
span. These results motivate a tokenizer/copy-mechanism experiment with learned
selection; they do not justify deploying the current model.

Remaining gates include independent human review of realistic corpora, human-rated claim support,
follow-up evidence reasoning, fact-specific freshness/contradiction resolution,
human training-candidate review, broader tokenization, multiple authenticated owners, multi-replica leases,
total-process memory enforcement and agreed quality/
latency thresholds. Passing builds does not satisfy these release gates.
