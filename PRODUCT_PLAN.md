# Centroid: installable native AI SDK

Status: M0–M6 first SDK gates implemented and verified.
The owner approved MIT licensing for SDK code and original model assets.
Updated October 6, 2026.

## Main goal

Build an installable, local, strict C11 AI SDK that lets applications use
task-specific centroid models for code assistance, conversation and gameplay.
Train and improve those models through the Life-authorized learning method using
attributed source, work and LLM context, independently verified targets and
measured candidate promotion. Applications must be able to deploy frozen models
without installing the trainer, research suite, a script runtime or a hosted
service.

This document defines the product direction and delivery order.
[AGENTS.md](AGENTS.md) defines implementation constraints.
[The research roadmap](research/ROADMAP.md) records implemented contracts,
experiments and their interpretation limits. The
[archived blueprint](research/archive/FRESH_START_BLUEPRINT.md) remains a record
of the earlier bounded release; closing it did not complete this SDK goal.

The SDK supplies a common runtime and training contract. Code, chat and game
models have separate feature schemas, outputs, data and acceptance criteria.
There is no assumption that one small model will perform all three tasks well.

## First product and support scope

The first reference application is a native code-context helper for an existing
LLM assistant. It indexes an application's admitted source and work context,
returns bounded evidence with provenance, and lets the host include that evidence
in its own LLM requests. A deterministic retrieval baseline is useful by itself;
a learned ranking or selection feature earns a separate quality claim.

Initial supported deployment targets are Windows and Linux on x86-64 CPUs.
Validate Windows with MSVC and clang and Linux with GCC before advertising
support. Ship static and shared C libraries and a relocatable CMake package.
The SDK exposes a C interface usable by other hosts. All project-authored
executable examples, adapters, training, evaluation and tooling remain C11;
foreign-language applications and game engines remain external hosts.

Chat and gameplay use the same runtime boundary, with their own task models and
integration examples after the first code helper. GPU kernels, additional CPU
architectures, broad autonomous patch generation and fluent standalone generation
are later tracks that require their own evidence and resource budgets.

## Implemented first-product scope

| Area | Implemented contract | Evidence and remaining scope |
| --- | --- | --- |
| Native build | Independent Runtime and optional Training/tools; static/shared installation and relocatable CMake exports. | Native Windows MSVC/clang and Linux GCC consumers; declared CRT/system ABI dependencies. |
| Model | Immutable value-only inference, caller outputs and independently identified task profiles. | Runtime links no trainer, backward kernels, process launcher or research fixtures. |
| Durable state | Strict trainer checkpoints plus canonical portable deployment bundle v1. | Transactional validation and cross-toolchain prediction/action agreement; continuation remains strict. |
| Context | Exact versions, attribution, split quarantine, bounded evidence and explicit formatting. | Installed native host; fixed retrieval/abstention, latency and memory measurements. |
| Learning | Life-authorized updates and an independently qualified bounded search-selection feature. | Frozen protocol, retained comparators, portable reference asset and native reproduction recipe. No Life-superiority claim. |
| Applications | Provider-free evidence helper and explicit learned finite-action demonstration. | Host owns LLM connection and execution; chat and gameplay remain subsequent separately gated tracks. |

The current quoted-answer path uses fixed encoding and retrieval rather than
learned model prediction. Its correctness does not demonstrate a learned ranking
benefit. The existing byte generator and code chooser do not establish general
conversation or coding ability. Life cell interventions control the training
world; they are not already a gameplay policy. Retained positive, negative and
tied results remain in the excluded research area, linked from the roadmap.

## Deployment architecture

