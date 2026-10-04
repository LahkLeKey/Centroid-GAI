# Development

Migration status: the active [C11 Life deliverables](centroid-next-deliverables.md)
replace service/script workflows with native library, CLI and test executables.
Existing build commands below describe the previous checkout; Node, TypeScript,
Python helpers and deleted-workflow restoration are not part of the new backlog.

The centroid neural model connects to persistent conversations through the native
bridge and bounded API workers. See the [implementation plan](chatbot-plan.md)
for remaining acceptance criteria. Commands below run from the repository root
unless a working directory is shown.

## Build the native prototype

Requirements: CMake 3.20 or newer and an ISO C11 compiler. Use a separate build
directory for each compiler or generator.

```sh
cmake -S . -B build/dev -DCMAKE_BUILD_TYPE=Release
cmake --build build/dev --config Release
ctest --test-dir build/dev -C Release --output-on-failure
```

With a single-configuration generator, the CLI is `build/dev/cgai` (or
`build/dev/cgai.exe` on Windows). Visual Studio places it at
`build/dev/Release/cgai.exe`. The [neural guide](neural-centroid.md) gives training,
evaluation and generation commands using the supplied text fixtures.

To run only the neural suites:

```sh
ctest --test-dir build/dev -C Release -R neural --output-on-failure
```

The separate original engine remains useful as a baseline. Save a small UTF-8
training corpus as `baseline-train.txt` before running these commands:

```sh
./build/dev/cgai train baseline-train.txt build/dev/baseline.cgai 12
./build/dev/cgai generate build/dev/baseline.cgai "centroid models" 30 0 42
```

These `.cgai` artifacts are different from neural `.cgnn` artifacts. Baseline
CLI/HTTP composition operates on count models. The isolated Life adapter has a
separate gated neural consolidation implementation; standalone/chat APIs do not
gain that operation through baseline composition.

## Develop the service

Host development uses Node >=24.11, Bun 1.3.9 (the pinned CI/container version),
and a C toolchain supported by node-gyp. Docker supplies these dependencies when
building images. The workspace contains only `api` and `db`.

```sh
cd persistence
bun install --frozen-lockfile --ignore-scripts
bun run --cwd api native:build
bun run typecheck
bun run test:native
```

The addon compiles the baseline and separate neural chat engine. The typed chat
bridge and worker IPC are implemented. Chat and memory migrations are generated
from the contract and committed. See the [chat service](chat-service.md) for its
Compose/curl workflow and source-first behavior.

The API uses Node's erasable TypeScript syntax; typechecking rejects constructor
parameter properties that would fail at runtime in strip-only mode. Local Windows
node-gyp requires an actual Python 3 installation, not a stale Python launcher.
The Docker build supplies Python and the native compiler.

## Verify changes at their boundary

| Change | Relevant checks |
| --- | --- |
| Neural math/training/artifacts | Native neural tests, gradient checks, held-out metrics, CLI round trips |
| C model or ABI | Full CTest suite and relevant Node addon tests |
| Observable NPC pilot | `ctest --test-dir build/dev -C Release -R centroid_gai_npc --output-on-failure`; offline `node tools/npc/workflow.mjs verify-evidence`; accepted heads additionally require `verify` and compatible-toolchain `npc_replay` |
| Iterative NPC recovery | `ctest --test-dir build/dev -C Release -R centroid_gai_npc_v2 --output-on-failure`; offline `node tools/npc_v2/workflow.mjs verify-delivery`; accepted heads additionally require compatible-toolchain `npc_v2_replay` |
| HTTP/persistence behavior | Typecheck, native tests, API tests against migrated PostgreSQL |
| Knowledge compiler | `node --test persistence/api/src/knowledge/compile-native-knowledge.test.ts` |
| Repository retrieval | `node --test persistence/api/src/knowledge/retrieval.test.ts` |
| Repository conversations | `bun run test:repository` from `persistence/`; captured HTTP transcripts, exact citations, follow-ups and snapshot transitions |
| Documentation | Local links, source/command accuracy, Doxygen build |

