# Versioned encyclopedia clusters

`simplewiki-v1/` contains a real, reproducible initial training release:

- **20,000 articles**, selected deterministically from the 241,787-row Simple
  English Wikipedia snapshot dated 2023-11-01.
- **32 document clusters**, partitioned into **147 source/model shards**.
- **2,352 learned native context centroids** and **5,195,609 training examples**.
- About **21.6 MB** of generated files; the largest generated file is **117,358 bytes**.
- Every generated file is capped at **1 MiB**; a release is capped at **128 MiB**.

These are ordinary Git files with LFS filtering disabled for compressed shards.
The 156,885,218-byte upstream Parquet file stays in the ignored local build cache.
The data and attribution license is [separate from the code license](simplewiki-v1/LICENSE.md).

`sources.json` pins the public dataset's Git revision, original download checksum,
selection/cluster algorithms, attribution, and source-shard hashes. `models.json`
maps each source shard to its compressed complete native model and readable
centroid inspection. No timestamps or machine-local paths enter generated data.

Source JSONL gzip files retain page IDs, titles, complete selected article text,
article URLs, attribution, and revision-history URLs. Model gzip files contain
normal `.cgai` artifacts. The centroid JSON files expose learned vectors and the
12 leading next-token counts per centroid; full counts are in the model artifact.
Generation samples are deterministic smoke checks, not factual-quality evaluations.

Document clustering uses deterministic signed term hashing plus spherical
k-means. Each size-bounded shard is trained independently with 16 native context
centroids, 32 dimensions, a context window of 3, and seed 42. Document routing
clusters and native context centroids serve different purposes. Raw counts should
not be interpreted as 2,352 independently verified knowledge facts.

See [bulk build, verification, and monitoring](../../docs/git-encyclopedia.md#bulk-encyclopedia-release)
for exact commands. Source preparation was rebuilt independently with identical
hashes. Each native model was trained twice and compared byte-for-byte, and the
complete native release was regenerated against the existing files. Reproducing
native bytes assumes the same floating-point ABI/compiler behavior; the artifact
format is native, not a cross-platform portable interchange format.
