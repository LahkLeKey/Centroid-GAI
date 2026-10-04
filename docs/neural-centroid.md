# Centroid neural network

Migration direction: retain the C11 model math, inference and format contracts
as inputs to the [Life training deliverables](centroid-next-deliverables.md).
Existing public training APIs, script workflows and CLI training commands are
retiring; the new Life engine becomes the production mutation authority.

The working neural prototype learns token embeddings, an ordered context encoder,
centroid positions, and token distributions through backpropagation. It predicts
the next token from a fixed window, then feeds generated tokens back into that
window. The separate [chat service](chat-service.md) adds structural conversation
training and persistent question conditioning to this learning core.

The implementation uses a feed-forward encoder with soft centroid routing.
There is no transformer or attention layer in this architecture. The existing
HTTP model endpoints use the separate count-based engine; they do not serve
`.cgnn` models. Follow the [chatbot roadmap](chatbot-plan.md) for the remaining
work and [API contract](api-contract.md) for the available HTTP behavior.

## Small models for gameplay features

Give each bounded gameplay capability its own typed task head, scenario suite and
acceptance policy. Aligned specialist modules combine within one shared centroid
network. The engine supplies a compact
state description, receives a proposal, and checks that proposal before applying it.
An event/state contract can serve several genres: an encounter specialist can use
threat, available space and difficulty rather than assuming a particular combat system.
The engine retains authoritative rules, legal actions and deterministic fallbacks.

| Specialist | Proposed output | Gameplay acceptance checks |
| --- | --- | --- |
| NPC barks and short dialogue | Short text or an authored line ID | Event relevance, lore consistency, repetition and length |
| Encounters and loot | Template IDs and bounded parameters | Legal combinations, difficulty and economy limits |
| Quests and rooms | Existing template IDs and parameters | Reachability, prerequisites and completion across seeded simulations |
| Tactical intent | An enumerated intent for an existing controller | Legal actions, scenario success and stability |
| Procedural variation | Parameters for an existing content generator | Valid generated content, diversity and generation cost |

The composed network implements bark selection and tactical intent with separate
task-local output IDs and automatic promotion. Encounters, quests and procedural
variation remain integration targets. Gameplay heads operate on bounded categorical
state without a text tokenizer. Further tasks need authored teachers and acceptance
gates. The scholarly seed has no gameplay acceptance results.

Each specialist's versioned contract should specify the input fields and their stable
encoding, output schema, vocabulary/shape limits, maximum output tokens and bytes,
fallback, and permitted update cadence. Its performance profile should include maximum
resident model bytes, bytes per active session, active session count, and measured
per-step and complete-request latency on named target hardware. For example, a bark
can run on an event and an encounter proposal before a room loads; calling every
specialist every frame is an engine scheduling choice that needs its own benchmark.
Treat numerical budget values as targets until measurements establish them.

### Composed centroid gameplay network

The [observable NPC pilot](npc-planner.md) extends this foundation with complete
episodes, bounded observed memory and independent family evaluation. Its separate
`npc-pilot-v1` profile implements the [six-milestone epic](npc-planner-epic.md)
through a deterministic C11 simulator, teacher, training tool, comparative
evaluation, performance benchmark and sealed release workflow.
The [v2 recovery profile](npc-planner-v2.md) adds successive-policy training
trajectories, independent detour families and authoritative reference replay.

The [composed C11 API](../include/centroid_gai_gameplay.h) owns one shared encoder,
learned outer routing centroids and an internal expert bank for each specialist.
The [task registry](../data/gameplay/composed-v1/tasks.tsv) defines bark head zero
and tactical intent head one. The [module registry](../data/gameplay/composed-v1/modules.tsv)
defines social and tactical modules; both contribute to both requested heads.
Each module therefore participates in a neural mixture for the same task, rather
than supplying an unrelated output to a host-side dispatcher.

```text
typed state + requested task
    -> trainable categorical embeddings and shared encoder
    -> normalized distance routing across specialist centroids
    -> normalized distance routing across each specialist's internal centroids
    -> each module's task readout plus each internal expert's learned output bias
    -> mixture of conditional task-local output distributions
    -> permitted output ID, recent-ID suppression and fallback

p(output | task, state) = sum_module outer_weight(module)
                         * sum_expert inner_weight(expert | module)
                         * expert_probability(output | task, module, expert, h)

expert_probability = softmax(expert_bias + module_task_readout_weights * h / sqrt(H))
```

Each eligible specialist projects the shared encoded state through its own task
readout. Its internal experts reuse that projection and add their own learned output
biases before softmax. Dividing the projection by `sqrt(H)` keeps its scale tied to
the encoder fan-in. Both the module readouts and centroid routing learn through
backpropagation. This lets specialist predictions vary independently with state while
retaining their internal-centroid mixtures. Resource inspection includes
`maximum_head_multiply_adds` across the module readouts; projection work is reused
inside each module rather than repeated for every internal expert.

The contract orders nine categorical observations: event, danger, relationship,
setting, health, distance, cover, objective and readiness. Bark uses the first four;
intent uses event, danger and the five tactical observations. A task's relevant-feature
mask zeros irrelevant encoder inputs. Bark consequently retains the same prediction
across all tactical-context variants, while intent ignores relationship and setting.
Every module uses the same learned coordinate system. Independently trained legacy
checkpoints cannot be merged merely because their dimensions match.

Bark returns eight authored line IDs or silence. Intent returns abstain, wait,
approach, seek cover, engage, retreat or hold. The host supplies allowed output and
module masks and retains navigation, action preconditions and gameplay execution.
Fallback ID zero is always legal; empty permissions produce fallback without a
forward pass. A permitted normal request performs one complete composed forward
with no allocation, tokenization or I/O. Returned module weights describe routing;
module contributions describe the selected ID's posterior share. Probability is
model likelihood, not calibrated confidence.

Load inference weights with `cgai_gameplay_load`, inspect complete requested heap and
dense work with `cgai_gameplay_get_resources`, then allocate capped reusable sessions.
Sessions borrow a live immutable model and require exclusive access. Distinct sessions
may share a model. The general configuration supports up to sixteen categorical
fields, eight task heads, eight specialist modules and sixty-four IDs per task;
unused configuration and state entries must remain zero. The authored bundle uses
nine fields, D32/H64, two modules and sixteen internal centroids per module.

