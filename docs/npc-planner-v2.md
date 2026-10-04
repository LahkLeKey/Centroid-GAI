# Iterative NPC centroid training

The `npc-pilot-v2` profile extends the [v1 pilot](npc-planner.md) with training
states caused by successive frozen policies, verified recovery trajectories,
fresh world families and a pinned reference-controller interface. Learning,
simulation, host validation and runtime inference compile as C11. Node coordinates
the experiment and verifies compact artifact identities.

The v1 audit remains consumed and its evidence remains independently verifiable.
V2 reserves different geometry across training, development and audit. Initial
worlds are compared under translation, rotation and reflection against all v1
worlds and against other v2 families. Item barriers require detours; hazard rooms
alternate recovery routes; cue rooms use paired outward-bent exit stems.

## Build and inspect

```sh
cmake -S . -B build/npc-pilot-v2/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/npc-pilot-v2/native --config Release
ctest --test-dir build/npc-pilot-v2/native -C Release -R centroid_gai_npc_v2 --output-on-failure
node tools/npc_v2/workflow.mjs inspect
```

Visual Studio places executable files under `Release/` and uses `.exe`. The
separate `cgai_npc_v2_tool` and `cgai_npc_v2_demo` targets link the v2 host contract.
The policy shape remains D16/H48/M2/K8 with seven action outputs, seed 42
and routing temperature 32. The v2 wrapper identifies the changed recipe and
world semantics; compatible tensor dimensions alone do not establish semantics.

## Training and recovery

The recipe runs six ordered collection/training rounds. Each round visits an
eight-variant block in every training family, spanning all 48 variants over a
complete cycle. Base demonstrations and correction records carry family,
variant, decision, round, phase, actual executed action and the complete observed
history encoding. The history-disabled comparator trains on identical ordered
records and targets with fields 8 through 11 masked.

The collection policy is the previous round's frozen trained checkpoint. During
the bounded policy prefix, the collector labels visited states with the
observation-limited teacher. A training-only host check can replace a proposal
that would cause death with the teacher's observed-information action and then
complete a recovery suffix. This check uses simulated consequences to admit
training experience. It does not add hidden information to model inputs or shield
the learned controller during development, audit or host inference.

Only successful, legal, exactly replayed teacher/correction trajectories are
admitted. Coverage reports retain actual collected, rejected and corrected
experience. Generation, corpus ordering, optimizer state and all round boundaries
belong to the replay recipe. Exact replay regenerates corpus versions from each
preceding frozen checkpoint and reconstructs weights and optimizer moments.

Joint AdamW minimizes action negative log likelihood plus a routing penalty,
`0.2 * KL(uniform-two-modules || routing-probabilities)`. The v2-only optimizer
backpropagates both terms through learned module centroids, the shared encoder
and observed category embeddings in the same update. This penalty keeps both
modules learning; inference uses the ordinary learned mixture. Both history
profiles use identical objective settings, record ordering and update budgets.
Analytical derivatives are checked against finite differences. Reported action
losses and module ablations measure negative log likelihood without the penalty.

Earlier round evaluations are learning diagnostics. Release selection requires a
complete six-round collection cycle, so a short prefix cannot be presented as a
candidate trained across all variants. Quality and resource targets remain
requirements, independent of progress in the learning diagnostics.

## Reference comparisons

The default reference is the executable observation-limited planner. A descriptor
pins its source identity, memory policy, 16 input fields, seven actions and
64-decision episode budget. Comparisons report paired family differences in
completion, survival and decision cost, including all failed episodes.

An external reference can instead supply a pinned descriptor and raw action
capture. Each row includes the exact observation/history encoding at that
reference actor's own pre-action state. Native replay checks the capture against
the authoritative host, rejects missing or extra decisions and scores actual
terminal outcomes. It does not trust caller-supplied scores. A descriptor and
action capture identify the declared comparator and its consequences; they do
not independently authenticate a remote provider or prove how that controller
generated its actions.

The comparison protocol freezes a two-percentage-point equivalence margin for
completion and survival and a 95% paired family-block interval before audit.
Passing release quality gates does not establish equivalence. A comparison with
the built-in planner supports claims about that planner on these reserved tasks.
It cannot establish parity with a general coding or research assistant.

## Sealing and publication

```sh
node tools/npc_v2/workflow.mjs prepare --tool build/npc-pilot-v2/native/cgai_npc_v2_tool --work build/npc-pilot-v2/experiments
node tools/npc_v2/workflow.mjs audit --tool build/npc-pilot-v2/native/cgai_npc_v2_tool --candidate PREPARED_DIRECTORY
node tools/npc_v2/workflow.mjs verify-delivery
node tools/npc_v2/workflow.mjs replay --tool build/npc-pilot-v2/native/cgai_npc_v2_tool --work build/npc-pilot-v2/replay-validation
```

Use `Release/cgai_npc_v2_tool.exe` with Visual Studio. Preparation qualifies
development, replay and measured runtime resources without consulting reserved
audit outcomes. Audit consumes the pinned family batch once, including rejected
or failed attempts. Promotion requires every release gate and preserves previous
heads when rejected. Existing v1, bark, composed and scholarly namespaces remain
separate.

