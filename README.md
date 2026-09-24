# Centroid-GAI

A small, dependency-free C11 implementation of centroid-based text generation.
It is an experimental, inspectable baseline—not a neural large language model.

The model hashes tokens into fixed-size embeddings, averages the recent context,
learns context centroids with online k-means, and records a next-token distribution
at every centroid. During generation it finds the nearest centroid and samples its
learned distribution.

## Build

Requirements: a C11 compiler and CMake 3.20 or newer.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

All targets require ISO C11 with compiler extensions disabled. The lint gate
uses `clang-tidy` to check every owned C source: core, ABI, CLI, tests, and the
Node addon. It treats compiler, analyzer, and readability warnings as errors
and enforces a 20-statement function budget:

```sh
cmake --build build --target cgai_lint
cmake --build build --target cgai_format_check
```

Install `clang-tidy` and `clang-format` (21.1.0 in CI) before configuring. Addon lint also needs
Node headers, normally downloaded by `npm run native:build` in
`persistence/prisma-postgres`. CMake discovers headers for the running Node version
in the node-gyp cache; otherwise configure with
`-DCGAI_NODE_INCLUDE_DIR=/path/to/include/node`. The full lint target fails if
those headers are missing. `cgai_lint_core` and `cgai_lint_addon` are available
for checking either layer independently, including when tests are not built.

## Try it

On Linux/macOS or with a single-config Windows generator:

```sh
./build/cgai train examples/tiny_corpus.txt tiny.cgai 12
./build/cgai generate tiny.cgai "centroid models" 30 0.7 42
```

With Visual Studio, the executable is normally at `build/Release/cgai.exe`.

```powershell
.\build\Release\cgai.exe train examples\tiny_corpus.txt tiny.cgai 12
.\build\Release\cgai.exe generate tiny.cgai "centroid models" 30 0.7 42
```

CLI arguments after the prompt are optional: maximum tokens, temperature, and
random seed. Temperature `0` uses deterministic greedy selection.

## Library API

The documented public API is in [`include/centroid_gai.h`](include/centroid_gai.h). A typical
embedding application creates a model, calls `cgai_model_train_text`, then uses
`cgai_model_generate` or persists the model with `cgai_model_save`.

Implementation boundaries and strong internal identifier types are described in
[`docs/architecture.md`](docs/architecture.md). New APIs follow the project's
[`documentation standard`](docs/documentation-standard.md). To build strict HTML
API and implementation documentation, configure with `-DCGAI_BUILD_DOCS=ON` and
build the `docs` target (Doxygen is required). The
[C reading guide](docs/c-reading-guide.md) explains pointers, ownership, status
codes, and the training/generation call paths for readers new to C. Function
contracts and numbered walkthrough comments are included in the generated pages.

Durable model storage uses the isolated
[`Prisma 8 PostgreSQL adapter`](persistence/prisma-postgres/README.md). It stores
complete model artifacts in PostgreSQL with queryable compatibility metadata and
a SHA-256 checksum; see the [persistence architecture](docs/persistence.md).

Model files contain native numeric representations and are intended for trusted
files produced by the same architecture. A production format should define byte
order, checksums, resource limits, and compatibility guarantees.

## Project layout

- `include/` — stable public C API
- `src/` — focused library modules, private strongly typed headers, and CLI
- `tests/` — deterministic API and persistence tests
- `examples/` — a tiny demonstration corpus
- `docs/` — architecture and documentation standards
- `persistence/prisma-postgres/` — Prisma ORM 8 contract and PostgreSQL repository
- `.github/workflows/` — cross-platform build and test checks

## Current scope

This first version is deliberately modest: token hashing, online centroid updates,
a short context window, and centroid-conditioned token frequencies. Useful next
experiments include better embeddings, centroid splitting/merging, approximate
nearest-neighbor search, portable model serialization, and evaluation tooling.

## License

MIT
