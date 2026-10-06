# Native SDK contract and local use

This is the first, pre-stable SDK contract for API/ABI 1 and package version
0.1.0. Product acceptance status and evidence are in the source checkout's
`PRODUCT_PLAN.md` and `research/ROADMAP.md`. The library API, deployment format,
task profile and trainer continuation are independently versioned. SDK code and
original model assets are MIT licensed, as authorized by the owner on October 6,
2026. The source-root `LICENSE` and the installed `LICENSE` alongside this guide
contain the copyright and permission notice.

## Components and dependencies

| Installed component | Public interface | Dependency contract |
| --- | --- | --- |
| Runtime | `Centroid::Runtime`; `centroid_runtime.h`, `centroid_context.h`, `centroid_code_helper.h`. | Native C runtime, OS file enumeration, and system math where needed. No Life world, optimizer, backward kernels, process launcher or research fixtures. |
| Training | `Centroid::Training`; legacy headers and `centroid_training.h`. | Runtime and the existing optional native training/workflow compatibility library. Capture/research services remain in this development component. |
| Tools | `Centroid::CLI`, `Centroid::SDKQualify`. | Training; native compiler only for explicit verified compilation workflows. |
| Examples | `centroid_code_helper` and its installed C source. | Runtime. External LLM connections belong to the host. |

The legacy `centroid` build target and APIs remain available. Runtime packages
and inference are independent of that compatibility component. Building the SDK
or a C host requires CMake 3.20+ and a native C11 compiler; running installed
inference binaries does not require CMake or a trial compiler.

