# Native domain training through Life

The installed C11 interface includes `centroid_life_domain.h`. Its first built-in
adapter is an authored categorical probe, with three actions and visible inputs
`x` and `y`. A separate native verifier supplies `(x + 2*y) % 3` after the
complete round has proposed actions. This adapter exercises the training engine;
it does not establish coding, gameplay or conversational quality.

The world and policy support a configured count of two through eight ownership
groups. Four remains the engine default; `group_count=0` selects four for source
initializers that omit the new field. Capacity is eight, and unused public array
entries are zero. Retirement preserves the configured ownership universe.
The categorical probe uses four centroid experts per group. Its current expert
count is distinct from the planned capacity of 128 experts for other adapters.

## Run locally

Build the normal native project, then run the C probe command:

```powershell
cmake -S . -B build/native-context
cmake --build build/native-context --config Release
build/native-context/Release/cgai_life_probe.exe run build/native-context/probe.domain --groups 8 --steps 1
build/native-context/Release/cgai_life_probe.exe inspect build/native-context/probe.domain
build/native-context/Release/cgai_life_probe.exe resume build/native-context/probe.domain --steps 7
```

The command admits two distinct authored TRAIN observations, `(1,0)` and `(0,2)`.
Their target is independently verified as action one; equal initial heads choose
fallback zero. It reports frozen predictions, measured receipts and group clocks.
Per-UID visit and deferral counts report which groups processed those tasks.
Resume loads the complete owner, including its queued tasks and replay, before
continuing authentic world generations. The initial fixture is deterministic and
does not depend on either probe target. For smaller worlds use `--groups 2..8`.

## Ownership and rounds

Admission copies a typed visible task, source identity, family, split, reviewed
flag, legal actions and eligible stable UIDs. It accepts no target. Reviewed
TRAIN inputs enter a bounded FIFO. Held-out inputs can be predicted without
admission. The current native registry supports the categorical probe and
[native NPC actions](native-npc-training.md). Structured neural tasks require
another concrete native adapter.

A generation runs on a private clone. Life computes authentic causal contacts
and cell actions. Each contact reserves at most two compatible FIFO tasks.
All per-UID and combined domain proposals use one frozen domain parent version.
Only after every proposal is frozen does the native verifier produce domain
targets. Those targets are separate from the recorded cell teacher and toggle
actions. Commit publishes the world, auxiliary policy, domain policy, queue,
replay, counters and receipts together. A failed generation leaves its incumbent
unchanged; completed prior generations remain published.

The cell policy and probe share the participant-restricted Adam implementation.
Embeddings, encoder and bias remain frozen. Each participating group owns its
outer/inner centroids, readout, class heads, moments, decay and Adam clock.
Historical replay is admitted only for original eligible UIDs that also
participate in the current authentic round. Auxiliary cell replay likewise
intersects the current round's participant union. Extinction retains learned
state; the domain owner currently requires merges disabled, pending coordinated
domain consolidation.
The fixed encounter renews every 32 training generations, independently of task
content and targets. Renewal preserves group UIDs, ancestry, model state, replay
and cumulative diagnostics. Frozen evaluation preserves the renewal counter.

Frozen prediction performs no verification, queue admission or optimizer update.
Frozen evaluation advances cells with their existing policy and leaves domain
training state intact. Domain snapshots persist complete numerical state,
stable UID bindings, FIFO, replay, round receipts and the embedded Life snapshot.
Receipts retain the latest complete round and admitted replay; older deferred
outcomes require an external trace. Full event-ledger reconstruction remains open.
Loads validate a private candidate before replacing the owner.

## Compatibility and own-work context

The public package is API/ABI version three. New files use Snapshot3, Context3,
Policy2, and two-digit Trace2 claims when the configured count exceeds four.
The loaders retain four-group Snapshot1/2, Context1/2 and Policy1 with their
original semantic hashes, heads and clocks. Saving a migrated owner changes
its wire representation while retaining its four-group numerical state.
The existing 1,086-record Context2 memory loaded with its original hash
`1552317308497463996`, five feedback observations and four clocks of 40.
Its Context3 copy matched after each of 32 continuation generations. The original
artifact remained unchanged; this establishes migration and restart behavior.
An eight-group native probe run through generation 40 matched a fresh-process
one-plus-39 continuation byte for byte, including the encounter renewal at 32.
Both ended with two verified observations, two owned updates and domain version
one; both probe actions changed from zero to one. These measurements establish
this fixture's behavior, without establishing quality on other task families.
Source-search state format two declares capacity eight and serializes configured
counts; older pinned source-search bundles belong to their original executable
and source inputs. Their context files remain independently loadable.

The [native context pipeline](native-context.md) continues to consume this
codebase, implementation activity and explicitly attributed LLM proposals.
`cgai_life run --groups 8` and `cgai_context ingest --groups 8` select eight groups;
loaded owners retain their saved count. LLM text supplies attributed context and
does not establish outcome authority. The [source evolution loop](native-code-evolution.md)
has its own verified TRAIN feedback and separate development/confirmation gates.
To consume updated working source while retaining existing context and feedback
heads, use `cgai_context ingest --root . --input PREVIOUS.context --out NEXT.context`.
Seed and group-count overrides apply to fresh owners only.
