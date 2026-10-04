# Observable NPC centroid pilot

The `npc-pilot-v1` profile learns seven bounded action IDs for a deterministic
grid-world NPC. It combines two learned centroid modules inside the existing
C11 gameplay network. The host owns movement, collision, inventory, hazards,
terminal outcomes and the decision budget. The model supplies an action proposal.

This pilot extends the [six-milestone epic](npc-planner-epic.md). Its completion
and resource gates apply to this bounded task. Comparing it with a general
assistant would require a separate, preregistered evaluation.

The first sealed candidate was rejected. All six implementation milestones are
present, while learned-policy release acceptance remains unmet. No
`npc-pilot-v1/current.tsv` exists and existing composed, bark and scholarly heads
are preserved. The compact consumed-audit evidence lives under
`models/gameplay/npc-pilot-v1/audits/`.

## Build and run

```sh
cmake -S . -B build/npc-pilot/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/npc-pilot/native --config Release
ctest --test-dir build/npc-pilot/native -C Release -R centroid_gai_npc --output-on-failure
node tools/npc/workflow.mjs verify-evidence
```

On Windows, native executables are under `build/npc-pilot/native/Release/`;
single-configuration generators place them directly under the build directory.
Pass the executable path explicitly with `--tool` when running the workflow.
The `npc_train`, `npc_verify` and `npc_replay` CMake targets supply that path.
`npc_verify_evidence` checks the compact historical audit record offline.
`verify` and `replay` require an accepted head; this failed candidate has none.

To separate development qualification from final audit consultation:

```sh
node tools/npc/workflow.mjs prepare --tool build/npc-pilot/native/cgai_npc_tool --work build/npc-pilot/experiments
node tools/npc/workflow.mjs audit --tool build/npc-pilot/native/cgai_npc_tool --candidate PREPARED_DIRECTORY
node tools/npc/workflow.mjs verify --tool build/npc-pilot/native/cgai_npc_tool
node tools/npc/workflow.mjs replay --tool build/npc-pilot/native/cgai_npc_tool --work build/npc-pilot/replay
```

Use `Release/cgai_npc_tool.exe` in those commands for Visual Studio builds.
The first command returns a local prepared directory only after development,
resource and replay gates pass. The audit command accepts that sealed directory.
`train` runs both stages unattended. Neither command commits or pushes to Git.
The current profile's reserved audit families cannot be consulted again.
The shown preparation/audit sequence describes a fresh reserved experiment;
the current profile's consumed batch cannot produce another promotion attempt.
Use `cgai_npc_demo PATH/model.cggp` for a local host integration demonstration.
That executable checks host execution and session isolation, not release quality.

## Observable state and memory

The frozen architecture has 16 categorical fields, 16 embedding dimensions,
48 hidden dimensions, two modules, eight centroids per module and one
seven-output head, seed 42 and routing temperature 32. `npc_world.h` defines the authoritative host world separately
from the observation that controllers receive. Model inputs contain neighboring
visible tiles, an observed objective bearing, inventory, the current cue,
remembered cue and recent action/outcome, cue age, objective stage, target
occupancy, observed decision age and mechanic type.

The history adapter stores previous observations in caller-owned session state.
It resets before every episode. This is explicit bounded memory supplied to a
feed-forward network. A separately trained comparator zeros fields 8 through 11
at both training and inference. Every actor produces its own closed-loop history.

The action contract uses fallback, wait, north, east, south, west and interact.
Fallback consumes a decision as a safe wait. Repeating a movement is allowed;
the adapter sets `recent_output` to zero. Host permissions describe observable
execution rules. They do not identify the correct hidden route. The paired cue
test checks equal current observations and masks with different remembered cues.

## Training and promotion

The teacher enumerates at most four observable one-step moves, uses Manhattan
distance, prefers the dominant goal axis before a fixed cardinal tie order, and uses the same bounded history as
the learned policy. The generated corpus contains verified complete teacher
trajectories and bounded repairs of initialized-policy rollouts from training
families. Generated data, traces and pending experiments stay under ignored
`build/` paths. The authored contract, actions and family assignments are tracked
as TSV files under `data/gameplay/npc-pilot-v1/`.

Each split contains 24 complete families with 48 distinct variants per family,
balanced across prerequisite, hazard/recovery and disappearing-cue mechanics.
The eight cue families per split have distinct unordered branch-length pairs and
post-item corridor lengths. All 3,456 initial worlds are compared across families
after translation, rotation and reflection normalization. Both branches have
the same neutral exit stems; private item/exit contents remain occluded until
commitment. Early lateral entry also commits the route and enforces its outcome.

