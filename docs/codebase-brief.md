# Reviewed codebase brief

This brief describes source contracts, not the result of a new test run. Read it
from the selected committed snapshot. Dirty files and later commits are not visible
until a new snapshot is prepared and selected. Source paths below are repository
paths; citations use snapshot-normalized lines rather than checkout line numbers.

## Purpose and architecture

Centroid-GAI is an API-first chatbot experiment with a trainable centroid neural
network written in C11. C owns tokenization, embeddings, ordered context encoding,
centroid routing, training, generation and artifact validation. Node TypeScript
owns HTTP validation, subprocess workers, persistence, repository retrieval,
public research and owner-scoped memory. There is no browser application or transformer.
Sources: `README.md`, `docs/architecture.md`, `src/neural_math.c`,
`persistence/api/src/server.ts`.

## Engines and artifact formats

The count-centroid baseline stores `.cgai` artifacts. The neural continuation
prototype stores `.cgnn` artifacts. The structured neural conversation engine
stores `.cgchat` artifacts. These formats are distinct; renaming a file does not
convert it. Baseline merge operations cannot combine learned neural parameter spaces.
Sources: `docs/architecture.md`, `src/chat_codec.c`, `src/neural_file.c`,
`include/centroid_gai_chat.h`, `include/centroid_gai_neural.h`.

## Chat data flow and durable state

The HTTP chat service validates owner, request ID and transcript revision before
admitting work. PostgreSQL stores immutable chat artifacts by checksum, named model
heads, versioned conversations, training jobs and memory documents. Admission and
completion each advance the conversation revision. Retrying the same request ID
and input returns the authoritative transcript; changed input conflicts. Restart
recovery marks interrupted requests as errors instead of completed assistant facts.
Sources: `persistence/api/src/chat/service.ts`, `persistence/api/src/chat/postgres-store.ts`,
`persistence/db/src/prisma/contract.prisma`, `docs/architecture.md`.

## Native training and generation

Chat training uses independent structured examples with user/assistant roles and
optional evidence. Only assistant answer words and EOS are supervised. Input-only
role markers cannot become generated words. The fixed prompt preserves the current
question; a rolling suffix holds generated answer tokens. Older history enters as
complete pairs within the prompt budget. Adam moments span one training call but
are not stored in artifacts, so another call is not exact optimizer resume.
Sources: `src/chat_model.c`, `src/chat_prompt.c`, `src/chat_evaluation.c`,
`src/neural_training.c`, `docs/neural-centroid.md`.

## Model quality and operating limits

The documented fixed six-case neural dialogue evaluation scores 0/6 exact answers.
Learning the two demo answers checks training fit, not conversational generalization.
Neural replies remain experimental. Lexical similarity, centroid distance and token
probability are not factual confidence. Source mode returns evidence or uncertainty.
Workers have one training slot, two inference slots and sixteen queued tasks;
inference has a 30-second deadline and training a 600-second deadline. The 512 MiB
child JavaScript heap limit does not enforce total process or native memory usage.
Sources: `docs/chat-service.md`, `persistence/api/src/chat/workers.ts`,
`persistence/api/src/chat/worker.ts`.

## Public research and memory

Public source mode can start without a model. It checks reusable public source
memory, then automatically researches unsupported messages through Wikipedia;
SearXNG is configurable. `autoSearch: false` disables automatic public search for a
message, and deployment setting `CGAI_SEARCH_PROVIDER=disabled` disables all public
research. Queries never append private conversation history or repository excerpts.
Remembered fetched sources require explicit `rememberSources: true`. Memory reuse
requires owner, exact normalized question, applicability and freshness; source
evidence has a one-day TTL. Explicit user statements remain user-attributed.
Interactions do not change live neural weights.
Sources: `persistence/api/src/chat/research.ts`, `persistence/api/src/chat/memory.ts`,
`persistence/api/src/chat/public-fetch.ts`, `docs/chat-service.md`.

## Repository knowledge and conversation scope

