# Centroid Life

The repository's main goal is an installable, local C11 AI SDK: task-specific
centroid models for code assistance, chat and gameplay, improved through
Life-authorized training with attributed context and independently verified
targets. The [product plan](PRODUCT_PLAN.md) defines the delivery order and release
gates, starting with a code-context helper for existing LLM applications.

The implementation now includes an independent inference runtime, optional Life
training/tools, portable immutable model bundles and relocatable static/shared
CMake packages. The native code-context example returns bounded attributed
evidence; a separately qualified small model selects a verified search strategy.
Installation, source archives and local training/export/rollback are tested on
Windows with MSVC/clang and Linux with GCC. See [SDK use](docs/SDK.md),
[bundle format](docs/BUNDLE_FORMAT.md) and [reference assets](models/README.md).
Start with the [local code-assistance walkthrough](docs/CODE_HELPER_TRIAL.md).
The [first installed use-case check](research/sdk-release/CODE_HELPER_USE_CASE_REPORT.md)
records source evidence, editor refresh, abstention and model rollback results.
SDK code and original model assets are [MIT licensed](LICENSE), as authorized
by the owner on October 6, 2026. The
[research roadmap](research/ROADMAP.md) records implementation status, measured
results and interpretation limits. The original design is retained in the
[archived fresh-start blueprint](research/archive/FRESH_START_BLUEPRINT.md).
The [blueprint closure review](research/verification/2026-10-05-blueprint-closure/README.md)
records the final workflow, capture and source-freshness contract checks.
The [subsequent durability review](research/verification/2026-10-05-contract-durability/README.md)
records failed-capture persistence, full-suite compiler checks and frozen
physical-contact provenance for finite code trials.

All project-authored runtime, capture, training, evaluation and test code is C.
CMake, CTest, native compilers and the operating system are external dependencies.
The fresh repository has [no required foreign-language components](research/MIGRATION.md).
Local operation needs no script runtime, database service or hosted LLM.

## Build

Windows with Visual Studio:

