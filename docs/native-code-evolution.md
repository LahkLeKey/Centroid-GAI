# Native source evolution with Life feedback

`cgai_life_evolve` connects local source ingestion, attributed LLM context,
physical Life encounters, native candidate evaluation and learned action choices.
The executable and its project-authored helpers are C11. CMake, a C compiler,
CTest, clang-format and clang-tidy provide the external build and checking tools.

This is a bounded source experiment. The action catalog changes one or two of
four initialization literals in `src/gameplay/gameplay_model.c`: embedding,
encoder, outer-centroid and inner-centroid scales. Each site has three declared
values; a complete four-site profile has an action ID from 0 to 80. It does not
generate arbitrary functions or implement general code repair.

## Run locally

Build the normal native project first, then use a new output directory whose
parent exists:

```powershell
cmake -S . -B build/native-context
cmake --build build/native-context --config Release
build/native-context/Release/cgai_life_evolve.exe --out build/source-experiment --candidates 2 --steps 64 --context build/native-context/llm-context.txt
build/native-context/Release/cgai_context.exe inspect --input build/source-experiment/memory.context
```

`--context` is optional and reads an explicit local text file. Its contents are
attributed LLM proposals, not executable instructions or verified targets. The
tool scans the current working source, including uncommitted edits, using the
[native context scanner](native-context.md). It excludes generated outputs,
data and tests from automatic source admission.

Source, validation inputs, external tools, initial memory and injected context
are content-bound. Changes during evaluation stop the run; finish source edits
and rebuild before starting it. Outputs inside the repository must be under its
excluded `build` directory; an output outside the repository also works.
Candidates live in separate output directories.
The tool leaves working source unchanged and writes the accepted source to
`best.c`, including the original baseline when no candidate passes.

## What learns

Before search, 64 Life generations fit source/context records. Search uses up to
the requested `--steps` and `--candidates` budgets. At each real contact, a
declared deterministic domain schedule uses the source world identity,
generation, conflict and frontier cells to choose one or two distinct source
sites. It ignores the auxiliary cell policy's selected output and toggle bits.
Genuine contacts can therefore schedule code trials even when the cell policy
chooses no world edit. The tool enumerates finite alternatives for those sites
and freezes a decision input
containing the current parent identity/profile and centroid-retrieved source or
LLM excerpts. Source and LLM kinds are ranked separately so a larger source corpus
cannot suppress a relevant fitted LLM proposal. The source excerpt prefers the
catalog's exact file. Historical activity and held-out measurement records do not enter
that input.

Each participating group owns a separate context centroid and utility readout
for each complete profile action. A supported readout predicts bounded utility
from the frozen lexical input; unsupported alternatives have neutral predicted
utility. The deterministic fallback wins ties. A successful
training measurement updates only the chosen action's participating slices with
projected SGD. Life cell-policy supervision continues to use its own independent
world teacher.

The native evaluator builds the unchanged baseline and every candidate, runs
CTest and formatting/lint checks, then scores separate TRAIN, development and
confirmation recipes. Choice utility is the clipped difference between parent
and candidate TRAIN mean loss. Both TRAIN reports must name the same pack and
record count. A content-bound receipt records the frozen choice, source
identities, losses and utility before the result is consumed once.

Development and confirmation reports remain candidate admission gates and never
teach the code-choice head. They are regression checks derived from the same
four authored Life scenario families with different seeds/warmups. They do not
establish transfer to fresh independent families or general coding quality.
Negative measured TRAIN utility can teach a choice even when its candidate is
rejected. Failed build, test, lint, launch or scoring stages defer the choice
without model updates because this runner cannot reliably distinguish code
failures from infrastructure failures.

The runner stores TRAIN measurement activity through the context adapter and
stores development/confirmation summaries as AUDIT records. AUDIT records can
neither fit lexical memory nor become retrieval answers. A final 64 generations
provide contact opportunities for the admitted training activity. Preparation,
search and activity generations are accounted separately in the report.