New categorical embeddings use orthogonal unit category coordinates whenever `D`
can represent the total category count. If both `D` and `H` cover that count, the
shared encoder initially maps each feature's active category to its own hidden axis:
active relevant categories produce `tanh(1)`, with all other axes zero. The authored
shape has 23 categories and D32/H64, so it uses this exact alignment. This prior
depends only on cardinalities and dimensions, with no task targets or event rules.
Configurations too small for the aligned encoder retain seeded dense encoder
weights; embeddings also use seeded dense values when `D` cannot hold all category
axes. Every embedding, encoder weight and bias remains trainable.

After this shape-based initialization, the native training recipe seeds each bank
from class prototypes computed exclusively from the
384 training records' initial encoded states. For each task-local target, the mean
initial hidden vector becomes one inner centroid. The matching task expert begins
with a +2 target preference and -2 alternatives; its other task heads begin uniform.
Each module receives small seeded prototype perturbations, and outer centroids start
near the aggregate training-state mean. Module readout matrices start at zero. This
is supervised RBF initialization, followed by ordinary joint AdamW updates to all
parameter groups. Development and test records supply no initialization targets or
coordinates. The recipe requires every output label to occur in training and at
least as many inner centroids as the sum of task output counts: sixteen for the
current nine bark and seven intent IDs. New tasks must satisfy that coverage and
capacity requirement when using this initializer.

`cgai_gameplay_train_continue` trains complete independent state/task/target examples
through both routers, the shared encoder, module/task readout weights and expert output
biases using clipped AdamW
with decoupled weight decay.
The deterministic balanced recipe performs 384 updates per epoch: four cycles of
the 48 bark training cases plus one cycle of the 192 intent training cases, followed
by the recorded seeded shuffle. Development and test each contain 12 bark and 32
intent cases, with state families kept together. Seed 42, routing temperature four,
learning rate 0.001, weight decay 0.05 and clipping five are pinned in the
[contract](../data/gameplay/composed-v1/contract.tsv). Checkpoints retain Adam moments,
epoch/update counters and shuffle state. Every promotion must reproduce the complete
checkpoint exactly from initialization with its producing recipe and compatible build.

The initial recipe trains 40 complete epochs. Development checks selected this
stopping point: longer runs overfit and reduce bark development accuracy. Additional
proposals train 20 epochs by default and must still pass every frozen gate; a
regressing proposal stays local and preserves the accepted model and checkpoint.
The trainer follows the pinned pass count rather than monitoring a split within
each epoch.

### Train and promote the composed bundle

The [gameplay workflow](../tools/gameplay/workflow.mjs) runs initialization, training,
quality evaluation, benchmarking and exact replay through the C11 tool. Node owns
registry validation, hashes, locks and atomic publication. It hashes an explicit
dependency list with canonical LF source text; unrelated chat or legacy bark sources
cannot silently alter this recipe. New task descriptors and module eligibility are
validated generically before any native training starts.

```sh
cmake -S . -B build/dev
cmake --build build/dev --config Release --target cgai_gameplay_tool cgai_composed_demo
node tools/gameplay/workflow.mjs train --tool ./build/dev/cgai_gameplay_tool
node tools/gameplay/workflow.mjs verify
node tools/gameplay/workflow.mjs replay --tool ./build/dev/cgai_gameplay_tool
```

Visual Studio builds use `--tool .\build\dev\Release\cgai_gameplay_tool.exe`.
`train` initializes a missing head or proposes additional complete epochs from the
accepted checkpoint. A rebuilt executable must first replay that accepted checkpoint
byte for byte before it can continue training. Changed recipes use isolated state and
work directories, for example `--state build/gameplay-experiment/models --work
build/gameplay-experiment/runs`. Pass the same state to verify or replay.

Promotion requires every task to improve development target cross-entropy by more
than 0.000001, retain held-out accuracy and avoid a test loss increase above 1e-9.
Each task must achieve at least 95% held-out accuracy. Bark therefore needs 12/12
correct on each split; intent needs at least 31/32. Additional independent gates require:

- Zero illegal outputs, suppressed-ID repeats or forbidden module evaluations across
  18,504 bark and 3,840 intent host-control checks.
- Both modules' mean routing and selected-ID contributions at least 0.05 for each head.
  Restricting inference to either module must change mean absolute case loss by at
  least 0.000001. Reports retain each module's ablation loss and effect.
- All 2,304 bark context-invariance checks correct, and all 256 intent scenarios legal,
  surviving, meeting the objective and stable in the bounded four-tick simulator.
- Resident inference heap at most 262,144 bytes and each session at most 65,536 bytes,
  excluding allocator overhead, stack and static host content.
- Per-head p95 at most 500 microseconds and p99 at most 1,000 microseconds; two sequential
  head requests have separate ceilings of 1,000 and 2,000 microseconds.

Each workload measures 2,048 complete samples through four preallocated sessions on
one worker after warmup. Paired timing covers two full selections on one shared state,
using the same network weights and session. These measurements describe named hardware
and build, not a universal frame deadline or four-worker contention. The simulator is
an authored abstract environment, not evidence of performance in an arbitrary game.
Repeated promotion makes the frozen test split a regression guard rather than an
independent estimate of broad gameplay quality.

Accepted releases contain `model.cggp`, `checkpoint.txt`, `report.tsv` and `release.tsv`
under `models/gameplay/composed-v1/releases/<release-id>/`, with an atomic `current.tsv`
pointer. `.cggp` has its own versioned `CGAI-GAMEPLAY-MODEL` header and portable lossless
hexadecimal weights; checkpoints use `CGAI-GAMEPLAY-CHECKPOINT` and additionally retain
optimizer state. Renaming `.cgnn` does not convert a legacy model. This bundle starts a
fresh bark baseline because task-head likelihood is different from the old whole-word
vocabulary loss. Publication requires unchanged staged artifacts, sources and executable;
failure or rejection preserves the accepted pointer. Git admits the seven named authored
TSVs and compact release files. Generated scenarios, raw JSON, traces and pending
publication directories stay ignored. The workflow does not commit or push.

