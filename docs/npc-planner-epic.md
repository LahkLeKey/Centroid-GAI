# C11 NPC planner pilot epic

Status: implementation delivered; learned-policy release acceptance unmet.
The C11 simulator, observable memory, teacher/corpus, training, comparative
evaluation, performance benchmark and sealed release workflow are implemented.
The first sealed candidate was rejected for hazard completion and survival.
The [pilot guide](npc-planner.md) records measured results and reproducible commands.

The [existing gameplay network](neural-centroid.md#composed-centroid-gameplay-network)
provides the neural engine, deterministic training and release foundations. The
pilot adds a new episode contract, training recipe and evaluation protocol. All
targets below remain acceptance requirements. The measured first attempt is
reported separately, and no accepted v1 NPC model head has been published.
The separate [v2 recovery profile](npc-planner-v2.md) has an accepted release;
its measured results do not establish planner or general-assistant equivalence.

This is the frozen v1 specification. Completed implementation instructions,
numeric gates and protected-source rules below retain that profile's scope. Use
the [conflict training plan](centroid-training-parity-plan.md) for cross-family
work; it carries forward the relevant observation, replay and release invariants
without adopting every original recipe or treating the failed audit as acceptance.

## Outcome and scope

A C11 host loads one immutable model, creates an NPC session, observes the world,
updates bounded memory, requests a legal action and advances the simulation. It
repeats this loop until the objective succeeds, the NPC dies or the episode times
out. Each next decision uses the state caused by that actor's own previous action.

The pilot uses one NPC in a small deterministic grid world, with maps at most
9 by 9 cells and episodes at most 64 decisions. Three mechanics establish the
initial curriculum:

- Acquire an item before opening the exit.
- Navigate around observable hazards and recover after an obstructed move.
- Remember a previously visible route cue after that cue disappears.

The proposed action domain has seven IDs: fallback, wait, four cardinal moves and
interact. Fallback performs a safe wait and consumes a decision, so repeatedly
abstaining cannot artificially complete the objective. The host owns collision,
inventory, hazard effects, prerequisites and terminal-state validation.

Broad conversation, code generation, research answers, free-form quests,
multiplayer coordination, engine-specific integrations and learned recurrent
memory are later epics. An external language model is optional for a later
comparison; network access and paid teacher calls are not dependencies here.

## Existing constraints and design decisions

The [public API](../include/centroid_gai_gameplay.h) accepts at most 16 categorical
fields, 64 values per field and 10,000 independent training records per call. It
has no episode memory or sequence-gradient training. The
[existing native recipe](../tools/gameplay/gameplay_training.c) pins 384 records
per epoch and the bark and intent contract. The pilot needs its own native tool
and recipe rather than passing a new corpus into that recipe.

Use a separate `npc-pilot-v1` profile, source directory and release namespace.
Reuse the public centroid engine without editing the source dependencies or
artifacts pinned by `composed-v1`. A future core change requires an explicit new
identity and compatibility assessment. The pilot must not reinterpret existing
bark or intent IDs as navigation actions.

Start with a proposed D16/H48 encoder, two jointly trained specialist modules,
eight inner centroids per module and one seven-output head. Each specialist must
contribute inside the neural mixture. Module names describe intended roles;
measured routing and ablation establish their actual contribution.

Freeze the observation and memory encoding at no more than 16 fields. Preflight
the exact shape with resource inspection before generating a large corpus.
Expanding the current D32/H64 encoder to 16 fields would consume 262,144 bytes
in encoder weights alone, exceeding the complete model budget after other
parameters are included.

Memory belongs to each NPC session and contains only information previously
observed by that NPC: remembered route cues, their age, recent executed actions
and observed action outcomes. Define unknown values, update order and reset
behavior in the contract. Memory updates never use hidden world state or planner
answers. This is a policy learning to use explicit memory; the epic does not
claim that a recurrent network learns what to remember.

```text
visible observation -> bounded memory update -> categorical encoding
    -> shared encoder and centroid specialists -> admitted action
    -> authoritative world step -> next visible observation
```

Observation-equivalent worlds must produce identical current observations,
permission masks and memory updates when their visible histories are identical.
Masks describe observable execution permissions, not the correct route or an
unseen hazard. A legal action can still have an unfavorable outcome.
Set `recent_output` to zero for navigation: repeated moves in the same direction
are valid. Detect stalls through episode outcomes rather than suppressing repeats.

## Milestones and implementation backlog

| Order | Milestone | Concrete deliverable | Depends on |
| --- | --- | --- | --- |
| 1 | Episode contract and simulator | C11 observation, action, memory and transition contracts with deterministic replay | Existing public engine |
| 2 | Reference planner and corpus | Verified trajectories, family splits, bounded training corpus and baseline report | 1 |
| 3 | Learned centroid policy | Separate native tool, joint training, checkpoint continuation and exact replay | 2 |
| 4 | Independent episode evaluation | Candidate selection, memory ablation and a sealed final audit | 3 |
| 5 | Runtime performance and host demo | Complete policy timing, resource accounting and reusable NPC sessions | 3 and 4 |
| 6 | Release and CI integration | Compact certified bundle, failure preservation and cross-platform checks | 4 and 5 |

### Milestone 1 Episode contract and simulator

Implement the world generator, observable-state extractor, memory adapter,
authoritative transition function and runner. Give each episode a stable family,
seed, mechanic set and contract version. Use bounded integer state and a pinned
random stream. Explicitly handle blocked moves, invalid actions, death, successful
completion and timeout.

The first vertical slice is a pair of route-cue episodes. Their earlier visible
cues differ, but the current observation and permission mask at the junction are
identical. Correct actions differ because of remembered information. A reference
controller using memory must complete both; a deterministic controller using
only that current observation cannot distinguish the paired decision. A wrong
branch has a declared irreversible consequence or enough cost to miss the goal
deadline. Verify the episode-success gap before freezing the benchmark; freely
exploring both branches must not solve every cue episode within its budget.

**Acceptance:** replay reproduces every observation, action outcome and terminal
state; sessions reset without retaining another episode's memory; no hidden cue
enters the current inputs or masks; a deliberately invalid action cannot bypass
host checks. The runner requests a new decision after each world step.

### Milestone 2 Reference planner and corpus

Build a deterministic executable planner using the same observable information
and bounded memory representation available to the learner. Pin search limits,
tie breaking and action costs. Keep any full-state oracle separately labeled as
an upper bound; it is not a fair observation-limited reference or training teacher.

Replay every proposed teacher trajectory from its initial state to verified
completion before admitting its training examples. Detect identical encoded
states with incompatible targets. Resolve valid alternative actions consistently;
otherwise reject the example or revise the representation before freezing it.

Assign complete layout and mechanic-combination families to train, development
or final audit before generation. Keep seeds, rotations, mirrors and other
variants of each family together. All supported feature values and action labels
must be represented in training; new evaluation layouts and combinations must
not introduce undeclared vocabulary. Start with a proposed 24 audit families and
48 variants per family: 1,152 episodes balanced across the three mechanics.
Choose the final family count using development evidence or a preregistered power
calculation before sealing a candidate. Prioritize additional independent families
over additional variants of one family. Freeze equal-family weighting and the
paired family-block bootstrap method, using 10,000 resamples and a recorded seed.
After the audit is opened, an inconclusive result cannot trigger expansion of
that audit within the same experiment.

Cap the canonical training corpus at 8,192 examples for this epic. Record complete
episode provenance, selected-step IDs, class coverage and any repetition or
balancing schedule. Generated trajectories stay under ignored `build/` paths.
Freeze corpus hashes and generator identities before comparing checkpoints.

**Acceptance:** split and duplicate audits pass; teacher rollouts satisfy the
host's outcomes; identical observable histories cannot acquire different inputs
through hidden-state masks; corpus generation replays byte for byte. Produce
baseline episode reports before training a model.

### Milestone 3 Learned centroid policy

Add a separate C11 pilot tool that creates the bounded shape, trains through the
public engine, exports inference weights and checkpoints, and reconstructs a
checkpoint from its complete recipe. Pin initialization, corpus order, shuffling,
AdamW settings, epoch count and software/toolchain identity. Record the actual
updates per epoch instead of inheriting the existing 384-record assumption.

Train the shared encoder, both module routers, inner centroids and output heads
jointly. If class-prototype initialization is used, its statistics come exclusively
from training examples and its full method belongs in the recipe. Report the
initialized model separately so an effective initialization is not mistaken for
improvement caused by later optimizer updates.

Generate recovery demonstrations only from training families after the first
policy rollout. Bound and pin that collection schedule, then freeze a new corpus
version. Do not add failed development or audit episodes to that corpus.
Each changed corpus starts a separately identified recipe unless an explicit
continuation protocol records every prior corpus and update schedule.

**Acceptance:** the model uses the history encoding; training and checkpoint
replay are exact on a compatible toolchain; malformed or oversized inputs fail
without publication. All model parameters remain learned neural components;
inference does not call the teacher to choose an action.

### Milestone 4 Independent episode evaluation

Run each actor independently from the same episode initial conditions. Each actor
receives the history caused by its own executed actions, not a teacher trajectory.
Compare the observable-information reference planner, an authored reactive
controller, the initialized centroid policy, the trained policy, and a separately
trained policy with history disabled at both training and inference. Keep that
policy's architecture, non-history features, training budget and seed schedule
equivalent. Report initialized-to-trained changes separately before attributing
any quality improvement to optimizer updates.

Existing `composed-v1` scores remain a historical baseline for bark and intent.
Its action domain differs from this pilot, so it is not an equivalent navigation
comparator. A later stronger-model reference must pin its version, prompts,
observation/action interface and captured outputs; report its inference resources
separately from the C11 runtime budget.

Score terminal goal completion, survival, deaths, timeouts, path cost, recovery
after obstruction, attempted and executed illegal actions, and fallback use.
Break down outcomes by family and mechanic. Exact teacher agreement and action
cross-entropy are diagnostics; completed episode outcomes determine quality.
Softmax probability is not calibrated confidence.

Choose candidates using development episodes and the preregistered rule only.
Then seal the selected artifact and recipe before running the final audit once.
The audit result may accept or reject that sealed candidate; it must not select
another checkpoint from the same experiment. After consultation, that audit batch
is consumed. A later independent claim needs newly reserved families; a reused
suite must be labeled as a regression guard.

**Acceptance:** paired evaluation is repeatable; deliberate memory removal exposes
the cue-task gap; metrics cannot omit failed episodes or count individual steps
as independent trials. Report a paired difference in episode success using a
family-block bootstrap with a fixed resampling seed and method. An inconclusive
comparison is reported as such and cannot establish improvement or parity.

### Milestone 5 Runtime performance and host demo

Provide a C11 example with one shared immutable model and independently resettable
NPC sessions. The host updates memory, encodes inputs, requests an action and
validates execution. Inference performs no allocation, file I/O, tokenization or
network work after sessions are created.

Count persistent adapter storage, including caller-owned history, as well as
neural scratch in the per-NPC budget. Load inference-only weights with no resident
optimizer state. Measure the complete memory-update, encoding, selection and
output-validation path. Report warmup, sample count, CPU, compiler, build flags,
model identity and session count.
Measure at least 2,048 calls through four preallocated sessions on one worker,
including fallback paths separately. Also report a 32-NPC serial scheduling
workload; do not present a single-worker measurement as concurrent frame timing.

**Acceptance:** the requested inference model heap is at most 262,144 bytes and
the complete per-NPC session is at most 65,536 bytes. Policy-call p95 is at most
500 microseconds and p99 at most 1,000 microseconds on the declared reference
hardware. Disclose allocator overhead, stack, static content and simulator cost
separately. Failed measurements cannot be replaced by estimates.

### Milestone 6 Release and CI integration

Create a profile-specific manifest and atomic head under
`models/gameplay/npc-pilot-v1/`. The wrapper must bind the existing `.cggp` shape
to the pilot contract and action semantics; a weights file alone does not identify
those semantics. Checkpoints retain exact continuation state. Compact reports
bind the corpus, family manifests, generator, teacher, adapter, runtime, trainer,
evaluation protocol, toolchain and artifact hashes.

Reject a candidate if quality, resource, replay, provenance or legality gates
fail. After the first accepted pilot release, additional candidates must also
improve the preregistered primary development metric against the incumbent and
avoid per-mechanic regression under the same evaluation contract. Changed contracts
start an isolated profile. Rejection preserves the accepted pilot head and all
other model heads.
Prevent publication after source or artifact changes during verification.
Offline verification must work without the raw trajectories or network access.

Extend the Git allowlist only for named authored contracts, family manifests and
compact accepted artifacts. Raw JSON, traces, generated datasets and pending runs
stay ignored. Add boundary tests that also catch forbidden files forced into the
index. The training workflow may promote a model locally; it does not commit or
push automatically.

**Acceptance:** Linux, macOS and Windows compile the C11 pilot and run functional,
split, host-control and replay checks. Strict lint, formatting and documentation
remain enabled. Use the named reference machine for absolute latency certification;
hosted CI runners do not establish that hardware claim. Existing composed, bark,
scholarly and chat checks remain passing. Each platform checks exact replay of its
own compatible-toolchain training runs; published weights and source identities
are verified across platforms. Cross-compiler checkpoint byte equality is not an
acceptance requirement.

## Proposed release gates

Freeze these targets and their measurement rules with milestone 2 before candidate
selection. A target revision starts a new benchmark/recipe version and is visible
in the report.

| Measure | First release requirement |
| --- | --- |
| Goal completion | At least 90% overall and 80% for each mechanic on final audit |
| Survival | At least 95% on final audit, with deaths and timeouts reported separately |
| Temporal benefit | At least 20 percentage points above the reactive controller on cue episodes |
| History benefit overall | At least 5 percentage points above the separately trained history-disabled policy overall; the 95% family-block interval for the paired difference must exclude zero |
| Legality | Zero executed illegal actions and zero host-validation bypasses; report rejected proposals separately |
| Neural composition | Each module's mean routing and selected-output contribution is at least 0.05; module restriction changes mean fixed-observation loss by at least 0.000001; report each module's complete-episode ablation |
| Resources | At most 256 KiB model and 64 KiB complete per-NPC session; measured p95/p99 within the declared limits |
| Repeatability | Exact compatible-toolchain checkpoint replay and exact deterministic episode replay |

Measure composition on a pinned observation sample with both modules eligible
and no forced-fallback requests; report that sample's size and identity. Resource
and outcome reports still include forced fallbacks. Freeze composition criteria
and this sampling rule with milestone 2.

Measure the gap to the reference planner even if the pilot passes. A parity claim
requires a separately preregistered comparison and margin; successful completion
of this epic does not automatically establish parity with a general assistant.
If quality targets fail, deliver the simulator and failed experiment report as
measured progress, but keep the learned-policy release gate unmet.

## Proposed repository boundaries

The implementation uses these repository boundaries:

| Location | Responsibility |
| --- | --- |
| `tools/npc/` | Simulator, observation/history adapter, planner, corpus generator, trainer, evaluator, benchmark and lifecycle tool |
| `tests/test_npc_*.c` and workflow tests | Episode correctness, isolation, hidden-state leakage, masks, split integrity and publication failures |
| `examples/npc_planner.c` | Complete engine-neutral C11 host loop |
| `data/gameplay/npc-pilot-v1/` | Named authored contract, action catalog and family manifest |
| `models/gameplay/npc-pilot-v1/` | Consumed-audit evidence; accepted releases and head pointer only after all gates pass |
| `build/npc-pilot/` | Ignored trajectories, candidate checkpoints, raw reports and experiment traces |

Keep the underlying `include/centroid_gai_gameplay.h`, `src/gameplay/` and existing
`tools/gameplay/` recipe unchanged for this epic. Add the pilot's build and checks
through separate CMake targets and dependency lists.

## Original first implementation slice

Begin with milestone 1 and the smallest part of milestone 2: the observable
contract, one cue-pair simulator, deterministic replay, an observable-memory
reference controller and a reactive baseline. Deliver a report proving that the
task requires remembered information and that masks do not reveal the answer.
This slice must pass before corpus expansion or neural training begins.

The implementation now includes the simulator, verified generation recipe,
training tool, comparative audit, performance evidence and runnable C11 demo.
The first sealed experiment did not meet the release gates. Its compact failed
report is delivered under the contingency above; an accepted learned-policy
release remains a requirement for a later independently audited candidate.
