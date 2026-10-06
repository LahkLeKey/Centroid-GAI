# Native SDK technical acceptance and release status

Recorded October 5, 2026 for package 0.1.0, API/ABI 1 and portable bundle v1.
The first SDK has implemented and exercised its inference, installation,
code-context and bounded learned-feature contracts. On October 6, 2026 the owner
approved the [MIT License](../../LICENSE) for SDK code and original model assets.
The first-product M0–M6 gates are complete within the declared scope. Licensed
source, platform SDK and separate reference-model archives carry the notice;
their exact identities and installation checks are recorded in the local release
manifest. Remote publication and backup are not established by these archives.

The primary scope is [PRODUCT_PLAN.md](../../PRODUCT_PLAN.md). The installed
ownership, dependencies and compatibility contract is in
[docs/SDK.md](../../docs/SDK.md), with the canonical artifact schema in
[docs/BUNDLE_FORMAT.md](../../docs/BUNDLE_FORMAT.md). Deterministic retrieval and
learned usefulness have separate evidence in [M4_REPORT.md](M4_REPORT.md) and
[M5_REPORT.md](M5_REPORT.md).

## Implemented contracts

| Product gate | Technical evidence | Status |
| --- | --- | --- |
| M0, first contract | Public C interfaces, ownership, resource bounds, profile identities and portable format are written and implemented. | Accepted; owner-approved MIT licensing covers SDK code and original model assets. |
| M1, deployment boundary | Runtime builds independently of Training, Tools, backward kernels, optimizer, Life state, process launcher and audit fixtures. | Accepted for the tested configurations. |
| M2, portable assets | Value-only export, canonical serialization, immutable inference, inspection, transactional loader and cross-toolchain conformance. | Accepted for the tested format and toolchains. |
| M3, installed SDK | Static/shared libraries, controlled exports, exact-version CMake package, detached installed consumers and relocated prefixes. | Accepted for the tested platform/ABI matrix. |
| M4, code-context host | Installed native host, attributed complete source history, bounded evidence, refresh, quarantine, abstention and measured fixture behavior. | Accepted as deterministic evidence retrieval. |
| M5, first learned feature | Independently verified finite strategy selection passes its registered quality, retention, contact and resource gates. | Accepted only within `lower-bound-strategy/v1`. |
| M6, complete product release | Source/platform archive preparation, reference assets, installed local workflow, final regressions and rollback checks. | Accepted; MIT-licensed archives include the notice and separate qualification/checksum metadata. |

Runtime contains five authored C modules: model, bundle, context, code helper
and framing. Its deployed interface contains 30 `cr_*` functions. Private hashes and
value-validation helpers are not public exports. Shared-library inspection found
no trainer, parameter-update, tool-execution or research entry points. The
Training compatibility component retains these explicit development workflows
and links Runtime; Runtime does not depend on Training.

Opaque models retain parameter values and identity metadata without optimizer
moments. Prediction accepts a caller-owned 554-double workspace; its convenience
wrapper supplies that workspace on the stack. Prediction, typed TEXT framing and
context query make no per-call allocation and use bounded storage plus caller
outputs. Context admission and scanning allocate outside
the request path. Caller-owned views preserve immutable bytes and provenance;
current flags in returned structures are snapshots. Hosts synchronize mutable
contexts, model replacement and destruction with outstanding readers.

## Platform and installed-consumer evidence

All configurations below target x86-64 and strict C11. Linux validation used GCC
on Ubuntu 24.04 through WSL. Windows used MSVC 19.38.33145.0 and clang 21.1.0.
These tests establish the named native platform/ABI combinations, rather than a
single binary usable on both operating systems.

The table below retains the earlier verification matrix. The completion refresh
adds caller-workspace, binary framing and installed context-lifecycle coverage;
its current counts and separate log identities are recorded later in this report.