The [published bundle](../models/gameplay/composed-v1/current.tsv) completed 40 epochs
and 15,360 updates. Its [accepted report](../models/gameplay/composed-v1/releases/8d80b5375ee42f294e61ce149e064ef5bdbd8bae5e19bc91ca45ee7b292f20e5/report.tsv)
records 12/12 bark and 32/32 intent cases correct on each held-out split, all 2,304
bark invariance checks correct, and all 256 intent simulator cases legal, surviving,
meeting the objective and stable. Requested inference heap is 192,536 bytes per
model and 11,368 bytes per session. On an i5-12400F, Windows x64, MSVC 193833145 C11
build, measured p95/p99 were 29/61.4 microseconds for bark, 26.3/48.6 for intent and
57.2/96.1 for paired sequential requests. These results apply to the bounded synthetic
tasks and four-session, single-worker workload above.
A default 20-epoch continuation was rejected when bark development loss rose from
0.107856 to 0.120971; the accepted 40-epoch pointer remained byte-identical.

### Scaffold another task

The [task scaffold](../tools/gameplay/scaffold.mjs) creates a typed C11 adapter,
teacher stub, registry descriptor, contract/catalog/scenario placeholders and local
checks in an explicit new directory. It refuses an existing destination.

```sh
node tools/gameplay/scaffold.mjs --name encounter --task-id 2 --output-count 4 --output build/encounter-scaffold
node tools/gameplay/scaffold.mjs check build/encounter-scaffold
```

The initial check intentionally fails: the teacher and acceptance policy are pending,
the descriptor's completion flags are zero, and no frozen scenarios exist. Implement
the teacher and independent training/development/test families, define legal output
and simulation gates, then integrate the adapter and completed descriptor into the
native task registry. Extend the bundle output counts, relevant-feature masks and
module eligibility together. The general network and workflow schema support a third
head; the native authored recipe must supply and evaluate its records before promotion.
Scaffolding alone never creates an approved or trained task. Tests compile and run a
generated third-head adapter and exercise the generic three-task release lifecycle.

Read the release ID from `models/gameplay/composed-v1/current.tsv` and run the seeded
[host demo](../examples/composed_gameplay.c) against that bundle:

```sh
./build/dev/cgai_composed_demo models/gameplay/composed-v1/releases/<release-id>/model.cggp 42 8
```

The demo requests both heads from the same immutable network, resolves permitted bark
IDs to authored content, checks intent preconditions and applies the abstract controller
transition. It prints each proposal's actual specialist contributions. On Visual Studio,
use `.\build\dev\Release\cgai_composed_demo.exe`. The CMake targets `gameplay_train`,
`gameplay_verify` and `gameplay_replay` run the same release workflow when Node is installed.
Run the scaffold, lifecycle and artifact-policy tests with
`node --test tools/gameplay/*.test.mjs`. Set `CGAI_GAMEPLAY_TOOL` to an absolute native
tool path to additionally train, verify and replay an isolated bundle in that test suite.

The original bark release and pointer remain byte-for-byte preserved. For historical
integrity independent of current-source recipes, run:

```sh
node tools/gameplay/workflow.mjs verify-legacy
```

This verifies its original manifest, artifact hashes and pinned version-one quality
policy. It makes no claim that the current executable can replay that old recipe.
The legacy workflow below retains its original current-source and replay checks.

### NPC bark specialist, contract one

The [bark C11 API](../include/centroid_gai_bark.h) accepts four bounded observations
in a fixed order: event, danger, relationship and setting. Events are idle, greet,
threat, victory, discovery and retreat; the remaining fields select low/high danger,
friendly/neutral/hostile relationship and indoor/outdoor setting. A request carries
`CGAI_BARK_CONTRACT_VERSION`, an allowed-ID bit mask and the most recent line ID.
Unknown contract versions, enum values, mask bits and recent IDs are rejected before
publishing a result. This vocabulary can be mapped from different games' own events.

The [authored catalog](../data/gameplay/barks-v1/catalog.tsv) has eight spoken IDs
and `abstain`, which means silence. `cgai_bark_select` returns the highest-likelihood
admitted catalog ID, using lower IDs to break ties. Abstention is always legal, and
a nonzero recent ID is suppressed. If only abstention remains, selection returns
silence without a network forward pass. Otherwise selection performs one full pass,
with no allocation, tokenization or I/O. The returned probability is unconditional
model likelihood, not calibrated confidence. The host owns gameplay legality and
can resolve the stable ID to its own localized content.

Load an immutable specialist with `cgai_bark_load`, inspect its requested heap with
`cgai_bark_get_resources`, and allocate reusable scratch with
`cgai_bark_session_create`. Sessions require exclusive access; separate sessions may
share an immutable model. Destroy sessions before their model. The
[compiled demo](../examples/bark_selector.c) maps host event strings to typed enums,
restricts outputs to the current event's authored IDs, and prints a validated line
or silence. Engine bindings are still future work.

The [versioned task contract](../data/gameplay/barks-v1/contract.tsv),
[scenario table](../data/gameplay/barks-v1/scenarios.tsv) and
[C11 fixture generator](../tools/bark/bark_fixture.c) define 72 synthetic rule cases.
Training uses 48 cases; development and test have 12 each. An event/danger/relationship
family stays in one split with both of its setting variants. This prevents an indoor
variant from supplying the answer for its outdoor held-out counterpart. These cases
measure this bounded authored task; they do not establish dialogue quality across
arbitrary games or genres. No scraped paper text enters bark training.

`cgai_neural_train_examples_continue` learns independent `cgai_neural_example`
records containing `prompt` and one lexical `target`. Each prompt must contain
1..`context_window` known tokens, and each target must be one known output token
excluding BOS, EOS and UNK. Every record and scratch allocation is prepared before
optimizer mutation. Prompts are independently BOS-padded: prior records, prompt-token
losses and appended EOS labels never contribute. A pass performs exactly one target
update per record. The frozen vocabulary comes only from the training split.

