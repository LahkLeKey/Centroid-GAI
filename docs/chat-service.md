# Neural conversations, research and memory

The service supports persistent conversations before any model has been trained.
Public conversations check reusable memory, then research an unsupported
message using Wikipedia. A fresh `hello world` message therefore triggers research.
Responses contain fetched excerpts and citations; they are not neural synthesis.

Use `scope: "repository"` for offline codebase questions and next-step follow-ups.
That scope pins verified source material, persists task context and never searches
the Internet. See [Repository chat](repository-chat.md) for snapshot preparation,
Compose startup, curl examples and the conversation scenario suite.

The C centroid network remains experimental: the fixed six-case dialogue fixture currently scores **0/6
exact answers and 6/6 EOS terminations**. The count-centroid baseline also scores
0/6 exact answers. Exact match is a diagnostic; claim support still needs review.

Default `answerMode: "sources"` uses that research path, with clarification or
abstention when research cannot supply evidence. Explicit `answerMode: "neural"`
exercises learned generation and requires a conversation pinned to a trained model.
The two-example training demo learns `hello` → `hello there` and
`what can you do` → `i can return source excerpts`, both ending at EOS.
This checks that training works on supplied examples; it does not measure generalization.

## Start without training

Run one API replica per database. Choose a private token; Docker-network clients
must provide it. Baseline routes retain their unauthenticated contract.

PowerShell, from the repository root:

```powershell
$env:CGAI_CHAT_API_TOKEN = 'replace-with-a-private-token'
docker compose up --build --detach --wait
$chatHeaders = @('-H', "Authorization: Bearer $env:CGAI_CHAT_API_TOKEN", '-H', 'Content-Type: application/json')
$conversation = curl.exe --fail-with-body @chatHeaders --data-binary '@examples/api/chat-create.json' http://localhost:3000/api/v1/conversations | ConvertFrom-Json
$reply = curl.exe --fail-with-body @chatHeaders --data-binary '@examples/api/chat-message.json' "http://localhost:3000/api/v1/conversations/$($conversation.id)/messages" | ConvertFrom-Json
$reply.conversation.messages[-1] | ConvertTo-Json -Depth 8
```

Creating a conversation with `{}` returns `modelName: null` and `modelChecksum: null`.
No training job or dummy model is created. The message fixture sends `hello world`
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

Neural generation is a separate explicit mode. Train the small demo and create a
new conversation with `modelName: "tiny-chat"`:

```sh
export CGAI_CHAT_API_TOKEN='replace-with-a-private-token'
docker compose up --build --detach --wait
curl --fail-with-body http://localhost:3000/api/v1/health
curl --fail-with-body -H "Authorization: Bearer $CGAI_CHAT_API_TOKEN" \
  -H 'Content-Type: application/json' \
  --data-binary @examples/api/chat-train.json \
  http://localhost:3000/api/v1/chat-models/train
```

PowerShell uses `$env:CGAI_CHAT_API_TOKEN` and `curl.exe`. Training returns HTTP 202
with a job ID. Poll `GET /api/v1/chat-jobs/<id>` until complete or error. Completed
jobs record model identity, training measurements and a corpus hash.

```sh
curl --fail-with-body -H "Authorization: Bearer $CGAI_CHAT_API_TOKEN" \
  -H 'Content-Type: application/json' -d '{"modelName":"tiny-chat"}' \
  http://localhost:3000/api/v1/conversations
curl --fail-with-body -H "Authorization: Bearer $CGAI_CHAT_API_TOKEN" \
  -H 'Content-Type: application/json' --data-binary @examples/api/chat-neural-message.json \
  http://localhost:3000/api/v1/conversations/<id>/messages
```

Use the returned revision on the next new request. Admission and completion each
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
| POST | `/chat-models/train` | `name`, structured `examples`, optional `config`, `training`, `provenance`; returns job |
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
normalized question/applicability match and a fresh one-day TTL. Original provenance
remains visible. This is excerpt reuse, not automatic verification of every claim.

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
at most 256 slots; current questions must fit in full. Usage reports dropped history.
Adam moments span examples/epochs within one call but are not stored in artifacts.
New chat models use zero BOS padding held fixed during training and distinct initial
token preferences across centroid experts to prevent early training collapse. Other
embeddings and all output logits remain trainable. Existing artifacts keep their
stored inference behavior; the standalone neural prototype keeps its own initialization.

Workers have one training/two inference slots, sixteen queued tasks, 30-second
inference and 600-second training deadlines. Children use a 512 MiB JS heap limit;
native allocations have separate count/shape bounds. Conversations are capped at
1,000 entries and checked against 8 MiB before admission. Memory has 1,000 records.

```sh
cmake --build build/dev --config Release
ctest --test-dir build/dev -C Release --output-on-failure
cd persistence
bun run typecheck
bun run test:native
bun run test:e2e
node api/src/evaluation/chat.ts
```

The evaluator writes `build/evaluation/chat/report.json` and `model.cgchat`, recording
corpus/split and implementation hashes, protocol/tokenizer identity, source commit/dirty flag, seed,
hardware, losses, exact-answer comparisons and per-case termination/latency. Dataset
validation rejects repeated IDs, normalized dialogues and cross-split source/family
leakage. Final test examples are never vocabulary inputs or checkpoint selectors.

Remaining gates include realistic reviewed corpora, human-rated claim support,
follow-up evidence reasoning, fact-specific freshness/contradiction resolution,
training-candidate review, multiple authenticated owners, multi-replica leases,
total-process memory enforcement and agreed quality/
latency thresholds. Passing builds does not satisfy these release gates.
