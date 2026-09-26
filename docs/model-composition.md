# Artifact inspection and model composition

## Design

The C model is the authority for vocabulary, centroid means, observation counts,
and next-token frequencies. Inspection reads those arrays through an additive C
ABI; TypeScript does not parse the binary format. Existing ABI v2 structures and
format-v1 artifacts remain compatible.

Inspection exposes a summary (including seed and active versus reserved rows),
paged vocabulary with aggregate target counts, paged centroid occupancy, and a
centroid detail with its actual vector and paged nonzero token distribution.
Counts are decimal strings in JSON. The artifact stores learned statistics, not
the source text or individual training examples. Visualization must not imply
that vectors are original samples or that more centroids guarantee better text.

## C merge contract

`cgai_model_merge(sources, count, target_centroids)` returns a new owned model;
inputs stay unchanged. Sources must have identical dimensions, context window,
and embedding seed. Token embeddings depend on spelling and seed, not vocabulary
ID, so a union vocabulary plus per-source ID remapping preserves their meaning.
Different centroid capacities are allowed. Empty/untrained sources, malformed
statistics, repeated source pointers, counter overflow, excessive allocation,
and excessive distance-work budgets fail before a result is published.

- **Preserve** (`target_centroids = 0`): concatenate active centroid rows in source
  order, preserve their means and counts, and omit unused reserved rows. This is
  lossless with respect to stored statistics, not a promise of identical output:
  nearest-centroid competition changes when models are combined.
- **Compact** (`1 <= target_centroids <= total active rows`): seed the output with
  the first target-count source rows, then assign each remaining row to its
  nearest current output mean. Update that mean with the source row's observation
  weight, and add remapped token counts. This deterministic streaming algorithm
  conserves observations and token totals, but is approximate and source-order
  dependent. It does not reconstruct or retrain original examples.

One source is supported for compaction of an existing superset. Repeated merges
are possible because outputs are ordinary independently usable artifacts. Shared
training history is additive: byte-identical sources are rejected by the API,
but semantic overlap cannot be detected without original data. Mixing embedding
spaces or changing dimensions/context/seed requires retraining; a future routing
ensemble should keep such models separate rather than average incompatible means.

## API and persistence

`GET /api/v1/models/:name/contents` supports summary, vocabulary, centroids, and
centroid-detail sections with bounded pagination. The response includes the
artifact checksum so a page can detect replacement during inspection.

`POST /api/v1/models/:name/merge` accepts ordered source names, optional source
checksums, and a target count (zero means preserve). It validates all sources,
creates a distinct destination, and never replaces an existing named model.
The result and its source names/checksums/configuration recipe are stored together.
New recipes default to `autoRebuild: true`. Matching, discovery, generation, artifact inspection,
metadata reads, downloads, and CLI reads resolve the current sources first. If a
source checksum changed, the result is rebuilt and persisted before returning it.
Nested live supersets refresh in dependency order. Preserve mode follows the new
active row count; compact mode retains the requested target. Unchanged inputs do
not rewrite the artifact. Rebuilds use source versions observed during that read;
a source write racing the read is picked up on the next access.

This is refresh-on-use, not a background watcher. The catalog lists the last
materialized metadata; pinned inspection returns 409 after a rebuild, and the UI
automatically refreshes its catalog/checksum before reading again. The recipe
records the source checksums used by the latest successful build. Conditional
publication prevents concurrent refreshes from overwriting a replaced destination.
Dependency traversal is limited to 32 levels and 128 distinct models per request.

Missing sources, incompatible configurations, duplicate artifacts, or a compact
target larger than the new active count fail the read with an actionable error.
The last successful artifact remains stored, but generation does not silently use
stale training. Restoring compatible sources allows the next access to rebuild.
Deleting a source never cascades deletion to supersets.

Set `autoRebuild: false` for a fixed snapshot independent of subsequent source
changes or deletion. Existing recipes without this field retain snapshot behavior.
Training or uploading a replacement clears the destination's composition recipe.
Downloaded binaries are standalone snapshots; live dependencies remain in the DB.

Synchronous C work is bounded by source count, total payload size, output numeric
storage, and distance-operation estimates. The API caps both combined source
payloads and the resulting artifact at 64 MiB. Large-scale optimization should later
move to cancellable background jobs, with held-out evaluation before choosing a
configuration. No quality improvement is inferred from storage reduction alone.

## Superset workspace

The web app opens on **Supersets**, selecting an existing superset before an
ordinary source. The main view offers frequent observed tokens as discovery
chips: click one to match its learned context, then follow target tokens to explore
further. No prompt is required. Composition opens only through **Compose a superset**,
and advanced training settings are collapsed until needed.

**Match patterns** supports custom contexts without generating text or training.
It displays the used context suffix, unknown-token mappings, nearest centroids,
and their observed target distributions, with links to centroid vector inspection.
Distances describe the native hashed embedding space, not semantic similarity or
classification confidence. Changing models clears previous matching results.

## Composition workflow

1. Open artifact contents and inspect storage, vocabulary, occupancy, vectors,
   and top target tokens using paged controls.
2. Select source models in an explicit order. Show their compatibility and the
   summed number of active centroids and examples.
3. Choose preserve or compact, review the configuration and overlap explanation,
   leave automatic rebuilding enabled (or choose a fixed snapshot), give the
   result a new name, and create it without changing sources.
4. Select the persisted result, inspect its content and composition recipe, and
   use **Run model** to generate from the combined training in the playground.

## Verification

C tests cover vocabulary remapping, count conservation, weighted means,
serialization, source immutability, incompatible spaces, invalid counters, and
capacity limits. Native and API tests cover inspection pagination, rejected merge
requests, immutable destinations, provenance, and persisted generation. Browser
tests cover drill-down, stale-response handling, composition selection, validation,
and creation/navigation. Existing training, upload, generation, and label workflows
remain regression checks.
