# Delivering the centroid neural chatbot

Migration status: **Superseded roadmap**. The active [C11 Life
deliverables](centroid-next-deliverables.md) replace this HTTP/service sequence.
Retain useful native dialogue, reviewed-data and independent quality requirements;
retire old APIs and non-C executable workflows.

This roadmap includes implemented infrastructure and remaining release gates.
The [chat service guide](chat-service.md) documents the service feature contract.
The target is a local chatbot using the C centroid neural network, accessed
through HTTP/JSON with curl and Docker Compose. Start with short questions and
follow-ups about this repository so that answers and evidence can be checked.
Broader conversation is a later quality milestone.

The [conflict training parity plan](centroid-training-parity-plan.md) proposes
bringing Centroid Life's participant updates, verified supervision, replay and
consolidation to composed, scholarly and chat models. It separates training
feature parity from independently measured assistant capability and retains the
relevant native and quality requirements from this roadmap.

The repository service implements commit-pinned information, cited next steps and
durable follow-ups. Its [workflow guide](repository-chat.md) and 40-scenario runner
depend on bootstrap/capture tools and fixture files absent from this checkout.
Those inputs remain removed and the service lane is retired. The
[delivery plan](repository-chat-plan.md) retains the historical specification;
the [documentation reconciliation](centroid-documentation-reconciliation.md)
records current requirements and dependencies.

## First useful release

A user can select a trained neural chat model, create a conversation, ask a
question and follow-up, restart the service, reload the same history and continue.
Replies identify the model version and stopping reason. Supported repository
answers include evidence; unsupported questions prompt clarification or abstention.
The API stays responsive while inference or training runs in a bounded worker.

The browser interface is deferred. The project already has a differentiable
centroid model; implementing a transformer is not a prerequisite for this release.
Evaluate the centroid design on this task before choosing a different encoder.

## Current starting point

- **Implemented:** single-window neural training, evaluation, generation and file
  save/load in C; neural CLI commands; baseline HTTP endpoints and artifact storage.
- **Implemented:** structured chat C engine, Node bridge, bounded workers, neural
  HTTP lifecycle, chat/memory migrations, split-validated dialogue fixture and
  fixed evaluation report; repository/source results and owner-scoped memory.
- **Implemented with restrictions:** source conversations without training, automatic
  public research after a memory miss, default Wikipedia and configurable SearXNG.
  Requests can disable automatic research or override the public query.
- **Implemented:** a separate offline repository scope with verified snapshots,
  exact citation metadata, reviewed task selection and explicit revision switching.
- **Implemented:** protocol-two evidence budgets, complete-quotation checks for
  neural HTTP replies, candidate quality gates, source freshness policies and a new
  versioned synthetic extraction dataset under `data/chat/`. Development selects
  candidate restarts; final tests remain separate. Current native exact responses
  remain 0/6, so these controls do not establish conversational quality.
- **Implemented:** repository dataset version three with 200 authored examples,
  source/split-bound review, immutable offline releases and generated API training
  requests. The initial release declares agent review only. A vocabulary experiment
  measures held-out unknown tokens, byte expansion and source-copy representability;
  it does not change the production tokenizer or establish model accuracy.
- **Implemented:** a closed-vocabulary native learning sanity suite, free-running
  training diagnostics, separate refusal/clarification decision metrics, scorer
  positive controls and development-only evaluation. The source baseline now ranks
  complete evidence units; exact factual support and the HTTP publication gate remain
  strict. Sanity memorization success does not establish held-out conversation quality.
- **Unmet release gates:** robust dialogue quality, human-rated claim support,
  calibrated support routing, broad datasets, multi-owner authentication,
  reviewed training-candidate workflows and agreed hardware quality/latency targets.

## 1. Restore a reproducible service baseline

The native bridge, worker IPC, addon sources and chat/memory migrations are now
implemented. Tests exercise completion, errors, cancellation and shutdown. Keep
the acceptance checks below enabled as the service evolves; initial implementation
does not establish every release criterion.

**Acceptance:** C tests, service typecheck, native tests and clean Compose startup
pass. A health request succeeds during bounded worker activity. Document the
actual commands and remaining failures rather than bypassing checks.

Standalone neural work can proceed while service baseline checks and missing
repository workflow inputs are verified or restored. No HTTP route should
advertise a neural operation it cannot execute.

## 2. Build a dialogue dataset and a fixed evaluation suite

Use structured examples containing an ID, ordered role/content messages, and a
separate assistant answer. Retain source references and question-family identifiers
for grounded examples. Start with repository questions, follow-ups, corrections,
ambiguous requests and cases for which no answer is available.

Split by source/question family before adding paraphrases. Build the vocabulary
from training data only. Keep training, development and final test splits separate;
do not select checkpoints against the final test split. Store corpus hashes,
split identities, tokenizer version and source commit with the training report.

**Acceptance:** input validation and duplicate/leakage checks pass. A fixed suite
scores direct answers, multi-turn behavior, clarification, abstention and citation
support. Tiny next-token fixtures remain correctness tests, not chatbot benchmarks.

