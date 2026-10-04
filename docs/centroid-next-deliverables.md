# Next deliverables: C11 Centroid Life training

Status: **Planned**, October 3, 2026. This is the active delivery sequence for the
[C11 training plan](centroid-training-parity-plan.md).

Implementation progress, October 4: the native boundary and executable retirement
are implemented, along with local code/LLM context ingestion, collision-gated
lexical memory and exact context continuation. A separate contact-owned action
head now learns bounded C initialization choices from native TRAIN measurements;
development/confirmation results remain regression gates. Source search now has
immutable complete-generation bundles carrying parent code, memory, receipts,
fixed budgets and pinned identities for process continuation. See
[native source evolution](native-code-evolution.md). These are working foundations,
not completion of D2–D9: generalized ownership/adapters, full search reconstruction,
independent family audits, neural/chat training and product capability remain open.
The implementation now includes configured two-to-eight group ownership, a shared
restricted optimizer, and a typed native categorical probe with frozen complete
rounds and stable UID replay. Its integration and compatibility gates are tracked
in [native domain training](native-domain-training.md); this is machinery evidence
and does not complete the gameplay, neural/chat or independent quality gates.
The seven-action native NPC adapter now shares that engine and owns its simulator,
actor history and checkpoint cursor. Its fresh family geometry, own-history
training and frozen evaluation are being verified; see
[native NPC training](native-npc-training.md). Independent quality, confirmation
consumption and the full historical ledger remain separate gates.

Make the existing centroid Game of Life implementation the new training method.
Its evolving world and collisions drive participant learning; trained decisions
change cell evolution, and conflicts resolve through validated merging, useful
separation or deferral. Replace the old public APIs and training paths with one
native C11 interface. All project-authored executable code, workflow tools, tests
and inspection tools are C11. CMake/CI configuration and data formats remain
declarative inputs; compiler/build/documentation tools remain external tools.

The removed Git tooling and examples stay removed. There is no restoration,
port or replacement of the retired repository snapshot/service workflow in this
plan. HTTP, Node/Bun, TypeScript, JavaScript, Python and database orchestration
leave the active product. These removals are deliverables below, not changes
already made by this planning document.

## Delivery order

| Delivery | Concrete output | Depends on | Completion evidence |
| --- | --- | --- | --- |
| D1 — Retire the old stack and establish the C11 boundary | Lean Life library/CLI/test build; new native API boundary; old service, script workflows and public training interfaces removed | Existing runnable Life implementation | Build, train, save, resume and test without a scripting runtime or browser; no legacy training exports or active commands |
| D2 — Make Life the reusable training engine | World-driven round scheduler, stable participants, domain adapter contract and verified events | D1 | A physical collision trains owned domain slices and learned cell edits alter later contacts; isolation and coverage checks pass |
| D3 — Complete replay, checkpoints and frozen evaluation | Versioned complete bundles, immutable publication, reconstruction and a label-free evaluation mode | D2 | Continuous/split/process-resume equivalence; reconstruction matches; corrupted or stale candidates cannot change the incumbent |
| D4 — Train composed gameplay through Life | C11 gameplay/NPC adapter and complete native experiment | D2–D3; D6a baseline inputs | Actual legal decisions learn from verified collisions; own-history evaluation reports task quality and retention |
| D5 — Train flat neural and structured chat through Life | Expert ownership, persistent optimization, multi-token targets and routing-aware inference format | D2–D3; D6a baseline inputs | Changed native answers; masked state isolation; exact continuation with role/evidence/EOS contracts |
| D6 — Deliver C11 data, evaluation and release tooling | Reviewed native event packs, independent families, quality/resource reports and release verifier | Starts with D1; binds D2–D3 contracts | All attempted cases reported; held-out inputs never train; machinery gates pass and quality failures remain visible |
| D7 — Validate merge and separation across families | Independent consolidation gate, coordinated topology publication and useful-specialist retention | D4 or D5, plus D6 | Bad merges reject without mutation; accepted merges preserve useful parent behavior and provide measured benefit |
| D8 — Finish the native product migration | C11 local inference/conversation interface, installation, documentation and final retirement sweep | D4–D7 for supported families | Only the new API and Life production training path ship; clean C11-only acceptance run |
| D9 — Expand measured model capability | Representation improvements and independent assistant-task evaluation | D5–D8 | Scoped gains or parity against a captured reference, with frozen task protocol and uncertainty |