## Outputs and continuation

`run.tsv` records input identities, budgets, measurements, candidate admissions
and model counters. Each candidate directory retains its C source, native stage
logs, fitness reports, frozen `choice-input.txt`, edit/contact provenance and
training receipt. `memory.context` contains admitted source/context/activity,
lexical prototypes, learned utility heads and the complete Life continuation
state. `cgai_context inspect` reports both lexical coverage and choice-model
versions, observations, deferrals and per-group update clocks.

The source search also publishes immutable `checkpoint-NNNN` directories after
the measured baseline (generation zero) and after each fully processed search
generation. A bundle contains exact parent C bytes, complete centroid memory,
baseline/current fitness reports, fixed budgets, cumulative progress, pinned
source/tool/context identities and the completed receipt ledger. Each receipt
retains its frozen input and legal alternatives, contact provenance, exact
TRAIN/deferred proof and separate audit admission. Publication stages all members
before an atomic directory rename; earlier bundles remain intact on failure.

Pause an experiment after one completed search generation and resume it in a
new output branch:

```powershell
build/native-context/Release/cgai_life_evolve.exe --out build/source-paused --candidates 2 --steps 64 --context build/native-context/llm-context.txt --pause-after 1
build/native-context/Release/cgai_life_evolve.exe --out build/source-resumed --resume build/source-paused/checkpoint-0001
```

`--pause-after` limits completed search generations in this invocation; it does
not reset the saved total budget. Zero pauses after the validated baseline.
Resuming skips source admission, preparation and baseline scoring, restores the
same parent and receipts, and continues the remaining budget. Fresh seed,
context, memory and budget overrides cannot accompany `--resume`. The loader
checks complete member hashes, receipt lineage, the compiled build recipe and
current pinned inputs before creating output or advancing Life. Source, tool,
build-configuration and external-context changes reject. A resumed invocation
with `--pause-after 0` keeps the original immutable bundle and hash chain.

Paused runs do not perform the final 64 activity generations. Budget exhaustion
performs them once, writes the final artifacts and publishes a distinct
`checkpoint-NNNN-final` bundle. A finalized bundle cannot start another resume.
Bundles are generation-boundary recovery points: an interrupted in-flight
candidate is retained as evidence but is not resumed or merged into the last
complete generation. Reconstruction from initialization and recovery of pending
external evaluations remain separate work. Exact numerical continuation assumes
the same native build, numeric environment and C numeric locale.

Reuse the learned memory in a new experiment:

```powershell
build/native-context/Release/cgai_life_evolve.exe --out build/source-experiment-next --candidates 2 --memory build/source-experiment/memory.context
```

This continues the Life/context models while scanning current source again. It
retains the checkpoint's seed, epoch count and Life recipe; `--seed` applies to
fresh memory only. The report distinguishes the requested fresh seed from the
loaded recipe source, and the checkpoint carries the actual configuration. It
starts a new source-search baseline from working files; it is not exact resume
of the previous search parent, budget or candidate lineage. Apply and review a
chosen `best.c` separately when its measured scope warrants it.

Checkpoint format 2 includes action heads and an optional pending decision.
Format 1 context checkpoints still load with empty action heads and upgrade on
the next save. The public API allows at most one pending choice, prevents world
advance until observation, requires exact owner-issued decision matching and
rejects stale, unverified or held-out feedback without mutation. Evidence hashes
and tokens are deterministic integrity checks, not cryptographic measurement
authority; native callers must supply independently measured results.

Tests establish contact gating, changed action selection, participant isolation,
frozen prediction, invalid-feedback preservation and exact checkpoint
continuation. They do not establish autonomous programming capability. The
broader [native migration deliverables](centroid-next-deliverables.md), including
general model adapters, independent evaluation and local structured conversation,
remain in progress.
