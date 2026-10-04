# Centroid Life training and capability plan

Status: **Planned**, following the user direction reviewed on October 3, 2026.

Centroid Game of Life becomes the training method for the newer centroid models.
Its cellular world owns contact scheduling, learned interventions, replay and
conflict resolution. Domain models learn actual gameplay actions or assistant
outputs from independently verified targets during those encounters.

Implementation, orchestration, evaluation, hashing, persistence, tests and viewing
are C11. CMake and CI YAML remain declarative build configuration. The breaking
public entry point is the proposed `include/centroid_life.h`. Existing public APIs
and training tools are retired through delivery; reusable private C numerical,
inference and formatting code remains behind the new core. The old stack is still
present in this checkout until those changes land.

Use [next deliverables](centroid-next-deliverables.md) for reviewable work packages
and [documentation reconciliation](centroid-documentation-reconciliation.md) for
retained evidence and superseded requirements. Removed Git assets have no recovery,
porting or replacement deliverable. HTTP, Node, TypeScript and database compatibility
are outside this new native implementation.

## Existing machinery and what changes

[Centroid Life](centroid-life.md) already implements synchronous Conway evolution,
four lineage identities, bounded learned cell edits, participant-restricted AdamW,
replay, validated consolidation and exact saved continuation. Its model predicts
23 cell-edit IDs. It does not train language answers or NPC navigation outputs.

The generalized trainer needs domain adapters and a separate cellular edit policy.
Applying the grid encoder to prose, or renaming an ordinary optimizer, does not
implement this method. Contact must cause real training; learned edits must change
later contacts; validated resolution must affect domain routing and ownership.

Current composed models have hierarchical routing and conditional task readouts.
Flat neural/chat models share an encoder and centroid-specific token distributions;
chat preserves structured prompts, evidence and causal answer slots. Their private
C components are reusable. Existing representation limits remain until separately
measured changes succeed.

## World, groups and scheduling

The first generalized scope supports at most eight ownership groups. Each group
has a stable UID, owned parameter slices and a world claim bit. A composed module
is one group. Flat models partition up to 128 experts into at most eight disjoint
cohorts recorded in the recipe. Every expert belongs to one cohort; cohort names
do not establish semantic specialization.

The initial world remains a bounded toroidal grid with synchronous B3/S23 updates
and bounded legal interventions. A cell claim byte represents eight groups. Grid
positions and claims are scheduling state, distinct from continuous neural centroid
coordinates. Generated answer tokens are not interpreted as cell-edit IDs.

The trainer owns world identities, context queues, replay, update clocks and the
auxiliary cellular policy. Physical cross-lineage contacts produce encounters.
Participants propose outputs for the same admitted domain context and frozen model.
An independently verified shared failure can teach even when proposals agree.
Without a compatible context or supported target, contact is recorded and deferred.

The auxiliary policy chooses bounded cell edits. Its initial teacher scores legal
edits through bounded cellular lookahead and declared viability/contact objectives.
Domain teachers independently check actions, answers or task outcomes. Cellular
survival is not a language-correctness target. Both policies' work and supervision
are reported and belong to the complete saved training state.

## Coverage and mapping contract

Queue records by complete context identity and adapter-declared eligible groups.
Eligibility derives from input/task semantics, not hidden teacher answers. Contacts
select the oldest compatible records using deterministic quotas. Every participant
sees the same observation or formatted dialogue. Preserve failed, rejected and
unscheduled contexts in the coverage report.

Pin initial patterns, edit bounds, queue order, tie breaking, random streams,
minimum coverage and maximum waiting generations. If an active group loses its
pattern or exceeds the waiting budget, a logged deterministic reseeding intervention
restores its contact opportunity in a reserved bounded region. Reseeding is an
explicit recipe operation, not an unreported neural update. Distinguish its cost
and frequency from learned changes to evolution.

Replay quotas retain every active group's task coverage. Reject a quality run that
silently starves groups or leaves required families unvisited. Defer contexts with
fewer eligible groups than the encounter requires; a singleton cannot pretend to
be a two-party conflict. The recipe declares a minimum active group count for
continued training; consolidation cannot invalidate that requirement.

World extinction never deletes weights or retires routing eligibility. Retirement
requires accepted model consolidation. Pattern births, deaths and reseeding remain
world events, while model activity has a separate validated state.

## Unified native contract

`include/centroid_life.h` declares opaque model, session and trainer owners. Private
adapters support composed, flat and structured chat layouts. Declare backend kind,
shape, task semantics, tokenizer/formatter identity, groups, resource limits and
input/output bounds.

Expose creation/loading, bounded prediction/scoring, trainer creation, generation
stepping, inspection, checkpoint save/load and weights-only serving export.
`cgai_life_train_step` advances a bounded quantum of the Life-owned state machine.
No public ordinary train-all or fresh-Adam operation bypasses encounters.

Events contain input/source/split identities, model version, participant UIDs,
restrictions, frozen proposals, verified target/outcome, teacher identity and
lifecycle. Typed payloads distinguish categorical actions, token sequences and
structured assistant answers. Validate lengths, vocabulary, roles, restrictions
and provenance before optimizer mutation.

Participant IDs and membership use bounded C arrays. Flat eligibility uses two
internal 64-bit words or a 128-entry array; a single 64-bit expert mask is insufficient.
Declare group capacity separately from expert capacity. Host validation retains
legal effects, safe fallback, category bounds and zeroed-unused-field requirements.

Inference borrows immutable weights and exclusive session scratch. It uses supplied
context, permitted outputs and exported learned routing state. Audit inference has
no teacher labels, target lookups, world-driven updates or replay admission.
Training-only scheduling data cannot select an audited answer.

