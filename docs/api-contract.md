# REST API Contract

The TypeScript service is the application boundary. Callers use HTTP/JSON or the model artifact media type; they do not call PostgreSQL, Prisma, or the C ABI directly. The service validates requests, invokes the native ABI, and persists complete artifacts through Prisma PostgreSQL.

## Domains

| Domain | Responsibility |
| --- | --- |
| System | Health and native schema discovery |
| Model catalog | List, inspect, and delete named models |
| Training | Train text through native C and persist the artifact |
| Inference | Generate from a persisted artifact through native C |
| Artifacts | Upload and download complete `.cgai` payloads |

## Versioned Endpoints

| Method | Endpoint | Response |
| --- | --- | --- |
| GET | `/api/v1/health` | Service/native/database status |
| GET | `/api/v1/native/schema` | C-exported persistence JSON Schema |
| GET | `/api/v1/models` | Persisted model metadata list |
| GET | `/api/v1/models/:name` | Binary `.cgai` artifact |
| PUT | `/api/v1/models/:name` | Validate and persist an artifact |
| DELETE | `/api/v1/models/:name` | Delete result |
| GET | `/api/v1/models/:name/metadata` | Persisted metadata |
| POST | `/api/v1/models/:name/train` | Train and persist |
| POST | `/api/v1/models/:name/generate` | Generate continuation |
| GET | `/api/v1/models/:name/contents` | Native artifact contents and composition recipe |
| POST | `/api/v1/models/:name/merge` | Create a distinct merged or compacted model |

The existing unversioned endpoints remain compatibility aliases. New callers
should use `/api/v1` exclusively. JSON `uint64_t` values are decimal strings;
artifact downloads use `application/vnd.centroid-gai.model` and an SHA-256 ETag.

## Artifact contents

`contents?section=summary` (default) returns the C-derived configuration, seed,
active centroid count, counters, and numeric storage breakdown. `section=vocabulary`
returns token IDs, spellings, and aggregate target counts. `section=centroids`
returns active centroid IDs, observation counts, vector norms, and distinct target
counts. `section=centroid&centroid=0` returns an actual centroid vector plus its
nonzero target tokens sorted by count (ties by token ID).

`offset` is a nonnegative integer; `limit` is 1–100 (default 25). Centroid IDs
are zero based. `checksum=<sha256>` pins inspection to an artifact snapshot; a
replacement returns 409. All responses include `name`, `section`, `data`,
`checksumSha256`, `artifactBytes`, and `composition` (null for ordinary models).
Counts and seeds are decimal strings. The API caps inspected payloads at 64 MiB.

## Model composition

```json
{
  "sources": [
    { "name": "model-a", "checksumSha256": "<64 lowercase hex characters>" },
    { "name": "model-b", "checksumSha256": "<64 lowercase hex characters>" }
  ],
  "targetCentroids": 0,
  "autoRebuild": true
}
```

Post to `/api/v1/models/new-superset/merge`. The ordered list accepts 1–32 sources.
Checksums are optional for API callers; the UI supplies them. Zero/omitted target
preserves all active rows. A positive target performs count-weighted streaming
compaction; one source is sufficient to compact an existing artifact. Sources
must match dimensions, context window, and seed; capacities and vocabularies may
differ. The union vocabulary is remapped in C. Totals are additive and overlapping
training examples cannot be deduplicated. Compaction is approximate and order
dependent. See [the C design](model-composition.md).

201 returns `{id, name, metadata, composition}`. The recipe records algorithm,
target count, and each source's name, checksum, active count, and example count.
The recipe is stored atomically with the new artifact in PostgreSQL; it is not
embedded in format-v1 binary downloads. Sources remain untouched. Existing
destinations and stale checksums return 409, missing sources return 404, malformed
or incompatible input returns 400. Input payloads above 64 MiB total or a merged
artifact above 64 MiB return 413. Repeated names and byte-identical artifacts are rejected. C also limits union
input vocabulary entries to 200,000, output numeric arrays to 64 MiB, and estimated
distance work to 100 million dimension comparisons. The database additionally
requires observation totals to fit a signed 64-bit integer.

`autoRebuild` defaults to true for new supersets. Before generation, inspection,
metadata reads, or download, the API refreshes nested live sources and rebuilds
changed artifacts. Preserve follows the current active centroid total; compact
keeps its target. Unchanged sources do not cause a write. The catalog exposes the
last materialized metadata; checksum-pinned inspection returns 409 if a rebuild
changes it. Rebuilds publish atomically and cannot overwrite a concurrently
replaced destination. Traversal is bounded to 32 levels and 128 distinct models.

Missing or incompatible live sources return 400 with the dependency error and
retain the last successful artifact without serving it as fresh. Restoring valid
sources permits the next read to rebuild. Set `autoRebuild: false` for an independent
snapshot; older recipes without the field also remain snapshots. Training or
uploading over a superset name clears its recipe. Downloads contain a standalone
model, not live dependencies. See [composition behavior](model-composition.md).

## Compose Smoke Test

```sh
docker compose up -d
cd persistence/api
CGAI_API_URL=http://localhost:3000 bun run test:all
```