The [repository data workflow](chat-service.md#repository-training-data-and-releases)
now implements ingestion, explicit review, approved-only export and release
verification. The first 120/40/40 split uses 50 families with four variants each;
shared templates still limit the strength of generalization claims. The next data
gate is independent human review and broader examples driven by measured failures.
Production tokenizer changes, optimizer-state checkpoints and candidate promotion
review remain separate implementation work.

Use the [learning sanity checks](chat-service.md#learning-sanity-checks) to establish
basic reproduction before scaling experiments. During iteration, keep final-test
prediction/scoring disabled with `--development-only`. Review rubric-action conflicts
and corpus audit findings before publishing a new dataset version.

## 3. Preserve conversation conditioning and extend training

Persistent prompt conditioning and a separate rolling answer input are implemented.
Preserve the current question throughout generation and keep combined context
within measured allocation limits. Choose new window sizes through experiments.
The parity plan adds continuation/checkpoints and participant training; these
must preserve the existing formatter and supervision contract.

Encode roles and turn boundaries as structural IDs. Marker-looking user text
stays ordinary data. Use the same formatter and truncation rules for training and
inference: preserve the current question, admit complete recent turns that fit,
and report dropped context. Bound retrieved evidence separately.

Train on assistant answer tokens and EOS. User/history/evidence tokens condition
the answer but are not targets for that example. Preserve optimizer state across
independent examples within a training run; reset sequence boundaries between
dialogues and never leak future answer tokens into the current prediction.

**Acceptance:** gradient checks cover every new parameter group. Independent
examples remain independent; role-looking text cannot change structure; the
question continues to affect predictions after the answer exceeds its rolling
window. Record held-out answer quality and termination behavior after training.

## 4. Preserve artifacts and add versioned training bundles

The bounded inference codec and Node train/inspect/evaluate/reply wrapper are
implemented. Preserve validation of sizes, shapes, vocabulary, numeric values
and protocol versions. Keep `.cgai`, `.cgnn` and `.cgchat` distinguishable; new
resumable training bundles require their own contract. Encoder or structural-token
changes require a compatible version policy and usually retraining.

Persist immutable artifacts by checksum. A model name may point to a newer
version, but existing conversations remain pinned to their original checksum.
Persist immutable training/dataset/protocol provenance with artifacts and keep
candidate validation metrics in the durable training-job quality report. Rejected
candidates and their reports remain inspectable without changing the model head.

**Acceptance:** round trips preserve predictions; malformed/incompatible artifacts
fail safely; the API cannot mix count-centroid models with neural weights. Training
runs outside the request event loop with bounded queue length, memory and time.

## 5. Preserve the HTTP conversation lifecycle

The following routes are implemented; see the [chat service](chat-service.md)
for request bodies and operational restrictions:

| Method and path | Intended behavior |
| --- | --- |
| `GET /api/v1/chat-models` | List compatible neural model versions |
| `POST /api/v1/chat-models/train` | Submit a bounded training job |
| `GET /api/v1/chat-jobs/:id` | Inspect training status, metrics and artifact identity |
| `POST /api/v1/conversations` | Create a model-free source session or a session pinned to a compatible neural model |
| `GET /api/v1/conversations/:id` | Load authoritative session history |
| `POST /api/v1/conversations/:id/messages` | Submit a message with request ID and expected revision |
| `POST /api/v1/conversations/:id/cancel` | Cancel an identified active request |
| `DELETE /api/v1/conversations/:id` | Remove the session and its messages |

Keep shared types, native bridge and worker messages aligned with route changes.
Serialize sends within a session, make retries idempotent, and reject stale revisions.
Persist explicit pending, complete, cancelled and error states. A failed response
must not become completed assistant context for the next turn. Cancellation must
stop the worker, not just discard a result after computation finishes.

Start with complete JSON responses containing content, usage, finish reason,
request ID, model identity and evidence. Add streaming only with actual incremental
inference and disconnect propagation.

**Acceptance:** curl fixtures exercise the full lifecycle through Compose, including
restart/reload, duplicate retry, two isolated sessions, stale revision, cancellation,
worker failure and database failure. Existing baseline API tests continue to pass.

## 6. Extend grounded answers and persistent memory

Repository passage retrieval is connected to source conversations. Preserve exact
excerpts and source identities independently of generated wording. Repository
scope currently rejects neural model selection, so a neural repository pilot needs
a versioned admission change preserving offline and snapshot constraints. Train
and evaluate evidence use before enabling broader synthesis; retrieved excerpts
remain source results rather than proof of generated-answer quality.

Scoped memory, bounded public research, citations and retention controls are
implemented. Carry forward the [research and memory design](research-memory.md)'s
remaining calibrated support, fact-specific freshness, authenticated owner and
training-removal requirements. Network failure or insufficient evidence produces
uncertainty; storing a memory does not implicitly admit it into weight training.

**Acceptance:** citations support the emitted claims; unrelated evidence does not
create a confident answer; source corrections and deletion affect later retrieval;
offline mode works; personal memory does not cross user boundaries.

## Release evidence

Before training for a pilot, freeze the scenario set and agree numerical quality
and latency thresholds on named hardware. Report denominators and failures by
category: direct factual answers, follow-ups, supported claims, appropriate and
unnecessary abstention, natural termination, latency and resource use.

Compare the neural chatbot against the baseline engine and retrieval-only output
using the same answer rubric. Record checkpoint, corpus, formatter and evaluation
identities. A successful build, reduced loss, or saved transcript does not establish
conversational readiness. Use measured failures to choose the next model change.
