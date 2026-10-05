# Centroid Life: fresh-start goal and native C codebase

Date: October 4, 2026. Status: proposed architecture and research plan.

This document is standalone. It describes a fresh repository and does not require
the old source tree, installed APIs, model files, or directory structure.
Its design is a recommended starting point; numerical choices and the value of
Life scheduling still require experiments before they can be called optimal.

## 1. The goal

Build a local, native C system that learns useful representations and bounded
coding decisions from our own codebase, development activity, and explicitly
injected LLM context. Centroid specialists train through a real Game of Life
encounter process. The system retrieves attributed evidence, proposes changes,
measures their effects, and improves from independently verified training results.

Replace project functionality written in other languages with native C
implementations whose observable behavior is tested. Every project-authored
runtime, trainer, workflow, evaluator, test, and inspection tool should be C.
CMake, CI configuration, documentation, and data remain declarative inputs.
Compilers, operating systems, Git, and build/checking tools remain declared
external tools; this project does not need to recreate those toolchains.

**Reusable project directive:**

> Build a local native C centroid learning system whose production training is
> driven by genuine Life contacts. Consume our source, visible development work,
> corrections, and attributed LLM context through a provenance-aware centroid
> memory. Port any required non-C project functionality to tested C equivalents.
> Learn coding choices only from independently measured training outcomes,
> preserve exact continuation, and demonstrate gains on frozen independent tasks
> before expanding capability or claiming autonomous coding competence.

## 2. What success means

The first useful release must:

1. Build and run locally on Windows and Linux without Python, Node, Bun, a browser,
   a database service, or a hosted LLM.
2. Ingest actual working-source bytes, including uncommitted changes, with paths,
   spans, versions, coverage, and content identities.
3. Accept attributed LLM suggestions and captured development activity without
   confusing either with verified task answers.
4. Make authentic physical Life contacts necessary for production model updates.
5. Keep unrelated parameters, optimizer moments, decay, and update clocks unchanged.
6. Save and resume the complete training state, including pending measurements.
7. Return exact supported excerpts or explicitly abstain when evidence is absent.
8. Perform one bounded code-improvement experiment with isolated candidates,
   independently measured feedback, retained failures, and a reviewed winning patch.
9. Report quality, retention, coverage, runtime, and memory on frozen task families.

Passing infrastructure tests establishes a working mechanism. Useful model
improvement requires task-quality evidence in addition to those tests.

## 3. Research before choosing the final model

Resolve these questions with small native experiments before building a large
assistant or adding more infrastructure.

| Question | Experiment | Decision evidence |
| --- | --- | --- |
| Does Life scheduling help? | Compare learned Life, frozen Life, and a deterministic alternative scheduler using the same model, records, optimizer, and update budgets. | Task quality and retention, plus scheduling cost and total compute. |
| Do learned world edits matter? | Disable edits while preserving the other recipe inputs. | Changed contacts, coverage, and downstream task results. |
| Are multiple specialists useful? | Compare owned cohorts with a matched single-expert model and routing/eligibility ablations. | Complementary behavior and measured benefit under comparable resources. |
| What representation preserves code? | Compare bytes with lossless byte/subword tokenization on training-only source families. | Exact reconstruction, context cost, code validity, and held-out task quality. |
| What context actually helps? | Compare source-only, source plus LLM proposals, and source plus attributable work history. | Independent gains without target leakage. |
| Can experts merge safely? | Evaluate an isolated merge candidate on parent-task retention and independent tasks. | Preserved useful behavior and a measured storage, inference, or quality benefit. |

Report every attempted case. Match both optimizer-update budgets and total
compute where possible; record any remaining mismatch. Fix acceptance margins
before opening held-out results. Different seeds of the same fixture are useful
regressions but do not establish transfer to independent families.

### Primary research starting points

