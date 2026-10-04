# Centroid Life

The current build uses the native boundary in `include/centroid_life.h`, with
native CLI inspection and complete atomic checkpoints. The JavaScript viewer and
legacy services are removed. Broader model adapters remain in the
[next deliverables](centroid-next-deliverables.md). The separate
[native context adapter](native-context.md) consumes code and LLM/activity notes
through collision-gated lexical memory.

Centroid Life is a native C11 experiment combining Conway's Game of Life with
four persistent centroid specialists. Contact between lineage patterns creates
training examples. In learned mode, the participants propose bounded changes to
the next generation and train on the observed collision state. Persistent
coupling can lead to a validated model consolidation; sustained absence of
contact records separation.

The tool builds on the existing composed gameplay encoder, hierarchical routing
and analytic gradients. Its Life adapter owns participant-specific AdamW state
and exact checkpoints. It is a separate development profile, with no changes to
the accepted NPC models or their release artifacts.

## Build and run

```sh
cmake -S tools/life -B build/life/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/life/native --config Release
ctest --test-dir build/life/native -C Release --output-on-failure
cmake --build build/life/native --config Release --target life_lint life_format_check
```

Visual Studio places executables in `build/life/native/Release/` and adds `.exe`.
The following examples use that layout; single-configuration generators place
the executable directly in `build/life/native/`.

```sh
build/life/native/Release/cgai_life.exe run --scenario crowd --mode learned --seed 42 --steps 64 --epochs 8 --out build/life/crowd
build/life/native/Release/cgai_life.exe run --scenario gliders --mode conway --out build/life/conway
build/life/native/Release/cgai_life.exe run --scenario gliders --mode teacher --out build/life/teacher
```

`run` defaults to learned mode, the gliders scenario, seed 42, 64 generations,
eight training epochs per collision generation, and the output prefix
`centroid-life`. Supported scenarios are `gliders`, `block`, `blinkers`, and
`crowd`; the first three introduce a glider encounter with another glider, a
block, or a blinker. All worlds contain four initial identities. Seed changes
pattern translation and model initialization. Seeds range from zero through
4,294,967,295; steps range from zero through 100,000; CLI epochs range from one
through 64. `--no-merge` disables model consolidation. Output directories must
already exist.

Each successful command writes:

| Artifact | Contents |
| --- | --- |
| `PREFIX.trace` | Versioned ASCII world frames, identities and collision diagnostics |
| `PREFIX.tsv` | Per-generation population, outcomes, edits, training, loss and state hashes |
| `PREFIX.snapshot` | Atomic complete world, identities, conflicts, recipe, replay, policy and optimizer bundle |

Use `cgai_life inspect PREFIX.snapshot` for terminal inspection, or read the
native `.trace` and `.tsv` files. A frame's frontier describes the most recent
transition into that generation. A resumed trace starts with the saved world and
regenerates transition diagnostics from its next step. Lineage
populations count claims, so a cell with shared ancestry can contribute to more
than one lineage population.

## Exact continuation

```sh
build/life/native/Release/cgai_life.exe resume build/life/crowd.snapshot --steps 32 --out build/life/continued
build/life/native/Release/cgai_life.exe replay build/life/crowd.snapshot --steps 32 --out build/life/replayed
build/life/native/Release/cgai_life.exe evaluate build/life/crowd.snapshot --steps 32 --out build/life/frozen
build/life/native/Release/cgai_life.exe inspect build/life/crowd.snapshot
```

Both commands execute the same deterministic continuation from the saved state.
`replay` is a convenient second run for comparing TSV hashes; it resumes learning
with the saved recipe. It does not reconstruct generations preceding the saved
snapshot. `evaluate` advances the world with frozen policy inference, without
teacher targets, replay admission, optimizer updates or merges. Continuation
accepts only steps and output prefix;
changing the learning recipe requires a new run.

Version-two snapshots contain the policy and optimizer in one file. Legacy
version-one snapshots still require the adjacent `.policy` sidecar, derived from
the selected snapshot path. The loader validates version, fixed
shape, bounded integers, categorical observations, topology, seed and ancestry/
routing-mass consistency, ring metadata and the complete continuation-state hash,
and rejects trailing non-whitespace data.
Weights and numerical state use hexadecimal floating-point text for exact
round-trips. The native CLI uses the default C numeric locale. Embedding callers
must keep that locale while reading or writing checkpoints. Exact numerical replay
assumes the same binary and numerical environment. Checkpoint writes replace the
destination atomically after completing a temporary file in the same directory;
failure preserves the existing checkpoint. Traces and metrics are separate
diagnostic outputs.

## Generations and collisions