`verify` requires an accepted head. `verify-delivery` verifies that head when
present, otherwise verifies completed rejected-audit evidence and reports its
rejection explicitly. It cannot turn a failed experiment into an accepted model.
Raw corpora, action captures, intermediate checkpoints and generated JSON stay
under ignored work directories. Source control admits authored TSV contracts,
named compiled release artifacts and compact measured evidence.

## Measured delivery

The first v2 release was accepted on 2026-10-03. Its immutable release ID is
`7b9271bf8ba65a5494246fd436983f041490660777de18768b718af1001eaa2a`, selected by
the [current pointer](../models/gameplay/npc-pilot-v2/current.tsv). The compact
bundle contains inference weights, complete optimizer checkpoints, the frozen
recipe, measured reports and the consumed-audit receipt. All release gates passed.

The six rounds collected 22,026 records and applied 440,520 updates to each
profile. All 1,152 base episodes and 960 policy-prefix episodes were admitted;
none required the fatal-proposal guard or were rejected in this selected run.
The guard's recovery behavior is tested independently. Intermediate evaluations
show that deterministic continuation does not guarantee monotonic quality:

| Completed epochs | Round records | Cumulative updates | Development completion |
| --- | ---: | ---: | ---: |
| 20 | 1,932 | 38,640 | 100% |
| 40 | 3,992 | 118,480 | 100% |
| 60 | 4,120 | 200,880 | 96.35% |
| 80 | 3,870 | 278,280 | 100% |
| 100 | 3,992 | 358,120 | 100% |
| 120 | 4,120 | 440,520 | 100% |

Only the full 120-epoch candidate was eligible. Two earlier development-only
experiments using ordinary action loss at routing temperatures 32 and 64 failed
the unchanged module-contribution gate. Neither consulted the reserved audit.
The selected recipe adds the routing penalty described above and retains every
quality, resource and comparison threshold.

The sealed audit evaluated 24 previously unused families with 48 variants each.
Every actor used its own observations and history, including failed episodes.

| Audit measurement | Trained model | Reference or requirement |
| --- | ---: | ---: |
| Overall completion | 1,098/1,152 (95.31%) | At least 90%; planner 100% |
| Survival | 1,098/1,152 (95.31%) | At least 95%; planner 100% |
| Item completion | 384/384 (100%) | At least 80% |
| Hazard completion | 330/384 (85.94%) | At least 80% |
| Cue completion | 384/384 (100%) | Reactive controller 50% |
| History-disabled completion | 852/1,152 (73.96%) | Identical corpus and update budget |
| Illegal executed actions | 0 | Exactly 0 |
| Deaths / timeouts | 54 / 0 | All deaths occurred in hazard episodes |

The paired family-block history benefit was 21.35 percentage points, with a
95% interval of [12.50, 30.73] points. The learned model remained 4.69 points
behind the observation-limited planner in completion and survival; the paired
95% difference interval was [-8.85, -1.04] points. This does **not** support the
frozen two-point equivalence claim. Lower mean terminal decision cost includes
early failed episodes and is not evidence of a more efficient successful policy.
No external language model was run, and general assistant parity was not assessed.

On the fixed development composition sample, routing shares were 49.943% and
50.057%. Removing either module changed mean action loss by about 1.49e-6,
passing the frozen 1e-6 gate. Both module-only actors completed the same number
of audit episodes as the full mixture. These results establish numerical
contribution; they do not establish distinct module roles or a completion gain
from combining the modules.

The measured deployment footprint is 15,744 scalar parameters, 126,744 bytes for
the resident model and 7,472 bytes per NPC adapter plus neural scratch, excluding
allocator overhead. Deployed optimizer storage is zero. On a 12th Gen Intel Core
i5-12400F, Windows x64, MSVC 19.38 with `/O2 /Ob2 /DNDEBUG`, C11 and extensions
disabled, 256 warmup calls preceded 2,048 samples per workload:

| Workload | p95 | p99 | Execution scope |
| --- | ---: | ---: | --- |
| Normal policy | 15.6 us | 24.0 us | Four independently reset sessions, one worker |
| Forced fallback | 0.1 us | 0.1 us | Complete adapter and admission path |
| Serial 32-NPC batch | 572.9 us | 766.0 us | Entire batch, one worker |

These measurements describe this hardware and scheduler, not engine frame-time
guarantees. See the release's `report.tsv`, `training.tsv`, `comparison.tsv` and
`benchmark.tsv` for exact counters, recipe identities and confidence intervals.
The v2 audit batch is now consumed; another independent improvement claim needs
newly reserved families. Existing v1 evidence and other specialist heads remain
unchanged.

Local verification passed all 40 CTest suites, strict C lint and formatting,
strict API documentation, 99 scholarly tests and 198 chat-quality tests. After
publication, a fresh work directory regenerated every corpus and coverage report
from source and reproduced both accepted checkpoints byte for byte, including
weights, Adam moments, counters and shuffle state. Windows was tested locally;
the configured Linux, macOS and Windows CI matrix has not run remotely for these
uncommitted changes.