From `persistence/`, `bun run test:e2e` builds an isolated Compose project, tests
the HTTP API, and removes its own containers and database volume. Default test
ports are 3100 (API) and 55432 (PostgreSQL); override with `CGAI_E2E_API_PORT` and
`CGAI_E2E_POSTGRES_PORT`. The regular development stack and its volume are separate.
The suite includes independent candidate validation and rejection, persisted
quality reports, health during work, transcript retries, revision conflicts,
isolated sessions, memory controls and service restart/reload. Demo-answer fit is
a native training diagnostic, not the HTTP publication gate.
It runs test files sequentially so the restart test cannot interrupt baseline tests.
The normal runner and CI disable external research and verify its disabled status.
The `bun run test:repository` wrapper is intended to add the repository overlay
and 40-scenario regression suite, preparing independent A/B fixture commits from
allowlisted sources. Its `tools.knowledge.e2e_repository` module and scenario
JSON inputs are absent from this checkout. This lane is retired, with no
restoration or replacement planned. See the
[documentation reconciliation](centroid-documentation-reconciliation.md) for the
dependency inventory and [repository chat](repository-chat.md) for its contracts.
Fixture preparation must not modify the developer branch.
Set `CGAI_E2E_RESEARCH_LIVE=1` to additionally exercise a real Wikipedia lookup for
`hello world` from a conversation with no model, then verify memory reuse. This
opt-in check requires Internet access and provider availability.

API tests skip when `CGAI_API_URL` is absent. A skipped suite is not an API pass.
The tests cover baseline and neural chat endpoints. Record model checksum, dataset/split identity, seed,
metrics, machine and command alongside any quality or latency claim.

Repository-retrieval gold excerpts track the current documentation. Rebuild the
source snapshot from the same committed revision as the question suite after
documentation changes. The Git importer excludes dirty and untracked files;
an older snapshot requires its matching historical suite.

CI configures cross-platform C builds, knowledge-ingestion checks, native analysis,
documentation generation, and an API/PostgreSQL integration job. It has no browser
build or browser test job. The [workflow](../.github/workflows/ci.yml) is the
source of truth for configured commands and pinned versions. A configured step
whose input/tool is missing is not a passing check.

## C implementation and documentation conventions

Use ISO C11, focused modules, and the `cgai_` naming convention. Public contracts
belong in installed headers under `include/`; private implementation details
belong in `src/internal/`. Keep identifier domains distinct and document all
ownership transfers, capacities, units, partial initialization and cleanup.

Each C source/header needs a Doxygen file description. Document public types and
fields, and each function's purpose, parameters, return values, ownership and
failure behavior, including static helpers. Number meaningful implementation
phases with `Step 1`, `Step 2`, and so on. State the actual guarantees: an error
does not imply rollback, and saving a file does not imply atomic replacement.

The configured function budgets are 20 statements, eight branches and seven
parameters. Use the checked-in Clang configuration rather than suppressing checks
to fit a new implementation. With Clang tools and Node headers available:

```sh
cmake --build build/dev --target cgai_lint cgai_format_check
```

CI pins clang-format 21.1.0. If CMake cannot discover the Node headers, reconfigure
with `-DCGAI_NODE_INCLUDE_DIR=/path/to/include/node`. The lint and format targets
are created only when their respective tools are found; addon lint requires headers.

## Generate the reference docs

With Doxygen installed:

```sh
cmake -S . -B build/docs -DCGAI_BUILD_DOCS=ON -DCGAI_BUILD_TESTS=OFF
cmake --build build/docs --target docs
```

The output is `build/docs/docs/html/index.html`. The documentation index is the
main page; core sources, headers, native bindings and tests supply the API reference.
Warnings and missing parameter documentation fail the build. Chat implementations
and their ownership contracts are included in the generated reference.

When changing a contract, update its header, implementation, tests and guide
together. Keep planned APIs in the roadmap until routes and integration tests
exist. Preserve source spelling in quoted commands and evidence, even though the
current neural tokenizer normalizes input text.