Training uses the public engine's seeded initialization and joint AdamW updates.
The recipe records actual corpus size and update count. Development compares
20, 40 and 80 epochs at learning rate 0.001, gradient clip 5 and weight decay
0.05; equal-family completion selects the winner, with the earliest epoch breaking
ties. A compatible-toolchain
replay must reproduce checkpoints exactly. Development episodes select a
candidate before its source identity, weights, recipe and reserved audit batch
are sealed. The final audit is consumed once, including failed attempts. A
rejected candidate preserves the accepted head. Future independent claims need
new reserved families; a reused suite is a regression check.

Release verification binds inference weights to the observation and action
contract, source files, corpus identity, recipe, toolchain, development outcomes,
audit outcomes and measured performance. It works offline without generated
trajectories. Only named compact release artifacts are admitted by `.gitignore`.

## Measurements

Evaluation includes the observation-aware planner, reactive controller,
initialized model, trained history model, separately trained history-disabled
model and both single-module ablations. Reports retain deaths, timeouts,
obstructions, recoveries, rejected proposals and fallback use. The paired history
comparison uses 10,000 fixed-seed bootstrap resamples of complete families.
Individual decisions and related variants are not independent trials.

The family count and resampling protocol were frozen before audit. Development
showed a 14.6 percentage point overall history benefit concentrated in
eight cue families, comfortably exceeding the five-point target. Twenty-four
family blocks retain balanced mechanic coverage and a positive family-bootstrap
lower bound without treating 1,152 variants as independent observations. This is
a bounded pilot design, not a power claim for other games or smaller effects.
Exploratory routing temperatures 4, 8 and 32 were examined on development data.
An independent review then caught translated cue families and private exit-stem
leakage; both were corrected and geometry/paired-observation checks were expanded
before the reserved audit. The corrected profile used temperature 32 and met the
unchanged development composition gate before sealing.

The benchmark measures memory update, encoding, neural selection and output
validation through preallocated sessions, with no inference allocation or I/O.
It reports 2,048 measured calls through four sessions, forced fallback separately,
and 2,048 serial batches of 32 NPC calls on one worker. Model and session sizes
are requested heap bytes; allocator overhead, transient stack, static content
and world simulation are outside those heap counts. Hosted CI validates
functionality and artifact identity; absolute latency certification belongs to
the named reference machine.

The timing workload cycles 32 precomputed training observations after bounded
teacher prefixes. Bounded replay timestamps change for each session so the memory
update runs on every measured call. These timings measure policy scheduling;
complete-episode outcomes are measured separately by the evaluator.

## First sealed audit

Development selected 20 epochs from the frozen 20/40/80 grid. Each policy trained
on the same 2,260 verified records for 45,200 updates; the comparator masked
history fields throughout. Both selected checkpoints replayed exactly on MSVC
19.38.33145 before sealing. The final audit consulted 24 reserved families once,
with 48 variants per family and no failed episode omitted.

| Measure | Measured result | Gate |
| --- | --- | --- |
| Goal completion | 1,068/1,152 (92.71%) | At least 90%; passed |
| Item completion | 384/384 (100%) | At least 80%; passed |
| Hazard completion | 300/384 (78.13%) | At least 80%; failed |
| Cue completion | 384/384 (100%) | At least 80%; passed |
| Survival | 1,068/1,152 (92.71%); 84 deaths, zero timeouts | At least 95%; failed |
| Reactive cue completion | 192/384 (50%) | Trained cue gain 50 percentage points; passed |
| History-disabled completion | 924/1,152 (80.21%) | Overall history gain 12.5 percentage points; passed |
| Paired family interval | 95% interval for history gain: 1.04–23.96 percentage points | Excludes zero; passed |
| Host execution | Zero executed illegal actions or rejected proposals | Passed |
| Inference model heap | 126,744 bytes (123.8 KiB), no optimizer | At most 256 KiB; passed |
| Complete NPC session heap | 7,472 bytes (7.3 KiB) | At most 64 KiB; passed |
| Complete policy call | p95 14.1 µs; p99 21.4 µs | At most 500/1,000 µs; passed |
| 32-NPC serial batch | p95 535.4 µs; p99 623.7 µs | Scheduling report; one worker |

Both modules passed routing/posterior share and mean-loss restriction gates.
The observation-aware planner completed all 1,152 episodes. Module-only episode
ablations completed 1,068 and 1,116 respectively; the audit cannot select an
ablation after seeing those results.

The latency reference is a 12th Gen Intel Core i5-12400F on Windows x64, MSVC
19.38, `/O2 /Ob2 /DNDEBUG`, C11 with extensions disabled, 256 warmup calls and
2,048 samples per workload. The consumed-audit evidence is a historical record
of this sealed experiment. Adding offline rejection-evidence support changed the
wrapper afterward; verification does not claim the historical wrapper hash is
the current source hash. Normal accepted-release verification still requires
current source identity and all release gates.

The learned-policy head remains absent. Further training needs broader hazard
coverage and newly reserved audit families before another independent release
claim; reusing this batch can only support regression checks.
The [v2 iterative recovery profile](npc-planner-v2.md) implements that next scope
under a separate recipe and independent family reservations.