## Learning and resolution

Freeze proposals for each generation, identify physical contacts, obtain checked
targets, validate edits and commit evolution before participant updates. Updates
affect the encounter's owned centroids/readouts and permitted heads. Unrelated
weights, Adam moments, decay and clocks remain unchanged. Shared representations
stay fixed during this local phase.

A separately gated joint phase may improve embeddings and encoders through the
same Life trainer and admitted replay. Its counters, schedule, retention tests
and loss of local isolation are explicit. It does not restore a parallel public
training API. Check masked/mass-aware derivatives with finite differences;
independently test clipping and Adam bias correction.

Keep unresolved, separated, coupled, absorbed/extinct-world and merged states
distinct. Useful separation retains groups whose different behavior improves tasks.
Repeated disagreement alone does not require merging. Unsupported or contradictory
targets remain deferred or retain their applicable contexts.

Consolidation trains a private candidate from verified targets and useful parent
behavior. Require parent/task coverage, actual constrained inference, old-task
retention and a measured quality, work or storage benefit. Preserve correct useful
behavior without requiring agreement with a known wrong parent.

Parents share tokenizer/formatter and encoder coordinate system, with compatible
heads. Matching dimensions do not align independently trained spaces. Later
alignment, cloning, splitting or capacity growth needs a separately evaluated
contract and is outside the first implementation.

For flat cohorts, routing mass is distinct from member count. Initial mass equal
to member count and a per-expert `log(mass/member_count)` offset preserve ordinary
flat routing. Validate later mass changes at actual inference with remapped
restrictions. Fixed-slot retirement does not itself reduce allocated bytes.

One accepted transaction covers domain weights, auxiliary policy, world claims,
identities, routing, clocks, queues, replay and ancestry. Rejection preserves the
incumbent. Child UIDs and parent lineage remain inspectable; maintain the declared
minimum training-group capacity.

## Checkpoints, serving and retirement

A versioned native checkpoint contains both models, optimizer arrays, per-owner
clocks, world/conflicts, groups/mass, queues/replay, sampling state and the complete
recipe/provenance. Bind collection predecessors and admission decisions. Reconstruct
training from initialization as well as comparing split execution with save/load
continuation in the recorded numerical environment.

A separate serving format includes inference weights, task/tokenizer/formatter
contracts, active groups, membership and mass, without optimizer or replay state.
Renaming `.cgnn`, `.cggp` or `.cgchat` cannot supply that metadata. Read-only native
imports of old neural weights may seed an explicitly new recipe; old artifacts
are not exact resume of Life training.

C11 code performs bounded parsing, hashing, writer locking, staging and atomic
publication through tested platform primitives. Validate finite numbers, shapes,
controls, counters and complete consumption. Recheck staged identities and compare
the expected parent head before publication. A changed parent rejects publication;
interrupted writes preserve the incumbent. Replace the current nonatomic Life
sidecar output with this complete release transaction.

Historical accepted hashes and measured failures remain evidence. Changed root
CMake/native dependencies create a new build/source identity, not rewritten old
manifests. Retire old public headers, wrappers and training entry points as native
consumers migrate. Keep reusable math, inference and chat formatting private.

## Ordered implementation

The [next deliverables](centroid-next-deliverables.md) are the authoritative D1–D9
sequence. D1 establishes the C11-only target graph and new public boundary; D2
generalizes the world/ownership contract with a tiny native probe; D3 completes
bundles, reconstruction and frozen evaluation. D6a supplies validated packs and
baselines before D4/D5 gameplay/text experiments. D6b completes their evaluation
and release reports; D7 adds independent merge/separation gates, D8 finishes
native product migration and D9 expands measured capability.

Adapters can proceed in parallel after the contract stabilizes. Composed quality
proof does not block flat/chat engineering. The first viewer is a C11 terminal
playback tool over native traces. All project-authored executable tools/tests are
C11; compiler and Doxygen implementation languages are outside that source rule.
The existing non-C11 stack is not kept as a compatibility obligation.

## Evidence, consent and capability

Only training-split records enter gradients and replay. Development selects rules
and candidates without updates. Reserve audit families before variants; seal one
candidate and record consumption before outcomes. Freeze audit weights, use each
actor's own history and retain failures/timeouts. Old consulted audits remain
regression evidence, not fresh independent tests.

Source review binds content, provenance, applicable versions and split. Preserve
reuse eligibility and independently checked targets. Reading documentation is not
permission to train private content or benchmark answers. Teacher answers remain
proposals until checked; no private reasoning trace is required. Initial inputs
and fixtures are newly declared native data, without resurrecting removed assets.

Use frozen read-only baselines and the same Life trainer with no-learning,
no-merge, eligibility, coverage and world-edit ablations. Pin data, seeds, budgets
and family-level intervals; report actual domain and auxiliary work. These checks
measure improvement over frozen models and contributions within Life. Superiority
over ordinary training remains unassessed without a matched comparison; delivery
does not retain another training path to supply one.

Measure actual answers/actions, supported claims, clarification/abstention,
instruction following, executable task success, retention, latency and memory.
Token loss, cell survival and agreement are diagnostics. Pin a reference assistant,
evidence, tools and budgets before a scoped equivalence claim. Current normalized
word tokens, 8,192 vocabulary entries, 256 input slots and two-million parameters
are engineering bounds, not a demonstrated intelligence ceiling. Case-preserving
tokens, copy/span behavior and broader representations need measured native
experiments. Assistant intelligence parity remains unassessed.
