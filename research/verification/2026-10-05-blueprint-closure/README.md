# Fresh-start blueprint contract closure

Raw artifacts named below are retained locally under the [artifact policy](../../ARTIFACTS.md); their identities are in the [artifact manifest](../../local-artifacts.txt). They are excluded from Git.

October 5, 2026. The follow-up audit found real gaps behind the earlier bounded
completion claim. This closure implements those missing contracts and retains
new verification separately from the [earlier completion evidence](../2026-10-05-completion/README.md).
The declared stages 0–7 are implemented within their native, bounded scope.
This is implementation and integration evidence; it adds no independent quality
trial and changes none of the mixed or negative findings in the
[authoritative roadmap](../../ROADMAP.md).

## Corrective implementation

| Audited gap | Implemented behavior | Meaningful regression |
| --- | --- | --- |
| Source-training orchestration lived in the CLI, with implicit selection coverage. | The public [c_source_train API](../../../include/centroid_source.h) owns the recipe in [source.c](../../../src/domain/source.c). The CLI parses, dispatches, reports and persists successful calls. First 32 current TRAIN SOURCE records by immutable ID; four fixed byte/EOS positions; up to 128 enqueue attempts per epoch; explicit available/selected/omitted coverage. | Current/split/role filtering; binary byte and EOS teachers; physical-contact authority; 35-record selection and idempotence; queue and task-ID limits; committed failure progress; split-checkpoint continuation; parity with explicit original enqueue/step calls. |
| Individual clocks could fit while the complete canonical optimizer-clock sum overflowed after publication. | [trainer.c](../../../src/life/trainer.c) preflights cumulative live and retired clocks, production update bounds and relevant counters before publishing a Life generation. | Two/four owners at uint64 boundaries, genuine merged retired clocks and archived supervision, unconsumed CODE receipts, exact incumbent/model/world/policy/RNG/task/report preservation, checkpoint round trips and valid updates beside huge inactive clocks. |
| Work capture checked known split paths too late, after launching or writing output. | [work.c](../../../src/context/work.c) validates ordinary no-link cwd and the new output's existing parent before launch. Known audit/development/fixture paths reject. Windows nonordinary raw names reject. Launch and reread use retained canonical paths. | Rejected cwd/output and link ancestors leave context, ID and result unchanged, create no raw artifact and execute no child. A child deleting an intermediate lexical directory still yields the exact canonical binary capture. Ordinary research activity and complete 64 MiB output-cap behavior remain supported. |
| Windows physical spelling could hide known reserved paths or leave case aliases stale. | [ingest.c](../../../src/context/ingest.c) expands existing Windows paths to long names after no-link validation and revalidates. Absolute SOURCE case variants refresh only after matching ordinary native file identities. | Exact dirty versions, immutable original provenance and rescan idempotence; case aliases where lookup is supported; short-name TRAIN imports, scans and capture preflight where the volume provides a distinct 8.3 alias. |

The source workflow bounds epochs to 1–1000 and generations per epoch to
0–1,000,000. Aliases count as separate immutable records. Duplicate positions,
idempotent admissions and a failed admission count as attempts. Invalid arguments
and research owners preserve trainer/output; runtime failures return committed
progress. The complete multi-epoch call is not atomic, and callers own durability.
Selecting 32 records is explicit bounded coverage, not corpus-wide training.

Short-name and directory-link regressions use the capabilities available on the
local filesystem. Production long-name normalization and no-link validation are
compiled on both Windows toolchains; the Linux suite checks its native path rules.
Arbitrary raw-byte admissions still require honest caller provenance and splits.

## Final integrated verification

| Toolchain | Complete final output | Result | Elapsed |
| --- | --- | --- | --- |
| Windows MSVC 19.38 | `msvc-tests-final.log` | 15/15 pass | 35.47 s |
| Windows clang 21.1 | `clang-tests-final.log` | 15/15 pass | 36.81 s |
| Linux GCC 13.3 | `linux-tests-final.log` | 15/15 pass | 42.92 s |