- [Growing Neural Cellular Automata](https://distill.pub/2020/growing-ca/)
  studies learned local update rules for growing and maintaining patterns.
  It is relevant background, but it does not validate this proposed Conway
  encounter scheduler or establish coding improvement.
- [Neural Machine Translation of Rare Words with Subword Units](https://aclanthology.org/P16-1162/)
  introduces subword segmentation for open-vocabulary translation. Study its
  segmentation approach; choosing a lossless representation for source code
  remains our own design decision and benchmark question.
- [Adam: A Method for Stochastic Optimization](https://arxiv.org/abs/1412.6980)
  provides the adaptive-moment optimizer reference. Independent participant
  clocks, ownership masks, and transactional updates are additional project
  requirements to verify explicitly.

Keep research papers, licensed source references, hypotheses, and results in a
small research ledger. Label recommendations, measured results, and unresolved
questions separately. Reading a paper is not permission to import its benchmark
answers into searchable or trainable memory.

## 4. Core architecture

Use one native library, one thin CLI, one provenance-aware event format, and one
production training authority. Start with synchronous execution and a CPU scalar
reference implementation. Add parallelism or optimized kernels only after
correctness and resource measurements justify them.

```text
working source + visible work + attributed LLM context
                         |
                  native ingestion
                         |
             immutable records and provenance
                         |
              centroid context retrieval
                         |
                 bounded task queue
                         |
           Life world and authentic contacts
                         |
             freeze complete proposal round
                         |
       independent target or candidate measurement
                         |
          participant-owned optimizer updates
                         |
         checkpoint + quality/retention evaluation
                         |
               reviewed candidate publication
```

The Life grid is scheduling state. Neural centroid coordinates are representation
state. Answer tokens, code-edit actions, and cell-edit actions have distinct
contracts and are never interchangeable IDs.

### Recommended module layout

```text
centroid-life/
  CMakeLists.txt
  README.md
  LICENSE
  AGENTS.md
  include/
    centroid.h                  # opaque public owners and bounded operations
  src/
    core/
      status.c                  # errors and operation outcomes
      limits.c                  # checked sizes and resource accounting
      hash.c                    # content identities with known-answer tests
      random.c                  # owned deterministic random streams
    platform/
      filesystem.c              # regular-file checks and atomic publication
      process.c                 # direct argv, deadlines, child lifetime, capture
    model/
      tokenizer.c               # lossless bytes first; optional native BPE later
      encoder.c                 # ordered, versioned context representation
      centroid.c                # eligibility- and mass-aware routing
      heads.c                   # task readouts and token probabilities
      gradient.c                # derivatives checked by finite differences
      optimizer.c               # one generic owned-scalar optimizer
    life/
      world.c                   # synchronous B3/S23 evolution and claims
      contacts.c                # causal encounters and stable participant UIDs
      policy.c                  # bounded cellular interventions
      trainer.c                 # sole production model-update authority
      replay.c                  # bounded, split-aware, participant-aware replay
      resolution.c              # separation; gated consolidation later
    context/
      records.c                 # source, proposal, activity, measurement, audit
      ingest.c                  # exact bytes, versions, exclusions, coverage
      retrieve.c                # frozen centroid lookup with exact attribution
      work.c                    # visible work capture and LLM context admission
    domain/
      adapter.c                 # common encode/propose/verify/gradient contract
      source.c                  # source-bound retrieval/reconstruction tasks
      text.c                    # ordered answers, roles, causal prefixes, EOS
      code.c                    # finite edit actions and measured utility
    experiment/
      candidate.c               # isolated candidate sources and parent lineage
      evaluate.c                # builds, tests, scoring, resource measurements
      checkpoint.c              # complete validated state and immutable bundles
      report.c                  # denominators, coverage, failures, provenance
  cli/
    main.c                      # parsing and dispatch only
  tests/
    unit/
    integration/
    fixtures/                   # excluded from automatic context ingestion
  research/
    questions.md
    references.md
    decisions.md
    experiments/                # frozen protocols and complete reports
  data/
    train/
    development/
    audit/                      # never scanned into training context
  build/                        # excluded generated outputs
```

These are responsibility boundaries, not a requirement to create every file on
day one. Add modules as their first implementation needs them. Avoid parallel
legacy backends, duplicate optimizers, alternate production trainers, and a large
plugin framework before the first end-to-end experiment works.

The numerical layer must work without Life or filesystem access. Life chooses
when and which owned slices may update. Domain adapters supply task meaning and
verification. Retrieval supplies inputs. Evaluation measures outcomes. The CLI
must not hide training, persistence, or subprocess logic.

## 5. Native C and API rules

Use strict C11 initially, with compiler extensions disabled and bounded explicit
ownership. Require the requested standard in CMake: its standard property can
otherwise fall back to an earlier supported level.
See [C_STANDARD](https://cmake.org/cmake/help/latest/prop_tgt/C_STANDARD.html)
and [C_STANDARD_REQUIRED](https://cmake.org/cmake/help/latest/prop_tgt/C_STANDARD_REQUIRED.html).

```cmake
cmake_minimum_required(VERSION 3.20)
project(centroid_life VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(CENTROID_BUILD_TESTS "Build native C tests" ON)
```

Public handles should be opaque: model, inference session, context store, trainer,
and experiment. Return explicit status values; document ownership, capacities,
mutability, and failure semantics. Validate all lengths and allocation arithmetic.
Allocate inference scratch before execution and expose bounded work quanta.
Keep operating-system differences in the platform layer.

Production training exposes trainer stepping, not an unrestricted train-all
function. Frozen inference must require no teacher targets, optimizer, world
advance, tool execution, or external LLM. Research comparisons may vary the
scheduler through a declared benchmark harness using the same numerical kernels;
they do not create an alternate public mutation authority.

A compiler and CMake/CTest are sufficient to build and test the reference system.
Formatting, static analysis, and sanitizers are development checks. Foreign-language
programs may appear as explicit migration inputs or optional reference comparators;
they must not become runtime requirements for the shipped native system.

## 6. Make Life genuinely responsible for learning

Start with a small bounded toroidal world and two to eight ownership groups.
Every group has a stable UID, a physical claim, disjoint owned parameter slices,
its own optimizer clock, and explicit routing eligibility. Parameter ownership
does not depend on whether its current world pattern remains alive.

Each production generation must:

1. Freeze the starting world, model version, queue, and applicable constraints.
2. Compute synchronous world evolution and authentic cross-lineage contacts.
3. Select compatible tasks using input-derived eligibility and deterministic quotas.
4. Freeze every participant's proposal for the complete admitted round.
5. Verify domain targets or execute isolated candidates and measure outcomes.
6. Validate the complete update candidate, including numerical and resource bounds.
7. Publish world, task state, parameter changes, moments, clocks, and receipts
   together, or preserve the generation's starting state.

The cellular policy may learn bounded cell interventions from its own lookahead
teacher. Domain experts learn task outputs from independent domain verification.
Report those objectives and their costs separately. Cell survival is not evidence
that an answer is correct or a code patch is useful.

Placement and encounter renewal must be independent of hidden targets and task
outcomes. Coverage reports show waiting tasks, starved groups, reseeds, deferrals,
and attempted updates. Extinction does not erase model weights. Persistent contact
does not force a merge. Consolidation is a later independently evaluated operation.

## 7. Centroid models and representations

Start with an ordered context encoder and centroid specialists with explicit
task heads. Use lossless byte tokens for the first source/code implementation;
preserve case, punctuation, whitespace, and exact reconstruction. Add native
subword tokenization only if measured context savings justify its complexity.

For structured text, preserve the complete current request, admitted evidence,
explicit role boundaries, causal answer prefixes, and an end-of-answer target.
Role markers are input-only. Keep separate dialogue examples independent.
Oversized current requests must fail clearly rather than silently losing their
beginning. Unknown words must remain representable through the byte path.

Map expert cohorts to ownership groups. In the initial local-update phase, freeze
shared embeddings, encoders, and biases; update only admitted cohort centroids and
readouts. Normalize routing over eligible experts with explicit supported mass.
An inactive group contributes no probability and receives no parameter or moment
update. Full eligibility with unit masses must match the unpartitioned reference.

Freezing a random shared encoder can limit model capacity. Study a later shared
representation phase through the same Life trainer, with its own permission in
the recipe, retained-task checks, and separately reported update budget.

Lexical centroid memory is a useful initial retrieval baseline. It is not semantic
understanding, a calibrated truth score, or a sufficient architecture for arbitrary
code generation. Start with supported excerpts and bounded code choices; widen
generation only after sequence quality and verification gates pass.

## 8. Consume our work without inventing supervision

Capture available visible user requests, LLM outputs, patches, source versions,
native argv, working directories, tool results, corrections, decisions, and
publication receipts. Treat the work ledger as authoritative about what occurred,
not about whether every assertion or proposed solution was true.

| Record kind | Meaning | Permitted learning |
| --- | --- | --- |
| SOURCE | Exact bytes with path, span, version, and content identity. | Reconstruction and reviewed source-bound tasks; eligible retrieval. |
| LLM_PROPOSAL | Attributed suggested reasoning, explanation, or patch. | Input/context representations; useful hypotheses, never automatic truth labels. |
| ACTIVITY | A visible action and its observed result. | Attributed context; utility learning requires a separate admissible measurement. |
| TRAIN_MEASUREMENT | Verified training outcome bound to frozen input, action, parent, and evaluator. | Participant-owned task or utility updates. |
| DEVELOPMENT / AUDIT | Reserved evaluation inputs and results. | Reporting and promotion gates; no fitting or retrieval used to train candidates. |

Use immutable event IDs and content identities. Changed bytes create a new version;
repeated identical admissions are idempotent. Retrieval must declare whether it
returns current versions or historical evidence. Keep raw outputs separately from
bounded model views, record omissions explicitly, and preserve exact provenance.

An external LLM can provide context through an explicit local file or native
adapter. The local system must function without that provider. Capture hooks are
required to consume editor or agent activity; there is no automatic access to every
tool or private application just because a work ledger exists. Unavailable activity
must be reported rather than invented.

Source scanning excludes secrets, dependencies, generated outputs, model artifacts,
and held-out fixtures. Stored events still need split and provenance checks before
becoming model inputs. An audit result may be retained in a quarantined ledger
without becoming searchable training context.

## 9. Actually migrate non-C functionality

Create a migration ledger with: component, language/runtime, observable contract,
inputs, outputs, errors, persistence behavior, reference fixtures, native replacement,
license/provenance, and completion evidence.

For each required component:

1. Capture its behavior and failure cases before removing the reference.
2. Implement the native C equivalent against that contract.
3. Compare outputs, error behavior, resource limits, and restart behavior.
4. Replace callers and verify the native-only dependency graph.
5. Retire the runtime dependency when the required parity checks pass.

Reading a Python or TypeScript file into centroid memory is ingestion, not a port.
Do not restore obsolete HTTP/database/snapshot services simply because the old
repository mentioned them. If the fresh product has no required non-C components,
record the empty migration inventory and focus on the learning mechanism.

## 10. Closed-loop code improvement

Begin with a finite catalog of meaningful edits to declared source sites. Each
action identifies a complete candidate and its parent; action IDs are unrelated
to Life cell toggles. Expand the catalog or admit LLM-proposed patches only after
the basic loop is reliable.

At an authentic contact:

1. Retrieve supported source and attributed LLM context from eligible memory.
2. Freeze the task input, alternatives, selected action, parent identity, and model.
3. Materialize the candidate in an isolated directory.
4. Compile and run required correctness, resource, and training-quality checks.
5. Calculate task utility from independently measured TRAIN outcomes.
6. Consume the measurement once to update only participating choice slices.
7. Use separate development/audit gates for candidate admission and publication.

A correctly attributed negative training result can teach even if the patch is
rejected. A timeout or infrastructure failure should defer when its cause cannot
be attributed. Build success alone is not a general-quality reward. Keep baseline,
rejected candidates, raw logs, frozen inputs, and evaluator identities.

Publish a reviewable patch or candidate artifact. Applying it to the working tree
is a separate explicit publication action. Start with one process and a complete
measurement ledger before attempting unattended multi-project editing.

## 11. Exact continuation and integrity

A checkpoint must preserve the world, cellular policy, domain model, ownership,
UIDs, routing mass, tokenizer/formatter identity, parameters, moments, clocks,
random streams, replay, task queues, coverage, pending external work, receipt
consumption state, and initialization/build recipe identities.

Use a versioned canonical codec with explicit integer widths, byte order, lengths,
finite-value validation, and whole-bundle integrity checks. Never serialize raw
struct padding or process pointers. Atomic same-directory publication preserves
the previous checkpoint on failure. Do not overwrite accepted experiment history.

Test continuous execution against split calls and a fresh-process restart.
Corruption, truncation, trailing bytes, incompatible recipes, and stale measurements
must preserve the incumbent. Exact floating-point continuation is a same-build,
compatible-numerical-environment guarantee; cross-platform comparisons need an
explicit tolerance protocol unless stronger reproducibility is demonstrated.

## 12. Implementation order and gates

| Stage | Build | Required exit evidence |
| --- | --- | --- |
| 0. Research | Hypotheses, task families, comparator contracts, budgets, and acceptance margins. | Written protocols before held-out consumption. |
| 1. Native foundation | C11 library/CLI, bounded process/filesystem helpers, source/work ledger, lossless tokens. | Native-only build; exact bytes and honest capture/exclusion checks. |
| 2. Life training authority | World, genuine contacts, ownership, derivatives, generic optimizer, replay. | Contacts change owned outputs; finite differences and unrelated-slice isolation pass. |
| 3. Durable state | Canonical checkpoints, pending receipts, atomic publication, frozen inference. | Continuous/split/restart equivalence and failure preservation. |
| 4. Useful context and text | Centroid retrieval, source-bound targets, structured causal sequence adapter. | Supported citations, abstention, changed answers, role/EOS correctness, independent quality report. |
| 5. Migration and code trial | Any required real language port, bounded edit catalog, native evaluator. | Required behavioral parity and one measured, reviewable code-improvement experiment. |
| 6. Independent evaluation | Fresh families, scheduler/routing ablations, resource and retention checks. | Complete reports establishing scoped gains or documenting failure. |
| 7. Expansion | Gated merging, larger representations, wider edits, local conversation interface. | Each addition earns its own quality, continuation, and resource gate. |

The first end-to-end demo should build a fresh model, ingest a small authored
training source family and LLM proposal, capture a local task, train at actual
contacts, save/restart, retrieve evidence, evaluate a bounded candidate, and record
its independently measured result. It must show actual model-output changes,
not just more stored records or advancing world generations.

## 13. Fresh-start instructions

Carry this document into the new repository as the initial specification.
Preserve any behavior fixtures, licensing information, or reference measurements
needed for claimed parity before deleting their only copy. Historical models and
results can be retained as optional evidence; the fresh runtime should not depend
on them or pretend to continue their training state.

After the research gate, create the minimal CMake project and public ownership
contract, then implement stages in order. Keep one authoritative roadmap and
update it from actual results. Do not scaffold an entire assistant stack or mark
the goal complete merely because ingestion, tests, or checkpointing work.

**Completion criterion:** the native system demonstrably learns useful supported
codebase behavior through Life-owned centroid updates, performs independently
verified local code improvements, and retains reliable behavior and continuation
on frozen independent tasks within declared resource limits.