The bark recipe uses D16/H16/K16/W4, seed 42, routing temperature 1, learning rate
0.01 and gradient clipping 5. Initial training runs 200 complete epochs; subsequent
proposals add 20 by default. Random encoder and centroid initialization is preserved.
The 13 state-token categories start with orthogonal unit embeddings in the public
enum order, avoiding accidental similarity between unrelated categorical values.
Control and target embeddings retain their random initialization. Experts begin
with distinct +2 preferences for unique training labels in their
first-occurrence order, with other logits at -2. This breaks near-uniform expert
symmetry without encoding the rule teacher in the inference runtime. All parameter
groups remain trainable. Checkpoints retain weights, Adam moments, epoch/update
counters and the shuffle stream; exact replay reconstructs this initialization and
every update using the same ordered records and producing build/platform.

### Train, verify and replay the bark release

The [bark workflow](../tools/bark/workflow.mjs) invokes the compiled C11 tool for
initialization, training, scoring, timing, export and replay. Node orchestrates
artifact hashes, locks and publication. Build the native tool and demo, then use
these commands from the repository root for a single-configuration build:

```sh
cmake -S . -B build/dev
cmake --build build/dev --config Release --target cgai_bark_tool cgai_bark_demo
node tools/bark/workflow.mjs train --tool ./build/dev/cgai_bark_tool
node tools/bark/workflow.mjs verify
node tools/bark/workflow.mjs replay --tool ./build/dev/cgai_bark_tool
```

On Visual Studio builds, pass `--tool .\build\dev\Release\cgai_bark_tool.exe`.
`train` initializes a missing head or continues the accepted checkpoint. `--epochs`
overrides its additional pass count. A rejected proposal stays under the ignored
work directory and preserves the accepted head. Promotion requires:

- At least 95% exact teacher agreement on both frozen held-out splits, with no
  accuracy decrease. With 12 cases per split, every case must be correct.
- Development mean target cross-entropy improvement greater than 0.000001 and test
  loss regression no greater than 0.000000001. Loss uses the full output vocabulary,
  without renormalizing over admitted catalog IDs; reports also retain abstention counts.
- Zero mask or repetition violations across all 512 allowed-ID masks and nine recent
  IDs, exercised as 4,608 requests rotated over the frozen scenarios.
- Requested resident model heap at most 262,144 bytes and one session at most
  65,536 bytes. Counts exclude allocator overhead, stack and static catalog storage.
- Measured complete-selection p95 at most 500 microseconds and p99 at most
  1,000 microseconds on the named report hardware/build.
- Exact checkpoint replay and unchanged fixture, implementation and executable hashes.

The latency workload warms 128 requests, then measures 2,048 successful requests
served round-robin through four preallocated sessions on **one worker**. This checks
complete selection, including typed state encoding and output masking. It measures
neither four-worker contention nor a universal frame deadline. Reports record raw split
counts, resource payloads, compiler/hardware, workload and aggregate percentiles;
individual timing traces remain temporary.

Accepted artifacts live under `models/gameplay/barks-v1/releases/<release-id>/`.
Git admits `checkpoint.txt`, `model.cgnn`, `report.tsv` and `release.tsv`, with an atomic
`current.tsv` pointer. The manifest binds checkpoint/model/report hashes, authored
contract/catalog/scenario identities, source groups, native executable and optimizer
recipe. Raw JSON, generated traces, temporary runs and failed candidates remain local
under ignored paths. This workflow performs local promotion without a Git commit.

The [published seed](../models/gameplay/barks-v1/current.tsv) passed 48/48 training,
12/12 development and 12/12 test scenarios after 200 epochs. Its weights artifact is
17,300 bytes; requested resident model and session payloads are 82,937 and 5,208 bytes.
The accompanying report records p95 23.8 and p99 289.2 microseconds on an i5-12400F,
Windows x64, MSVC 193833145 build. These figures apply to the synthetic scenarios and
single-worker workload above. A further 20 epochs improved development loss but
increased test loss, so the workflow rejected that proposal and retained the seed.

`verify` checks the compact release, source identities, recipe and recorded passing
report without native execution or network access. Add `--tool <executable>` to also
load and score the accepted weights with a local C11 build. The executable fingerprint
records the producing build. A rebuilt executable must reproduce the accepted checkpoint
byte for byte before continuation; replay always checks that same exact result.
Source identities must still match. To experiment with changed sources or contract, use an isolated state and work
directory, for example `train --state build/bark-example/models --work
build/bark-example/runs --tool <executable>`. Pass the same `--state` to verify or replay
that experiment. Repeated promotion use makes the frozen test split a regression guard,
not an independent final estimate of general gameplay quality.

The CMake targets `bark_train`, `bark_verify` and `bark_replay` run the same workflow
when Node is available. A live workflow lock prevents concurrent publication. If a process
crashes, a stale lock is preserved to avoid races between competing recovery attempts;
remove it only after confirming that no workflow process owns it.

Read the accepted release ID from `models/gameplay/barks-v1/current.tsv`, then run:

```sh
./build/dev/cgai_bark_demo models/gameplay/barks-v1/releases/<release-id>/model.cgnn greet low friendly outdoor
./build/dev/cgai_bark_demo models/gameplay/barks-v1/releases/<release-id>/model.cgnn victory low neutral indoor victory
```

The second request suppresses the recent victory ID and resolves to silence. On Visual
Studio builds, use `.\build\dev\Release\cgai_bark_demo.exe`. Run fixture, decoding,
quality-gate and workflow checks with:

```sh
ctest --test-dir build/dev -C Release -R "centroid_gai_bark|centroid_gai_neural_examples" --output-on-failure
node --test tools/bark/*.test.mjs
```

### Inspect resources and schedule generation

The [neural C11 API](../include/centroid_gai_neural.h) provides
`cgai_neural_get_resources(model, &resources)` for allocation-free inspection. It reports
the configuration, vocabulary/output sizes, scalar parameter count, parameter and
optimizer bytes, model bytes, numerical workspace bytes and reusable session bytes.
The per-forward counters report encoder multiply-adds, routing coordinates and expert
logits. They describe the network shape rather than elapsed time or total CPU
instructions: normalization, exponentials, sampling and token handling also cost work.
The current routing evaluates every centroid and its output distribution. Reducing
the vocabulary and output domain therefore reduces both storage and repeated work.

