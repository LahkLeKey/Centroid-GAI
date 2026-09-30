# HTTP API: current baseline

The product interface is HTTP/JSON, exercised through curl and Docker Compose.
The router serves the original count-centroid `.cgai` engine and the separate
neural conversation engine. This page describes the baseline routes; the
[chat service contract](chat-service.md) covers conversations, training jobs,
research and scoped memory.

The contract below describes [server.ts](../persistence/api/src/server.ts) and
[artifact-routes.ts](../persistence/api/src/artifact-routes.ts). Chat and memory
migrations are committed. See [development](development.md) for verification.

## Conventions

- Base URL: `http://localhost:3000/api/v1`; use URL-encoded model names.
- Send JSON with `Content-Type: application/json`.
- Native 64-bit counters and seeds appear as decimal strings in JSON.
- Model downloads use `application/vnd.centroid-gai.model` and an ETag of
  `"sha256:<checksum>"`.
- Request bodies default to a 64 MiB limit, configured by `MAX_REQUEST_BYTES`.
- Unversioned routes remain compatibility aliases; new clients should use `/api/v1`.

## Docker Compose workflow

Run from the repository root:

```sh
docker compose up --build --detach --wait
curl --fail-with-body http://localhost:3000/api/v1/health
docker compose logs api
```

Compose runs PostgreSQL, database migrations/verification, and the API. Use
`docker compose down` to stop the stack while preserving its database volume.
See the [API package guide](../persistence/api/README.md) for ports and tests.

## Implemented routes

| Method | Path after `/api/v1` | Result |
| --- | --- | --- |
| GET | `/health` | Service liveness and configured engine/database labels |
| GET | `/native/schema` | Native persistence JSON Schema |
| GET | `/models` | List last-persisted model metadata |
| GET | `/models/:name/metadata` | Current model metadata |
| POST | `/models/:name/train` | Train and replace a named baseline model |
| POST | `/models/:name/generate` | Generate a text continuation |
| PUT | `/models/:name` | Validate and replace an artifact from raw bytes |
| GET | `/models/:name` | Download artifact bytes |
| DELETE | `/models/:name` | Delete a model; return `{ "deleted": boolean }` |
| GET | `/models/:name/contents` | Inspect native arrays and composition recipe |
| POST | `/models/:name/match` | Inspect nearest learned contexts |
| POST | `/models/:name/merge` | Create a new composed baseline model |

`/health` does not run a database query. Use model operations to verify actual
database access. Baseline routes retain their existing unauthenticated contract.
Chat and memory routes enforce a configured owner and require a bearer token for
remote clients, including clients reaching the API through Docker networking.

## Train and generate

Training accepts a nonempty `text` string and optional configuration:

```json
{
  "text": "centroid models learn online. centroid models generate words.",
  "config": { "dimensions": 16, "centroidCount": 12, "contextWindow": 4 }
}
```

Success returns HTTP 201 with `{ id, name, metadata }`. Training replaces existing
payloads under the same name and clears their composition recipe. Omitted settings
use native defaults. The native configuration's BigInt seed has no JSON conversion
in this route; omit `config.seed` when calling through HTTP.

Generation accepts:

```json
{ "prompt": "centroid models", "maxTokens": 30, "temperature": 0, "seed": "42" }
```

`prompt` is required. Defaults are 40 tokens, temperature 0.8 and seed `"0"`.
Temperature 0 selects greedy output; seed 0 uses the model's seed. HTTP 200 returns
`{ name, prompt, continuation }`. Each request is independent: this route stores no
conversation history and performs no neural training or learning from users.

With a working stack, run from the repository root:

```sh
curl --fail-with-body -X POST http://localhost:3000/api/v1/models/demo/train -H "Content-Type: application/json" --data-binary @examples/api/train.json
curl --fail-with-body -X POST http://localhost:3000/api/v1/models/demo/generate -H "Content-Type: application/json" --data-binary @examples/api/generate.json
curl --fail-with-body http://localhost:3000/api/v1/models/demo --output demo.cgai
```

Use `curl.exe` in Windows PowerShell if `curl` is an alias. The committed JSON
request files avoid shell-specific quoting.

## Inspect and match

`GET /models/:name/contents` accepts the following query parameters:

| Parameter | Behavior |
| --- | --- |
| `section=summary` | Default: configuration, seed, active rows and storage counts |
| `section=vocabulary` | Token IDs, spellings and aggregate target counts |
| `section=centroids` | Active rows with occupancy, norm and distinct target count |
| `section=centroid&centroid=0` | One vector and its nonzero target distribution |
| `section=highlights` | Most frequent ordinary target tokens, starting at offset 0 |
| `offset=0&limit=25` | Page offset and size; limit 1–100 |
| `checksum=<sha256>` | Reject a changed artifact with HTTP 409 |

The response contains `name`, `section`, `checksumSha256`, `artifactBytes`,
`composition` and `data`. Counts are strings; these are learned statistics, not
the original training passages.

Aggregate centroids cannot retrieve original source passages.

`POST /models/:name/match` accepts `{ "text": "centroid models", "limit": 5 }`.
Text must be nonempty, contain no NUL, and fit 16,384 UTF-8 bytes. Limit is 1–10.
The response contains `name`, `checksumSha256` and `data` with input/unknown token
counts, the used context suffix, and nearest centroids with target distributions.
Distances measure the hashed embedding space; they are not factual confidence.

## Compose baseline models

`POST /models/new-model/merge` accepts an ordered set of existing sources:

```json
{
  "sources": [{ "name": "model-a" }, { "name": "model-b" }],
  "targetCentroids": 0,
  "autoRebuild": true
}
```

Use 1–32 sources with matching dimensions, context window and embedding seed.
Optional `checksumSha256` values pin sources. Duplicate names or identical
artifacts are rejected. The destination must be new. Zero/omitted target preserves
active rows; a positive target applies approximate, order-dependent weighted
compaction. Counts are additive; overlapping training examples are not deduplicated.
Combined input and output artifacts each have a 64 MiB cap.

HTTP 201 returns `{ id, name, metadata, composition }`. `autoRebuild` defaults to
true: reads refresh changed source dependencies before generation, inspection or
download. Catalog entries show the last materialized result. Missing/incompatible
sources fail the read while retaining the last stored artifact. Use false for an
independent snapshot. Downloads always contain a standalone model. Neural weights
do not participate in this composition algorithm.

## Errors and boundaries

Most failures return `{ "error": "description" }`: 400 for invalid input/native
failures, 404 for missing models or routes, 409 for stale checksums/existing merge
destinations, and 413 for size limits. The current top-level handler also maps
other thrown `RangeError` values to 413. Missing deletion returns 404 with
`{ "deleted": false }`.

Baseline native calls run synchronously in the HTTP process. Bounded worker
execution, request cancellation, session ownership, persistent chat history and
neural artifact serving are implemented separately in the [chat service](chat-service.md).