| Component | Responsibility | Dependency boundary |
| --- | --- | --- |
| `Centroid::Runtime` | Validated bundle loading, immutable model inference, bounded context/evidence and task outputs. | C runtime and declared system libraries; no trainer, backward kernels, process execution or research fixtures. |
| `Centroid::Training` | Life world, owned optimizer state, replay, continuation checkpoints and explicit candidate export. | Runtime plus training internals; production parameter mutation remains in `src/life/trainer.c`. |
| Native tools | Capture, verified trials, evaluation, export, inspect and release preparation. | Optional training/tooling components and explicitly declared compiler/OS dependencies. |
| Host adapters | Translate host requests or game observations into a registered profile; return evidence or action scores. | Runtime; host owns LLM requests, game actions and application policy. |
| Model assets | Frozen weights, profile identities, compatibility metadata and qualification record. | Separate from SDK binaries and private application context. |

Extract forward kernels and a neutral value codec from the current mixed model
and experiment modules. Merely hiding trainer symbols in a shared library does
not establish the runtime boundary. Preserve the existing repository-facing APIs
through a documented migration layer while the installed API is introduced.

An installed host loads a compatible model and creates its own context/session
or scratch state. It submits observations, receives typed evidence or scores,
and decides what to do with them. Inference does not mutate model parameters or
execute commands. Training and publication are explicit operations outside the
request or game-frame path. A host adopts a new validated bundle at a defined
boundary and can retain the prior model for rollback.

## API and artifact contracts

### C API

Use opaque handles, explicit byte lengths, fixed-width IDs, status codes, export
and calling-convention macros, and sized/versioned option structures. Specify
ownership, output lifetimes, allocation rules and capacity failures. Memory
allocated by the SDK must be released through the SDK; do not expose compiler
dependent structures or require hosts to use private headers.

Models are immutable and shareable after validation. Context and session state
are independently owned, with documented synchronization requirements. Define a
caller-provided scratch path for bounded inference without per-call allocation.
Context ingestion and indexing happen outside a game frame. Reject unsupported
profiles, exhausted capacity and invalid inputs explicitly rather than truncating
or reinterpreting them.

Version the library API, binary ABI, model format and task recipe independently.
An ABI promise applies to a declared OS/architecture/ABI combination; it does not
make one binary portable between Windows and Linux. Define the compatibility
policy before the first stable release and test it with installed consumers.

### Deployment bundles

A deployment bundle contains only what inference needs: canonical parameter
values, model/parent identities, feature encoder and framing identities, token
or action mapping, dimensions, routing/owner information, resource limits and
qualification/provenance references. Optional application context is a separate
asset; private source and work history are not automatically included in a
redistributable model.

The loader validates the entire candidate, including lengths, bounds, finite
values, supported recipes, semantic identities and integrity, before making it
available. A failed replacement preserves the incumbent. Checksums identify
bytes; they do not replace provenance or quality evaluation.

Trainer checkpoints continue to contain optimizer moments, clocks, world, RNG,
queues and receipts under their existing exact-continuation restrictions.
Sessions and checkpoints must not simply be renamed deployment bundles. Portable
inference decoding needs its own format and numerical agreement tests across
declared toolchains. Cross-toolchain inference tolerance is a different contract
from bit-identical training continuation.

### Task profiles

Every profile declares its input semantics, encoder, eligible owners, head/action
catalog, legal-action mask, output interpretation, teacher and evaluation recipe.
Preserve legacy CODE/TEXT semantics. Add a versioned supervision interface for
new numeric or structured tasks instead of using code action IDs as NPC labels.

Similarity and action mass are scores, not calibrated truth probabilities.
Host APIs return the profile identity, provenance and an explicit unsupported or
abstention result where appropriate. A confidence claim requires calibration
evidence for that profile.

## Learning and publication loop

1. Capture complete source, requests, LLM proposals, tool results and work activity
   with immutable versions, identities and attribution. Use bounded views for
   retrieval while preserving original bytes and explicit omissions.
2. Admit only permitted TRAIN material. Keep DEV/AUDIT and opened research results
   outside training and retrieval. Refresh source versions before deriving tasks.
