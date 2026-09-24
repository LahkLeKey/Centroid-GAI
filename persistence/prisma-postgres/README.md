# Prisma 8 PostgreSQL persistence

This adapter stores complete Centroid-GAI model artifacts in PostgreSQL through
Prisma ORM 8. The C library stays independent of the TypeScript runtime and
database driver; its versioned binary artifact is the boundary between them.

The application-facing REST domains and versioned endpoint contract are defined
in [`docs/api-contract.md`](../../docs/api-contract.md). Callers should use the
TypeScript REST service rather than accessing Prisma, PostgreSQL, or the native
C ABI directly.

Prisma ORM 8 is currently a release candidate. Package versions are pinned so
an upstream release cannot silently change the contract or query API.

## Setup

Use Node.js 24.11 or newer. Copy `.env.example` to `.env`, replace the connection
string, then run:

```sh
npm install
npm run contract:emit
npm run db:init
npm run typecheck
```

## Tests

Run the native bridge tests without external services:

```sh
npm test
```

With the Compose stack running, run the full TypeScript, native, and API
integration suite:

```sh
CGAI_API_URL=http://localhost:3000 npm run test:all
```

The API tests cover health, native training, PostgreSQL persistence, native
generation, artifact download, deletion, and malformed request handling.

`db:init` is for an empty database. After changing the contract, emit it, plan a
migration, review the generated migration, and apply it:

```sh
npm run contract:emit
npm run migration:plan -- --name describe_the_change
npm run db:migrate
```

## Store and retrieve models

Train a model with the C CLI, then persist it under a stable logical name:

```sh
../../build/cgai train ../../examples/tiny_corpus.txt tiny.cgai
npm run model -- put tiny tiny.cgai
npm run model -- get tiny restored.cgai
../../build/cgai generate restored.cgai "centroid models"
```

The adapter verifies the model magic/header before insertion and records a
SHA-256 checksum. A `put` with an existing name atomically replaces its payload
and metadata while retaining the database identity and creation timestamp.

