# Documentation reconciliation for centroid conflict training

Reviewed on October 3, 2026. This review consumed all 20 existing Markdown
documents in the root, `docs/` and persistence package guides, including the first
[parity plan](centroid-training-parity-plan.md). It also checked the eight public
C headers, shared chat/research/repository contracts, reviewed task registry,
relevant build and migration definitions, and model recipes and release evidence.
Generated corpora, every scenario row and every generated report were not treated
as documentation or exhaustively reread. This is a requirements and source audit,
not a new model-quality or service test run.

The audit preceded the user's C11-only correction. Useful model/data/correctness
requirements remain in the revised parity plan; old service infrastructure is
retired scope rather than a compatibility obligation. Frozen experiments remain
historical evidence. Reading a document for planning does not admit its contents
or embedded benchmark answers into training.

## Active direction after the review

The user's latest instruction supersedes the previous restoration and service
integration proposals: make the centroid Life implementation the new training
method, remove the old APIs, and keep executable implementation entirely in C11.
Do not restore, port or replace material removed from Git.

The [next deliverables](centroid-next-deliverables.md) now govern execution.
HTTP/Node/TypeScript/JavaScript/Python, worker IPC, the database application and
script-driven workflows leave the active product. Life's browser viewer is also
retired. The new native API replaces old public interfaces; retained C11 model
math, inference and formatters are private implementation inputs. Relevant
source admission, ownership, memory bounds, continuation, release integrity and
independent quality requirements carry into the new native contracts. This is
a planned migration; existing source has not yet been deleted by this audit.

## How conflicting sources are resolved

Current user direction defines the goal. Executable public contracts and current
source establish implemented behavior; checked release artifacts establish their
recorded outcomes. Maintained guides explain those contracts. Older roadmaps
provide candidate requirements, not proof of implementation or performance.
When source, fixture or evidence is absent, keep the useful design requirement
and record the gap. A newer timestamp alone does not make a claim authoritative.

Use these dispositions within a document:

- **Keep:** current contracts and enduring constraints needed by the new work.
- **Carry forward:** useful unfinished requirements, clearly labeled planned.
- **Replace:** superseded instructions, incorrect scope or unsupported current claims.
- **Historical:** frozen recipes, earlier baselines, consumed audits and bounded
  measurements retained with their original scope.
- **Retire:** superseded APIs, service architecture and non-C executable workflows;
  these are not restored or rebuilt as prerequisites for Life training.

Documents can contain several dispositions. Preserve accepted release identities,
failed experiments and original criteria. Do not relax an old contract to fit the
new method or rewrite an old failure as success.

## Documentation coverage and disposition