| Configuration | Completed installed/package evidence |
| --- | --- |
| Windows clang, static Runtime | 8/8 runtime and package tests; detached consumer configure/build/run after installation and relocation. |
| Windows clang, shared Runtime | 8/8 runtime and package tests; DLL/import library and relocated detached consumer. |
| Windows MSVC, static Runtime plus Training | Installed Runtime/Training consumers pass; final full recheck passed 30/30 in 51.59 seconds. |
| Windows MSVC, shared Runtime | 8/8 runtime/package tests in `build-sdk-runtime-msvc-shared/runtime-package-tests.log`; exactly 28 DLL exports and public-only installed deployment/rollback pass. |
| Windows clang, shared Runtime plus static Training | 11/11 package tests, including a real installed-consumer Life update and portable export/inference parity. |
| Windows clang, full development build | 30/30 tests in `build-sdk-full-clang/final-full-tests.log`, 52.48 seconds. |
| Ubuntu 24.04 GCC, static full build | Final 30/30 tests in `build-sdk-full-linux/full-release-tests.log`, 63.34 seconds, including the installed Training consumer. |
| Ubuntu 24.04 GCC, shared Runtime | 8/8 final runtime/package tests, including relocated installed consumers; installed helper runs from the relocated prefix. |

Runtime-only packages configure without training inputs, research outputs or a
trial compiler. The detached consumer uses installed public headers and
`find_package(Centroid 0.1.0 EXACT CONFIG REQUIRED COMPONENTS Runtime)` with
`Centroid::Runtime`. Package tests also install and relocate the optional Training
component, then compile and run a consumer using `Centroid::Training` without
private/source-tree include paths.

Windows shared binaries still require their declared native CRT dependencies.
SDK allocations are released through SDK destruction functions; hosts do not
free them through a different CRT. The DLL must be available through the host's
normal loader path. Linux shared binaries retain native system C/math and dynamic
loader ABI requirements. A relocated prefix and tested Ubuntu build do not imply
compatibility with every Linux distribution, libc version or architecture.
Package 0.1.0 is pre-stable and uses exact package-version compatibility.

Local source-archive verification used explicitly selected authored inputs,
rather than a generic sweep of the working tree. A detached fresh full-source
build passed 30/30 tests in
`build-sdk-runtime-static/source-package-fresh-full-02.log`, 49.48 seconds.
A fresh Runtime-only source build passed 8/8 in
`build-sdk-runtime-static/source-package-fresh-runtime-03.log`, 1.68 seconds.
Generated runs, checkpoints, private context and historical model outputs remain
outside the normal source archive. Archive identities are intentionally recorded
by separate manifests, not copied into a report included in those archives.

## Portable-model and failure transactions

The earlier fixed conformance set covered 48 inputs across both supported heads,
eligibility/routing masks and boundary conditions. Clang and MSVC outputs agree
exactly. GCC's maximum absolute probability difference from clang was
`8.6736173798840355e-19`; all selected decisions agreed. The declared portable
inference tolerance is distinct from exact same-build trainer continuation.
Logs are `build-sdk-full-clang/conformance-clang-msvc.log` and
`build-sdk-full-clang/conformance-clang-gcc.log`, with the complete native vector
tables retained beside their builds.

The refreshed vector format adds explicit exact-input SHA256 and encoded feature
columns. Its 90 rows cover 15 fixed inputs, both heads and every nonzero
two-owner mask. Inputs include the historical eight observation bands, empty
bytes, all 256 byte values, the exact 16 MiB encoder limit, request/evidence/LLM/
ACTIVITY role framing, assistant prefixes and an exact 1 MiB complete frame.
Native framing tests independently compare with the preserved Training encoder
and reject quarantined records, duplicate evidence, aliasing and over-limit
frames without changing output. Encoder limit-plus-one and caller-workspace
capacity/overlap checks preserve failed outputs. Refreshed numerical comparison
results are recorded below separately from the earlier 48-row evidence.

The complete v2 native comparison passed on all three toolchains. Clang and
MSVC agree exactly on encoded features and scores; GCC's maximum absolute
difference is `8.6736173798840355e-19`. All 90 selected decisions agree under
the declared absolute `1e-12` plus relative `1e-10` tolerance. Input byte hashes
also agree, including typed framing and maximum-size inputs. The retained logs
are `build-sdk-full-clang/completion-refresh-conformance-msvc.log` and
`build-sdk-full-clang/completion-refresh-conformance-gcc.log`; each build retains
its separate `*-vectors-v2.tsv` table.

Native tests reject truncation, incorrect lengths, corrupted integrity digests,
unsupported versions and semantic schemas, invalid dimensions, nonfinite values,
duplicate/zero owner identities, noncanonical string padding, nonzero reserved
fields and mismatched value-model identities. Tests also reject a qualified label
without explicit evidence metadata. A failed candidate load preserves the exact
incumbent handle and bytes. Successful loading replaces a complete validated
candidate only after validation. Saving an asset refuses an existing path.