3. Derive targets from independent verified outcomes: tests and measurements for
   code tasks, or a registered simulator/teacher for game tasks. LLM suggestions
   and activity remain context; neither automatically becomes a target.
4. Fork a candidate from an identified parent and train under the registered
   Life recipe. A physical B3/S23 encounter authorizes every production parameter
   update. Preserve owner/head isolation and single-consumption receipt rules.
5. Evaluate against frozen comparators with preregistered quality, retention and
   resource gates. Retain every attempted candidate and failure. Shared learning,
   cell policy and merges keep their existing explicit permission and contact
   requirements; research candidates never silently replace the default recipe.
6. Publish only an accepted, explicitly selected candidate as an inference bundle,
   with its evidence manifest and rollback parent. Opening an AUDIT result does
   not make that family available for a new independent trial.

Consuming development work through attributed centroid context remains part of
the workflow. It must not turn all visible bytes into training material or
overwrite the independent verification requirement. A complete record can be
retained while its bounded retrieval view omits data for capacity or split rules.

## Delivery milestones

Milestones are acceptance gates, not calendar estimates. M0 precedes boundary
work; M1 and M2 establish the runtime; M3 makes it installable; M4 exercises it in
a host; M5 qualifies learning; M6 packages the first complete product release.
The first-product technical evidence is in
[SDK_RELEASE_REPORT.md](research/sdk-release/SDK_RELEASE_REPORT.md),
[M4_REPORT.md](research/sdk-release/M4_REPORT.md) and
[M5_REPORT.md](research/sdk-release/M5_REPORT.md).

The original learning trial registered resource thresholds before evaluation;
its timing host was recorded retrospectively. That historical limitation is
retained. The refreshed deployment-resource candidate uses the separate
[prospective host protocol](research/sdk-release/RESOURCE_PROTOCOL.md), without
changing the frozen learning recipe or treating opened AUDIT as new evidence.

| Milestone | Current acceptance status |
| --- | --- |
| M0 | Complete: API/ownership, bundle and resource contracts frozen; SDK code and original model assets licensed under MIT. |
| M1 | Complete: independent Runtime, preserved legacy training contracts and symbol/dependency inspection. |
| M2 | Complete: read-only export, canonical transactional loader, malformed-asset tests and fixed conformance vectors on three toolchains. |
| M3 | Complete: installed/relocated native consumers, static/shared libraries and exact-version CMake package. |
| M4 | Complete within the fixed deterministic evidence-helper scope; standalone installed host and reproducible measurements. |
| M5 | Complete within the registered finite search-selection profile; independent usefulness, retention/resource gates and deployable asset. |
| M6 | Complete: MIT-licensed source/platform/reference-model assets, checksums and installed training/qualification/export/adoption/rollback evidence. |

### M0 — Freeze the first product contract

Write the runtime API/ownership contract, bundle schema and code-helper profile.
Inventory public symbols, dependencies and compatibility obligations. Define the
first resource envelope and benchmark machines before measuring candidates.
Choose a project license and record redistribution permissions for code and
model inputs before publishing packages. The owner selected the
[MIT License](LICENSE) on October 6, 2026 for SDK code and original model assets.
The reference model uses this project's native synthetic workload and measured
algorithm outcomes; its identities and license are recorded in
[the asset manifest](models/reference-manifest.json). Preserve existing source
and research artifact policies.

Done when a host developer can identify the installed components, supported
platforms, expected asset, output meanings, error behavior and limits. Record
unresolved release decisions explicitly; do not invent a license or performance
promise while implementing the foundation.

### M1 — Extract the inference runtime

Split forward inference and value-only model state from gradients, optimizer
state and update kernels. Separate the runtime value codec from experiment-owned
continuation codecs. Classify context/session operations so capture, subprocess
execution, source-training orchestration and evaluators remain optional tools.
Introduce runtime and training targets without moving production mutation out of
`src/life/trainer.c` or weakening existing authority checks.