Numbers identify deliverables rather than a strictly serial schedule. D6a supplies
packs and frozen baselines alongside D1–D3; D6b completes reports/release checks
with D4/D5 outcomes. D4 and D5 can run in
parallel after the common contracts stabilize. D7 can begin with gameplay before
text is ready. The first implementation cycle is D1–D3 plus D6's initial Life
fixtures and evaluator, followed by one gameplay demonstration.

## D1: C11 boundary and explicit retirement

Stop importing the entire root product as an incidental dependency of Life.
`tools/life/CMakeLists.txt` currently adds the root project, so disabling its tests
still builds legacy CLI/ABI/training tools. Define a deliberate target graph that
links only the required native components and the new Life trainer.

Introduce a breaking native interface, proposed as `include/centroid_life.h`,
with opaque trainer/model handles and explicit ownership/status contracts.
Initially it supports the existing Life adapter. Subsequent deliveries add model
adapters through that same interface. Remove old installed public headers and
exports from the new product; retained C11 numerical/inference code becomes
private implementation. Build/install a declared new major interface version.

Retirement includes:

- `persistence/api/`, `persistence/db/`, `persistence/shared/` and
  `persistence/e2e/`; Node addon/build metadata, package/runtime dependencies,
  HTTP routes, worker IPC and database application contracts.
- `compose.yaml` and `compose.repository.yaml` service deployment, and old CI
  jobs/targets that run the retired services or script workflows.
- JavaScript workflows under `tools/bark/`, `tools/gameplay/`, `tools/npc/`,
  `tools/npc_v2/` and `tools/npc_v3/`; Python documentation helpers under
  `tools/docs/`; their runtime discovery and addon lint paths in CMake.
- Life's `viewer.html`, `tests/test_viewer.mjs` and authored CMake CLI test driver.
  Replace active inspection with a C11 terminal renderer/native trace reporter
  and CLI verification with C11 test executables. CTest may invoke those binaries.
- Old public mutation entrypoints: `cgai_model_train_text`, `cgai_abi_model_train`,
  `cgai_neural_train`, `cgai_neural_train_continue`,
  `cgai_neural_train_examples_continue`, `cgai_chat_train` and
  `cgai_gameplay_train_continue`; old ABI and training CLI commands.

Do not leave wrappers that call the retired trainers. Reuse reviewed internal
encoders, derivatives, formatters and codecs under the new mutation authority.
New inference access also goes through the new native interface; migration
support for historical neural artifacts is explicitly read-only.

Keep historical data, recipes, release/rejection reports and accepted artifacts
as evidence, not active non-C programs. Root CMake and shared sources are part of
recorded NPC source identities. Assign the new build a new identity; never rewrite
an accepted manifest's hashes to make it appear compatible with the migration.

**Exit:** configure/build/install/execute on supported compilers with no
Node/Bun/Python or browser requirement. Native Conway, isolated-update, merge
rejection and continuation regressions pass. The installed interface and target
graph contain no old API/training commands. Standard Doxygen documents the new
C header without a Python filter; strict C11 warnings, lint and format apply.

## D2: World-driven training contract

The `src/life/` core now shares participant-restricted optimization between
the cell policy and a typed native categorical probe. Configured ownership,
complete round freezing, UID bindings and restart state are implemented;
see [native domain training](native-domain-training.md). Continue this machinery
into concrete gameplay and text adapters, preserving synchronous world updates,
participant-restricted optimization, replay and private candidates.

