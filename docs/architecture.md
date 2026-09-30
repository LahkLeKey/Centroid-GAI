# Centroid chatbot architecture

C owns tokenization, centroid math, training, inference and artifact validation.
The Node service owns HTTP validation, workers, persistence, retrieval, public
fetching and memory admission. There is no browser application or transformer.

## Engines and artifacts

| Engine | Sources | Format and interface |
| --- | --- | --- |
| Count-centroid baseline | `src/model/` | `.cgai`, baseline HTTP/CLI and count-model composition |
| Neural continuation prototype | `src/neural/` | `.cgnn`, native C API and neural CLI |
| Neural conversation engine | `src/chat/`, shared neural math | `.cgchat`, structural dialogue C API and worker-backed HTTP |

The formats are distinct. Renaming does not convert them. Neural parameter spaces
cannot be combined with baseline merge operations. The chat codec uses `CGAICHAT`
magic, versioned little-endian integers and IEEE-754 doubles. It validates shapes,
vocabulary, finite weights and exact length, with a 64 MiB artifact limit.

## Conversation execution

```text
HTTP -> owner/token check -> transcript revision CAS -> bounded process worker
              |                      |                         |
              |                      |                   native chat engine
              |                      +-> pending / complete / error / cancelled
              |
              +-> source mode -> scoped memory / repository retrieval
                                      |
                                      +-> explicit public query -> bounded research
```

Each conversation pins its model checksum. Changing a model name's head does not
change existing sessions. Admission and completion each advance the revision.
Request IDs and persisted fingerprints make retries idempotent and reject changed
input. Failed/cancelled message pairs never become completed conditioning context.
Restart recovery marks interrupted requests/jobs as errors. One API replica must
own a chat database; distributed worker leases are not implemented.

Workers allow one training process, two inference processes and sixteen queued
tasks. Timeouts, cancellation and shutdown terminate children. Native operations
run outside the event loop. The C API remains synchronous. Workers have a 512 MiB
JavaScript heap limit in addition to native dimension/allocation bounds.

PostgreSQL is authoritative: immutable bytes/provenance live in `NeuralChatArtifact`,
model heads in `NeuralChatModel`, and versioned documents in `ChatConversation`,
`ChatTrainingJob` and `ChatMemoryState`. A lock-protected atomic file store supports
local tests, including reopen/reload. Committed migrations cover every chat table.

## Neural conditioning

The neural engine concatenates embeddings in order, applies a tanh encoder, routes
through learned centroids using squared distances, and mixes expert distributions.
Clipped Adam trains embeddings, encoder, centroids and output logits.

Chat divides the encoder input into a fixed prompt and rolling answer suffix.
The current question never shifts out. Older history is admitted as complete
user/assistant pairs; evidence has a separate quarter-window ceiling. Oversized
current questions are rejected. Structural roles and boundaries are input-only IDs
appended after lexical vocabulary. Marker-looking text is ordinary tokenized data.
Training and inference share the formatter.

Only answer words and EOS are supervised. Independent prepared prefixes and target
offsets prevent cross-dialogue and future-target leakage. One optimizer spans all
targets/epochs per call. Artifacts omit moments, so another call is not exact resume.

## Evidence, research and memory

Repository retrieval uses deterministic lexical BM25 over verified snapshots.
Excerpts retain paths, commits and snapshot-normalized line coordinates. Catalog
vectors remain manually authored retrieval data, separate from neural weights.
Neither lexical similarity nor token probability is factual confidence.

Default source mode works without a trained model. It returns stored excerpts or
automatically researches unsupported current messages through Wikipedia, with
SearXNG available by configuration. `autoSearch: false` keeps a message offline;
`publicQuery` overrides the public terms. History and private memory are never
appended to queries. Explicit neural mode requires a pinned model and remains experimental. Fetching
validates public addresses and redirects, pins DNS results to sockets, refuses
compressed responses, bounds bytes/duration and never executes HTML.

Memory belongs to a configured owner and supports view, correct, dispute, forget,
disable and retention settings. Reuse requires a fresh exact-query/applicability
match. No interaction changes live weights or publishes a public cache. Shared
multi-user deployment requires additional authenticated ownership infrastructure.

See [chat service](chat-service.md) for routes and measured limitations,
[neural computation](neural-centroid.md) for math, and the
[roadmap](chatbot-plan.md) for unmet quality gates. Public C interfaces remain
under `include/`; private ownership/layout helpers remain under `src/internal/`.

## Native contracts

Native implementations are grouped by responsibility:

| Directory | Responsibility and consumers |
| --- | --- |
| `src/core/` | Shared errors, file/JSON helpers, random numbers, tokenization and vocabulary |
| `src/model/` | Count-centroid baseline, composition and `.cgai` artifacts; used by the ABI, CLI and Node addon |
| `src/neural/` | Neural math, training, generation and `.cgnn` files; shared with chat and exposed by the C API/CLI |
| `src/chat/` | Conversation formatting, training/evaluation and `.cgchat` codec; used by the C API and Node addon |
| `src/spatial/` | Spatial indexing and queries; used by the ABI and Node addon |
| `src/knowledge/` | Static catalog, category translation units and queries; used by the C API, ABI and CLI |
| `src/abi/` | Exported shared-library interface and artifact inspection; the Node addon compiles the supported subset |
| `src/cli/` | Command-line entry point, parsing and commands |
| `src/internal/` | Shared private headers; never installed |
| `persistence/api/native/` | Node-API argument conversion, ownership and callbacks; built with the service package |

All of these subsystems remain in use. The baseline and standalone neural file
codec are still public interfaces even though chat has its own artifact format.
The static knowledge catalog serves native callers independently of the service's
repository retrieval. Keep the Node bridge with its package so node-gyp and the
container build retain the same package boundary.

`CMakeLists.txt` explicitly lists library, ABI and CLI sources;
`persistence/api/binding.gyp` lists the addon's subset. Update both when adding
shared implementations. Catalog categories are discovered under
`src/knowledge/knowledge_catalog/`. Native tests stay under `tests/`, and CMake's
lint/format targets discover subsystem sources recursively.

Baseline artifact encoding writes numeric arrays in native machine representation;
it does not convert byte order. Preserve representation compatibility between hosts.

Only headers under `include/` are installed. Headers under `src/internal/` are
private implementation details without compatibility guarantees. Token identifiers
and centroid identifiers are distinct struct types, `cgai_token_id` and
`cgai_centroid_id`, so the compiler rejects mixing them accidentally.
