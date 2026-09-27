# Categorized native codebase release

The static codebase builder consumes a checksummed Git snapshot, deduplicates
its text, trains category-specific native models, and builds an exact spatial
index over their native centroid vectors. The generated
[release](../knowledge/codebase/README.md) is available locally under
`knowledge/codebase/codebase-v1`.

## Build and verify

Build the existing Node native addon with
`bun run --cwd persistence/api native:build` if needed. Then, from the repository
root, choose new output directories:

```powershell
python -m tools.knowledge snapshot --config examples/knowledge/codebase.json --repo . --ref HEAD --output build/knowledge/static-source-v1
node persistence/api/src/knowledge/codebase-centroids.ts --snapshot build/knowledge/static-source-v1 --output build/knowledge/static-centroids-v1
node persistence/api/src/knowledge/codebase-centroids.ts --directory knowledge/codebase/codebase-v1 --verify
```

The snapshot profile includes package READMEs as well as root documentation.
Only committed source enters the snapshot. The release records the source commit,
snapshot manifest hash, generator hashes, native-addon hash, runtime environment,
configuration, checksums, and deduplication counts. Build and verify use local
files and the native addon; no database, network, or API key is needed.

Builds train every shard twice and require identical native bytes. A sibling
partial directory is verified before being renamed into place; existing release
directories are refused. An interrupted build may leave a `.partial-*` directory.
Generated files are limited to 1 MiB each and a complete release to 128 MiB.
The stored models use the native numeric format and require compatible byte order
and numeric representation. Binary reproduction also depends on the compiler and
runtime environment recorded in the manifest.

Verification loads complete native artifacts, checks configurations, token/example
and observation counts, compares all inspected vectors with the spatial index,
reconstructs source documents, and rechecks source hashes and categories. It
rebuilds the spatial tree and neighbor caches and compares every global and
same-category query with an exhaustive reference. Verification does not retrain
models; `repeatTrainingVerified` records the build-time repeat check.

## Deduplication and categorization

The snapshot first deduplicates whole normalized documents while preserving
source aliases. The builder splits each document into nonoverlapping blocks of
up to 32 normalized lines, drops blank-only blocks, and globally deduplicates
exact chunk text by SHA-256 before training. No approximate similarity threshold
deletes code. All source aliases and line ranges remain attached to each chunk.
Blank gaps are reconstructed as empty normalized lines and validated by the
original document hash.

Category assignment follows source paths: build, database, documentation,
native bridge, native core, native tests, public API, service, tooling, and web.
Markdown and reStructuredText belong to documentation regardless of directory.
If a duplicate chunk spans categories, it records every category and trains once
under the lexicographically first category. Categories describe source ownership;
they are independent of nearest-neighbor geometry.

Chunks sort by category and hash. Each category is divided into training shards
of at most 128 KiB of UTF-8 text. The models share 32 dimensions, 16 reserved
centroids per shard, context window 3, and seed 42. Matching dimensions, context,
seed, and embedding implementation allow comparisons across these models even
when their vocabulary IDs differ. All active centroids are indexed.

The native trainer receives one concatenated text call per shard, including one
EOS; context crosses chunk boundaries. Both snapshot splits are used as source
material, so this release is not a held-out model-quality experiment. Removing
repeated chunks changes training frequencies. Counts and spatial efficiency do
not establish answer quality; retain the separate
[repository-question evaluation](repository-retrieval.md).

## Spatial queries

Inspect a centroid's nearest neighbors:

```powershell
node persistence/api/src/knowledge/codebase-centroids.ts --directory knowledge/codebase/codebase-v1 --nearest native-core-000:0005 --limit 5
node persistence/api/src/knowledge/codebase-centroids.ts --directory knowledge/codebase/codebase-v1 --nearest native-core-000:0005 --category documentation --limit 8
```

IDs combine shard ID and zero-padded native centroid ID. Queries exclude the
input centroid and break equal-distance ties by full ID. Distances are squared
Euclidean distances over the inspected native coordinates, without projection or
renormalization. Identical vectors can belong to different rows; zero distance
does not imply interchangeable target counts or duplicate training data.

The spatial tree splits its widest bounding-box axis at the median, retaining
up to eight rows per leaf. Query traversal uses bounding-box distance to prune
branches and category sets to skip irrelevant branches. Larger requests (up to
100 neighbors) or cross-category filters use exact tree traversal. Global and
same-category requests for up to five neighbors use the static adjacency cache.
Queries checksum the spatial sidecars without reloading models or rerunning the
all-pairs verification; run `--verify` for the full release audit. Checksums detect
accidental edits, not coordinated changes to a release and its manifest.

The index accelerates queries between static native centroids in the release.
It does not replace the C library's inference search or provide a semantic
embedding of a repository question. The first audit saved 11,712 of 33,952 vector
distance comparisons (34.5%) with exact neighbor agreement. Pruning efficiency
depends on vector distribution and dimensionality.

## Use a native shard

Decompress a selected artifact with Python's standard library, refusing to
overwrite an existing output, then use the ordinary C CLI:

```powershell
python -c "import gzip,pathlib; pathlib.Path('build/native-core.cgai').open('xb').write(gzip.decompress(pathlib.Path('knowledge/codebase/codebase-v1/models/native-core-000.cgai.gz').read_bytes()))"
.\build\cgai.exe generate build/native-core.cgai "model training" 20 0 42
```

Use `build/Release/cgai.exe` for a Visual Studio build or `build/cgai` on Linux.
Individual shards also work with the existing persistence import CLI. Static
release generation neither deploys models nor installs automatic rebuilding.

## Regression checks

```powershell
node --test persistence/api/src/knowledge/codebase-centroids.test.ts
bun run --cwd persistence/api typecheck
```

Tests cover duplicate source aliases and chunks across categories, deterministic
ordering, size limits, exact spatial search with ties and category filters, native
repeat builds, cached queries, immutable destinations, and corruption even when
metadata has been rehashed. CI also verifies the generated static release.