The tested Windows dynamic-CRT packages require the x64 Visual C++ v14 Runtime
at least as recent as their MSVC build tools, plus the operating system's UCRT.
Install the supported runtime using
[Microsoft's redistribution guidance](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170).
The SDK archive does not redistribute Microsoft's installer. The tested Linux
binaries use Ubuntu 24.04's native libc/libm; other distributions require a
compatible system ABI or a native rebuild. Platform binaries are separate from
portable model files.

## Build, install and consume

Full development build on Windows:

```powershell
cmake -S . -B build-sdk -A x64
cmake --build build-sdk --config Release --parallel
ctest --test-dir build-sdk -C Release --output-on-failure
cmake --install build-sdk --config Release --prefix ./build-sdk/install
```

Runtime-only shared build on Linux (omit `BUILD_SHARED_LIBS` for a static runtime):

```sh
cmake -S . -B build-sdk-runtime -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON -DCENTROID_BUILD_TRAINING=OFF \
  -DCENTROID_BUILD_TOOLS=OFF
cmake --build build-sdk-runtime --parallel
ctest --test-dir build-sdk-runtime --output-on-failure
cmake --install build-sdk-runtime --prefix ./build-sdk-runtime/install
```

An external host uses the installation, with no private/source-tree headers:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_host LANGUAGES C)
find_package(Centroid 0.1.0 EXACT CONFIG REQUIRED COMPONENTS Runtime)
add_executable(my_host main.c)
target_link_libraries(my_host PRIVATE Centroid::Runtime)
```

Supply the installed prefix through `CMAKE_PREFIX_PATH`. The installed C example
is also a standalone CMake project. Windows shared hosts place the Runtime DLL
beside their executable or on their process DLL search path; the example copies
it explicitly. Linux installed SDK tools use a relative library search path.
The package is relocatable; rebuild an external host against its new prefix.

Run the context helper with `centroid_code_helper SOURCE_ROOT QUERY [MODEL_BUNDLE]`.
It prints exact attributed evidence with versions, spans and omitted counts.
Without a bundle it is a deterministic evidence helper. With a qualified
lower-bound bundle it additionally runs a native strategy-selection example.
A generic experimental export is loadable through the model API but does not
qualify for this task-specific recommendation interface.

Run `centroid_code_helper --context-lifecycle` for the provider-free, in-memory
editor example. It admits unsaved source, replaces it with version 2, preserves
the original bytes and shows that retrieval follows the new version. A second
context holds different source at the same path and remains independent. This
mode requires no source directory, model or provider.

## Ownership and compatibility

SDK allocations are destroyed through the SDK. `cr_model_create_values` copies
frozen caller values; it exposes no optimizer or training operation. A model is
immutable after construction/loading and may be shared by concurrent readers.
Synchronize destruction and loader replacement with every reader. A successful
load replaces and destroys the prior handle; failed loads leave it untouched.
Retain prior bundle files for rollback and load them at an explicit host boundary.

For explicit inference workspace, use `cr_model_predict_with_scratch` with
`double scratch[CR_PREDICT_SCRATCH_DOUBLES]`: 554 doubles, or 4,432 bytes on the
supported binary64 platforms. Each concurrent call owns its workspace and output.
Keep workspace, input, owner masses, model and output disjoint. An undersized
workspace returns `CR_LIMIT`; detected overlap returns `CR_INVALID`. Failed
predictions preserve output probabilities, although workspace contents may
change. `cr_model_predict` supplies the same bounded workspace on its stack.
Both paths allocate nothing during prediction and produce the same values.

Each mutable context belongs to a host session; synchronize all concurrent
access to a mutable context. Admission retains original bytes and historical
versions, including reappearance after explicit removal. Borrowed record bytes
and strings remain valid until context destruction. Returned view structures
are snapshots; retrieve a new view for an updated `current` flag.

Initialize sized options with their initialization APIs. For sized model/profile
outputs, set `struct_size` to `sizeof` the public structure and `api_version` to
`CR_API_VERSION`. A C caller supplies valid readable/writable storage of the
documented sizes; output buffers must not alias borrowed source/evidence storage.
Do not exchange allocations through a different Windows CRT.

Version 0.1.0 is pre-stable: the CMake package requires its exact version. ABI 1
applies to the declared OS/x86-64/compiler-ABI combination, with Windows `__cdecl`
and explicit exports. It is not a universal Windows/Linux binary. Trainer
checkpoint build-ID checks remain strict. Bundle portability promises decoding
and tested inference agreement, independently of bit-identical continuation.

## Bounds and output semantics

| Operation | Registered bound |
| --- | --- |
| Feature input | 32 ordered byte-projection features; at most 16 MiB of input. |
| Model | 1–4 owners; legacy CODE 4 actions and TEXT 257 byte/EOS outputs; bundle at most 512 KiB. |
| Context | Default 512 retained versions, at most 4096; default 16 MiB per record and 64 MiB total original bytes. Metadata/index allocation is additional. |
| Query | At most 16 hits; default 4, 8192 raw excerpt bytes, 2048 bytes per excerpt. Allocation-free bounded stack plus caller outputs. |
| TEXT context frame | At most 8 distinct evidence IDs and 1 MiB complete binary frame; caller-owned output and explicit required length. |
| Scan | Ordinary local files, at most 64 directory levels and 65536 visited entries. Explicit exclusions, failures and partial progress. |
| Search profile | Nondecreasing `int32_t` arrays with at most 4096 entries; sortedness is the caller's precondition. |

Byte budgets count raw returned excerpts. Use `cr_evidence_format`'s required
length for a separate complete formatted-buffer budget. It preserves embedded
NULs; consume written byte lengths rather than `strlen`. Host token budgets
require the host's actual tokenizer and prompt-overhead accounting.

The evidence profile is `evidence-lexical-cosine/v1`. It uses lexical support and
fixed centroid similarity; its scores are not truth confidence. Only current
TRAIN records of explicitly requested kinds are retrievable. LLM proposals and
activity preserve their labels and do not become verified SOURCE or targets.
DEV/AUDIT may be retained but are quarantined. Scans exclude evaluation, data,
research, tests, build, dependency and hidden locations, and reject links. Scan
reports are partial-progress reports on failure, not atomic whole-tree updates.
Explicitly call `cr_context_forget_source` when a source is removed.

`cr_text_frame_context` builds the versioned target-free TEXT input from an
explicit TRAIN request, up to eight TRAIN evidence records and an assistant
prefix. The request must be SOURCE or ACTIVITY; evidence may additionally be an
attributed LLM proposal. Explicit historical record IDs preserve their immutable
bytes. DEV/AUDIT records are rejected. Probe the required size with a null output,
then provide a separate buffer; insufficient capacity leaves it unchanged.
The binary frame preserves embedded NULs and contains no terminator. Pass its
explicit byte length to `cr_encode`. This frame identifies context roles and
provenance; it neither supplies a target nor counts an external LLM's tokens.

The learned search profile binds its encoder/catalog identities through model
metadata. `cr_code_recommend` requires those exact identities and qualified
evidence references; the host verifies the referenced record before adoption.
The example checks its local sidecar SHA256 with
`cr_model_check_qualification_file`; this validates recorded bytes, while review
of quality claims and publisher trust still belongs to the host.
Legal-action masks contain only four nonzero permitted bits. The host explicitly
executes a selected action or its conventional fallback. Comparison savings
measure algorithm work; selection overhead must be reported separately and is
not a promise of faster one-off search latency.

## Explicit local training, export and qualification

The native CLI keeps source/work ingestion and Life training outside inference:

```text
centroid new LOCAL_STATE.clife
centroid scan LOCAL_STATE.clife SOURCE_ROOT
centroid train LOCAL_STATE.clife 1
centroid export LOCAL_STATE.clife EXPERIMENTAL.cmodel
centroid inspect-model EXPERIMENTAL.cmodel
```

Experimental export does not modify the checkpoint and cannot silently acquire
a qualified label. Production updates still require physical Life contact.
LLM inputs use attributed admission; independently verified measurement receipts
remain the only finite-code teachers.

`centroid-sdk-qualify PROJECT_ROOT NEW_OUTPUT_DIRECTORY` implements the registered
bounded search-feature recipe and gates. It retains parents, every condition,
measurement, prediction and failure, then exports an explicitly accepted model
only if all gates pass. Use fresh output directories; do not overwrite attempts.
The recipe and opened results live under the source checkout's
`research/sdk-release/`, outside ingestion.
Reexecuting a frozen trial checks reproducibility; it is not a new independent
quality trial or permission to retune on opened AUDIT.

The source build also supplies `centroid_sdk_resource_check BUNDLE SIDECAR`.
Its prospectively registered host, frozen candidate, caller workspace, clocks,
warmup, 1,600 measured calls and resource gates are documented in
`research/sdk-release/RESOURCE_PROTOCOL.md`. It retains raw native clock
intervals and separates prediction from loading, encoding, validation and
logging costs. It evaluates deployment resources without training or a new
quality trial.

CPack prepares local platform SDK ZIP/TGZ archives from declared install rules.
Generic CPack source sweeps are disabled. Configure
`CENTROID_PREPARE_SOURCE_PACKAGE=ON` and build target `centroid_source_package`
for a source archive of explicit native/document inputs and a SHA256 manifest.
Release models are separate assets with committed identity and
qualification metadata. Include the copyright and permission notice from
`LICENSE` with redistributed SDK and original model packages.
