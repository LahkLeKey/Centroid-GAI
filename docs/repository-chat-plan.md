# Repository chat and development-loop plan

Migration status: **Superseded roadmap**. The active [C11 Life
deliverables](centroid-next-deliverables.md) retire this service workflow. Removed
tools and fixtures are neither restored nor replaced.

Status: repository snapshot loading, offline scope, reviewed next-step actions,
durable context, explicit snapshot switching and HTTP runner code are implemented.
The referenced bootstrap/capture tools and scenario inputs are absent from this
checkout; the incomplete workflow is now retired. Use the
[repository chat guide](repository-chat.md) for contracts and the
[documentation reconciliation](centroid-documentation-reconciliation.md) for
current dependencies. The baseline and implementation sequence below preserve
the historical specification. Acceptance requires captured results, rather than
the presence of a route or script. Streaming, total server/native memory
instrumentation and calibrated factual support remain open.

## Target interaction

Start a conversation without a neural model, select a verified repository revision,
and ask what the project does, where behavior lives, what is unfinished, which
change to make next, and how to check it. A developer makes the change and runs
the checks. The chatbot then loads the new committed snapshot and reassesses.

The loop is:

```text
inspect committed code -> ask for a next step -> clarify files and checks
        -> developer changes code and runs checks -> commit and refresh snapshot
        -> compare evidence and verification results -> ask what remains
```

The chatbot recommends work and reports evidence. It does not execute edits,
tests, commits or deployment merely because a conversation describes them.
User-reported progress and independently captured check results stay distinct.

## Baseline before this implementation

- The service already supports model-free conversations, persistence, retries,
  cancellation, source research and memory. The completed validation includes
  61 unit tests and nine Docker API tests, including live public research.
- The repository retriever already verifies immutable snapshots and exact passage
  citations. Its evaluation has 16 single-turn development questions: 12 with
  evidence and four unsupported. These are not HTTP conversation tests.
- `ChatService` passes only the current request into `ResearchService.answer`.
  Source retrieval cannot reliably resolve "which files?" or "what next?" from
  prior turns. The neural dialogue fixture still has 0/6 held-out exact answers.
- `server.ts` accepts `CGAI_REPOSITORY_SNAPSHOT`; Compose currently provides neither
  this setting nor a snapshot mount. The index loads once at startup.
- The importer reads committed files only. Its current inclusion policy omits
  important operational evidence such as Compose, package scripts, Dockerfiles and
  CI. YAML and selected extensionless filenames need explicit admission.
- `knowledge/codebase/codebase-v1` is an older categorized release that includes
  the removed frontend. It is not the raw snapshot directory expected by the
  retriever (`manifest.json`, train/validation JSONL and text files).
- Repository answers currently lose some citation fields when copying retrieval
  hits into chat sources. Memory is not enforced against a repository revision.

## Deliverable 1: codebase information and one next-step loop

### 1. Build a complete, versioned knowledge input

Extend the narrow inclusion policy in `examples/knowledge/codebase.json` and
`tools/knowledge/config.py`. Include application/native source, installed headers,
tests, current docs, and these operational files:

- `compose.yaml`, `compose.env.example`, `CMakeLists.txt` and `.github/workflows/ci.yml`;
- API/DB Dockerfiles, package manifests and `persistence/e2e/compose-e2e.ts`;
- `examples/api/` request fixtures and relevant model/training configuration.

Admit YAML and exact Dockerfile names explicitly. Exclude `.env`, generated build
output, dependencies, model weights, generated snapshots, evaluation reports and
the new scenario/gold-answer files. Do not broadly include hidden directories.
Explicit benchmark exclusions include `examples/evaluation/`, `examples/chat/`,
this planning document and any documentation generated from scenario answers.
Keep benchmark-bearing assertions out even when stored under an otherwise indexed
test/evaluation directory. Ordinary implementation tests remain useful evidence
for how to verify a change. Check the final snapshot manifest against an explicit
benchmark-file exclusion list so moving a fixture cannot silently expose answers.

Add a concise reviewed codebase brief and task registry, with source references.
For each task record an ID, current state, prerequisites, rationale, affected
paths, verification commands and supporting evidence. Recommendations select the
smallest unfinished task whose prerequisites are satisfied. A task cannot be
marked verified solely from "I finished it" in a chat message.

Use the existing importer after those inputs are committed:

```sh
python -m tools.knowledge snapshot --config examples/knowledge/codebase.json --repo . --ref <committed-sha> --output build/knowledge/repository/<committed-sha>
```

Record the commit, manifest hash, included paths and rejected/skipped files. Missing
required operational files fail preparation. Generated knowledge stays outside
the tracked source set. Dirty working-tree changes require a later commit and
snapshot; they must not be described as already visible to the assistant.

### 2. Expose repository scope through API and Compose

Add a read-only snapshot mount and `CGAI_REPOSITORY_SNAPSHOT` to the repository
Compose workflow. Provide a bootstrap script that prepares or validates the
snapshot before starting the API; the API must not silently read an old directory.

Introduce an explicit repository conversation scope, with the selected snapshot
commit and manifest hash pinned to the conversation. A model remains optional.
The current public-research flow remains available in its own scope. In repository
scope, a retrieval miss produces clarification or abstention and zero public
network requests. Missing or corrupt snapshots produce an explicit availability
error. A public memory entry must not override repository evidence.

Expose knowledge readiness, snapshot identity and coverage in a read-only API
response. Preserve source path, Git blob, document/passage hashes, commit and
`snapshot-normalized-lines` coordinates in chat citations. Do not present these
coordinates as line numbers from the mutable checkout.

### 3. Answer facts and support the first next-step conversation

Return a concise finding, its supporting excerpts, known limitations and, when
asked, a proposed next action. Each action includes why it is next, affected paths,
prerequisites and commands that would verify it. Commands must come from reviewed
repository evidence; label proposed checks separately from checks actually run.

Build this first using retrieval and the reviewed task registry. Centroid-generated
wording can be evaluated separately without making it a dependency for useful
repository information.

Pass bounded completed history and cited task/topic references from `service.ts`
to the repository answer layer. Resolve an unambiguous "that", "which file?" or
"which tests?" against the active topic; ask the user to choose when multiple
topics remain. Do not concatenate unlimited transcripts or use failed/cancelled
assistant replies as facts. Persist the active task/topic so restart can resume it.

### 4. Ship demonstrable output and a first gate

Provide PowerShell and shell examples that run against the actual Compose API.
Generate readable example transcripts from captured responses, so examples cannot
silently drift into hand-written claims about behavior the service lacks.

The first delivery must run all 24 repository cases below and meet the stated
acceptance gate, plus pass every turn of this core loop. The core loop counts as
one of the eight contextual scenarios in the full suite:

| Turn | User message | Required behavior |
| --- | --- | --- |
| 1 | "What does this codebase currently support?" | Cite implemented API/native behavior and distinguish documented goals. |
| 2 | "What is the next unfinished step for the repository chatbot?" | Select a task supported by this snapshot and reviewed task state. |
| 3 | "Which files should I change first?" | Resolve the selected task and cite its affected implementation paths. |
| 4 | "How should I verify that change?" | Supply relevant existing commands and proposed assertions; claim no execution. |
| 5 | "I meant source-mode follow-ups, not neural training." | Apply the correction, preserving snapshot and task context. |
| 6 | "What is the smallest next action now?" | Give one bounded action with evidence and an observable completion condition. |

Restart the API between turns 3 and 4. The same conversation must resume with
its task, citations and revision intact. No neural training or Internet access
is needed for this gate.

## Robust suite: 40 scenarios

Store versioned fixtures separately from the searchable corpus, for example
`examples/chat/repository-scenarios-v1.json`. Extend the existing evaluator with
a real HTTP scenario runner instead of replacing citation or lifecycle unit tests.

| Group | Scenarios | Coverage |
| --- | ---: | --- |
| Repository questions | 24 | 18 answerable and 6 unsupported; extend the existing 16 cases. |
| Contextual conversations | 8 | Follow-ups, ambiguous references, corrections, topic switches, long history, resume and two-session separation. |
| Change and refresh loops | 4 | Snapshot A, an external code change, verification evidence, snapshot B, and reassessment. |
| HTTP operations | 4 | Concurrent retry/revision, cancellation and failure recovery, restart/resume, concurrent sessions under load. |

The repository questions must cover architecture, native/chat data flow, artifact
formats, Docker startup, training and generation, memory/research routing, model
quality limits, test commands, and the next reviewed project task. Unsupported
cases include production secrets, unmeasured latency, uncommitted changes and
claims that a particular test run passed without an attached report.