```powershell
cmake -S . -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Linux with GCC or clang:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Use CMake 3.20 or newer and a C11 compiler. Executables are
`build/Release/centroid.exe` for Visual Studio and `build/centroid` for Linux.
Ninja/clang builds also work. Code experiments require an explicit clang/GCC
executable, or MSVC with its include and link environment configured. Tests cover
the mathematical, ownership, quarantine, continuation and publication contracts;
release verification and toolchain details are linked from the roadmap.

A full test build always registers the finite `experiment` and `code_suite`
checks. `CENTROID_TRIAL_COMPILER` accepts a native compiler executable path or
name; automatic selection looks for clang/GCC, then uses the configured supported
C compiler. Configuration compiles and links a C11 probe and fails clearly when
that dependency is unavailable. For cl/clang-cl, configure and run CTest from a
developer environment with the required SDK, include and link paths.

```text
cmake -S . -B build -DCENTROID_TRIAL_COMPILER=clang
```

`-DCENTROID_BUILD_TESTS=OFF` builds the library and CLI without the candidate-test
compiler requirement. That configuration provides no full-suite test evidence.

## Train on the local codebase

Create a `runs` directory, then use a compatible native executable throughout:

```powershell
build/Release/centroid.exe new runs/local.clife
build/Release/centroid.exe scan runs/local.clife .
build/Release/centroid.exe admit runs/local.clife llm data/train/llm-context.txt "attributed external LLM proposal"
build/Release/centroid.exe query runs/local.clife "participant centroid"
build/Release/centroid.exe record runs/local.clife 1
build/Release/centroid.exe train runs/local.clife 4
build/Release/centroid.exe report runs/local.clife
```

The supplied LLM file contains attributed suggestions. Proposals and activity are
inputs; independent source targets or admissible TRAIN measurements provide
supervision. `scan` captures dirty working bytes, immutable versions and provenance.
Current means the latest admitted version: rescan after edits. Capture hooks are
explicit; there is no background editor watcher or access to unavailable activity.
Scanning also refreshes admitted absolute SOURCE aliases for files it actually
reads. On Windows, differently cased aliases require matching native file identity;
case-sensitive directories therefore keep distinct files separate. Alias refresh
retains immutable historical versions and the original path and attribution.

`train STATE EPOCHS [GENERATIONS_PER_EPOCH]` dispatches the library's
[c_source_train API](include/centroid_source.h). Each epoch selects the first 32
current TRAIN SOURCE records by immutable ID and attempts four target-independent
positions per record, including byte zero and EOS: at most 128 enqueue attempts.
The report distinguishes available, selected and omitted sources; absolute aliases
count as separate records, and duplicate positions count as attempts. Epochs are
bounded to 1–1000 and generations per epoch to 0–1,000,000. Invalid arguments and
research owners fail before mutation. Runtime failures return library progress
and the current committed state; the multi-epoch operation does not roll back
earlier work. The caller owns persistence, and the CLI saves successful calls.

`step STATE GENERATIONS` advances existing tasks.
`next STATE SOURCE_ID BYTE_OFFSET` performs target-free next-byte/EOS inference
from an immutable TRAIN source prefix. `record` inspects exact provenance and
bytes, including quarantined records, without changing their learning permissions.

## Capture native work

```powershell
build/Release/centroid.exe run runs/local.clife runs/compiler-version.log 10000 clang --version
```

`run STATE NEW_RAW_LOG TIMEOUT_MS PROGRAM [ARGUMENTS...]` executes direct argv
with null stdin, merged binary output, a deadline and child lifetime cleanup.
The raw output path must be new. A complete raw artifact is separate from the
bounded admitted ACTIVITY view; every omitted byte is reported. The command
reports capture status separately from the child exit: inspect the observed exit
before treating a check as passed. Capturing a successful build provides activity,
not automatic utility labels. Each platform's checks use its native runner.
An admitted receipt is saved even when capture returns a capacity or I/O failure;
successful persistence keeps the original failure exit. Rejections that admit no
receipt preserve the checkpoint. The printed capture status remains separate from
the child's exit status.
Before launching a child or creating output, TRAIN work capture checks the working
directory and the raw output's existing parent. Known reserved development, audit
and held-out fixture locations, non-directory parents and link/reparse traversal
are rejected. Windows output names must be ordinary files: alternate streams,
device names, wildcards and trailing dots or spaces fail preflight. These checks
preserve the incumbent capture result and context on rejection.
Existing Windows directories expand to long names so short aliases cannot bypass
reserved-location checks. Launch and post-run verification retain canonical paths.

Quarantined evaluators use separate native experiment capture rather than admitting
their held-out inputs or results as TRAIN activity. Admission APIs require honest
kind, split, path and attribution; raw-byte callers cannot rely on the system to
discover hidden evaluation content in arbitrary bytes.

## Independently verify code candidates

```text
centroid trial STATE NEW_OUTPUT_DIRECTORY COMPILER
centroid suite STATE PROJECT_ROOT NEW_OUTPUT_DIRECTORY COMPILER
centroid apply ACCEPTED_DIRECTORY PROJECT_ROOT PARENT_SHA256
```

`trial` retains the original finite range-predicate catalog. `suite` covers checked
affine size arithmetic, sorted lower-bound lookup and unsigned saturating addition.
It freezes the exact current declared project source plus supported attributed
proposal/activity inputs, compiles every isolated candidate and obtains independent
TRAIN measurements. Genuine Life contacts consume the bound receipts once. DEV
and AUDIT are opened afterward for publication gates and never provide fitting
targets. Sources, failed alternatives, raw logs, distributions, receipts and
checkpoints remain reviewable in a new immutable output directory.
Both workflows defer before output creation or tool launch when no eligible
physical pair is available or pending work remains. Before any trial they retain
`model-parent.centroid` and canonical `contact.bin`; every TRAIN receipt binds
both identities. This preflight reads the world; trainer stepping advances it.

The suite's real project patch replaces the correct linear implementation in
`src/domain/algorithms.c` with a verified binary lower-bound implementation.
Checked size and saturation production APIs remain correct; their separate repair
catalogs do not insert broken production code. Review `winner.c`, `patch.manifest`
and the complete measurements before the separate explicit `apply` operation.
Application requires the expected parent SHA256, accepted bundle, matching compiled
artifact/evaluator/log/header identities and the known fixed-site winner. Conflicts,
tampering, changed destinations and link/reparse traversal are rejected. The exact
retained `parent.c` provides rollback; applying a patch does not train or run tools.

The suite accepts only the declared linear parent, allowing formatting whitespace.
After the working source is optimized, repeat experiments in a fresh sandbox:
place the retained `parent.c` at its `src/domain/algorithms.c` site, copy the required
public headers into `include`, then pass that sandbox as `PROJECT_ROOT`. A verified
rollback is also possible as an explicit local action. The optimized working file
is not a fresh baseline and the suite rejects it. The retained
[project code suite](research/experiments/2026-10-05-project-code-suite/report.md)
documents the actual experiment and its model-transfer limits.
Historical `parent.c`, candidate sources and raw measurements remain in the local
research archive; a Git clone contains their reports and identities. Build-time
and test fixtures are supplied separately in the versioned `data/audit` headers.

## Run registered research and gated extensions

```text
centroid benchmark NEW_OUTPUT_DIRECTORY
centroid research NEW_OUTPUT_DIRECTORY
centroid extensions NEW_OUTPUT_DIRECTORY
```

Each command creates fresh owners and an immutable experiment directory; it does
not load or change the working checkpoint. `benchmark` measures the first source
quality baseline. `research` compares Life, frozen Life, deterministic scheduling,
single/multiple specialists, routing/eligibility, reversible pair tokens and
source/LLM/activity context conditions. Reports retain every seed, update budget,
distribution, causal tail, attempted rollout, retention probe and resource cost.
Numerical research comparators are internal and cannot serve as public production
training authorities. Compute/resource mismatches and failed margins remain
reported; implementation does not establish scheduler optimality.

`extensions` evaluates optional shared diagonal scaling, bounded learned cellular
interventions and specialist consolidation. The initial production recipe keeps
shared representation fixed, uses target-independent encounter renewal and does
not merge owners automatically. The library's
[extension API](include/centroid_extensions.h) creates independent candidate forks.
Shared updates require all physical participants; policy supervision uses world-only
lookahead, separate from domain answers. A merge requires an authentic pair contact,
a drained queue, stable lineage and archived original supervision. Promotion
remeasures explicit frozen probes and retention under registered margins, retains
the previous parent for rollback and preserves rejected candidates. These optional
recipes are available mechanisms; their measured gates determine promotion.

Current checkpoints write schema 3 and preserve the complete optional policy/shared
state, merge lineage and retired immutable task archive. Compatible older schemas
are validated on load. Retired TEXT supervision can be explicitly requeued with
fresh task IDs and the caller's current paired eligibility mask:

```text
centroid replay-retired STATE FIRST MAX ELIGIBILITY_MASK
centroid step STATE GENERATIONS
```

The replay command considers at most `MAX` archive entries starting at `FIRST`;
CODE receipts are never replayed. At least two existing owners must be eligible;
full masks are decimal `3`, `7` or `15` for two, three or four owners. Original
source/request/evidence bytes, old owner UIDs and consumed receipts remain unchanged.
Replay queues work; only later genuine contacts authorize parameter updates.
The ordinary `c_trainer_replay` API also supports bounded active completed TEXT tasks.

## Use a frozen local session

```text
centroid session-new STATE SESSION
centroid session-ask SESSION REQUEST_FILE
centroid session-generate SESSION REQUEST_FILE MAX_NEW_BYTES
centroid session-history SESSION
centroid chat SESSION
```

`session-new` deep-copies the model and immutable context. Later source admissions,
training or original-owner destruction do not change that snapshot. `session-ask`
and interactive `chat` return exact supported current SOURCE spans with path,
version, digest and attribution, or explicitly abstain. Proposals and activity
cannot substitute for source evidence. Enter `/quit` to leave chat. File-based
requests preserve binary bytes and explicit user/assistant history roles.

`session-generate` is a separate bounded greedy byte/EOS mode whose output is
labeled unverified. It preserves full requests, typed history, supported evidence
and causal prefixes; hitting the byte limit is distinct from predicting EOS.
No session operation trains, advances Life, executes tools or contacts a service.
Session persistence contains the frozen model/context and complete history,
validates deterministic generation on load, and requires a compatible build.
Create a new session to use an updated working model or source snapshot.

## Bounds and durable ownership

- Context admits at most 4096 records, 1 MiB per record and 8 MiB total exact
  bytes, with bounded provenance strings. History and quarantine consume capacity.
  Overflow is explicit and current duplicate admissions are idempotent at capacity.
  Scanning excludes secrets, dependencies, generated output, model artifacts,
  tests, data and research; held-out paths and link/reparse traversal are rejected.
- Retrieval uses current TRAIN records and case-sensitive ASCII word support,
  then centroid ranking. Unsupported queries abstain. Scores are evidence
  similarity. Typed training frames admit at most eight evidence records and
  1 MiB input; finite code context is bounded to 64 KiB without silent truncation.
- The initial ordered projection has 32 lossy features while original bytes and
  byte/EOS targets are lossless. Code actions, text symbols and cell interventions
  have separate meanings. Reversible pair-token research preserves exact bytes.
- Two to four production owners inhabit a synchronous 16×16 B3/S23 torus. Physical
  cross-lineage encounters authorize updates; eligibility requires a real pair.
  Independent owner clocks, moments and unused head readouts remain unchanged when
  not participating. Changed routing can still move unused-head probabilities.
- Canonical checkpoints include context, world, complete numerical/optimizer state,
  RNG, queues, pending/consumed receipts, coverage and optional extension archives.
  Complete candidates are validated before atomic publication. Exact floating-point
  continuation requires a compatible build and numerical environment; build IDs
  identify the compiler/version/processor, not every executable source byte.
- Native work capture retains full raw output up to 64 MiB with explicit failure
  at the cap. Session requests are at most 64 KiB; history has at most 16 turns and
  256 KiB of content, excerpts at most 4096 bytes and generated prefix/output at
  most 1024 bytes. Capacity failures preserve the incumbent session turn/history.

## Evidence

The excluded research tree retains protocols, full outcomes and scope limits:

The [research artifact policy](research/ARTIFACTS.md) distinguishes the committed
design, protocols and reports from locally retained historical run outputs,
copied fixtures, raw measurements, logs and snapshots. Their hashes are versioned;
the full local artifacts require a separate backup and are not included in a Git
clone. The five `data/audit` headers remain versioned because the native library
compiles them directly.

- [Matched comparative research](research/experiments/2026-10-05-comparative-research/report.md)
- [Gated extension candidates](research/experiments/2026-10-05-gated-extensions/report.md)
- [Broader project code suite](research/experiments/2026-10-05-project-code-suite/report.md)
- [Frozen evidence session](research/experiments/2026-10-05-frozen-session/report.md)
- [Exact reconstructed-source syntax verification](research/verification/2026-10-05-phase2-representation-validity/README.md)
- [Initial source quality](research/experiments/2026-10-05-source-quality/report.md)
- [Attributed-context range trial](research/experiments/2026-10-05-context-code-trial/report.md)
- [Blueprint contract closure](research/verification/2026-10-05-blueprint-closure/README.md)

Held-out outcome numbers remain in those excluded reports. Useful bounded learning,
verified finite edits and supported quoting are separate claims from general coding,
learned conversational fluency or causal context benefit. The authoritative roadmap
ties completion evidence to the blueprint; [primary references](research/REFERENCES.md)
and [registered protocols](research/PROTOCOL.md) retain the research rationale.
