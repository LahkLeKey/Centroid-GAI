# Fresh-start blueprint durability closure

Raw artifacts named below are retained locally under the [artifact policy](../../ARTIFACTS.md); their identities are in the [artifact manifest](../../local-artifacts.txt). They are excluded from Git.

October 5, 2026. A further independent contract review found three real gaps
behind the earlier completion claim. They are now implemented and covered by
native regressions. The [archived blueprint](../../archive/FRESH_START_BLUEPRINT.md) stages
0–7 are complete within their declared bounded scope. The
[authoritative roadmap](../../ROADMAP.md) retains the separate quality gates;
these fixes do not establish Life superiority, fluent generation or broad coding
ability. The [previous closure](../2026-10-05-blueprint-closure/README.md) remains
historical evidence rather than being overwritten.

## Corrective implementation

| Audited gap | Implemented behavior | Regression coverage |
| --- | --- | --- |
| The work library admitted failed-capture activity, but the CLI exited without saving it. | [main.c](../../../cli/main.c) saves any admitted `run` receipt, including `C_LIMIT` and `C_IO`, and preserves the capture failure status after successful publication. A save failure takes precedence. A rejected preflight admits nothing and leaves the disk checkpoint intact. | [test_work.c](../../../tests/test_work.c) launches the actual CLI, exceeds the 64 MiB capture bound and attempts a missing program. Fresh native reloads verify exact original argv/cwd, complete field lengths, hashes and escaped bytes, raw-output identities and unchanged trained model/world/optimizer/RNG/task/report state. |
| Full test configurations silently omitted `experiment` and `code_suite` when a preferred compiler was unavailable. | [CMakeLists.txt](../../../CMakeLists.txt) always registers both tests when tests are enabled. `CENTROID_TRIAL_COMPILER` accepts an explicit compiler; discovery falls back to the supported project C compiler. A native C11 compile/link probe rejects unusable dependencies with a clear configure error. Tests-off core builds bypass that dependency. | All fifteen tests are present and pass on each toolchain. Separate configure checks verify missing-compiler rejection, native GCC fallback with ordinary search paths disabled, and successful tests-off configuration/build with the deliberately missing candidate compiler. |
| Finite code workflows could execute tools without first proving an available encounter, and TRAIN receipts omitted the frozen pretrial model/contact identities. | [contact.c](../../../src/experiment/contact.c) validates the production trainer, requires no pending task and computes an authentic next B3/S23 encounter without advancing the world. Both [range trials](../../../src/experiment/experiment.c) and the [project suite](../../../src/experiment/code_suite.c) defer before creating output or launching children if contact is unavailable. Before any trial process, they retain `model-parent.centroid` and canonical `contact.bin`; every TRAIN receipt binds both hashes. | Native range and suite regressions verify no-contact/pending preservation, shared-owner participation, no directory or child on deferral, original parent model/world restoration, canonical physical proof fields, receipt bindings and hash mismatch after proof mutation. Failed compiler attempts retain the parent/proof. |

The contact envelope uses `CCONT001`, version 1, starting generation and cells,
owner UIDs, eligible/participant masks, contact graph and the parent checkpoint
SHA256. Its own integrity envelope and whole-file hash are retained. Only
`src/life/trainer.c` advances production training or consumes measured receipts;
the preflight and snapshot do neither. Existing checkpoint/session schemas and
public owner layouts are unchanged.

## Final integrated verification

| Toolchain | Complete final output | Result | Elapsed |
| --- | --- | --- | --- |
| Windows MSVC 19.38 | `msvc-tests-final.log` | 15/15 pass | 42.74 s |
| Windows clang 21.1 | `clang-tests-final.log` | 15/15 pass | 45.10 s |
| Linux GCC 13.3 | `linux-tests-final.log` | 15/15 pass | 55.40 s |

Final captured builds are MSVC (`msvc-build-final.log`; local artifact),
clang (`clang-build-final.log`; local artifact) and Linux (`linux-build-final.log`; local artifact), all warning-free.
Final build/test children returned exit 0, timeout 0 and omitted 0. No production
or test edits followed these final runs. The toolchains used separate compatible
checkpoints. Concurrent timings are verification observations rather than
matched-compute quality comparisons.

Strict C11 (`strict-c11-final.log`; local artifact) checks all 40 production/CLI/native-test C
files with warnings treated as errors; formatting (`format-final.log`; local artifact) checks
all 49 authored C/header files under include/source/CLI/tests.
Targeted static analysis (`static-analysis.log`; local artifact) checks the new contact helper,
both finite-code workflows and CLI with zero warnings. These successful raw
logs are empty; their `.activity` sidecars retain exact command and status.

The initial MSVC (`msvc-tests.log`; local artifact) and clang (`clang-tests.log`; local artifact) runs failed the
new Windows receipt assertion: it compared an original backslash path with the
correctly escaped representation. The test was corrected to compare entire
fields, original-byte lengths, SHA256 identities and complete escaped values.
The production receipt was correct. Those child exits of 8 and the original
logs remain retained; the initial Linux suite (`linux-tests.log`; local artifact) passed.