Each conversation scenario contains at least four user turns. Fixtures specify
snapshot identity, starting session state, request options, required claims and
citations, forbidden claims, expected routing, state transitions and any failure
injection. Assertions check supported meaning and state, not a single exact prose
string. A stale gold quotation is an invalid fixture and fails setup; it is never
silently removed from the denominator.

Keep development cases distinct from a frozen set of question families and
paraphrases used for final acceptance. Repository source documents may be retrieved
in both sets; gold answers, assertions and generated transcripts may not. Preserve
the separate neural train/development/test boundaries and its existing test set.

## Refresh and progress rules

Pin conversations to snapshot A until an explicit switch to verified snapshot B.
Old messages retain their A citations. A switch revalidates the active task and
evidence references, records the transition, and reports removed or changed facts.
Never describe B as loaded merely because the user mentions its commit hash.

Namespace repository memory by owner, repository, commit and applicability. An
answer cached for A cannot be treated as current evidence for B. Retain public
source memory separately and require explicit scope changes before using it.

Verification reports include the source commit, command, exit status and captured
result. For the first loop, a developer runs a separate verification command and
imports its report. User statements remain attributed progress; report provenance
is visible, and the chatbot does not treat arbitrary pasted text as trusted
execution proof. A failed or missing check keeps the task unverified.

The four refresh cases cover a completed task, a still-failing check, an invalid
or missing new snapshot, and a changed/deleted source with stale memory. Use small
temporary Git fixture repositories for deterministic A/B changes; tests do not
edit or commit the developer's main working tree.

## Acceptance and measurement

These gates define acceptance; actual numerators, denominators and failures are
recorded in the generated run report:

- **First delivery:** all citations validate against the pinned snapshot; zero
  public requests; no invented paths, commands or execution results. At least
  16/18 answerable questions contain the required supporting passage, all six
  unsupported questions abstain or clarify, and the core loop passes every turn.
- **Context:** at least 7/8 expanded chains satisfy the final intent. Session,
  owner and version isolation must pass every case, regardless of aggregate score.
- **Refresh:** all four loops distinguish old and new evidence correctly and
  preserve uncertainty about unverified work.
- **HTTP:** all four operation scenarios pass, with bounded work and no orphaned
  pending request after cancellation or recovery.

Report numerator, denominator and failed scenario IDs for every metric. Measure
local response p50/p95, health-response latency during work, peak memory and queue
time on named hardware, with fixed corpus size and concurrency. Record a baseline
before choosing enforceable latency budgets. Do not derive factual confidence from
centroid distance or token probability.

Every run emits raw request/response JSONL, per-assertion results, a summary,
snapshot/suite/implementation hashes, mode/routing trace, source spans, memory IDs
and timings. Public artifacts use synthetic owner data and exclude credentials.
A Markdown transcript is generated from the same JSONL for inspecting real examples.

## Delivery order and verification lanes

1. Commit-pinned corpus coverage, reviewed brief/task registry, snapshot validation
   and Compose wiring. Establish the 24-case repository information baseline.
2. Repository-only answer policy, citation metadata, bounded task/topic context
   and the six-turn core loop. This completes deliverable 1.
3. Explicit snapshot switching and verification-report handling; complete the
   eight context chains and four refresh loops.
4. Complete the four HTTP stress/failure scenarios, CI artifacts and measured
   latency/resource budgets. Keep focused unit tests for precise race regressions.
5. Add progress events or SSE if required by the measured interactive workflow.
   Current requests return complete JSON. Any later stream must expose real stages,
   ordered events, cancellation/disconnect behavior and a single final result;
   do not simulate token streaming for a response that was already generated.

Fast checks use deterministic snapshot and provider fixtures without network.
Compose checks use real PostgreSQL and workers with public research disabled.
Keep opt-in live-provider checks for `hello world`, one informative public question
and memory reuse separate from repository acceptance; third-party availability
and changing article text must not gate local codebase functionality.

Primary implementation areas: `tools/knowledge/`, the codebase import configuration,
`compose.yaml`, `persistence/shared/chat.ts`, chat service/research/memory modules,
repository retrieval/evaluation, the E2E runner and API scenario fixtures. Reuse
the existing persistence and native layers; introduce schema changes only for
new durable state that cannot be represented safely in the existing documents.