Initially support up to eight ownership groups. Each group has a stable UID and
grid claims; a flat model's up to 128 experts can be partitioned into at most
eight disjoint groups. Advertise these capacities explicitly. Continuous neural
centroids and Life cell locations are separate representations.
Extending from four to eight world groups changes claim/ancestry, routing and
codec bounds; validate participants five through eight and UID remapping explicitly.

Keep the bounded cell-edit policy as an auxiliary scheduling policy. Domain heads
predict gameplay actions or text; their outputs are not reinterpreted as cell
toggles. Contacts make groups eligible to process the same queued task context.
Independent domain feedback trains their model slices; the bounded Life teacher
supervises legal world edits. Record both so survival cannot stand in for answer
correctness. Freeze placement, task selection and target-independent reseeding
rules, and report coverage of every active group/task family.

Replace the current record containing only state, target and four-bit mask with
versioned events carrying split/family, context/source identity, world contact,
stable participants, legal outputs, frozen individual proposals, executed
behavior, verified target/result, teacher identity, parent version and admission
decision. Preserve observed-information limits and defer unsupported targets.

Collect a complete bounded round before mutation. Local updates leave unrelated
and shared weights, moments, decay and clocks unchanged. A later shared-encoder
phase needs an explicit permission and retention gate inside the same trainer.

**Exit:** a deterministic collision changes actual domain output and world
evolution. Invalid/held-out events fail before mutation; optimizer isolation,
masked/mass-aware finite differences, clipping and Adam clock checks pass. Test
compressed-observation target conflicts and teacher information privileges. A
world-extinct group retains its trained model; separated groups remain available.
Use a small authored C11 probe-domain adapter for this gate, so D2 does not depend
on the later full gameplay/text adapters.

## D3: Exact bundles and genuinely frozen evaluation

Native world, context and domain owners now save complete single-file state
atomically, and source search publishes pinned complete-generation bundles.
Reconstruction and broader source/build compatibility audits remain required.
CLI `replay` continues learning. `evaluate` already advances the frozen cell
policy without teacher labels, replay admission or optimizer changes. The domain
adapter also provides frozen prediction and evaluation; full reconstruction
remains a separate deliverable.

Add a bounded versioned bundle carrying world/scheduler/domain model state,
ownership and UIDs, moments/clocks, replay/queues, routing mass, ancestry, sampler,
initialization, complete recipe, data/event hashes and collection predecessors.
Implement staged immutable publication and expected-parent checks in C11, with
platform-specific filesystem/locking code where needed. Loading validates the
entire bundle before replacing live state.

Separate proposed native commands for training, continuation, reconstruction,
immutable evaluation and inspection. Frozen evaluation uses learned decisions
without teacher labels, updates, replay admission or topology mutation. External
scoring runs after decisions are committed. A weights-only import starts new
training state and cannot claim exact resume.

**Exit:** same-build continuous versus split-call versus process-restart runs
match complete state and outputs. Reconstruction from initialization also matches.
Truncation, nonfinite numbers, invalid topology, incorrect identities, missing
members and trailing data reject without mutation. Interrupted publication or
stale parents preserve the previous head and leave inspectable failure evidence.
Include claim/ancestry, routing mass, UID remapping and checkpoint round trips
for participants five through eight in the generalized-format checks.

## D4–D5: Model adapters under one method

For **composed gameplay**, assign module parameter/moment slices to participants.
Use observation-limited supervision plus authoritative task replay. Each contact
participant proposes on the same actual observation/history; evaluate every
comparator on its own history. Reuse the C11 simulator, legal outputs and memory
contracts. Begin with conflict/no-merge, then add D7. Historical NPC v2 remains a
pinned read-only comparator when its artifact can be loaded; unavailable old
candidate weights are omitted with explanation, not recovered or regenerated
through retired workflows.