Exported predictions are bit-identical to legacy predictions on the source
build, including both heads, all tested owner masks and shared-scale fixtures.
Checkpoint bytes remain unchanged by export, and the original checkpoint still
resumes under its existing strict build-ID and recipe contract. Portable bundles
do not carry source/work context, optimizer, world, receipts or continuation
state.

`cr_model_check_qualification_file` verifies the exact recorded sidecar SHA256.
It checks identity rather than evaluating quality or authenticating a publisher.
Experimental owners defer; missing, malformed or different sidecar files fail.
The qualified metadata is an explicit attributed attestation, not an automatic
promotion performed by the loader. The native qualification tool supplies the
registered independent acceptance gate, and an adopting host reviews its record
and trusts its publisher. The installed example checks the adjacent bounded
basename reference before task-specific use.

## Installed local workflow and deployed learned feature

`runs/sdk-product-e2e-001/installed-workflow.log` records an installed native CLI
run that admitted 55 source records, retained a separately attributed LLM proposal
and captured complete native compiler output as activity. The source scan
excluded 26 entries and reported no failures. No proposal or activity became an
automatic target label.

An explicit one-epoch source recipe selected 32 sources, reported 23 omitted
sources and enqueued 128 source tasks. It completed 128 updates with zero pending
tasks and zero deferrals. All 2048 reported generations had physical contacts;
both active owners reached clock 128, with other owner clocks zero. This is
local workflow/authority evidence, not a quality claim for the experimental
source-trained byte model.

A refreshed installed-tool run in `runs/sdk-product-e2e-002/installed-workflow.log`
starts from the retained prior context/checkpoint snapshot. It admits ten changed
or new source versions, keeps 46 sources unchanged, excludes 30 entries and
reports no scan failures. The attributed continuation proposal and complete
112-byte compiler output are admitted separately as LLM context and ACTIVITY.
The explicit one-epoch source recipe selects 32 of 56 current sources and
reports the other 24; it completes 128 additional contact-authorized updates.
The cumulative state reaches 256 updates and generation/contact count 4096,
owner clocks 256/256 and zero pending tasks or deferrals. A new experimental
export remains unqualified. The complete trained checkpoint hash is unchanged
by export and strict continuation/report loading. Parent and post-train snapshots
are retained separately before later context refresh. This records local
source consumption and authority, without promoting that byte model or
introducing new quality evidence.

The installed workflow exported and inspected a 135,980-byte experimental bundle.
The checkpoint's complete-file SHA256 before and after export was identical;
the log explicitly records `unchanged_by_export=true`. Raw output, the source
checkpoint, proposal input and exported bundle remain in that ignored run
directory. Training and capture were explicit commands outside deployment
inference.

The installed `centroid-sdk-qualify` was also run in
`build-sdk-full-msvc/installed-qualification-001`, with `accepted=1` recorded in
the adjacent `.log`. This reruns the frozen recipe and previously opened workload
families as installed-tool reproduction/conformance. It is not a new independent
AUDIT result and introduces no post-AUDIT tuning.

The first fresh M5 trial and retained attempts are detailed in
[M5_REPORT.md](M5_REPORT.md). The accepted feature is bounded recommendation among
four correct lower-bound algorithms, with explicit observation/catalog identities
and legal-action masks. Its benefit is fewer value comparisons within that
registered workload profile; inference-selection overhead is measured separately.
The schedules tie, so this report makes no Life-superiority claim.

Installed deployment consumers on Windows MSVC, clang and Linux GCC load the
reference model, check its qualification-record identity and execute a legal
recommendation.
They then attempt an invalid replacement, preserve the incumbent, load the
experimental zero-update parent, receive `CR_DEFERRED` from task-specific
recommendation, execute the host's conventional binary-search fallback and
re-adopt the qualified asset. Logs are
`build-sdk-full-msvc/installed-deployment.log` and
`build-sdk-full-clang/installed-deployment.log`, with the Linux output retained
as `build-sdk-full-linux/installed-deployment.log`. This verifies numerical rollback
and explicit host fallback; a previous qualified model may instead be retained
by another host's rollback policy.

Reference asset identities, byte sizes and checksums are in
[models/reference-manifest.json](../../models/reference-manifest.json), with
asset ownership/use described in [models/README.md](../../models/README.md).
The reference bundle and parent are separately prepared local assets, not private
application context or ordinary source-checkout inputs. Complete raw attempts are
listed in [local-artifacts.txt](local-artifacts.txt).

