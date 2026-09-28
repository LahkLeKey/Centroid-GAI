# Compiled encyclopedia knowledge

The runtime encyclopedia centroids are compiled into a master translation unit
and 42 separately compiled category implementations under [`src/knowledge_catalog`](../../src/knowledge_catalog).
Each row has a comment with topic cues, representative frequent content words,
its stable ID, and observation count; the native API in `centroid_gai_knowledge.h`
exposes those descriptions and the sorted table. Topic cues are inspection aids,
not summaries of specific articles. It contains:

- **20,000 articles**, selected deterministically from the 241,787-row Simple
  English Wikipedia snapshot dated 2023-11-01.
- **32 document clusters**, originally partitioned into **147 source/model shards**.
- **2,352 learned native context centroids** and **5,195,609 training examples**.
- Sorted stable centroid IDs, categories, observation counts, and 32D vectors.

The compressed article/model payloads are not committed. The manifests retain the
dataset revision, cluster/shard IDs, and original hashes; the attribution license
is [separate from the code license](simplewiki-v1/LICENSE.md). Regeneration writes
compressed intermediates under `build/knowledge/rebuilt-release/`, not into this
directory.

`sources.json` pins the public dataset's Git revision, original download checksum,
selection/cluster algorithms, attribution, and source-shard hashes. `models.json`
records the native training configuration and original model hashes. No timestamps
or machine-local paths enter the compiled table.

The C table is a spatial index, not a generation model: it does not contain article
text or next-token distributions. Recreate the source and model archives locally
when retraining, inspecting article text, or using generation APIs. Centroid JSON
inspection records remain available for reviewing vectors and leading target counts.

Document clustering uses deterministic signed term hashing plus spherical
k-means. Each size-bounded shard is trained independently with 16 native context
centroids, 32 dimensions, a context window of 3, and seed 42. Document routing
clusters and native context centroids serve different purposes. Raw counts should
not be interpreted as 2,352 independently verified knowledge facts.

See [bulk build, verification, and monitoring](../../docs/git-encyclopedia.md#bulk-encyclopedia-release)
for regeneration commands. The compiled table is tested by the native spatial
suite. Native artifact reproduction still assumes compatible compiler, floating-
point ABI, and byte order.