The world is a 32 by 32 torus. Each cell carries four possible lineage claims;
any nonzero claim mask counts as one live cell for Conway B3/S23. Normal survival
and birth are computed synchronously from the old generation. A surviving cell
keeps its claims; a new birth inherits contributing neighbor claims.

Different lineage claims in local neighborhoods create a contact graph.
Connected encounters become one conflict so a multi-participant collision has
one consistent update. Up to four disjoint conflict patches are produced per
generation. Each patch selects at most six frontier cells, and its 23-output
head contains fallback, deliberate no change, six single-cell toggles and
fifteen two-cell toggle pairs. Outputs are restricted to available frontier
cells. Local neighbor support prevents creating an isolated cell remotely.
Every proposal reads the same generation and frozen policy version; the host
validates all proposals before committing a generation.

The three modes provide useful comparators:

| Mode | Evolution and learning |
| --- | --- |
| `conway` | Exact B3/S23 evolution, with identity and collision tracking |
| `teacher` | The lookahead teacher chooses collision edits; records are retained without policy training |
| `learned` | Participant-restricted centroid mixtures choose edits; teacher targets train their model slices after commit |

The teacher tries every legal nonfallback action, applies that edit, and follows
eight additional Conway generations. It ranks outcomes lexicographically using
a fixed integer score: survival of all initially viable participants, number
of viable participants, sustained separation or coupling, balanced retained
population, total retained population and fewer edits. Population contributions
are capped at 16 per participant. Other conflict patches use fallback during
each candidate rollout. Ties favor the lowest output ID, so deliberate no change
wins equally scored comparisons. This is bounded local supervision, not an
oracle for the long-term world.

## Learning and model consolidation

Observations have sixteen categorical fields: six frontier live/neighborhood/
claim-role values, six occupancy/claim-role values, conflict-age and two lineage
population buckets, and participant count. The composed model has a shared
16-dimensional category embedding, a 48-dimensional encoder, four outer
modules, four inner experts per module, and one 23-output task head.

The shared embeddings and encoder stay fixed in this first implementation.
Collision training updates only the participating modules' outer centroids,
inner centroids, conditional heads and decoders. Uninvolved modules keep their
weights and optimizer state. Updates use AdamW with learning rate 0.01, global
gradient clipping at 5, and weight decay 0.0001. Up to sixteen deterministic
samples from a 256-record replay ring enter each collision-generation training
batch; training uses the configured additional complete epochs. The report's
before/after losses measure the retained replay records. No pretrained Life
checkpoint is assumed; early learned decisions come from generic initialization.

Conflict resolution distinguishes several outcomes:

| Outcome | Meaning |
| --- | --- |
| Coupled | All participating lineages remain connected through contact for eight consecutive generations |
| Separated | All participants remain viable without cross-lineage contact for eight consecutive generations |
| Absorbed | Some, but not all, participating lineages disappear |
| Extinct | No participant remains viable |
| Merged | A coupled encounter also passes model consolidation and publishes one child identity |

A coupled conflict can attempt consolidation at most once per eight world
generations. The adapter trains and validates a private candidate, preserving
the live parent models if it rejects that candidate. Acceptance requires replay
coverage for every parent, at least 95% per-parent and global argmax agreement,
and mean negative log likelihood degradation no greater than 0.10. The candidate
must also preserve at least 95% of legal runtime-action selections using the
remapped participant mask. The survivor is the lowest participant slot; routing
mass accumulates there. Accepted merges
give the child a new UID and combined ancestry, remap cells and retained records,
and deactivate retired slots. Extinction alone leaves a topology slot active.

## Validation and interpretation

The native tests exercise Conway patterns, toroidal boundaries, lineage
propagation, synchronous bounded edits, conflict resolution, isolated training
updates, policy checkpointing and merge acceptance/rejection. Snapshot tests
compare learned continuation generation by generation, including the complete
model/optimizer hash, and exercise a full replay ring. They reject malformed
headers, shape, numeric input, trailing data and missing sidecars. CLI checks
compare independent resumed and replayed traces.

This experiment demonstrates executable collision learning and observable
changes to cell evolution. It does not establish that learning improves Conway
survival, that merged models outperform parents, or that the teacher's local
objective generalizes. Replay agreement is a consolidation gate on retained
experience, not an independent quality evaluation. Seeds currently vary
translation and initialization rather than providing a reserved scenario
benchmark. Meaningful quality claims require disjoint encounters and matched
Conway, teacher, untrained, trained-mixture and individual-module evaluations.

Encounter grouping follows the global graph of participating identities, so
geographically separate contact sites involving the same identities can share
one conflict. Only the first six canonical frontier cells in each component
enter its patch; later contact sites can remain outside the intervention budget.
Observations distinguish the first two participant roles explicitly, while
third and fourth participants share the remaining role class. These bounds make
the implementation reproducible and small, but limit multi-participant control.
