# Hazard recovery and centroid specialization

`npc-pilot-v3` is an isolated C11 training profile built on the same compact
centroid network as the [accepted v2 release](npc-planner-v2.md). It adds fresh
world reservations, deterministic exploratory training experience and an observed
role objective. Promotion requires both gameplay quality and useful module
specialization. The accepted v2 weights are pinned as an independently evaluated
baseline throughout preparation, sealing, audit and publication.

## Build

```sh
cmake -S tools/npc_v3 -B build/npc-pilot-v3/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/npc-pilot-v3/native --config Release
ctest --test-dir build/npc-pilot-v3/native -C Release --output-on-failure
cmake --build build/npc-pilot-v3/native --target npc_v3_lint npc_v3_format_check
node tools/npc_v3/workflow.mjs inspect
```

Visual Studio places executables in `Release/` with `.exe`. The root build entry
point belongs to v2's accepted source identity; v3 has its own entry point and CI
workflow so its implementation can evolve without invalidating v2. Numerical
learning, simulation, adapters, authoritative replay and evaluation compile as
ISO C11 with extensions disabled. Node coordinates the artifact lifecycle.

## Experience and observed roles

Six collection rounds use disjoint eight-sibling blocks in all 24 training
families, followed by 20 full optimization passes per round. Successive frozen
policy prefixes contain 0, 4, 8, 12, 16 and 20 decisions. Teacher demonstrations,
policy-caused corrections and recovery suffixes retain actual executed actions,
observations and bounded history. A training-only host verification fork can
replace a fatal policy proposal with the observation-only teacher's action;
development, audit and inference execute the learned policy without that guard.

Every round also attempts exactly 48 exploratory episodes: the first three
siblings in each of the 16 noncue families. Following the teacher until a visible
legal cardinal alternative exists, the collector injects one action differing
from the teacher target, excludes visibly dangerous neighbors, then follows the
teacher to recovery. Direction order starts at `(round + sibling) % 4`.
Exploration and its recovery suffix are phases 3 and 4; demonstration, policy
prefix and policy recovery are phases 0, 1 and 2. Only successful, legal,
authoritatively replayed complete trajectories enter training. Reports preserve
attempts, admissions, rejections, guards and observed coverage.

The original full observation determines a training role: module 1 handles cue
mechanics or an observed blocked outcome; module 0 handles other navigation
states. The loss is the assigned module's conditional action negative log
likelihood plus 0.2 times router cross entropy against a soft role target of
0.95/0.05. Shared embeddings and encoder, outer routing centroids, inner centroids
and task readouts use exact derivatives and persistent AdamW continuation.
Both derivative passes produce one combined update per record.

The history-disabled comparator trains on identical records, targets, roles,
ordering and update budgets, with observation fields 8 through 11 masked.
Roles are derived before masking. Runtime always executes the learned mixture
with both modules eligible; the training role does not choose the runtime module.

## Reservations and comparisons

V3 interleaves layout indices across partitions to share displacement and detour
factor support. Its geometry introduces actual two-cell item barriers, secondary
hazard turns and symmetric zigzag cue stems. All 3,456 initial worlds are
translation/rotation/reflection-disjoint from every v1 and v2 world and from
other v3 families. Tests execute complete training/development teacher episodes
and verify cue privacy using initial approaches; they do not measure reserved
audit terminal outcomes before sealing.

Each of 1,152 development or audit initial conditions runs eight controllers
with separately reset, independently caused histories: planner, reactive,
initialized, trained, history-disabled, module 0 only, module 1 only and previous
accepted v2. Every death, timeout and fallback remains in the denominator.
The previous weights and their accepted head, release manifest, source identity
and artifact hashes are copied into the candidate before training and checked
throughout the lifecycle. Comparison uses paired family blocks with 10,000
deterministic bootstrap samples, seed 7331.

Promotion requires completion and survival of at least 98%, completion of at
least 95% for each mechanic, a cue/history benefit of at least 20 percentage
points, and a positive paired history benefit of at least 5 points whose 95%
interval excludes zero. Each module must contribute at least 5% and change
fixed-observation action loss under ablation. Full-mixture cue completion must
exceed module-0-only completion by at least 20 points; hazard completion must
exceed module-1-only completion by at least 5 points. These gates require
meaningful restricted-controller differences in addition to numerical routing.

Prior-v2 gains and their intervals are reported. Planner equivalence is assessed
separately against a frozen ±2-point margin; passing promotion does not establish
general assistant parity. Exact replay promises the recorded compatible native
toolchain, not bit-identical floating-point results across compilers.

## Sealed lifecycle