The first missing-compiler (`config-missing-compiler.log`; local artifact) and
tests-off (`config-tests-off.log`; local artifact) configure attempts failed because the capture
harness had no Ninja on PATH. Repeating with the explicitly retained Ninja path
produced the intended missing-compiler error (`config-missing-compiler-final.log`; local artifact)
(expected child exit 1) and successful
tests-off configure (`config-tests-off-final.log`; local artifact) and
core build (`tests-off-build.log`; local artifact). The
native fallback check (`config-native-fallback.log`; local artifact) configured with GCC while
system/environment program discovery was disabled and declared `/usr/bin/gcc`
as the mandatory candidate-test compiler. These are distinct dependency checks,
not additional complete test-suite runs.

## Current-codebase consumption

The native scan (`source-scan.log`; local artifact) observes the actual dirty repository: 8 new
versions, 30 unchanged observations, 9 excluded entries, 0 failures and 466,712
read bytes. Research, data, tests, generated output, model artifacts and reserved
evaluation paths remain excluded from automatic source ingestion. Current
versions retain immutable original bytes and provenance.

The native [finite trial](code-trial/report.md) then retains all four attempted
range candidates, sources, evaluator, frozen input, executable identities and
complete compile/TRAIN/AUDIT logs. Its pretrial physical proof has generation
15,360, eligible mask 3, participants 3 and graph 2:

| Frozen artifact | SHA256 |
| --- | --- |
| Local `code-trial/model-parent.centroid` | `cb12383ab9e6e37f7853960da54c8c48e32cdc8227751a1d7efdfb554cfdebf5` |
| `code-trial/contact.bin` | `b2cedd1daf78834f66af2b12a0be7f24262fe156e597bc39240f03664c6197f8` |
| `code-trial/parent.c` | `e0a751b3accfd409bd29d7ead1b5dee298f73a74119a38116ae757c3f4d9a94d` |
| `code-trial/evaluator.c` | `9e0787c60bdb5de17226c4cd8b41f10a00431aa9906311e5d1dbcac451c710f4` |
| `code-trial/input.bin` | `78e67d4b71ddff0ef5a09fa9733042de13902ce3636f0b3e86d68ec7190826ca` |

Each of the three TRAIN-family receipts (`code-trial/train-receipts.txt`; local artifact) binds
the parent code, independent evaluator, frozen input, measured log, action,
model parent and contact proof. Only independently measured TRAIN targets teach.
Three genuine-contact CODE updates advance 624→627 updates and
15,360→15,616 generations. The winner and complete probabilities remain in the
excluded trial report. This reruns previously opened fixture families to verify
instrumentation; it is not a fresh independent quality trial, selects no global
recipe and applies no new patch.

The existing strict C11 integration driver (`../2026-10-05-completion/consume.c`; local artifact)
was rebuilt (`consume-build.log`; local artifact) against the final compatible MSVC library.
Its [structured consumption report](working-consumption.md) records the complete
current user request, attributed LLM proposal, visible ACTIVITY and exact
`src/experiment/contact.c` SOURCE bytes. Eight causal byte/EOS targets are
checked against that immutable SOURCE record. They advance 627→635 updates
through 1024 physical generations, 15,616→16,640, with zero pending tasks.
Both live owner clocks finish at 635; shared/policy clocks remain zero. CODE
readout values and both optimizer moments remain bitwise unchanged across those
eight TEXT updates. This verifies attributable input consumption and head
isolation, not context utility or reliable sequence generation.

All 27 top-level command records, including failed attempts, were exported using
their matching native CLI/checkpoint and retained as `.log.activity` sidecars.
Every raw hash and length matches its receipt; all records declare complete
bytes, timeout 0 and omitted 0. The five expected nonzero child exits are the two
initial Windows test failures, two initial configure harness failures and the
final missing-compiler rejection. All other captured children exit 0. The 27
exports are separately admitted as attributed ACTIVITY inputs to the working
checkpoint; admission adds no numerical updates. Evaluation outcomes stay in
excluded research/HOLDOUT storage.

Mutating child commands use `runs/local.clife`, while their capture parent uses
the separate `runs/closure-capture.clife`. Thus the parent's final save cannot
overwrite the child's published training state. Clang and Linux verification
states remain separate; exact checkpoint continuation requires a compatible
build and numerical environment. Local checkpoints are excluded from Git.

The final local MSVC snapshot, after all 27 receipt admissions, is 1,937,960
bytes with SHA256
`d111b264e349d19386705fa242b78fb2b8aba84b7a94de07b89934984a5ca681`.
It has 635 completed updates, generation 16,640, both live clocks 635 and zero
pending tasks. Later state writes change that identity.

The source identity manifest (`source-identities.tsv`; local artifact) records final source,
header, test, root declaration/documentation and explicitly attributed TRAIN
input bytes. The accepted algorithm and live public headers retain their
earlier publication hashes; this durability closure does not change that patch.

The source manifest is the historical verification snapshot from before the
blueprint was archived and documentation references were repaired. Its original
paths and hashes remain unchanged. The current design document is retained in
[research/archive](../../archive/FRESH_START_BLUEPRINT.md); current scope remains
in the roadmap. Repository attributes preserve exact staged source, fixture and
evidence bytes without automatic line-ending conversion. The oversized comparative
prediction artifact remains local with a separate
[lossless archive receipt](../../experiments/2026-10-05-comparative-research/ARCHIVE.md).
Historical run outputs, measurements, logs and model/session snapshots are excluded from Git
under the [artifact policy](../../ARTIFACTS.md); their identities remain in the
committed manifest. This changes storage scope, not the original measurements.