Done when a runtime-only build links with no Life trainer, backward/update
kernels, process launcher or `data/audit` dependencies. Inspect dependencies and
symbols and build a minimal native consumer. Existing mathematical, isolation,
continuation and verification regressions must still pass in the full build.

### M2 — Export and load portable inference assets

Implement explicit native export from an accepted or clearly labeled experimental
checkpoint, a bundle inspector and a transactional public loader. Separate the
bundle's compatibility/version rules from training continuation. Add fixed
conformance inputs covering framing, routing, all supported heads and boundary
conditions.

Done when export/load preserves predictions on the source build and meets declared
numerical tolerances and action-decision rules across the support matrix.
Unsupported schemas, mismatched profile identities, truncated or corrupt bytes,
invalid values and capacity overflow are rejected without replacing a loaded
model. The parent checkpoint remains unchanged and still resumes under its
original contract.

### M3 — Install and validate the C SDK

Install only supported public headers and libraries, with optional training/tools
components. Build static libraries, Windows DLLs and Linux shared libraries with
controlled public exports. Supply versioned `CentroidConfig.cmake` and exported
targets. Follow the official
[CMake package/export and relocation guidance](https://cmake.org/cmake/help/latest/manual/cmake-packages.7.html).

Done when a separate strict C11 project uses
`find_package(Centroid CONFIG REQUIRED COMPONENTS Runtime)` and links
`Centroid::Runtime` from an installed prefix. Move that prefix and repeat the
build without source/build-tree paths. Check static/shared configurations and
declared toolchains. Neither installation nor runtime inference may require
research outputs, training inputs or a trial compiler. Building a C application
still requires its normal native development toolchain.

### M4 — Deliver the first code-context host

Add a native installed-consumer example that ingests a source root through an
explicit policy, refreshes versions, and returns top evidence within a caller's
byte budget. Results include source identity, version, span, attribution,
score, omitted counts and abstention. Keep original bytes separate from prompt
formatting. Include dirty-source refresh and independent-context examples.

Guarantee byte limits in the first interface. A later token-budget mode requires
a versioned host-supplied tokenizer/counter, exact formatted-context counting and
declared prompt-overhead reservations. The SDK's byte/EOS mapping does not count
tokens for an arbitrary host LLM. The host accounts for the rest of its prompt.

The host integration accepts and returns bounded context through a documented C
interface; the host owns its LLM connection. The example must run locally without
a provider. Demonstrate deterministic retrieval first and label its behavior
accurately. Any external model used to measure downstream coding is a declared
evaluation dependency with fixed settings.

Done when the installed example works outside this repository, preserves source
provenance, obeys all budgets and split exclusions, and has reproducible retrieval
quality, abstention, latency and memory measurements. Accurate excerpts alone
are not evidence of improved LLM coding or trained centroid usefulness.

### M5 — Qualify the first learned feature

Choose one bounded code-helper feature, initially evidence ranking or verified
finite-action selection. Establish a deterministic baseline, frozen-model
baseline and matched learning comparisons before selecting a recipe. Introduce
new representations or capacity only through isolated candidates with declared
permissions and retention tests.

Register fresh TRAIN/DEV/AUDIT families, seeds, denominators, quality margins and
resource limits. Tune on TRAIN/DEV, then open the fresh AUDIT once for the stated
decision. Compare Life with the registered alternative schedule under explicit
cost accounting; matching update counts alone does not match total computation.
For an LLM improvement claim, also compare actual held-out coding outcomes with
the same host model, settings and context budget.

Done when at least one learned feature meets its independent usefulness,
retention and resource gates and can be reproduced, exported and deployed.
Ties and failures remain results, not permission to advertise an improvement.
The deterministic helper can ship earlier as a baseline SDK example; it does not
close this learned-feature milestone. Life superiority is a separate claim that
requires a successful comparative gate.

### M6 — Publish the first complete SDK product

Prepare source and platform SDK packages, documentation, supported-version matrix,
model qualification manifests and checksums. Supply a legally redistributable,
qualified small reference model as a separate release asset and a native recipe
for producing a compatible local model. The ordinary source checkout remains
small and contains no private work context or historical model/run outputs.

Done when a clean machine can install the package, load the supplied model,
integrate the native code helper, explicitly train an isolated local candidate,
evaluate/export it and replace or roll back the deployed model. Verify the
complete path using installed components and retain the release evidence.
An end-user inference installation needs no training compiler or research data.

## Subsequent application tracks

| Track | First bounded deliverable | Required evidence before a capability claim |
| --- | --- | --- |
| Chat memory and evidence | Native session API for conversation state, source evidence, abstention and optional host LLM context. | Provenance, isolation, budget behavior, persistence compatibility and supported-answer measurements. A fluent standalone centroid chatbot needs a separate generative representation and sequence-quality gate. |
| Gameplay policy | Numeric observation profile, finite legal actions, caller-owned scratch, deterministic fallback and a native simulation example. | Held-out maps/seeds, reward and failure outcomes against a fixed conventional policy, owner retention and frame-time/memory limits on stated hardware. |
| Broader code assistance | Wider evidence/ranking tasks and independently verified action catalogs. | Fresh project/task families, verified outcomes, abstention and retained failures; broad generated edits require additional execution and publication contracts. |

The first game example uses a headless native simulator, keeping engine setup out
of the core milestone. Select an actual engine adapter afterward. Godot exposes
a native C GDExtension route in its
[official C example](https://docs.godotengine.org/en/stable/engine_details/engine_api/gdextension/gdextension_c_example.html);
an adapter must declare the supported engine API/version and its external build
dependencies. Engine compatibility is tested separately from runtime compatibility.
Do not place model training, context indexing or blocking tool calls in a frame.

Each track can proceed once its runtime/profile dependencies exist. None may
rename the Life world policy into a gameplay model, present retrieved quotations
as free-form generation, or infer product quality from infrastructure tests.

## Implemented source layout

The runtime is extracted without deleting the verified legacy foundation.
Gameplay paths remain later deliverables.

```text
include/
  centroid_runtime.h             immutable inference/bundle surface
  centroid_context.h             bounded context/evidence surface
  centroid_code_helper.h         registered finite search profile
  centroid_training.h            optional training/export surface
src/
  runtime/                      immutable model, bundle, evidence and search
  context/                      legacy durable record/workflow compatibility
  life/trainer.c                sole production parameter mutation authority
  life/                         training world, policy and replay
  platform/                     optional capture/process services
  experiment/                   optional verified trials and research
cli/                            native workflow/export/inspect commands
cmake/                          install/export/package configuration
examples/code_helper/           installed C consumer
examples/game_policy/           later native simulator and host boundary
tests/                          native contract and installed-consumer checks
research/                       protocols, reports and artifact identities
```

Keep authored source, headers, native fixtures, build descriptions, concise docs
and protocols in Git. Follow [research/ARTIFACTS.md](research/ARTIFACTS.md) for local
evidence. Distribute qualified binaries/models through separately versioned
release assets with committed metadata; that distribution channel is future work,
not an existing backup. Historical ignored assets are not fresh-install inputs.

## Completion and next implementation slice

The first product goal is complete when M0–M6 pass: an external application can
install the C SDK, load a qualified frozen model, use an independently measured
learned feature, and reproduce the explicit Life training-to-publication loop.
Chat and gameplay remain separately gated expansions of this same primary goal.
SDK packaging success and model usefulness are recorded independently.

M0–M6 now have concrete implementation, MIT-licensed release assets and retained
evidence. Archive identities and detached installation checks are bound by the
local release manifest named in the release report. Chat and gameplay proceed
under their own protocols and acceptance criteria; first-product evidence is
not an automatic qualification for either application. The archives are prepared
release assets; no remote hosting or artifact backup is established by this work.