For **flat neural and chat**, map disjoint expert cohorts to world participants.
Freeze shared embeddings/encoder/bias during local updates and give each cohort
its own moments/clocks. Preserve structured roles, complete current questions,
admitted evidence, causal prefixes, assistant-token/EOS targets and independent
dialogue boundaries. Implement persistence directly in the new trainer; do not
add another public `cgai_chat_train_continue` path.

Serving exports must preserve eligibility, active groups and routing mass.
Historical `.cgchat` weights alone cannot express that behavior. Version the
new inference format and test full-eligibility equivalence before topology changes.
Carry tokenizer/formatter/output-mask identities through import and checkpoints.

**Exit:** run complete C11 collect/train/checkpoint/continue/infer/evaluate demos
for each supported family. Actual action/answer outputs change, isolated-state
and exact-resume checks pass, and independent quality/retention reports retain all
failures. Working adapters do not imply successful transfer or assistant parity.

## D6: Native data, evaluation and release tools

Author fresh native-readable event packs from available simulator/source inputs.
Implement bounded validation, review binding, source licensing/provenance,
family-level splits, vocabulary/context preflight, reporting and release checks
in C11. This is the Life trainer's data contract, not a recreation of removed
repository snapshot tooling. Reading all documentation for planning is not
permission to train on benchmark answers or private content.

Reserve genuinely distinct Life encounter families rather than translations of
the existing seed patterns. Freeze new gameplay and text task/source families
before variants or targets are generated. Consulted historical tests are
regression data. Record audit consumption durably before opening held-out results.

Use frozen read-only baselines and no-learning, no-merge, participant eligibility,
coverage and world-edit ablations through the same Life trainer. Include individual
and matched single-expert configurations where meaningful. Declare data, update
and compute budgets. There is no alternate ordinary trainer or revived legacy
training path. These comparisons support scoped claims about the tested method
components, not superiority over an unmeasured equal-budget old trainer. Report
CA scheduling/edit-policy costs separately from domain learning.

Report task success, supported claims, answer/clarify/abstain decisions, retention,
coverage, raw outputs, fallback/teacher use, abstentions, timeouts and all attempted
cases. Validate model/optimizer/scratch/replay/candidate/serialization allocations
and measure peak process memory plus collection/training/inference costs. Name
measurement exclusions and supported enforcement. The old worker-memory backlog
becomes a native resource requirement, not a service task.

**Exit:** reproducible input and result identities, complete denominators,
train-only admission, frozen audit execution and passing machinery/resource gates.
Quality margins and power analysis are fixed before results. A negative experiment
is a completed report; promotion requires its declared quality and retention gates.

D6a ends when validated packs, splits, frozen baseline decisions and the initial
Life evaluator are available. D6b adds family-adapter scoring, result reports and
release verification; its completion does not block starting D4/D5.

## D7–D9: Consolidation, product completion and capability

**D7:** validate merge candidates on independent parent-task slices and actual
constrained inference after routing remap. Commit domain weights, scheduling
policy, world claims, UIDs, mass, queues/replay and lineage together. Rejecting
preserves every parent. Retaining different useful specialists is successful
separation; persistent contact alone never forces a merge. Require measurable
quality, inference-work or storage benefit and distinguish retired slots from
reduced allocated bytes. Clone/split and cross-encoder alignment remain later work.

**D8:** expose supported model execution and bounded local conversations through
the new C11 library/CLI. Preserve explicit model identity, owned session scratch,
roles/evidence, completion/error/cancellation and correction/retention semantics.
Finish installed-header/symbol and source/dependency sweeps, then replace active
service documentation with new native usage. No HTTP or database rebuild is part
of this delivery. Historical contracts and reports remain labeled evidence.

**D9:** use measured errors to evaluate case/format-preserving tokenization,
copy/span selection, context representations and conditional expert readouts.
Current flat token distributions, 256 input slots and word normalization limit
what collision training can learn. Then add independently checked reasoning,
coding/tool and continual-learning tasks under explicit native execution contracts.
Compare against an identified captured assistant with matched evidence and budgets;
state parity only for capabilities whose independent gates actually pass.
