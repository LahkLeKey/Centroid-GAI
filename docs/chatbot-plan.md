# Delivering the centroid neural chatbot

This roadmap includes implemented infrastructure and remaining release gates.
The [chat service guide](chat-service.md) is the executable feature contract.
The target is a local chatbot using the C centroid neural network, accessed
through HTTP/JSON with curl and Docker Compose. Start with short questions and
follow-ups about this repository so that answers and evidence can be checked.
Broader conversation is a later quality milestone.

The [repository chat workflow](repository-chat.md) implements commit-pinned codebase
information, cited next-step actions and durable follow-ups, with a 40-scenario
HTTP suite. The [delivery plan](repository-chat-plan.md) records its acceptance
gates and remaining extensions; the milestones below retain the broader roadmap.

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

The standalone neural work in milestone 2 can proceed while the service shell
is repaired. No HTTP route should advertise a neural operation it cannot execute.

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

## 3. Implement conversation conditioning and training

Complete the draft chat design with a persistent prompt input and a separate
rolling answer input. The current user question must remain available throughout
generation. Keep the combined context within the implemented allocation limits;
choose window sizes through measured experiments.

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

## 4. Publish versioned neural chat artifacts

Implement a bounded byte codec and Node wrapper for train, inspect, evaluate and
reply. Validate sizes, shapes, vocabulary, numeric values and protocol versions
before accepting an artifact. Keep baseline `.cgai`, prototype `.cgnn`, and the
new chat format explicitly distinguishable. Changing the encoder or structural
token layout requires a compatible version policy and usually retraining.

Persist immutable artifacts by checksum. A model name may point to a newer
version, but existing conversations remain pinned to their original checksum.
Persist dataset/protocol identity and validation metrics with each trained model.

**Acceptance:** round trips preserve predictions; malformed/incompatible artifacts
fail safely; the API cannot mix count-centroid models with neural weights. Training
runs outside the request event loop with bounded queue length, memory and time.

## 5. Deliver the HTTP conversation lifecycle

The following routes are implemented; see the [chat service](chat-service.md)
for request bodies and operational restrictions:

| Method and path | Intended behavior |
| --- | --- |
| `GET /api/v1/chat-models` | List compatible neural model versions |
| `POST /api/v1/chat-models/train` | Submit a bounded training job |
| `GET /api/v1/chat-jobs/:id` | Inspect training status, metrics and artifact identity |
| `POST /api/v1/conversations` | Create a session pinned to a neural model checksum |
| `GET /api/v1/conversations/:id` | Load authoritative session history |
| `POST /api/v1/conversations/:id/messages` | Submit a message with request ID and expected revision |
| `POST /api/v1/conversations/:id/cancel` | Cancel an identified active request |
| `DELETE /api/v1/conversations/:id` | Remove the session and its messages |

Match the shared types to the final route contract during implementation. Serialize
sends within a session, make retries idempotent, and reject stale revisions.
Persist explicit pending, complete, cancelled and error states. A failed response
must not become completed assistant context for the next turn. Cancellation must
stop the worker, not just discard a result after computation finishes.

Start with complete JSON responses containing content, usage, finish reason,
request ID, model identity and evidence. Add streaming only with actual incremental
inference and disconnect propagation.

**Acceptance:** curl fixtures exercise the full lifecycle through Compose, including
restart/reload, duplicate retry, two isolated sessions, stale revision, cancellation,
worker failure and database failure. Existing baseline API tests continue to pass.

## 6. Ground answers and add persistent memory

Connect repository passage retrieval to the conversation service. Retain exact
excerpts and source identifiers independently of generated wording. Train and
evaluate evidence use before enabling neural synthesis from arbitrary passages;
until then, a retrieved excerpt is a source result, not a generated factual answer.

Next implement the [research and memory design](research-memory.md): retrieve
scoped memory, check answer support, research public sources when needed, return
citations, and retain sourced knowledge under explicit retention rules. Network
failure or insufficient evidence produces uncertainty rather than invented facts.

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