Create a reusable session with `cgai_neural_session_create(model, max_session_bytes)`.
A zero cap permits any supported session size. A nonzero cap covers the owned session
and its workspace; it excludes the model, caller-owned output, allocator overhead and
temporary prompt preparation. The inspector lets the engine account for those costs
separately. Inference `.cgnn` files omit Adam arrays; loading a continuation checkpoint
can retain optimizer storage that gameplay inference does not need.

`cgai_neural_session_begin(session, prompt, max_tokens, temperature, seed, output,
output_size)` prepares a request using the caller's bounded output buffer. It allocates
temporary prompt buffers, so prepare prompts outside a critical frame path. Repeated
`cgai_neural_session_step(session, max_forward_passes, &result)` calls perform no heap
allocation or file I/O. The engine can issue a small forward-pass quantum on a worker
or between other work, then inspect `IDLE`, `RUNNING`, `EOS`, `LIMIT` or `ERROR`.
Results include emitted tokens, cumulative attempted forward passes, prompt tokens
and unknown prompt tokens. An EOS prediction consumes a forward pass without emitting
a token. Keep each session exclusive to one caller and retain the immutable model and
output buffer until the request finishes; destroy sessions before releasing the model.

A forward pass is indivisible. A one-pass quantum bounds network work, but cannot
guarantee a frame deadline. Measure its worst observed cost and request latency on the
target device; an engine deadline/fallback policy must handle requests that become stale.
For reproducible gameplay, record the specialist artifact hash and explicit sampling
seed with the request. Exact output replay requires a compatible build/platform as
well as identical input, settings and weights.

The native example prints the model resources and advances generation in small quanta:

```sh
cmake --build build/dev --config Release --target cgai_gameplay_demo
./build/dev/cgai_gameplay_demo build/dev/order.cgnn "a b"
```

Export `build/dev/order.cgnn` with the controlled experiment below first. On Visual
Studio builds, use `.\build\dev\Release\cgai_gameplay_demo.exe`. The toy output proves
the scheduling interface; it does not demonstrate a gameplay-trained model.

### Train and promote a specialist

The implemented bark workflow establishes this sequence for further specialists:

```text
cited research and game rules -> task examples and simulation traces
    -> C11 training and exact replay -> fixed scenario and performance gates
    -> compact specialist release -> engine validation and fallback
```

Research citations can document an algorithm, source or evaluation method. The scholarly
pipeline verifies attribution and reuse eligibility; it does not establish that a model
produces playable levels, balanced rewards or useful tactical choices. Direct task
targets should come from authored examples, deterministic generators or bounded game
simulations with checkable outcomes. Split scenarios by map, template or scenario family
so variations of one case cannot supply both training targets and held-out answers.

Keep authored contracts, small scenario fixtures, simulator code, seeds and gate policy
in source control. Raw scholarly snapshots and discovered text remain under the existing
ignored research paths. Additional gameplay caches and traces should use an explicit
ignore/compact-release policy before acquisition. The gameplay allowlist covers only
named authored bark/composed fixture files and compact releases. All other files
under `data/gameplay/` and `models/gameplay/` remain ignored by default.

A gameplay promotion gate should require legal outputs and scenario-quality improvement
alongside the frozen regression suite, bounded unknown-token and fallback rates,
deterministic replay, memory caps and measured latency limits. Record the hardware,
compiler/build, model and suite hashes, concurrency, output limits and sample counts with
latency percentiles. Compare candidates under the same workload. A lower next-token loss
alone is insufficient for these tasks. Bark and tactical intent implement these checks
in the composed bundle; encounter, quest and procedural tasks need additional
scenario suites and gates.

### Connect an engine