| Document read in full | Retained contribution | Superseded or limited contribution |
| --- | --- | --- |
| [Repository overview](../README.md) | C11 centroid models and honest quality status | API-first HTTP product is superseded; old fixture scores remain scoped evidence. |
| [Documentation index](README.md) | Implemented/partial/planned vocabulary and source-backed claims | Declarations or a guide's presence do not establish a runnable workflow or passing result. |
| [Architecture](architecture.md) | Native ownership, immutable models, formatter and evidence semantics | Service execution is retired; joint updates need a separate gate within the Life engine. |
| [Baseline API contract](api-contract.md) | Historical behavior and useful bounded-input/error lessons | Routes and count-composition API are retired, with no HTTP replacement in this plan. |
| [Development](development.md) | C11, boundary checks, strict lint/format, standard Doxygen and isolated tests | Node/addon/Python workflow and documentation helpers are retired; configured CI is not a new passing run. |
| [Reviewed codebase brief](codebase-brief.md) | Snapshot-specific citations, durable context, static-investigation limits and reviewed next-step selection | Importer references are unavailable here. Source expiry is a five-minute/day heuristic, not an unconditional one-day rule. |
| [Neural guide](neural-centroid.md) | Mathematics, formats, ownership, continuation, constrained heads, resources and scholarly admission | Tiny fixtures and next-token metrics do not establish assistant quality. Tokenizer and fixed expert distributions are replacement candidates. |
| [Chat service](chat-service.md) | Structured native chat, evidence requirements, reviewed data and bounded historical quality reports | HTTP jobs, Node workers and service integration are retired; missing bootstrap assets are not restored. |
| [Chatbot roadmap](chatbot-plan.md) | Dialogue/data/support requirements and independent quality gates | Service delivery sequence is superseded by the C11 Life deliverables. |
| [Research and memory](research-memory.md) | Source support, query minimization, provenance, retention, forgetting and offline reviewed learning | Two-query example is superseded by one-query policy. Public cache, richer memory states and semantic support calibration remain planned. |
| [Repository chat guide](repository-chat.md) | Attribution, input scope and distinction between recommendations and execution evidence | Snapshot/service preparation, capture and fixture workflows are retired, not repaired. |
| [Repository chat plan](repository-chat-plan.md) | Data exclusions, independent verification and bounded-context lessons | HTTP conversation loop, Compose and missing-input restoration are superseded. |
| [NPC v1 guide](npc-planner.md) | Observations/actions, own-history comparisons, leakage checks and frozen evidence | First-candidate repair schedule is not generic current training. Its failed, consumed audit remains historical. |
| [NPC v2 guide](npc-planner-v2.md) | Accepted comparator, successive-policy collection, authoritative replay and immutable provenance | Round count, balancing loss, corpus caps and timings are profile-specific. Acceptance did not establish planner or assistant equivalence. |
| [NPC v3 guide](npc-planner-v3.md) | Isolated source identity, exploration, observed roles and outcome-based specialization tests | Failed complementarity remains failed. Fresh initialization with a v2 comparator does not establish causal encoder transfer. |
| [NPC pilot epic](npc-planner-epic.md) | Information-equivalence audits, label/coverage checks, resource accounting and release integrity | Completed v1 instructions and numerical budgets remain its frozen specification, not a universal backlog. |
| [Centroid Life](centroid-life.md) | Participant optimization, replay, consolidation, identity coordination and exact continuation | Cell encoding, actions and tick thresholds are domain-specific. Separation is not splitting; retiring slots is not proven byte compression. |
| [Parity plan](centroid-training-parity-plan.md) | Verified teaching, isolation, consolidation and scoped capability evaluation | Rewritten for a world-driven C11 training engine; old service/restoration milestones are superseded. |
| [API package guide](../persistence/api/README.md) | Historical native bridge and service behavior | Package, bridge and commands are retired; no replacement HTTP package. |
| [Database package guide](../persistence/db/README.md) | Immutable-artifact and expected-parent publication lessons | Prisma/PostgreSQL application and migration workflow are retired; local C11 bundles have their own contract. |

## Public and machine readable contracts checked

The full installed headers are [core](../include/centroid_gai.h),
[ABI](../include/centroid_gai_abi.h), [neural](../include/centroid_gai_neural.h),
[chat](../include/centroid_gai_chat.h), [gameplay](../include/centroid_gai_gameplay.h),
[bark](../include/centroid_gai_bark.h), [spatial](../include/centroid_gai_spatial.h)
and [knowledge](../include/centroid_gai_knowledge.h).
Their ownership, allocation, output-mask and representation lessons remain
applicable. Their public API compatibility is superseded by the new native
interface. Static knowledge/spatial retrieval is distinct from learned models;
proximity or exact duplicates do not establish semantic correctness or compatible
centroid coordinates.

Supplementary checks covered the [task registry](codebase-tasks.json), shared
[chat](../persistence/shared/chat.ts), [research](../persistence/shared/research.ts),
[repository](../persistence/shared/repository.ts),
[grounding](../persistence/shared/grounding.ts),
[artifact](../persistence/shared/artifacts.ts) and
[quality](../persistence/shared/chat-quality.ts) contracts; current service,
worker, native bridge and repository paths; Prisma/migrations and Compose;
CMake/addon build inputs and CI; and selected authored gameplay contracts,
model recipes, heads, release manifests and rejection reports.