Final captured builds are MSVC (`msvc-build-final.log`; local artifact),
clang (`clang-build-final.log`; local artifact) and Linux (`linux-build-final.log`; local artifact). All final
build/test children returned exit 0, timeout 0 and omitted 0. Complete raw output
and explicit argv/cwd/status/hash ACTIVITY sidecars are retained. Captures use
separate compatible toolchain checkpoints. Tests run concurrently across those
toolchains; elapsed times are verification observations, not matched-compute
quality comparisons. No production/test edit followed these final runs.

Strict C11 (`strict-c11-final.log`; local artifact) checks every production, CLI and native test C
file with warnings treated as errors. Formatting (`format-final.log`; local artifact) checks all
authored include/source/CLI/test C and header files. Static analysis (`static-analysis-final.log`; local artifact)
checks the new source workflow, trainer, work capture and ingestion with zero
warnings. These successful raw logs are empty; their sidecars retain command and
exit evidence. The initial analyzer output (`static-analysis.log`; local artifact) remains retained:
two sibling-field invalidation false positives prompted using an independent local
training-report buffer. The actual report writer writes only its declared output.
No warning was suppressed; final builds and all fifteen tests were repeated after
the buffer change. Earlier fifteen-test runs and intermediate builds also remain.

## Actual working-codebase consumption

The final native scanner ingests exact current dirty repository source while
excluding research, data, tests, generated output, model artifacts, dependencies,
secrets and links. The native CLI calls the new library workflow against this
working checkpoint. `source-train.log` reports actual bounded
coverage and physical updates, including explicit omitted sources.
The final scan reports 9 new versions, 28 unchanged observations, 9 excluded
entries, 0 failures and 452,724 read bytes. The workflow sees 41 current SOURCE
records, selects 32 and explicitly omits 9. Its 128 enqueue attempts produce 128
contact-owned updates: 488→616, generation 12,288→14,336, pending 0.

The existing strict C11 integration driver (`../2026-10-05-completion/consume.c`; local artifact)
is rebuilt against the final same-build MSVC library; `consume-build.log`
records the compatible Windows runtime link. It separately admits the complete
current user request, LLM proposal, activity and new `src/domain/source.c` bytes.
Eight causal SOURCE-byte/EOS tasks consume those attributed inputs through 1024
physical B3/S23 generations. [working-consumption.md](working-consumption.md)
retains exact record identities, versions, hashes, offsets, targets and clocks,
and verifies no pending tasks and bitwise unchanged CODE readouts/Adam moments.
This verifies integration, not context utility or useful sequence generation.
The structured pass then advances 616→624 updates and generation 14,336→15,360;
both live owner clocks end at 624, pending 0, shared and policy clocks 0.
The CODE readout/moment identity is unchanged across those eight updates.
All 22 complete command receipts, including intermediate analyzer output, are
exported through their matching native CLI build and admitted separately as
ACTIVITY inputs to the working checkpoint. Those admissions do not train or
change the numerical clocks. Every raw log matches its retained receipt hash,
complete-byte declaration, exit 0, timeout 0 and omitted 0.

Mutating children use `runs/local.clife`; their native capture parent uses the
separate `runs/closure-capture.clife`, so recording a command cannot overwrite
the child's published training state. Compatible clang/Linux verification states
remain separate. Local checkpoints are intentionally excluded from Git. The
source identity manifest (`source-identities.tsv`; local artifact) records the final authored
source/header/test/declarative input bytes; it does not change the checkpoint's
compiler/version/processor compatibility identity.
The accepted algorithm source and live public headers retain the exact hashes
in the earlier publication receipt; this closure does not modify that patch.

The final local MSVC checkpoint snapshot has 1786578 bytes
and SHA256 `27fbb8f5757a86cdb29172e3b0cb1246b532ba38ae0b5b6631575baf20cf7c5c`. It has generation 15,360,
624 completed updates, both live clocks 624 and zero pending tasks. The snapshot
identity is taken after all 22 receipt admissions; later state writes change it.