## Retained failures and final verification status

The completion refresh adds four meaningful tests to each Examples-enabled
configuration: binary framing, the build-tree context-lifecycle example, and
installation/execution of that example. Detached consumers now call both new
public APIs. The current native matrix is:

| Refreshed configuration | Result | Elapsed |
| --- | --- | ---: |
| Windows MSVC, full static Runtime/Training/tools | 34/34 | 52.05 s |
| Windows clang, full static Runtime/Training/tools | 34/34 | 50.51 s |
| Ubuntu 24.04 GCC, full static Runtime/Training/tools | 34/34 | 50.85 s |
| Windows clang, Runtime-only static | 12/12 | 1.13 s |
| Windows clang, Runtime-only shared | 12/12 | 1.16 s |
| Windows MSVC, Runtime-only shared | 12/12 | 2.29 s |
| Ubuntu 24.04 GCC, Runtime-only shared | 12/12 | 2.54 s |
| Windows clang, shared Runtime plus Training, package fixtures | 11/11 | 1.48 s |

The MSVC full log is `build-sdk-full-msvc/completion-refresh-full-tests.log`;
other matrix logs are `completion-refresh-tests.log` in their named build
directories. The Training-plus-shared configuration intentionally has Examples
disabled, so its package-only count is unchanged. DLL/SO inspection confirms
exactly 30 public Runtime exports, including caller workspace and binary framing.
Private helpers, trainer/update kernels and process launchers remain absent.
The Linux SO needs only its declared native libc/libm; Windows imports the native
CRT and Kernel32. Boundary logs and their combined summary remain beside the
Runtime builds.

Strict C11 warnings-as-errors checks pass for all five Runtime modules and six
changed native consumers/tools/tests. Separate clang analyzers report no
diagnostics for workspace, serializer alias guards, framing or the resource
benchmark. These logs are under `build-sdk-model/audit/*completion*` and remain
distinct from earlier review attempts. A Linux build initially retained
future-timestamp warnings while source-archive preparation followed ongoing
documentation edits on the mounted workspace. Disabling that optional preparation
for the ordinary development build produced a warning-free stable build in
`build-sdk-full-linux/completion-refresh-build-stable.log`; the failed/warning
attempts are retained and are not C compiler diagnostics.

### Prospectively registered deployment resources

[RESOURCE_PROTOCOL.md](RESOURCE_PROTOCOL.md) registers the physical host,
optimization, frozen model, input order and resource gates before this benchmark.
Each matched run validates the qualification sidecar, uses 32 warmup calls and
retains 1,600 raw native intervals, exactly 800 for each head. It uses the same
accepted immutable value model without learning, selection or new AUDIT.

| Registered configuration | CODE mean | TEXT mean | Native process peak | Gate |
| --- | ---: | ---: | ---: | --- |
| Windows MSVC `/O2` | 391.625 ns | 8,970.125 ns | 5,632,000 bytes | Pass |
| Windows clang explicit `-O2` | 431.625 ns | 8,404.500 ns | 5,632,000 bytes | Pass |
| Ubuntu 24.04 GCC Release | 362.085 ns | 8,874.509 ns | 2,605,056 bytes | Pass |

Raw clocks, compiler/host identity, denominator, validation and cost breakdowns
are in each build's `resource-conformance-001.log`; the matched clang build is
`build-sdk-resource-clang-o2-001`. Caller workspace is exactly 554 doubles or
4,432 bytes. The reported `caller_buffers=59768` counts principal input, mass,
workspace, scores and interval arrays; it excludes scalar bookkeeping and the
two caller model-info structures. Native process peak includes those additional
allocations. Separate before/after canonical serialization uses 271,960 bytes
and confirms the complete model remains unchanged. Loading, encoding,
qualification validation, probability validation, identity checks and logging
are outside prediction timing and have separate reported costs. Framing,
retrieval and callbacks are absent from this benchmark. Means pass the fixed
10 ms/head gate using unrounded totals; every process peak is nonzero and below
128 MiB. These measurements make no net search-speed or general CPU guarantee.

