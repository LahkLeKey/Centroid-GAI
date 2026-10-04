# Native codebase and LLM context

The `cgai_context` executable and the `cgai_life_context_*` functions in
`include/centroid_life.h` provide local C11 source memory. They replace the useful
source ingestion and lookup capability of the removed scripting stack without
restoring its HTTP, database or Git snapshot services. They read the actual
working files, including uncommitted edits.

This is lexical centroid retrieval with Life contact gating. The separate
[native source evolution loop](native-code-evolution.md) now uses retrieved
context and measured TRAIN feedback to learn finite C source choices. Neither
path establishes general code generation or semantic understanding. The later
neural/chat domain adapters and independent quality gates
in the [delivery plan](centroid-next-deliverables.md) remain necessary.

## Build and consume this repository

```powershell
cmake -S . -B build/native-context
cmake --build build/native-context --config Release
ctest --test-dir build/native-context -C Release --output-on-failure
build/native-context/Release/cgai_context.exe ingest --root . --out build/native-context/codebase.context --steps 64
build/native-context/Release/cgai_context.exe ask --input build/native-context/codebase.context --query "life collision training" --limit 3
```

Single-configuration generators place the executable directly in the build
directory. Output parent directories must exist. No Python, JavaScript,
TypeScript, network service, database, browser or hosted LLM is needed to execute
the pipeline.

Ingestion uses deterministic relative-path ordering and exact text chunks of at
most 2,048 bytes. Paths and line spans accompany every excerpt. Content identities
bind stored bytes rather than a Git commit. The scanner excludes hidden entries,
build/output/dependency directories, models, data and tests; it does not follow
symbolic links or Windows reparse points. This keeps generated experience and
held-out fixture answers out of automatic source admission. The native API allows
explicit development/audit records, which cannot enter fitting or answers.

After source changes, ingest into a fresh owner or retain the previous owner:

```powershell
build/native-context/Release/cgai_context.exe ingest --root . --input build/native-context/codebase.context --out build/native-context/updated.context --steps 64
```

Existing records remain versioned observations of the bytes originally admitted;
injecting a new version does not remove historical observations. This version does not implement
forgetting, automatic filesystem watching or freshness checks against live files.
Fresh owners accept `--groups 2..8`; four is the default. A loaded owner retains
its saved group count, feedback heads and optimizer state. `--seed` and `--groups`
cannot override an input checkpoint during re-ingestion.

## Inject context and activity

Write the context or activity you want to retain to a local text file, then run:

```powershell
build/native-context/Release/cgai_context.exe inject --input build/native-context/codebase.context --context build/native-context/notes.txt --kind llm --out build/native-context/with-context.context --steps 64
build/native-context/Release/cgai_context.exe inject --input build/native-context/with-context.context --context build/native-context/activity.txt --kind activity --out build/native-context/with-activity.context --steps 64
build/native-context/Release/cgai_context.exe inspect --input build/native-context/with-activity.context
```

`ingest` also accepts `--context FILE`. This is an explicit local input channel;
the executable does not automatically intercept editor, shell or LLM activity.
Source changes can be consumed by re-ingestion; tool results, corrections and
assistant summaries can be consumed as activity/context records. Include the
actual result and its provenance when recording compiler/test feedback. Notes
remain attributed proposals or activity, not independently verified code-task
targets, and are never executed as commands.

## Training and continuation

Each chunk has a fixed ASCII word/identifier histogram in 64 hash buckets. The
encoder is deterministic and versioned. Each ownership group has a separate
prototype for each chunk. At a real physical Life frontier, participating groups
fit a bounded batch of previously unseen eligible records to the encoded source
target. The target verifies reconstruction of the admitted bytes; it does not
verify the truth of an LLM note. Cell-edit supervision still comes from the
independent Life lookahead teacher.

Uninvolved group prototypes, counters and cursors remain unchanged. Only reviewed
training records can fit. Zero training epochs in the library configuration
disable both domain fitting and auxiliary gradient updates. The context adapter
requires learned mode and disables merges because coordinated domain topology
consolidation is not yet implemented.

```powershell
build/native-context/Release/cgai_context.exe train --input build/native-context/codebase.context --out build/native-context/continued.context --steps 64
```

The adapter renews the configured physical encounter every 32 generations. This
fixed placement schedule is independent of context content, targets and pending
queues. It retains the learned policy/optimizer, physical replay, domain state,
stable identities, cumulative diagnostics and generation counter. New context
therefore gets further opportunities for genuine contact after earlier patterns
stop interacting. The report distinguishes fitted records, deferred records,
physical contacts and domain fits; unfinished coverage remains visible. There is
no label-dependent reseeding or second trainer.

Frozen queries rank fitted records by centroid cosine similarity and require an
exact shared meaningful lexical token to reject hash-only matches. Results are
verbatim stored excerpts with path, line span, content identity and kind. A query
with no supported overlap abstains. This is a bounded source lookup, not a
guarantee that every returned excerpt answers the question.

One atomic checkpoint contains source bytes and metadata, independent group
prototypes/cursors/counters, and the complete Life world, policy, optimizer and
physical replay. Loading validates the entire bundle before replacing the owner.
Format 3 declares capacity and configured ownership count, and preserves
independent action utility heads, their clocks and any pending decision.
Older formats 1 and 2 load as four-group owners with their original hashes;
format 1 loads with empty action heads. The public package is API/ABI version 3.
Corruption and trailing data reject without changing it. Querying does not change
continuation state. Hashes are deterministic checks rather than cryptographic
authenticity signatures. Exact numerical continuation assumes the same build and
numeric environment and the C numeric locale.

The native tests cover changed lookup behavior after contact, participant
isolation, train-only admission, label-free frozen queries, exact continuation,
bundle rejection and bounded filesystem ingestion. These are machinery and
retrieval checks; assistant intelligence and general coding improvement remain
unassessed. `inspect` reports action-head observations and clocks separately from
lexical fitting, so stored notes do not imply measured action learning.