The C11 core can be linked by a native host. A Unity adapter would export a C interface
for C# calls using its [native plug-in interface](https://docs.unity.com/en-us/engine/6000.6/manual/scripting/compilation-and-code-reload/plug-ins/native/overview).
A Godot adapter would expose the core through [GDExtension](https://docs.godotengine.org/en/stable/engine_details/engine_api/gdextension/index.html),
which loads native shared libraries. These adapters are not included: the existing
shared-library ABI exposes the count-based engine, not these neural session functions.
Use platform-specific library builds. The legacy `.cgnn` format requires the same
architecture; the composed `.cggp` codec uses portable lossless numeric text.

For Unreal, an adapter can integrate specialist loading with the engine's
[asynchronous asset loading](https://dev.epicgames.com/documentation/unreal-engine/asynchronous-asset-loading-in-unreal-engine)
facilities. That would still require a model asset/wrapper and an inference scheduler;
asynchronous asset loading does not itself schedule neural computation. In every engine,
load/prepare away from critical gameplay, apply only validated completed results, and
switch immutable model versions between active requests. Engine bindings and additional
task contracts remain next steps. Typed bark and intent decoding, neural specialist
composition and automatic per-task promotion are implemented in the C11 core.

## Unattended research training

The [scholarly loop configuration](../data/research/autotrain.json) runs public
paper acquisition, citation verification, deterministic C11 training and automatic
promotion without approval prompts. The acquisition and orchestration layer uses
the existing Node/TypeScript runtime (Node 24.11 or newer). Gradients, checkpoint
continuation, scoring and exact replay execute in the compiled native C11 CLI.

Build the CLI, then start the loop from the repository root:

```sh
cmake -S . -B build/dev
cmake --build build/dev --config Release
node persistence/api/src/training/scholarly-cli.ts watch
```

Use `run` instead of `watch` for one bounded cycle. `--cgai <executable>` selects
a particular C11 build; `--state <directory>` selects an isolated artifact directory.
The default queries prioritize cited machine-learning and language-model papers, and
the loop waits six hours between cycles. The initial recipe uses up to 32 complete
passages per paper and one epoch at learning rate 0.005. Query topics, request limits,
time budgets, corpus size, epochs and quality thresholds are in the configuration.
Ctrl+C cancels requests and native subprocesses. A provider outage or failed gate
records its decision and preserves the accepted head for the next cycle.

Acquisition uses the official [Europe PMC API](https://europepmc.org/RestfulWebService)
for open-access JATS XML and the [Crossref API](https://www.crossref.org/documentation/retrieve-metadata/rest-api/)
for publication and citation metadata. Full text must carry an explicit CC BY or CC0
license; open-access availability alone is insufficient. Source requests run serially
with at least one second spacing, bounded responses and a per-cycle request budget.
Cursor pagination searches past excluded records while reserving requests for DOI
verification. Verified small batches accumulate toward minimum corpus coverage.
Successful cycles save query cursors in the runtime cache so the next cycle discovers
additional papers; exhausted searches start again. Failed acquisitions preserve progress.
Cached snapshots are immutable and expire for reuse within 24 hours. API routes are
fixed, redirects are checked before contacting another host, and XML external entities
are never resolved.

Admission rederives all fields from captured source bytes, verifies SHA-256 identities,
matches paper DOI/title/year to its registry record, checks publication types and notice
signals, and resolves two distinct citation links confirmed in both source and registry
bibliographies. The chosen links are deterministic and bounded; additional bibliography
entries remain unverified. Missing, inconsistent, restricted or unavailable evidence is
quarantined. Every admitted paper retains its original XML, metadata, reuse license and
the raw registry responses for its certified references.

Training uses complete, bounded source quotations prefixed with the paper's DOI.
Each passage has a DOI URL, exact source span and content hash. DOI and full-text mirrors
stay together, shared complete passages cannot cross splits, and the first usable batch
pins development and regression benchmark papers and text. Later discoveries never
enter the vocabulary through held-out text. Unchanged training text continues Adam;
changed text or native executable/settings rebuilds a fresh training-only vocabulary
and compares the resulting candidate against the incumbent on the pinned benchmark.

Confidence is represented by explicit evidence checks. Bibliographic agreement confirms
publication identity and citation resolution. The corpus also records complete passages
appearing in distinct works with different known lead authors. These signals do not
establish scientific truth, replication or calibrated factual confidence. This first
policy is `scholarly-attribution-v1`: it trains attributable source excerpts and evaluates
next-token behavior. It does not publish generated scientific claims or replace the chat
service's serving model.

Automatic promotion requires all source checks, a development loss decrease beyond
`minimumDevelopmentImprovement`, no regression in fixed benchmark loss or held-out
greedy accuracy, bounded unknown-token rates with no coverage loss, and bit-for-bit native
replay. Default unknown-token limits are 25% of text tokens, excluding EOS, and all counts
remain visible in `quality.json`. A fixed vocabulary cannot acquire new words from
evaluation. Repeated use of the regression set is a development guard, not an independent
final estimate of scientific answer quality.

Accepted versions live under `models/scholarly/releases/<release-id>/`. Git admits
only `model.cgnn` (compiled inference weights), `checkpoint.txt` (weights and Adam
state), `release.tsv` (recipe and artifact hashes), `citations.tsv` (publication
identities, licenses and certified reference hashes), and `metrics.tsv` (measured
quality and exact replay). The source-free `current.tsv` head is replaced atomically
only after the complete compiled release passes every gate. Previous versions remain
available. A local lock serializes writers and recovers a crashed owner's lock after
confirming its process is absent.

Raw paper snapshots, catalog/benchmark/dataset/admission JSON, quoted text splits,
private manifests, caches, failed runs and temporary exports remain on disk and are
ignored by Git. New scholarly files are ignored by default until explicitly admitted
in `.gitignore`. The training policy and authored chat/neural test fixtures remain
versioned. CI checks the tracked research paths and the compiled release's hashes.
The loop performs local promotion and creates no Git commits or HTTP deployment.

To lock an existing accepted local model into the compact artifact format, then
verify the published release without network access:

```sh
node persistence/api/src/training/scholarly-cli.ts export
node persistence/api/src/training/scholarly-cli.ts verify
```

New promotions produce these artifacts automatically. `export` preserves the model
and its private evidence identities, repeats exact C11 replay during legacy migration,
and performs no acquisition or new training. `verify` works on a clean checkout using
the committed compact artifacts alone.

Keep a separate backup of the ignored local training evidence. To resume training
on another machine, restore the matching benchmark, catalog and complete private
release bundle at their original paths, then run `verify` and `run` or `watch`.
The loop refuses to replace the pinned benchmark when that evidence is missing or
different. `--state <new-directory>` starts an explicitly isolated experiment.

The source-control seed at [`models/scholarly/current.tsv`](../models/scholarly/current.tsv)
contains 13 verified papers and 360 attributed passages. Its first epoch completed
15,809 updates and passed exact replay with the producing
[toolchain](../models/scholarly/toolchain.txt). Development cross-entropy decreased
from 7.789234 to 7.483621; fixed regression loss decreased from 7.793061 to 7.423248.
Unknown-token rates were 21.7% and 19.1%. A second continuation epoch was rejected
for increasing held-out loss, preserving the accepted checkpoint. These measurements
describe the pinned development guard, with the scientific-quality limits above.

Run the offline pipeline checks, including a compiled C11 adapter round trip:

```sh
node --test persistence/api/src/training/scholarly-*.test.ts
ctest --test-dir build/dev -C Release --output-on-failure
```

## Run the controlled neural experiment

The repository contains a trained, reviewable baseline at
[`models/neural/order-v1`](../models/neural/order-v1/). Its checkpoint stores weights,
Adam moments, completed epochs, target updates and shuffle state as exact hexadecimal
numbers in a versioned text format. The training and held-out corpora, fixed recipe,
SHA-256 identities, measured metrics and producing toolchain are kept beside it.
All training, checkpoint handling, replay and inference run in the native C11 core.

From the repository root, build and propose the next step:

```sh
cmake -S . -B build/dev
cmake --build build/dev --config Release
cmake --build build/dev --config Release --target model_validate model_step
```

`model_validate` checks the recorded file identities, loads the accepted checkpoint,
exports its inference weights and evaluates the held-out corpus. `model_step` adds
one epoch at learning rate 0.015 and writes `build/dev/model/candidate.txt` and
`candidate.metrics.tsv` after held-out cross-entropy strictly improves. A rejected
candidate returns failure without opening the checkpoint destination. The accepted
source checkpoint is preserved. The TSV reports parent/candidate epochs, updates,
training loss, held-out loss, accuracy and unknown counts. Both corpora must have
zero unknown tokens; validation never contributes training targets.

Adjust a proposal with CMake cache settings `CGAI_MODEL_STEP_EPOCHS` and
`CGAI_MODEL_STEP_RATE`. After reviewing a successful proposal, copy its checkpoint
and metrics into the baseline directory, update the total epoch count, checkpoint
SHA-256 and metrics SHA-256 in `recipe.cmake`, then commit these files together.
Keep the same learning rate, gradient clipping and training corpus for a recipe
that can be replayed from scratch.
Changing any of them requires a new recipe and baseline. Promotion is an explicit
source edit; build targets never promote candidates automatically.

With the recorded compiler, build configuration and math library, verify exact replay:

```sh
cmake --build build/dev --config Release --target model_verify model_reproduce
```

`model_verify` recreates the reference model using its saved configuration and checks
every weight, Adam moment, update counter and RNG value before saving a replay.
`model_reproduce` initializes the default network and executes the fixed recipe,
then compares the produced checkpoint's SHA-256. Floating-point library/compiler
changes can change final bits, so these exact checks require the toolchain recorded
in `toolchain.txt`; `model_validate` is the portable load/evaluate check. Native tests
prove bit-for-bit split-call and separate-process save/load equivalence within the
current build on each platform.

The controlled corpus teaches `a b` → `left` and `b a` → `right`. A different starting
phase and length form the held-out sequence. This baseline establishes ordered
learning and repeatable incremental optimization; it does not establish conversational
or factual answer quality. Repeatedly selecting on this held-out fixture also makes
it unsuitable as an independent final quality benchmark.

The same workflow is available directly through the CLI:

```text
neural-init <train.txt> <checkpoint.txt>
neural-step <checkpoint.txt> <train.txt> <heldout.txt> <candidate.txt> [epochs] [learning-rate]
neural-replay <checkpoint.txt> <train.txt> <replay.txt> [learning-rate]
neural-export <checkpoint.txt> <model.cgnn>
```

The step and replay commands default to rate 0.015; a step defaults to one additional
epoch and gradient clipping 5.0. `neural-init` uses the configuration defaults below.
`neural-export` writes the existing inference format for evaluation or generation.

Build the native CLI using [development instructions](development.md), then run
these commands from the repository root with a single-configuration build:

```sh
./build/dev/cgai neural-export models/neural/order-v1/checkpoint.txt build/dev/order.cgnn
./build/dev/cgai neural-evaluate build/dev/order.cgnn models/neural/order-v1/heldout.txt
./build/dev/cgai neural-generate build/dev/order.cgnn "a b" 18 0 42
```

For a Visual Studio build, use `.\build\dev\Release\cgai.exe` as the executable.
The destination directory must already exist. The older `neural-train` command
creates a fresh model and replaces the destination artifact when saving succeeds.

```text
neural-train <train.txt> <model.cgnn> [epochs] [learning-rate] [validation.txt]
neural-evaluate <model.cgnn> <heldout.txt>
neural-generate <model.cgnn> <prompt> [max-tokens] [temperature] [seed]
```

The example corpus repeats an artificial rule: `a b` precedes `left`, and `b a`
precedes `right`. The held-out file uses the same rules with a different starting
phase and length. It exercises learning, token order, evaluation, and artifact
loading; it is not a conversational or factual knowledge benchmark.

Training prints metrics before and after optimization for the training text and
optional validation text. Evaluation reports target count, mean cross-entropy
in natural units, perplexity, greedy accuracy, and unknown-token count. Each
sequence includes one final end-of-sequence (EOS) target. Compare held-out
results across runs using the same split and tokenization. A lower training loss
alone does not establish useful answer quality.

## Configuration

The CLI uses `cgai_neural_default_config()` and exposes only the positional
options shown above. Custom network shapes require the
[C API](../include/centroid_gai_neural.h).

| Setting | Default | Accepted range |
| --- | --- | --- |
| Embedding dimensions `D` | 8 | 1–64 |
| Hidden dimensions `H` | 16 | 1–128 |
| Centroid count `K` | 16 | 1–128 |
| Context window `W` | 4 tokens | 1–256 |
| Initialization/shuffle seed | 42 | `uint64_t` |
| Routing temperature | 1.0 | 0.01–100 |
| Training epochs | 20 | 1–10,000 |
| Adam learning rate | 0.01 | Greater than 0, at most 1 |
| Global gradient norm limit | 5.0 | Greater than 0, at most 1,000 |

Generation defaults to 40 tokens, sampling temperature 0.8, and the model seed.
Its temperature is separate from routing temperature: zero chooses the most
probable token; positive values rescale the final output distribution. Seed zero
uses the stored model seed. Generation stops at EOS or the token limit and
returns only the continuation. The CLI rejects a worst-case output allocation
above 64 MiB even if early EOS could produce a shorter response.

## What the network learns

For vocabulary size `V`, the five parameter groups are embeddings `E[V,D]`,
encoder weights `A[H,W*D]`, encoder bias `b[H]`, centroid coordinates `C[K,H]`,
and centroid-specific output logits `L[K,V]`.

For each target, collect the preceding `W` tokens in chronological order,
left-padding with beginning-of-sequence (BOS) tokens when needed:

```text
x = concat(E[context[0]], ..., E[context[W-1]])
h = tanh(A x + b)
g[k] = softmax_k(-sum_j((h[j] - C[k,j])^2) / routing_temperature)
q[k,v] = softmax_v(L[k,v])     # BOS is excluded from each output row
p[v] = sum_k(g[k] * q[k,v])
loss = -log(p[target])
```

The encoder preserves position because each slot occupies a different part of
`x`. Distance to each centroid determines its routing weight `g[k]`. Each
centroid contributes a normalized token distribution `q[k,v]`; the final
prediction mixes these probabilities. BOS is context-only. EOS is a learned
output that ends generation. Log-space calculations stabilize loss and gradients.

Training updates all five parameter groups with clipped Adam, one target at a
time. It shuffles target indices while keeping each target's original preceding
tokens intact. Repeated tokens accumulate embedding gradients from every context
position. `cgai_neural_train` creates fresh optimizer moments for each call. The new
`cgai_neural_train_continue` retains moments, update counters and shuffle RNG in
the model. It rebuilds target indices in canonical order before each shuffle,
so any partition of successful full epochs equals a single combined continuation
call with the same text, learning rate and clipping in the same build/platform.
An error may retain partial updates; exact resume is guaranteed at successful
full-epoch boundaries. Calling the original trainer clears continuation state
when fresh optimization begins. The chat trainer retains its existing fresh-Adam
semantics.

For bounded task records, `cgai_neural_train_examples_continue` uses the same Adam and
shuffle continuation state while mapping each complete prompt independently to one
target. Successful complete-epoch partitions reproduce a combined call with identical
ordered records and settings. Invalid records preserve the complete model state before
optimization begins. Existing continuous-sequence and chat training retain their behavior.

The parameter count is `P = V*D + H*W*D + H + K*H + K*V`. Weights occupy `8*P`
bytes on supported double-precision builds. Training additionally allocates
three `P`-double arrays for gradients and Adam moments, plus temporary buffers.
Output computation visits all centroid/vocabulary pairs. There is no GPU path,
minibatching, or sparse optimizer, so larger vocabulary and shape settings can
be expensive even within the accepted limits.

## Vocabulary and context boundaries

Vocabulary creation uses training text only and reserves BOS, EOS, and unknown
(UNK) IDs. Later training, evaluation, and generation keep that vocabulary fixed;
unseen words map to UNK. A large unknown-token count means the model cannot
distinguish many original spellings. Supplying validation text to the CLI does
not add its words to the vocabulary or update weights from it.

The shared tokenizer lowercases through C character conversion, groups word
bytes and apostrophes, and separates punctuation. It preserves ordinary UTF-8
bytes in the default C locale but does not perform full Unicode case folding,
Unicode word segmentation, or subword tokenization. Generation cannot preserve
original capitalization or arbitrary whitespace.

A continuous-text training file is one sequence. Newlines do not introduce dialogue
boundaries or reset context, and EOS is appended once at the end. During
generation, the prompt enters the same rolling window as generated tokens. Once
enough output has been emitted, prompt tokens leave that window. The implemented
standalone generation model has no separate persistent prompt, roles, or conversation
state. Independent task training instead accepts explicit prompt/target records,
and the typed bark selector performs one bounded prediction from the complete state.

## C ownership and model files

Use `cgai_neural_create`, `cgai_neural_train`, `cgai_neural_evaluate`, and
`cgai_neural_generate`, then release the owned handle with `cgai_neural_destroy`.
Generation writes to a caller-owned bounded output buffer. Failures return
`NULL` or `CGAI_STATUS_ERROR`; retrieve details with `cgai_last_error()`.
Training requires exclusive access and retains updates already made if a later
step fails. Immutable models support concurrent evaluation, generation, and
saving while callers retain the model and use separate result buffers.

`cgai_neural_save` and `cgai_neural_load` use the `.cgnn` neural artifact with
`CGAINN1` magic and version 1. It contains shape, seed, routing temperature,
vocabulary, and weights. Numeric fields use native representation: use trusted
artifacts on the same architecture. Loading validates shapes, vocabulary,
finite parameters, and exact file length. Saving replaces the destination
directly and is not atomic. Optimizer state is not stored. This format is
independent of the baseline `.cgai` format and its merge operations.

`cgai_neural_checkpoint_save` and `cgai_neural_checkpoint_load` use a separate
versioned text format with lossless hexadecimal floating-point values and encoded
vocabulary spellings. They preserve continuation state as well as inference weights,
validate bounded shapes, spelling uniqueness, finite weights/moments, nonnegative
second moments and complete input consumption, and cap checkpoints at 256 MiB.
Use the C numeric locale. Checkpoint saving replaces its destination directly and
is not atomic; evaluation rejection occurs before saving, while an I/O failure
may leave an incomplete destination. Checkpoints do not embed training data or
learning-rate history. Keep the fixed corpus and settings in the accompanying
recipe; changing them intentionally continues from the saved moments but is no
longer an exact replay of that fixed recipe.

Limits enforced by the neural implementation include 8,192 vocabulary entries
including controls, two million scalar parameters, one million text tokens per
operation before final EOS, 16 MiB per input text, 1 MiB per token spelling,
and 64 MiB per artifact. Generation accepts 0–1,000,000 output tokens and a
sampling temperature of 0–100. Accepted dimensions must also fit the total
parameter bound; the maximum of every dimension cannot always be used together.

## From continuation to chatbot

The [chat header](../include/centroid_gai_chat.h) now has implementations for
persistent prompt slots, rolling answer slots, structural roles, independent
dialogue examples, assistant-only training and the bounded `.cgchat` codec.
One Adam optimizer spans every example and epoch in a training call; another
call starts fresh moments. No new mathematical parameter groups were introduced:
the existing encoder consumes the two separately padded windows.

Chat training holds BOS padding embeddings fixed at zero for newly created models.
Centroid output rows start with different token preferences, then learn all output
logits through Adam. This avoids the padding saturation and expert collapse seen
with the prototype's initialization on the two-dialogue demo. Both supplied demo
answers now generate exactly with EOS; this is a training correctness check.
The held-out six-case dialogue fixture still scores 0/6 exact answers.

Chat generation also stops persistent repetition and reports `repetition` separately
from EOS and the token limit. These chat policies leave the standalone continuation
prototype's initialization and generation behavior unchanged.

The [chat service](chat-service.md) connects these artifacts to workers, HTTP
and PostgreSQL. Source mode returns original excerpts independently from generated
wording. Compiled knowledge does not automatically become neural weights.
The [roadmap](chatbot-plan.md) retains unmet quality and release acceptance gates.

The native neural tests cover analytical gradients against finite differences,
causal context, ordered tokens, learned parameter updates, frozen vocabulary,
determinism, held-out learning, and artifact validation. CLI tests cover training,
evaluation, generation, and argument errors. Run the neural checks with
`ctest --test-dir build/dev -C Release -R centroid_gai_neural --output-on-failure`;
use [development instructions](development.md) for the full check suite.