Repository knowledge comes from immutable committed Git snapshots. The importer
normalizes text, preserves Git blobs and commit IDs, records inclusion coverage and
checksums, and excludes secrets, generated data and benchmark answers. Retrieval
uses deterministic lexical BM25 and returns exact cited source passages. A repository
conversation pins its commit and manifest hash. Repository scope makes no public
research requests and does not use public source memory as codebase evidence.
Sources: `tools/knowledge/corpus.py`, `tools/knowledge/bootstrap.py`,
`examples/knowledge/codebase.json`, `persistence/api/src/knowledge/retrieval.ts`,
`persistence/api/src/chat/repository.ts`.

Explicit task IDs and file paths are resolved before an active task is reused.
Competing references prompt for a choice. An exact path or unambiguous filename
can select a file; unknown files do not become invented evidence. A selected file
and competing task references persist as bounded `focusPath` and `taskCandidates`
state for the pinned snapshot. Corrections, explicit topic changes and snapshot
switches clear stale references. Failed, cancelled and pending replies do not
establish completed context. These are implemented resolver rules; their broader
quality still requires reviewed development results and committed verification.
Sources: `persistence/api/src/chat/repository-context.ts`,
`persistence/api/src/chat/repository.ts`, `persistence/shared/repository.ts`,
`persistence/api/src/chat/service.ts`.

## Software development investigations

Repository-only impact investigations inspect a source file's recognized direct static
imports/includes and reverse dependents. Symbol investigations find lexical
identifier occurrences in indexed source. Both return exact snapshot citations,
structured investigation data and applicable reviewed task checks. A conversation
can retain the investigation target for follow-up questions about tests and
verification, while an explicit snapshot switch clears stale investigation state.
These investigations execute no commands and make no public research requests.
Dependency indexing covers TypeScript/JavaScript literal import/export forms,
including literal dynamic and type imports, and C quoted includes; Python imports
and computed loading are outside its scope.
Static dependency links do not establish complete runtime impact, lexical matches
do not prove semantic symbol references, and proposed checks do not establish
coverage or a passing run.
Sources: `persistence/api/src/chat/repository.ts`, `persistence/shared/repository.ts`,
`persistence/api/src/chat/service.ts`, `docs/repository-chat.md`.

## Docker startup and API access

`compose.yaml` runs PostgreSQL, a migration/verification init service and the API.
The API waits for database initialization. `compose.repository.yaml` adds a read-only
snapshot mount for repository conversations. Prepare a committed snapshot before
starting that overlay. Set `CGAI_CHAT_API_TOKEN` and send it as a Bearer token for
Docker-network chat clients. The current chat deployment has one configured owner
and one API replica per database; distributed leases and multi-user authentication
are not implemented. Baseline routes retain their separate unauthenticated contract.
Sources: `compose.yaml`, `compose.repository.yaml`, `compose.env.example`,
`persistence/api/src/chat/routes.ts`, `docs/chat-service.md`.

## Verification commands

From the repository root, configure and build with
`cmake -S . -B build/dev -DCMAKE_BUILD_TYPE=Release` and
`cmake --build build/dev --config Release`; run native tests with
`ctest --test-dir build/dev -C Release --output-on-failure`.
From `persistence/`, run `bun run typecheck`, `bun run test:native`,
`bun run test:repository` and `bun run test:e2e`. The E2E command uses an isolated Compose project and removes its
own containers and volume. Repository retrieval checks can run from the root with
`node --test persistence/api/src/knowledge/retrieval.test.ts`.
These are proposed commands to execute, not evidence that a particular run passed.
Sources: `docs/development.md`, `persistence/package.json`,
`persistence/api/package.json`, `persistence/e2e/compose-e2e.ts`.

## Reviewed remaining work

The smallest next repository-chat task is source-mode follow-up calibration:
review the implemented reference resolver against the separate eight-chain
development suite, then commit the source and capture the reviewed checks for that
snapshot. Implementation is recorded separately from verification; the registry
keeps this task eligible until its completion condition is reviewed and verified.
A deterministic initial
conversation loop does not establish robust follow-up behavior for arbitrary prose.
Keep development examples separate from frozen acceptance families and keep all
gold assertions outside the searchable snapshot. Review source support before
treating generated wording as an answer. Broader neural training data, total-process
memory enforcement and authenticated multi-user deployment remain separate tasks.
The reviewed task registry is `docs/codebase-tasks.json`. Task status records project
review; a chat statement such as "I finished it" is not an independently run check.
Sources: `docs/codebase-tasks.json`, `docs/chat-service.md`, `docs/development.md`.
