# `@centroid-gai/web`

Frontend-only React console for the Centroid-GAI persistence API
([`docs/api-contract.md`](../../docs/api-contract.md)). Built with
[Bun](https://bun.sh), [Vite](https://vite.dev), React, and Tailwind CSS. It
only talks to the versioned `/api/v1` REST endpoints served by
[`@centroid-gai/api`](../api/README.md); it never touches PostgreSQL, Prisma,
or the native C ABI directly.

## Features

- Open directly into the **Supersets** workspace, with an existing superset selected
  first. Composition settings open on demand, keeping the default view focused on discovery.
- Click frequent learned token chips to explore nearest contexts without typing.
  Follow observed target tokens to continue discovery.
- Use **Match patterns** to inspect custom contexts, unknown-token mappings,
  centroid distances, and target distributions without generating or training.
- List, refresh, and delete persisted models.
- Train a new model from pasted text and optional config overrides.
- Upload a `.cgai` artifact directly, or download an existing one.
- Generate a continuation from a selected model's persisted artifact.
- Drill into **Inspector → Artifact contents** for storage, vocabulary,
  centroid occupancy, actual vector components, and ranked target tokens.
- Use **Inspector → Compose superset** to merge compatible sources into a new
  artifact or compact a single source to a smaller centroid configuration.
  Source order, compatibility, resource estimates, and stored recipes are visible.
  Compaction is approximate; shared training history is counted again.
- Add, reuse, and remove model labels in the Inspector, or use **Edit labels**
  on the selected sidebar model. Save explicitly, save and advance to the next
  unlabeled model, or discard a draft. Search
  matches names and labels; the label filter includes an **Unlabeled** queue.
- Track label coverage and compare all models by vocabulary, training examples,
  dimensions, centroids, or context window. Comparison bars use a shared linear
  scale and select the corresponding model when clicked.
- Explore label distribution and compare a single label group or unlabeled
  models. The metric scale stays fixed across groups to keep bars comparable.
- Keep separate unfinished label drafts while switching models or app tabs.
  Use the draft shortcuts to resume editing; save or discard before closing or
  reloading the page. Drafts are held in memory, with a browser unload warning.

Labels are browser-local metadata, keyed by model ID and saved in local storage.
They are not synchronized across browsers or included in downloaded artifacts.
Clearing browser storage removes them. Retraining an existing model keeps its
labels; deleting and recreating it gives it a new ID and starts without labels.
Each model supports up to eight lowercase labels of 32 characters each.
Saved labels synchronize between tabs on the same browser origin. Saves check
for conflicting edits to the same model and preserve the draft when a conflict
is found. Discarding that draft loads the latest saved labels. Saves merge the
latest stored collection so other models' labels are retained; local storage
does not provide transactions for truly simultaneous writes across tabs.

## Setup

To combine trained models, select one and choose **Compose a superset** in the default workspace,
choose the other sources, and give the result a new name. Sources must share
dimensions, context window, and embedding seed. Preserve keeps all active
centroids; compact combines them using observation-weighted means.

**Automatically rebuild from source models** is enabled by default. Opening
artifact contents or running the super model refreshes changed sources, including
nested supersets. Missing or incompatible sources show an error until restored.
Uncheck it to create a fixed snapshot. After creation, **Run model** opens the
playground with the new model selected. Shared training history remains additive.

Discovery and matching also refresh live supersets. Match distances measure the
model's hashed context space, not semantic similarity or calibrated confidence.
The model stores aggregate observations, not original training passages.

```sh
cd persistence
bun install
cd web
bun run dev
```

The dev server proxies `/api/*` requests to `http://localhost:3000` (see
`vite.config.ts`); start `@centroid-gai/api` (or the Compose stack) first.
Copy `.env.example` to `.env` to point at a different API origin instead of
using the dev proxy.

## Build

```sh
bun run build
```

Outputs a static bundle in `dist/` that can be served by any static host or
reverse proxy in front of the API.

## End-to-end tests

```sh
bunx playwright install chromium   # first run only
bun run test:e2e
```

Playwright drives a real Chromium browser against the Vite dev server (auto
started on port 4173) and a running `@centroid-gai/api` instance (defaults to
`http://localhost:3000`; override with `VITE_API_PROXY_TARGET`). Tests create
uniquely named fixture models through the API and delete them in
`afterAll`/inline cleanup, so they are safe to run against a shared database
without disturbing seeded demo models.

`bun run test:e2e e2e/labels.spec.ts` runs the label and comparison UI tests with
mocked API responses, without a database or native API process.
