# Native own-history NPC training

The C11 package includes `centroid_life_npc.h` and the local `cgai_life_npc`
command. The opaque episode owner combines the native NPC simulator, independent
actor memory, actual action prefix and episode cursor with the Life domain
learner. Training uses authentic Life contacts and the same restricted optimizer
as the cell policy and categorical probe.

```powershell
cmake -S . -B build/native-context
cmake --build build/native-context --config Release
build/native-context/Release/cgai_life_npc.exe run build/native-context/cold.npc --groups 8 --steps 0
build/native-context/Release/cgai_life_npc.exe evaluate build/native-context/cold.npc --split dev --families 24 --variants 48
build/native-context/Release/cgai_life_npc.exe run build/native-context/trained.npc --groups 8 --steps 1024
build/native-context/Release/cgai_life_npc.exe resume build/native-context/trained.npc --steps 1024
build/native-context/Release/cgai_life_npc.exe evaluate build/native-context/trained.npc --split dev --families 24 --variants 48
```

Each training decision reconstructs the current episode from its initial recipe
and actual prior actions. It verifies current visible input and memory against
that history before admission. The model predicts from that exact visible input,
the owner queues it as reviewed TRAIN context, Life advances a generation, and
the host executes the previously frozen model action. The host never repairs a
model decision using a teacher or a private map. When the FIFO is full, bounded
Life generations drain it before the next host decision; observations are not
silently dropped. New episodes follow a fixed family/variant cursor independent
of targets and outcomes.

The generic domain record's `executed_action` is its frozen contact selection.
The NPC host serves a configured full UID mixture, which can differ from that
contact's participants. Its actual host action, outcome, prediction parent,
probability and context identity are recorded separately. Contact-limited
training and full-mixture serving share the same learned domain parameters.

The sixteen model fields are four neighbor tiles, target displacement in both
axes, inventory, currently observed cue, remembered cue, prior action/outcome,
cue age, stage, target proximity, bounded tick and visible mechanic. Source,
family and episode identities are provenance only. All sixteen fields must be
observed. The reserved private obstruction tile category is rejected, and legal
actions are derived from the declared visible input. Seven action IDs cover
fallback, wait, four movements and interaction.

The independent native verifier reads only those visible/history fields and
permissions after every contact proposal is frozen. It receives neither a host
world nor a caller target. The shared encoder is a fixed, target-independent
representation: 110 categories use 46 separate signed binary feature axes in a
48-dimensional hidden state. Embeddings, encoder and bias remain frozen;
participating UID slices own centroid/readout/head weights, moments and clocks.
Cold heads are tied and choose the declared fallback. This recipe makes the
visible categories distinguishable without adding learned target knowledge.

Frozen evaluation starts every requested family/variant episode independently,
uses the controller's own observations and memory, and executes every decision
without repair. Deaths and timeouts stay in the denominator. Reports include
success, death, timeout, decisions, illegal attempts/executions, blocked/recovered
transitions, fallback use and mechanic totals. The complete training owner hash
must remain unchanged. TRAIN and DEVELOPMENT can be evaluated; confirmation is
reserved until its durable consumption and release protocol is implemented.

The fresh room recipe freezes 24 families per split and 48 rotation/reflection
or cue/pattern siblings per family. ITEM and HAZARD have mandatory wall barriers;
CUE has a longer dogleg approach and paired branches. These differ from the
historical NPC fixtures. Family membership is frozen before targets or outcomes;
neutral geometry comparisons must establish split and historical disjointness
before independent quality claims. A working adapter alone does not establish
an improvement in episode quality.

The package is API/ABI3 because the public typed task and round structures grew.
Probe tasks and checkpoints retain their version-one bytes and semantic hashes;
NPC domain blocks use envelope two. NPC episode bundles additionally save their
recipe identity, cursor, cumulative diagnostics, complete current action prefix,
host prediction metadata and nested learner/world/queue/replay/optimizer state.
The NPC world and memory are reconstructed from that pinned recipe and prefix
and checked against their saved full-state hash before publication. Production
builds pin numerical/runtime sources and API contracts in the recipe fingerprint.
A changed recipe, corruption or trailing data rejects without replacing the
incumbent. Older completed episodes are represented by aggregate diagnostics;
full historical event-ledger reconstruction remains open.

The [native context pipeline](native-context.md) consumes source, measured work
records and explicitly unverified LLM proposals separately. A proposal is useful
context for implementation; it does not supply an admitted NPC target or prove
task quality.