The old registry records follow-up calibration implemented but not verified.
Its source-service backlog is now historical, not the new training queue. Carry
native resource measurement and reviewed data into D6 without reviving service
tasks. Preserve the distinction between reports and independently captured checks.

## Requirements inherited by the parity plan

| Requirement group | Source contracts retained | Destination in the parity plan |
| --- | --- | --- |
| Information and data admission | Visible-history equivalence, no hidden answers in masks, conflicting-label audits, training-only initialization, source licensing, deduplication and content/split-bound review | D2, D4–D6 |
| Causal evaluation | Each controller causes its own history; training guards do not protect audit execution; failures stay in denominators | D3, D4, D6 |
| Training reconstruction | Bind initialization, collection predecessors, corpus order, admissions, teacher and optimizer recipe; reconstruct separately from resume | D2–D3 |
| Native integration | Ownership, independent scratch, legal masks, fallback, unused fields and irrelevant-input invariance | D1, D4–D5, D8 |
| Local lifecycle | Pinned model identities, explicit completion/error/cancellation and expected-parent publication | D3 and D8; no service job/IPC compatibility requirement |
| Source retention | Support/freshness/applicability, correction, forgetting and no arbitrary live gradients | D2, D6, D8; public network research is not a first-delivery prerequisite |
| Resources and scheduling | Complete adapter costs, peak process memory and preparation/inference budgets | D1–D3, D6 |
| Release and audit integrity | Writer locking, durable audit consumption before consultation, immutable bundles, identity rechecks and compatible-toolchain replay | D3, D6–D7 |
| Deferred useful work | Richer synthesis, calibrated support, typed gameplay extensions and native engine/tool interfaces | D9 or later, each with its own contract and evidence |

## Retired missing inputs

These absences were verified in the checkout. They are recorded for historical
accuracy, not prerequisites to restore, port or replace:

- `tools/knowledge/`: snapshot bootstrap, verification, operator capture and the
  `tools.knowledge.e2e_repository` fixture module called by the E2E runner.
- `examples/knowledge/codebase.json`: snapshot inclusion/exclusion policy.
- `examples/api/`: documented baseline and repository request files.
- `examples/chat/repository-scenarios-v1.json`,
  `repository-followups-v1.json` and `repository-research-v1.json`.
- `examples/evaluation/repository-questions-v1.json`.

Dependency points remain in the [Compose E2E wrapper](../persistence/e2e/compose-e2e.ts),
[conversation runner](../persistence/api/src/evaluation/repository-chat-runner.ts)
and [single-turn evaluator](../persistence/api/src/evaluation/repository.ts).
Those wrappers, the Compose overlay, TypeScript service and package scripts are
retired scope. The old `knowledge/codebase/codebase-v1` release is also absent.
Local ignored `data/chat/releases/repository-v1` exists; it is not a portable
authoritative input or a reason to revive the workflow.

D6 authors the new Life-native event packs from available inputs with their own
source/family/split contracts. It does not recreate the deleted importer or its
fixtures. Relabeling previously consulted answers cannot create a fresh audit.

## Useful scope retained for later work

Keep calibrated support, fact-specific freshness and native engine interfaces
as possible later requirements. Any future network, multi-owner or tool runtime
would need a separate C11 contract; the old service is not rebuilt in this plan.
Typed encounter/loot, quest/room and procedural-generation tasks retain legality,
reachability, balance and diversity tests. They are not prerequisites for the
first grounded-text experiment.

The earlier API-first architecture is superseded. The initial native trainer
does not require a chat frontend or transformer. Life's browser viewer is retired
in favor of C11 inspection; later representation changes require measured evidence.