```sh
node tools/npc_v3/workflow.mjs prepare --tool build/npc-pilot-v3/native/cgai_npc_v3_tool --work build/npc-pilot-v3/experiments
node tools/npc_v3/workflow.mjs audit --candidate PREPARED_DIRECTORY --tool build/npc-pilot-v3/native/cgai_npc_v3_tool
node tools/npc_v3/workflow.mjs verify-delivery
node tools/npc_v3/workflow.mjs verify-development
node tools/npc_v3/workflow.mjs verify
node tools/npc_v3/workflow.mjs replay --tool build/npc-pilot-v3/native/cgai_npc_v3_tool --work build/npc-pilot-v3/replay
```

Preparation performs all six rounds, exact replay, development qualification,
resource verification and measured runtime benchmarks, then seals a candidate.
Audit writes a durable consumed receipt before evaluating the reserved batch
once. Acceptance publishes an immutable release and atomically updates the v3
pointer. Rejection retains compact historical proof and leaves v2 accepted.
Offline delivery verification requires an accepted release, a completed audit
rejection proof or a complete measured development rejection. A scaffold or empty
ledger is insufficient. Development rejection evidence records all six rounds,
both exact optimizer replays, native/artifact identities, the accepted baseline,
measured quality and the unopened audit. Its scope is recorded development
evidence; it does not establish held-out performance or current-build reproduction.

Source control permits only the exact named compiled weights, continuation
checkpoints and compact TSV release/evidence artifacts. Generated corpora,
raw captures, experiments, JSON and unexpected release files remain ignored.
The bundled `baseline.cggp` is compiled accepted v2 inference data; `baseline.tsv`
records its provenance. External comparator captures use the same authoritative
own-history replay interface as v2; descriptor identity is caller-declared.

## Delivery status

The complete v3 preparation reached 1,152/1,152 development completions
and survivals, including all 384 hazard episodes. Both individual centroid banks
also completed 1,152/1,152 episodes. Full-mixture cue and hazard completion gains
over the restricted controllers were therefore zero, below the frozen 20-point
and 5-point requirements. The workflow rejected this candidate before sealing
or consulting audit outcomes. V2 remains the accepted deployment model.

The measured rejection is recorded as
[`e9a560cadb45db38f1fd97f88709d92b2b16b61c8edd518067e865da48a0aae6`](../models/gameplay/npc-pilot-v3/development/e9a560cadb45db38f1fd97f88709d92b2b16b61c8edd518067e865da48a0aae6/result.tsv).
Offline `verify-development` and `verify-delivery` both validate its eight compact
TSVs. No accepted v3 pointer, seal, audit receipt or rejected weights are present
in the published proof. The audit reservation is still available.

| Development controller | Completed / attempted | Survived / attempted |
| --- | ---: | ---: |
| Full v3 mixture | 1,152 / 1,152 | 1,152 / 1,152 |
| History-disabled v3 | 960 / 1,152 | 960 / 1,152 |
| Module 0 only | 1,152 / 1,152 | 1,152 / 1,152 |
| Module 1 only | 1,152 / 1,152 | 1,152 / 1,152 |
| Previous accepted v2 on v3 worlds | 768 / 1,152 | 804 / 1,152 |
| Observation-only planner | 1,152 / 1,152 | 1,152 / 1,152 |

The paired development completion gain over v2 is 33.33 points, with a 95%
family-bootstrap interval of [16.67, 51.04] points. The paired history benefit is
16.67 points, interval [8.33, 27.08]. These are development results; no v3 audit
performance has been measured. Both full preparations regenerated identical
corpora and final optimizer checkpoints. Each trained 28,298 records across six
rounds, for 565,960 updates per history profile. All 288 exploratory attempts were
admitted, with no rejected or guarded trajectories.

On the named Intel Core i5-12400F, Windows x64, MSVC 19.38 Release C11 build,
complete normal adapter calls measured p95 15.9 microseconds and p99 18.8
microseconds. The inference model owns 126,744 heap bytes; each adapter/session
owns 7,472 bytes, with zero optimizer bytes. The 32-session serial batch measured
p95 588.2 and p99 771.7 microseconds; it is a single-worker measurement, not a
hard real-time guarantee.

This result demonstrates stronger development behavior on the new geometry.
Training initializes a fresh shared encoder; accepted v2 is a comparator, not a
warm start. The result does not isolate a causal encoder-transfer benefit or a
completion benefit from combining the banks. The next experiment should constrain
specialist capacity or add independently useful task demands, with a parameter- and compute-matched
single-expert comparator and a frozen independent evaluation protocol.
