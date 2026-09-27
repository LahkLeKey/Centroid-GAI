# Static categorized codebase centroids

`codebase-v1/` is a generated native release from repository commit
`50c7f509b30800c81c7da335045fcde964954c79`. It includes the committed core,
headers, tests, service, database, web application, tools, build configuration,
and documentation, including the three package READMEs.

- 144 source files represented by 143 unique normalized documents.
- 578 source chunk occurrences reduced to 551 unique chunks: 27 duplicates removed.
- 766,225 unique source-text bytes, down from 803,378 before chunk deduplication.
- 10 responsibility categories, 11 complete native model shards, 176 centroids,
  and 184,737 native training examples including one EOS per shard.
- 29 generated files totaling 752,128 bytes; largest file 152,883 bytes.

| Category | Shards | Native centroids |
| --- | ---: | ---: |
| Build | 1 | 16 |
| Database | 1 | 16 |
| Documentation | 1 | 16 |
| Native bridge | 1 | 16 |
| Native core | 2 | 32 |
| Native tests | 1 | 16 |
| Public API | 1 | 16 |
| Service | 1 | 16 |
| Tooling | 1 | 16 |
| Web | 1 | 16 |

The source license is retained in `codebase-v1/SOURCE_LICENSE.txt`. Source chunks
retain every originating file, Git blob, commit, document hash, and normalized
line range. `documents.json` and those chunks reconstruct all normalized documents
and verify their hashes. Category models retain aggregate observations rather
than a one-to-one mapping from individual centroids to passages.

`models/*.cgai.gz` decompress to complete artifacts for the existing native
loader. `spatial.json` contains inspected native vectors, centroid identities,
category membership, and a median-split bounding-box index. `neighbors.json`
caches the five nearest other centroids globally and within each category.
Rows at the same spatial position retain distinct IDs and token distributions.

All 352 global/within-category neighbor queries match exhaustive search.
Indexed traversal performs 22,240 distance comparisons versus 33,952 for
exhaustive scans, saving 34.5% on this audit. Cached queries use zero distance
comparisons after loading. These counts do not measure wall-clock speed or
question-answer quality, and the sidecar does not change native generation's
nearest-centroid loop.

See [build, verify, query, and reuse commands](../../docs/codebase-centroids.md).
