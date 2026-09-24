# `@centroid-gai/api`

The HTTP and CLI application boundary for Centroid-GAI. This package wraps the
native C library through a Node-API addon (`native/`, `native.ts`) and
persists complete model artifacts through the sibling
[`@centroid-gai/db`](../db/README.md) package. It never talks to PostgreSQL or
Prisma directly; `model-repository.ts` is the only module that imports
`@centroid-gai/db`.

The application-facing REST domains and versioned endpoint contract are defined
in [`docs/api-contract.md`](../../docs/api-contract.md). Callers should use the
TypeScript REST service rather than accessing Prisma, PostgreSQL, or the native
C ABI directly.

## Setup

Use [bun](https://bun.sh) 1.3 or newer. `persistence/` is a bun workspace
containing both this package and `../db`, so install once from the workspace
root, then build the native addon explicitly (bun does not run dependency
lifecycle scripts automatically):

```sh
cd .. && bun install
cd api
bun run native:build
bun run typecheck
```

Copy `.env.example` to `.env` and replace the connection string. Set up the
database itself from [`persistence/db`](../db/README.md) (`contract:emit`,
`db:init`) before starting this package.

## Tests

Run the native bridge tests without external services:

```sh
bun test
```

With the Compose stack running, run the full TypeScript, native, and API
integration suite:

```sh
CGAI_API_URL=http://localhost:3000 bun run test:all
```

The API tests cover health, native training, PostgreSQL persistence, native
generation, artifact download, deletion, and malformed request handling.

Contract changes, migrations, and `db:init`/`db:verify` are run from
[`persistence/db`](../db/README.md), not from this package.

## Compose E2E

Run the complete local deployment from the `persistence/` workspace root:

```sh
bun run test:e2e
```

The runner builds and starts `postgres`, `database-init`, and `api`, waits for
`GET /health`, then executes this package's API tests against
`http://127.0.0.1:3000`. The tests train a real native model through HTTP,
verify its PostgreSQL metadata, generate text from the stored artifact,
download the `.cgai` bytes, and delete the test model. Containers are removed
afterward; the named `postgres-data` volume is retained for local inspection.

## Seed example models

After a fresh checkout, install and build the persistence workspace, then start
Compose and seed the committed corpora:

```sh
cd persistence
bun install
bun run --cwd api native:build
cd ..
docker compose up -d
cd persistence
bun run seed:models
```

The seed command trains these stable names through the same HTTP route used by
the application: `tiny-contexts`, `generation-patterns`, and
`persistence-workflow`. It is safe to run repeatedly after code, corpus, or
database changes because each request replaces the named artifact. The corpus
sources live in [`examples/model_corpora`](../../examples/model_corpora/);
generated local files under `examples/models/` are ignored and are never needed
to recreate the database state.

## Store and retrieve models

Train a model with the C CLI, then persist it under a stable logical name:

```sh
../../build/cgai train ../../examples/tiny_corpus.txt tiny.cgai
bun run model -- put tiny tiny.cgai
bun run model -- get tiny restored.cgai
../../build/cgai generate restored.cgai "centroid models"
```

The adapter verifies the model magic/header before insertion and records a
SHA-256 checksum. A `put` with an existing name atomically replaces its payload
and metadata while retaining the database identity and creation timestamp.