The initial full-clang resource run used CMake's `-O3` Release flags despite the
protocol's registered Windows `-O2`. Its raw output is retained as informational
unmatched-optimization evidence in `build-sdk-full-clang/resource-conformance-001.log`.
The protocol was not rewritten after timing. The explicit `-O2` build above
supplies the matched check. A similarly optimized installed-clang recipe
repetition remains conformance only. The fresh MSVC `/O2` installed qualifier in
`build-sdk-full-msvc/installed-qualification-002` passes the registered whole-run
120-second and 128 MiB gates: 2.002951 seconds and 34,189,312 peak bytes, with the
same selected value-model identity. Its portable validation/observation/inference
mean is 12.122 microseconds over 240 calls. Its complete attempts and clocks are
retained; this frozen reproduction does not reopen independent quality evidence.

Refreshed platform/source archives and their detached final source checks are
bound by the ignored `build-sdk-release/LOCAL_RELEASE_MANIFEST.json`. The manifest
records exact byte hashes, source input identities and separate test-log paths.
It keeps prior local archive attempts identifiable without including archive
hashes in their own source inputs. The October 6 licensed archive refresh preserves
the earlier unlicensed preparation attempts and adds the exact MIT notice to
each SDK, source and reference-model package. The six original reference-model
files remain byte-identical; a seventh asset supplies the separate license.

The owner-approved license applies to original project code and model assets.
The reference model was trained on the project's registered native synthetic
arrays and independently measured algorithm outcomes, without imported pretrained
weights or an external training corpus. Its qualification record continues to
identify the unchanged original protocol, source and raw evidence. Licensing
does not reopen AUDIT, change the selected model or expand its qualified feature.
Native CRT/system-library dependencies retain their existing declared terms and
are not copied into the model asset. Release verification checks notice byte
identity, updated manifests and installed consumers; unchanged numerical source
continues to be covered by the recorded complete native matrix above.

The licensing work is also retained as attributed local context in
`runs/sdk-product-e2e-mit-001/context-refresh.log`. Five source versions refresh
and 51 remain unchanged, with no scan failures. The owner decision and exact
MIT notice are admitted separately as ACTIVITY, not teacher labels. The existing
Life state remains at 256 updates, generation/contact count 4096, owner clocks
256/256 and zero pending tasks; parent and refreshed snapshots are retained.

Earlier packaging work retained failures rather than removing them from the
record. These included an installed-training fixture with missing owner mass
(`build-sdk-training-shared/package-verification-failed-missing-mass.log`), and a
first fresh-source test attempt that exposed filesystem/consumer-configuration
issues (`build-sdk-runtime-static/source-package-fresh-full-tests.log`). Corrected
fixtures and subsequent fresh-source/installed checks passed as recorded above.
Native initialization/export contract tests, qualified-record mismatch checks and
raw earlier logs remain separate from learned quality evidence.

The earlier full MSVC suite passed 30/30 in
`build-sdk-full-msvc/full-tests-final.log`, 52.93 seconds. Its subsequent focused
installed/runtime/learning check passed 14/14 in `installed-final-tests.log`,
6.63 seconds. A later full-release repetition in `full-release-tests.log` failed
copying the installed Training prefix during `package_training_relocate`, leaving
its three dependent tests unrun. That failed repetition is retained. Repeating
the same copy command immediately succeeded, with no source change and no proven
cause for the transient, nonreproduced filesystem-copy failure. The subsequent
complete current-suite recheck passed 30/30 in
`build-sdk-full-msvc/full-release-tests-recheck.log`, 51.59 seconds. The expanded
current Linux suite passed 30/30 in `build-sdk-full-linux/full-release-tests.log`,
63.34 seconds. These full checks include installed Runtime/Training, learning and
the retained legacy mathematical, authority, continuation and workflow contracts.

Earlier read-only runtime reviews used clang static analysis on model, bundle,
context and code-helper modules without diagnostics. Those logs remain under
`build-sdk-model/audit/`. Review corrections covered source reappearance/version
history, bounded evidence formatting, directory enumeration errors and avoiding
hashing unread sidecar bytes after an I/O failure. Normalized Windows directory
handles preserve reserved-path checks without requiring ancestor enumeration
rights. These infrastructure checks do not establish broad model intelligence.

The technical acceptance gates for the completed matrix configurations have
passed. The owner's October 6 MIT choice closes the remaining redistribution
gate for original SDK code and model assets. Support claims apply to the completed
configurations recorded above. Archive preparation does not imply remote
publication or backup. Chat, gameplay,
standalone fluent generation and downstream LLM coding improvement remain
separately gated tracks; they are not claimed by the first code SDK evidence.
